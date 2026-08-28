// SPDX-License-Identifier: GPL-3.0-only
#include "lua_mod.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace btd5loader::runtime {
namespace {

constexpr int kInstructionHookInterval = 1'000;
constexpr std::uintmax_t kMaximumResourceBytes = 1024U * 1024U;

void set_nil(lua_State* state, const char* name) {
    lua_pushnil(state);
    lua_setglobal(state, name);
}

void set_api_function(lua_State* state, LuaMod* mod, const char* name, lua_CFunction function) {
    lua_pushlightuserdata(state, mod);
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

}  // namespace

LuaMod::LuaMod(LuaModOptions options) : options_(std::move(options)) {
    memory_.limit = options_.memory_limit_bytes;
    if (options_.mod_id.empty() || memory_.limit == 0 || options_.instruction_budget == 0 ||
        options_.callback_time_limit.count() <= 0 || options_.callback_recursion_limit == 0) {
        last_error_ = "invalid Lua mod options";
        return;
    }

    state_ = lua_newstate(&LuaMod::allocate, &memory_);
    if (state_ == nullptr) {
        last_error_ = options_.mod_id + ": unable to create Lua state within memory limit";
        return;
    }
    *static_cast<LuaMod**>(lua_getextraspace(state_)) = this;
    open_sandbox();
    register_api();
    load_storage();
}

LuaMod::~LuaMod() {
    if (state_ != nullptr) {
        lua_close(state_);
    }
}

bool LuaMod::valid() const noexcept {
    return state_ != nullptr;
}

bool LuaMod::load_script(const std::string_view script, const std::string_view source_name) {
    if (state_ == nullptr) {
        return false;
    }
    const std::string chunk_name = "@" + options_.mod_id + "/" + std::string(source_name);
    if (luaL_loadbufferx(
            state_, script.data(), script.size(), chunk_name.c_str(), "t") != LUA_OK) {
        const char* message = lua_tostring(state_, -1);
        report_error("load", message != nullptr ? message : "unknown Lua load error");
        lua_pop(state_, 1);
        return false;
    }
    return execute_at_stack_top("load");
}

bool LuaMod::invoke(const std::string_view callback) {
    if (state_ == nullptr || disabled_callbacks_.contains(std::string(callback))) {
        return false;
    }
    lua_getglobal(state_, std::string(callback).c_str());
    if (lua_isnil(state_, -1)) {
        lua_pop(state_, 1);
        return true;
    }
    if (!lua_isfunction(state_, -1)) {
        lua_pop(state_, 1);
        report_error(callback, "callback is not a function");
        disabled_callbacks_.insert(std::string(callback));
        return false;
    }
    const bool succeeded = execute_at_stack_top(callback);
    if (!succeeded) {
        disabled_callbacks_.insert(std::string(callback));
    }
    return succeeded;
}

void LuaMod::advance_timers(const std::uint64_t ticks) {
    if (state_ == nullptr || ticks > (std::numeric_limits<std::uint64_t>::max)() - current_tick_) {
        return;
    }
    current_tick_ += ticks;
    std::vector<Timer> pending;
    for (auto iterator = timers_.begin(); iterator != timers_.end();) {
        if (iterator->due_tick <= current_tick_) {
            pending.push_back(*iterator);
            iterator = timers_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    std::sort(pending.begin(), pending.end(), [](const Timer& left, const Timer& right) {
        return left.due_tick < right.due_tick;
    });
    for (const auto& timer : pending) {
        lua_rawgeti(state_, LUA_REGISTRYINDEX, timer.function_reference);
        (void)execute_at_stack_top("timer");
        luaL_unref(state_, LUA_REGISTRYINDEX, timer.function_reference);
    }
}

bool LuaMod::callback_disabled(const std::string_view callback) const {
    return disabled_callbacks_.contains(std::string(callback));
}

std::string_view LuaMod::last_error() const noexcept {
    return last_error_;
}

std::size_t LuaMod::memory_used() const noexcept {
    return memory_.used;
}

void* LuaMod::allocate(
    void* user_data,
    void* pointer,
    const std::size_t old_size,
    const std::size_t new_size) {
    auto& memory = *static_cast<MemoryState*>(user_data);
    const std::size_t accounted_old_size = pointer == nullptr ? 0 : old_size;
    if (new_size == 0) {
        std::free(pointer);
        memory.used = accounted_old_size > memory.used ? 0 : memory.used - accounted_old_size;
        return nullptr;
    }
    if (new_size > accounted_old_size &&
        new_size - accounted_old_size > memory.limit - std::min(memory.used, memory.limit)) {
        return nullptr;
    }
    void* resized = std::realloc(pointer, new_size);
    if (resized != nullptr) {
        memory.used = memory.used - std::min(accounted_old_size, memory.used) + new_size;
    }
    return resized;
}

void LuaMod::instruction_hook(lua_State* state, lua_Debug*) {
    LuaMod* mod = *static_cast<LuaMod**>(lua_getextraspace(state));
    if (mod == nullptr) {
        luaL_error(state, "Lua host context is unavailable");
        return;
    }
    lua_Debug frame{};
    std::size_t depth = 0;
    while (depth <= mod->options_.callback_recursion_limit &&
           lua_getstack(state, static_cast<int>(depth), &frame) != 0) {
        ++depth;
    }
    if (depth > mod->options_.callback_recursion_limit) {
        luaL_error(state, "callback recursion limit exceeded");
        return;
    }
    if (mod->instructions_remaining_ <= static_cast<std::uint64_t>(kInstructionHookInterval)) {
        luaL_error(state, "CPU instruction budget exceeded");
        return;
    }
    mod->instructions_remaining_ -= static_cast<std::uint64_t>(kInstructionHookInterval);
    if (std::chrono::steady_clock::now() >= mod->deadline_) {
        luaL_error(state, "callback time limit exceeded");
    }
}

int LuaMod::api_log(lua_State* state) {
    LuaMod* mod = from_upvalue(state);
    const char* level = luaL_checkstring(state, 1);
    const char* message = luaL_checkstring(state, 2);
    if (mod->options_.log) {
        mod->options_.log(level, message);
    }
    return 0;
}

int LuaMod::api_config_get(lua_State* state) {
    LuaMod* mod = from_upvalue(state);
    const char* key = luaL_checkstring(state, 1);
    const auto value = mod->options_.configuration.find(key);
    if (value == mod->options_.configuration.end()) {
        lua_pushnil(state);
    } else {
        lua_pushlstring(state, value->second.data(), value->second.size());
    }
    return 1;
}

int LuaMod::api_storage_get(lua_State* state) {
    LuaMod* mod = from_upvalue(state);
    const char* key = luaL_checkstring(state, 1);
    const auto value = mod->storage_.find(key);
    if (value == mod->storage_.end()) {
        lua_pushnil(state);
    } else {
        lua_pushlstring(state, value->second.data(), value->second.size());
    }
    return 1;
}

int LuaMod::api_storage_set(lua_State* state) {
    LuaMod* mod = from_upvalue(state);
    const std::string key = luaL_checkstring(state, 1);
    std::size_t value_length = 0;
    const char* value = luaL_checklstring(state, 2, &value_length);
    if (key.empty() || key.size() > 128 || value_length > 64U * 1024U) {
        return luaL_error(state, "storage key or value exceeds its limit");
    }
    mod->storage_[key] = std::string(value, value_length);
    if (!mod->save_storage()) {
        return luaL_error(state, "mod storage could not be saved");
    }
    return 0;
}

int LuaMod::api_localize(lua_State* state) {
    LuaMod* mod = from_upvalue(state);
    const char* key = luaL_checkstring(state, 1);
    const auto value = mod->options_.localization.find(key);
    if (value == mod->options_.localization.end()) {
        lua_pushstring(state, key);
    } else {
        lua_pushlstring(state, value->second.data(), value->second.size());
    }
    return 1;
}

int LuaMod::api_read_resource(lua_State* state) {
    LuaMod* mod = from_upvalue(state);
    const char* requested = luaL_checkstring(state, 1);
    std::filesystem::path resolved;
    if (!mod->safe_resource_path(requested, resolved)) {
        return luaL_error(state, "resource path is not allowed");
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(resolved, error);
    if (error || size > kMaximumResourceBytes) {
        return luaL_error(state, "resource is missing or exceeds 1 MiB");
    }
    std::ifstream input(resolved, std::ios::binary);
    std::string contents(static_cast<std::size_t>(size), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!input && !contents.empty()) {
        return luaL_error(state, "resource could not be read");
    }
    lua_pushlstring(state, contents.data(), contents.size());
    return 1;
}

int LuaMod::api_timer_after(lua_State* state) {
    LuaMod* mod = from_upvalue(state);
    const lua_Integer ticks = luaL_checkinteger(state, 1);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    if (ticks < 0 || static_cast<std::uint64_t>(ticks) >
                         (std::numeric_limits<std::uint64_t>::max)() - mod->current_tick_) {
        return luaL_error(state, "timer tick value is invalid");
    }
    lua_pushvalue(state, 2);
    const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
    mod->timers_.push_back({mod->current_tick_ + static_cast<std::uint64_t>(ticks), reference});
    return 0;
}

LuaMod* LuaMod::from_upvalue(lua_State* state) {
    return static_cast<LuaMod*>(lua_touserdata(state, lua_upvalueindex(1)));
}

void LuaMod::open_sandbox() {
    luaL_requiref(state_, LUA_GNAME, luaopen_base, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(state_, 1);

    set_nil(state_, "dofile");
    set_nil(state_, "loadfile");
    set_nil(state_, "load");
    set_nil(state_, "collectgarbage");
}

void LuaMod::register_api() {
    lua_newtable(state_);
    set_api_function(state_, this, "log", &LuaMod::api_log);

    lua_newtable(state_);
    set_api_function(state_, this, "get", &LuaMod::api_config_get);
    lua_setfield(state_, -2, "config");

    lua_newtable(state_);
    set_api_function(state_, this, "get", &LuaMod::api_storage_get);
    set_api_function(state_, this, "set", &LuaMod::api_storage_set);
    lua_setfield(state_, -2, "storage");

    lua_newtable(state_);
    set_api_function(state_, this, "get", &LuaMod::api_localize);
    lua_setfield(state_, -2, "localization");

    lua_newtable(state_);
    set_api_function(state_, this, "read_text", &LuaMod::api_read_resource);
    lua_setfield(state_, -2, "resource");

    lua_newtable(state_);
    set_api_function(state_, this, "after", &LuaMod::api_timer_after);
    lua_setfield(state_, -2, "timer");

    lua_setglobal(state_, "btd5");
}

void LuaMod::load_storage() {
    if (options_.storage_directory.empty()) {
        return;
    }
    std::ifstream input(options_.storage_directory / L"storage.json", std::ios::binary);
    if (!input) {
        return;
    }
    try {
        storage_ = nlohmann::json::parse(input).get<std::unordered_map<std::string, std::string>>();
    } catch (const nlohmann::json::exception&) {
        storage_.clear();
        report_error("storage", "existing storage file is malformed");
    }
}

bool LuaMod::save_storage() {
    if (options_.storage_directory.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(options_.storage_directory, error);
    if (error) {
        return false;
    }
    std::ofstream output(
        options_.storage_directory / L"storage.json",
        std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << nlohmann::json(storage_).dump(2) << '\n';
    return output.good();
}

bool LuaMod::execute_at_stack_top(const std::string_view callback) {
    if (callback_depth_ >= options_.callback_recursion_limit) {
        lua_pop(state_, 1);
        report_error(callback, "callback recursion limit exceeded");
        return false;
    }
    ++callback_depth_;
    instructions_remaining_ = options_.instruction_budget;
    deadline_ = std::chrono::steady_clock::now() + options_.callback_time_limit;
    lua_sethook(state_, &LuaMod::instruction_hook, LUA_MASKCOUNT, kInstructionHookInterval);
    const int result = lua_pcall(state_, 0, 0, 0);
    lua_sethook(state_, nullptr, 0, 0);
    --callback_depth_;
    if (result != LUA_OK) {
        const char* message = lua_tostring(state_, -1);
        report_error(callback, message != nullptr ? message : "unknown Lua callback error");
        lua_pop(state_, 1);
        return false;
    }
    return true;
}

void LuaMod::report_error(const std::string_view callback, const std::string_view message) {
    last_error_ = options_.mod_id + "." + std::string(callback) + ": " + std::string(message);
    if (options_.log) {
        options_.log("error", last_error_);
    }
}

bool LuaMod::safe_resource_path(
    const std::string_view requested,
    std::filesystem::path& resolved) const {
    if (requested.empty() || requested.find(':') != std::string_view::npos ||
        requested.find('\\') != std::string_view::npos) {
        return false;
    }
    const std::u8string utf8_path(
        reinterpret_cast<const char8_t*>(requested.data()), requested.size());
    const std::filesystem::path relative(utf8_path);
    if (relative.is_absolute()) {
        return false;
    }
    for (const auto& component : relative) {
        if (component == L".." || component == L".") {
            return false;
        }
    }
    resolved = options_.resource_directory / relative;
    std::error_code error;
    return std::filesystem::is_regular_file(resolved, error) && !error;
}

}  // namespace btd5loader::runtime
