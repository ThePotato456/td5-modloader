// SPDX-License-Identifier: GPL-3.0-only
#include "lua_mod.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>
#include <type_traits>
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
constexpr char kGameObjectMetatable[] = "btd5.game_object.v1";
constexpr std::array<std::string_view, 24> kEventNames{
    "match.starting", "match.started", "match.ending", "match.ended",
    "round.starting", "round.started", "round.ending", "round.ended",
    "cash.changing", "cash.changed", "lives.changing", "lives.changed",
    "tower.placing", "tower.placed", "tower.upgrading", "tower.upgraded",
    "tower.selling", "tower.sold", "bloon.spawning", "bloon.spawned",
    "bloon.popping", "bloon.popped", "bloon.leaking", "bloon.leaked"};

struct LuaGameObject final {
    LuaMod* owner{};
    GameObjectRegistry* registry{};
    GameObjectHandle handle;
};

bool known_event(const std::string_view name) {
    return std::find(kEventNames.begin(), kEventNames.end(), name) != kEventNames.end();
}

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
        options_.callback_time_limit.count() <= 0 || options_.callback_recursion_limit == 0 ||
        options_.event_recursion_limit == 0 || options_.maximum_event_handlers == 0) {
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

LuaEventDispatchResult LuaMod::dispatch_event(
    const std::string_view event_name,
    const LuaEventFields& fields,
    const bool cancellable) {
    LuaEventDispatchResult result;
    if (state_ == nullptr || !known_event(event_name)) {
        report_error("event", "unknown event: " + std::string(event_name));
        return result;
    }
    if (event_dispatch_depth_ >= options_.event_recursion_limit) {
        report_error("event." + std::string(event_name), "event recursion limit exceeded");
        return result;
    }
    for (const auto& [name, value] : fields) {
        if (name.empty() || name == "name" || name == "cancelled" ||
            (std::holds_alternative<GameObjectHandle>(value) &&
             (options_.object_registry == nullptr ||
              options_.object_registry->resolve(
                  std::get<GameObjectHandle>(value),
                  std::get<GameObjectHandle>(value).kind) == nullptr))) {
            report_error("event." + std::string(event_name), "event payload is invalid or stale");
            return result;
        }
    }

    ++event_dispatch_depth_;
    lua_newtable(state_);
    lua_pushlstring(state_, event_name.data(), event_name.size());
    lua_setfield(state_, -2, "name");
    lua_pushboolean(state_, 0);
    lua_setfield(state_, -2, "cancelled");
    for (const auto& [name, value] : fields) {
        if (!push_event_value(value)) {
            lua_pop(state_, 1);
            --event_dispatch_depth_;
            report_error("event." + std::string(event_name), "event payload could not be encoded");
            return result;
        }
        lua_setfield(state_, -2, name.c_str());
    }
    const int event_reference = luaL_ref(state_, LUA_REGISTRYINDEX);

    std::vector<std::uint64_t> snapshot;
    for (const auto& handler : event_handlers_) {
        if (handler.enabled && handler.event_name == event_name) {
            snapshot.push_back(handler.token);
        }
    }
    result.succeeded = true;
    for (const std::uint64_t token : snapshot) {
        const auto handler = std::find_if(
            event_handlers_.begin(),
            event_handlers_.end(),
            [token](const EventHandler& candidate) {
                return candidate.enabled && candidate.token == token;
            });
        if (handler == event_handlers_.end()) {
            continue;
        }
        const int function_reference = handler->function_reference;
        lua_rawgeti(state_, LUA_REGISTRYINDEX, function_reference);
        lua_rawgeti(state_, LUA_REGISTRYINDEX, event_reference);
        ++result.handlers_invoked;
        if (!execute_at_stack_top(
                "event." + std::string(event_name) + "#" + std::to_string(token), 1)) {
            result.succeeded = false;
            const auto failed = std::find_if(
                event_handlers_.begin(),
                event_handlers_.end(),
                [token](const EventHandler& candidate) { return candidate.token == token; });
            if (failed != event_handlers_.end() && failed->enabled) {
                failed->enabled = false;
                luaL_unref(state_, LUA_REGISTRYINDEX, failed->function_reference);
            }
        }
    }

    lua_rawgeti(state_, LUA_REGISTRYINDEX, event_reference);
    lua_getfield(state_, -1, "cancelled");
    result.cancelled = cancellable && lua_isboolean(state_, -1) && lua_toboolean(state_, -1) != 0;
    lua_pop(state_, 1);
    result.fields.reserve(fields.size());
    for (const auto& [name, original] : fields) {
        LuaEventValue updated = original;
        bool valid = true;
        if (!std::holds_alternative<GameObjectHandle>(original)) {
            lua_getfield(state_, -1, name.c_str());
            if (std::holds_alternative<bool>(original)) {
                valid = lua_isboolean(state_, -1);
                if (valid) updated = lua_toboolean(state_, -1) != 0;
            } else if (std::holds_alternative<std::int64_t>(original)) {
                valid = lua_isinteger(state_, -1);
                if (valid) updated = static_cast<std::int64_t>(lua_tointeger(state_, -1));
            } else if (std::holds_alternative<double>(original)) {
                valid = lua_isnumber(state_, -1);
                if (valid) updated = static_cast<double>(lua_tonumber(state_, -1));
            } else if (std::holds_alternative<std::string>(original)) {
                valid = lua_type(state_, -1) == LUA_TSTRING;
                if (valid) {
                    std::size_t length = 0;
                    const char* text = lua_tolstring(state_, -1, &length);
                    updated = std::string(text, length);
                }
            }
            lua_pop(state_, 1);
        }
        if (!valid) {
            result.succeeded = false;
            report_error(
                "event." + std::string(event_name),
                "event field has an invalid type: " + name);
        }
        result.fields.emplace_back(name, std::move(updated));
    }
    lua_pop(state_, 1);
    luaL_unref(state_, LUA_REGISTRYINDEX, event_reference);
    --event_dispatch_depth_;
    event_handlers_.erase(
        std::remove_if(
            event_handlers_.begin(),
            event_handlers_.end(),
            [](const EventHandler& handler) { return !handler.enabled; }),
        event_handlers_.end());
    return result;
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

int LuaMod::api_event_on(lua_State* state) {
    LuaMod* mod = from_upvalue(state);
    const char* event_name = luaL_checkstring(state, 1);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    if (!known_event(event_name)) {
        return luaL_error(state, "unknown event name");
    }
    const auto active_handlers = std::count_if(
        mod->event_handlers_.begin(),
        mod->event_handlers_.end(),
        [](const EventHandler& handler) { return handler.enabled; });
    if (static_cast<std::size_t>(active_handlers) >= mod->options_.maximum_event_handlers ||
        mod->next_event_token_ == 0 ||
        mod->next_event_token_ > static_cast<std::uint64_t>((std::numeric_limits<lua_Integer>::max)())) {
        return luaL_error(state, "event handler limit reached");
    }
    lua_pushvalue(state, 2);
    const int reference = luaL_ref(state, LUA_REGISTRYINDEX);
    const std::uint64_t token = mod->next_event_token_++;
    mod->event_handlers_.push_back({token, event_name, reference, true});
    lua_pushinteger(state, static_cast<lua_Integer>(token));
    return 1;
}

int LuaMod::api_event_off(lua_State* state) {
    LuaMod* mod = from_upvalue(state);
    const lua_Integer requested = luaL_checkinteger(state, 1);
    const auto handler = requested <= 0
                             ? mod->event_handlers_.end()
                             : std::find_if(
                                   mod->event_handlers_.begin(),
                                   mod->event_handlers_.end(),
                                   [requested](const EventHandler& candidate) {
                                       return candidate.enabled &&
                                              candidate.token == static_cast<std::uint64_t>(requested);
                                   });
    if (handler == mod->event_handlers_.end()) {
        lua_pushboolean(state, 0);
        return 1;
    }
    handler->enabled = false;
    luaL_unref(state, LUA_REGISTRYINDEX, handler->function_reference);
    lua_pushboolean(state, 1);
    return 1;
}

int LuaMod::api_game_object_is_valid(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    lua_pushboolean(
        state,
        object->registry != nullptr &&
            object->registry->resolve(object->handle, object->handle.kind) != nullptr);
    return 1;
}

int LuaMod::api_game_object_id(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    if (object->registry == nullptr ||
        object->registry->resolve(object->handle, object->handle.kind) == nullptr) {
        return luaL_error(state, "game object is stale");
    }
    lua_pushinteger(state, static_cast<lua_Integer>(object->handle.id));
    return 1;
}

int LuaMod::api_game_object_kind(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    if (object->registry == nullptr ||
        object->registry->resolve(object->handle, object->handle.kind) == nullptr) {
        return luaL_error(state, "game object is stale");
    }
    const std::string_view kind = game_object_kind_name(object->handle.kind);
    lua_pushlstring(state, kind.data(), kind.size());
    return 1;
}

int LuaMod::api_game_object_pop_count(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    void* const resolved = object->registry == nullptr
                               ? nullptr
                               : object->registry->resolve(object->handle, object->handle.kind);
    if (resolved == nullptr) {
        return luaL_error(state, "game object is stale");
    }
    if (object->handle.kind != GameObjectKind::Tower) {
        return luaL_error(state, "pop_count is available only on tower objects");
    }
    if (object->owner == nullptr || !object->owner->options_.game_object_integer_get) {
        return luaL_error(state, "tower pop_count is unavailable on this game build");
    }
    const auto value = object->owner->options_.game_object_integer_get(
        object->handle.kind, resolved, "pop_count");
    if (!value) {
        return luaL_error(state, "tower pop_count could not be read");
    }
    lua_pushinteger(state, static_cast<lua_Integer>(*value));
    return 1;
}

int LuaMod::api_game_object_set_pop_count(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    void* const resolved = object->registry == nullptr
                               ? nullptr
                               : object->registry->resolve(object->handle, object->handle.kind);
    if (resolved == nullptr) {
        return luaL_error(state, "game object is stale");
    }
    if (object->handle.kind != GameObjectKind::Tower) {
        return luaL_error(state, "set_pop_count is available only on tower objects");
    }
    if (lua_isinteger(state, 2) == 0) {
        return luaL_error(state, "tower pop_count must be an integer");
    }
    const lua_Integer requested = lua_tointeger(state, 2);
    if (requested < 0 || requested > (std::numeric_limits<std::int32_t>::max)()) {
        return luaL_error(state, "tower pop_count is outside the supported range");
    }
    if (object->owner == nullptr || !object->owner->options_.game_object_integer_set) {
        return luaL_error(state, "tower pop_count is unavailable on this game build");
    }
    std::string error;
    if (!object->owner->options_.game_object_integer_set(
            object->handle.kind,
            resolved,
            "pop_count",
            static_cast<std::int64_t>(requested),
            error)) {
        return luaL_error(
            state,
            "%s",
            error.empty() ? "tower pop_count could not be changed" : error.c_str());
    }
    lua_pushboolean(state, 1);
    return 1;
}

int LuaMod::api_game_object_sell_price(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    void* const resolved = object->registry == nullptr
                               ? nullptr
                               : object->registry->resolve(object->handle, object->handle.kind);
    if (resolved == nullptr) return luaL_error(state, "game object is stale");
    if (object->handle.kind != GameObjectKind::Tower) {
        return luaL_error(state, "sell_price is available only on tower objects");
    }
    if (object->owner == nullptr || !object->owner->options_.game_object_integer_get) {
        return luaL_error(state, "tower sell_price is unavailable on this game build");
    }
    const auto value = object->owner->options_.game_object_integer_get(
        object->handle.kind, resolved, "sell_price");
    if (!value) return luaL_error(state, "tower sell_price could not be read");
    lua_pushinteger(state, static_cast<lua_Integer>(*value));
    return 1;
}

int LuaMod::api_game_object_set_sell_price(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    void* const resolved = object->registry == nullptr
                               ? nullptr
                               : object->registry->resolve(object->handle, object->handle.kind);
    if (resolved == nullptr) return luaL_error(state, "game object is stale");
    if (object->handle.kind != GameObjectKind::Tower) {
        return luaL_error(state, "set_sell_price is available only on tower objects");
    }
    if (lua_isinteger(state, 2) == 0) {
        return luaL_error(state, "tower sell_price must be an integer");
    }
    const lua_Integer requested = lua_tointeger(state, 2);
    if (requested < 0 || requested > (std::numeric_limits<std::int32_t>::max)()) {
        return luaL_error(state, "tower sell_price is outside the supported range");
    }
    if (object->owner == nullptr || !object->owner->options_.game_object_integer_set) {
        return luaL_error(state, "tower sell_price is unavailable on this game build");
    }
    std::string error;
    if (!object->owner->options_.game_object_integer_set(
            object->handle.kind,
            resolved,
            "sell_price",
            static_cast<std::int64_t>(requested),
            error)) {
        return luaL_error(
            state,
            "%s",
            error.empty() ? "tower sell_price could not be changed" : error.c_str());
    }
    lua_pushboolean(state, 1);
    return 1;
}

int LuaMod::api_game_object_health(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    void* const resolved = object->registry == nullptr
                               ? nullptr
                               : object->registry->resolve(object->handle, object->handle.kind);
    if (resolved == nullptr) return luaL_error(state, "game object is stale");
    if (object->handle.kind != GameObjectKind::Bloon) {
        return luaL_error(state, "health is available only on bloon objects");
    }
    if (object->owner == nullptr || !object->owner->options_.game_object_number_get) {
        return luaL_error(state, "bloon health is unavailable on this game build");
    }
    const auto value = object->owner->options_.game_object_number_get(
        object->handle.kind, resolved, "health");
    if (!value || !std::isfinite(*value)) {
        return luaL_error(state, "bloon health could not be read");
    }
    lua_pushnumber(state, static_cast<lua_Number>(*value));
    return 1;
}

int LuaMod::api_game_object_set_health(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    void* const resolved = object->registry == nullptr
                               ? nullptr
                               : object->registry->resolve(object->handle, object->handle.kind);
    if (resolved == nullptr) return luaL_error(state, "game object is stale");
    if (object->handle.kind != GameObjectKind::Bloon) {
        return luaL_error(state, "set_health is available only on bloon objects");
    }
    if (lua_type(state, 2) != LUA_TNUMBER) {
        return luaL_error(state, "bloon health must be a number");
    }
    const double requested = static_cast<double>(lua_tonumber(state, 2));
    if (!std::isfinite(requested) || requested < 0.0 ||
        requested > static_cast<double>((std::numeric_limits<float>::max)())) {
        return luaL_error(state, "bloon health is outside the supported range");
    }
    if (object->owner == nullptr || !object->owner->options_.game_object_number_set) {
        return luaL_error(state, "bloon health is unavailable on this game build");
    }
    std::string error;
    if (!object->owner->options_.game_object_number_set(
            object->handle.kind, resolved, "health", requested, error)) {
        return luaL_error(
            state,
            "%s",
            error.empty() ? "bloon health could not be changed" : error.c_str());
    }
    lua_pushboolean(state, 1);
    return 1;
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
    if (luaL_newmetatable(state_, kGameObjectMetatable) != 0) {
        lua_newtable(state_);
        lua_pushcfunction(state_, &LuaMod::api_game_object_is_valid);
        lua_setfield(state_, -2, "is_valid");
        lua_pushcfunction(state_, &LuaMod::api_game_object_id);
        lua_setfield(state_, -2, "id");
        lua_pushcfunction(state_, &LuaMod::api_game_object_kind);
        lua_setfield(state_, -2, "kind");
        lua_pushcfunction(state_, &LuaMod::api_game_object_pop_count);
        lua_setfield(state_, -2, "pop_count");
        lua_pushcfunction(state_, &LuaMod::api_game_object_set_pop_count);
        lua_setfield(state_, -2, "set_pop_count");
        lua_pushcfunction(state_, &LuaMod::api_game_object_sell_price);
        lua_setfield(state_, -2, "sell_price");
        lua_pushcfunction(state_, &LuaMod::api_game_object_set_sell_price);
        lua_setfield(state_, -2, "set_sell_price");
        lua_pushcfunction(state_, &LuaMod::api_game_object_health);
        lua_setfield(state_, -2, "health");
        lua_pushcfunction(state_, &LuaMod::api_game_object_set_health);
        lua_setfield(state_, -2, "set_health");
        lua_setfield(state_, -2, "__index");
        lua_pushliteral(state_, "BTD5 game object v1");
        lua_setfield(state_, -2, "__metatable");
    }
    lua_pop(state_, 1);

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

    lua_newtable(state_);
    set_api_function(state_, this, "on", &LuaMod::api_event_on);
    set_api_function(state_, this, "off", &LuaMod::api_event_off);
    lua_setfield(state_, -2, "events");

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

bool LuaMod::execute_at_stack_top(
    const std::string_view callback,
    const int argument_count) {
    if (callback_depth_ >= options_.callback_recursion_limit) {
        lua_pop(state_, argument_count + 1);
        report_error(callback, "callback recursion limit exceeded");
        return false;
    }
    ++callback_depth_;
    instructions_remaining_ = options_.instruction_budget;
    deadline_ = std::chrono::steady_clock::now() + options_.callback_time_limit;
    lua_sethook(state_, &LuaMod::instruction_hook, LUA_MASKCOUNT, kInstructionHookInterval);
    const int result = lua_pcall(state_, argument_count, 0, 0);
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

bool LuaMod::push_event_value(const LuaEventValue& value) {
    return std::visit(
        [this](const auto& typed) {
            using Value = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Value, bool>) {
                lua_pushboolean(state_, typed);
            } else if constexpr (std::is_same_v<Value, std::int64_t>) {
                lua_pushinteger(state_, static_cast<lua_Integer>(typed));
            } else if constexpr (std::is_same_v<Value, double>) {
                lua_pushnumber(state_, typed);
            } else if constexpr (std::is_same_v<Value, std::string>) {
                lua_pushlstring(state_, typed.data(), typed.size());
            } else if constexpr (std::is_same_v<Value, GameObjectHandle>) {
                push_game_object(typed);
            } else {
                return false;
            }
            return true;
        },
        value);
}

void LuaMod::push_game_object(const GameObjectHandle& handle) {
    auto* object = static_cast<LuaGameObject*>(lua_newuserdatauv(state_, sizeof(LuaGameObject), 0));
    object->owner = this;
    object->registry = options_.object_registry;
    object->handle = handle;
    luaL_setmetatable(state_, kGameObjectMetatable);
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
