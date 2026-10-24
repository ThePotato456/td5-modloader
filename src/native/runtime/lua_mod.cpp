// SPDX-License-Identifier: GPL-3.0-only
#include "lua_mod.hpp"

#include "custom_tower_content.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <system_error>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

namespace btd5loader::runtime {
namespace {

constexpr int kInstructionHookInterval = 1'000;
constexpr std::uintmax_t kMaximumResourceBytes = 1024U * 1024U;
constexpr char kGameObjectMetatable[] = "btd5.game_object.v1";
struct ProtectedContext final {
    void (*operation)(void*);
    void* context;
};

class LuaReference final {
public:
    LuaReference(lua_State* state, int reference) : state_(state), reference_(reference) {}
    ~LuaReference() { if (reference_ >= 0) luaL_unref(state_, LUA_REGISTRYINDEX, reference_); }
    void release() noexcept { reference_ = LUA_NOREF; }
    LuaReference(const LuaReference&) = delete;
    LuaReference& operator=(const LuaReference&) = delete;
private:
    lua_State* state_;
    int reference_;
};

template <lua_CFunction Function>
int guarded_api(lua_State* state) {
    try {
        return Function(state);
    } catch (const std::exception&) {
        // Lua's own C++ error object is not a std::exception: let it reach pcall.
    }
    return luaL_error(state, "host API operation failed");
}
constexpr std::array<std::string_view, 28> kEventNames{
    "match.starting", "match.started", "match.ending", "match.ended",
    "round.starting", "round.started", "round.ending", "round.ended",
    "cash.changing", "cash.changed", "lives.changing", "lives.changed",
    "tower.placing", "tower.placed", "tower.upgrading", "tower.upgraded",
    "tower.selling", "tower.sold", "bloon.spawning", "bloon.spawned",
    "bloon.popping", "bloon.popped", "bloon.leaking", "bloon.leaked",
    "attack.created", "attack.destroyed", "projectile.created", "projectile.destroyed"};

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

// Converts the Lua value at `index` to JSON for tower content: tables with
// keys 1..n become lists, other tables need string keys; functions and
// userdata are refused. Returns an empty string or an error.
std::string lua_to_json(lua_State* state, const int index, nlohmann::ordered_json& out, const int depth = 0) {
    constexpr int kMaximumDepth = 16;
    if (depth > kMaximumDepth) return "is nested too deeply";
    const int absolute = lua_absindex(state, index);
    switch (lua_type(state, absolute)) {
    case LUA_TBOOLEAN:
        out = lua_toboolean(state, absolute) != 0;
        return {};
    case LUA_TNUMBER:
        if (lua_isinteger(state, absolute)) {
            out = static_cast<std::int64_t>(lua_tointeger(state, absolute));
        } else {
            out = static_cast<double>(lua_tonumber(state, absolute));
        }
        return {};
    case LUA_TSTRING: {
        std::size_t length = 0;
        const char* text = lua_tolstring(state, absolute, &length);
        out = std::string(text, length);
        return {};
    }
    case LUA_TTABLE: {
        const auto count = static_cast<lua_Integer>(lua_rawlen(state, absolute));
        lua_Integer keys = 0;
        bool string_keys = true;
        lua_pushnil(state);
        while (lua_next(state, absolute) != 0) {
            ++keys;
            if (lua_type(state, -2) != LUA_TSTRING) string_keys = false;
            lua_pop(state, 1);
        }
        if (keys == 0) {
            out = nlohmann::ordered_json::object();
            return {};
        }
        if (count == keys) {
            out = nlohmann::ordered_json::array();
            for (lua_Integer i = 1; i <= count; ++i) {
                lua_rawgeti(state, absolute, i);
                nlohmann::ordered_json item;
                auto error = lua_to_json(state, -1, item, depth + 1);
                lua_pop(state, 1);
                if (!error.empty()) return error;
                out.push_back(std::move(item));
            }
            return {};
        }
        if (!string_keys) return "mixes list entries and named fields";
        out = nlohmann::ordered_json::object();
        lua_pushnil(state);
        while (lua_next(state, absolute) != 0) {
            const std::string key = lua_tostring(state, -2);
            nlohmann::ordered_json item;
            auto error = lua_to_json(state, -1, item, depth + 1);
            lua_pop(state, 1);
            if (!error.empty()) {
                lua_pop(state, 1);
                return "field '" + key + "' " + error;
            }
            out[key] = std::move(item);
        }
        return {};
    }
    default:
        return std::string("has an unsupported ") + lua_typename(state, lua_type(state, absolute)) + " value";
    }
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
    if (!protect("setup", [](void* context) {
            auto* mod = static_cast<LuaMod*>(context);
            mod->open_sandbox();
            mod->register_api();
        }, this)) {
        lua_close(state_);
        state_ = nullptr;
        return;
    }
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

int LuaMod::protected_operation(lua_State* state) {
    auto* context = static_cast<ProtectedContext*>(lua_touserdata(state, 1));
    try {
        context->operation(context->context);
    } catch (const std::exception&) {
        return luaL_error(state, "host boundary operation failed");
    }
    return 0;
}

bool LuaMod::protect(
    const std::string_view boundary, const ProtectedOperation operation, void* context) {
    if (state_ == nullptr) return false;
    if (!lua_checkstack(state_, 2)) {
        report_error(boundary, "Lua stack allocation failed");
        return false;
    }
    const int top = lua_gettop(state_);
    ProtectedContext call{operation, context};
    // A zero-upvalue C function and light userdata do not allocate.
    lua_pushcfunction(state_, &LuaMod::protected_operation);
    lua_pushlightuserdata(state_, &call);
    const int status = lua_pcall(state_, 1, 0, 0);
    if (status != LUA_OK) {
        const char* message = lua_type(state_, -1) == LUA_TSTRING
            ? lua_tostring(state_, -1) : "non-string Lua boundary error";
        report_error(boundary, message);
    }
    lua_settop(state_, top);
    return status == LUA_OK;
}

bool LuaMod::load_script(const std::string_view script, const std::string_view source_name) {
    struct Context { LuaMod* mod; std::string_view script; std::string_view source; bool result{}; };
    Context context{this, script, source_name};
    const bool succeeded = protect("load", [](void* value) {
        auto& call = *static_cast<Context*>(value);
        call.result = call.mod->load_script_impl(call.script, call.source);
    }, &context);
    return succeeded && context.result;
}

bool LuaMod::invoke(const std::string_view callback) {
    struct Context { LuaMod* mod; std::string_view callback; bool result{}; };
    Context context{this, callback};
    const bool succeeded = protect(callback, [](void* value) {
        auto& call = *static_cast<Context*>(value);
        call.result = call.mod->invoke_impl(call.callback);
    }, &context);
    if (!succeeded) disabled_callbacks_.insert(std::string(callback));
    return succeeded && context.result;
}

void LuaMod::advance_timers(const std::uint64_t ticks) {
    struct Context { LuaMod* mod; std::uint64_t ticks; };
    Context context{this, ticks};
    (void)protect("timer", [](void* value) {
        auto& call = *static_cast<Context*>(value);
        call.mod->advance_timers_impl(call.ticks);
    }, &context);
}

LuaEventDispatchResult LuaMod::dispatch_event(
    const std::string_view name, const LuaEventFields& fields, const bool cancellable) {
    struct Context {
        LuaMod* mod; std::string_view name; const LuaEventFields* fields;
        bool cancellable; LuaEventDispatchResult result;
    };
    Context context{this, name, &fields, cancellable, {}};
    const auto depth = event_dispatch_depth_;
    if (!protect("event." + std::string(name), [](void* value) {
            auto& call = *static_cast<Context*>(value);
            call.result = call.mod->dispatch_event_impl(call.name, *call.fields, call.cancellable);
        }, &context)) {
        context.result = {};
    }
    event_dispatch_depth_ = depth;
    return std::move(context.result);
}

bool LuaMod::has_event_handlers(const std::string_view event_name) const noexcept {
    return std::any_of(event_handlers_.begin(), event_handlers_.end(),
        [event_name](const EventHandler& handler) {
            return handler.enabled && handler.event_name == event_name;
        });
}

bool LuaMod::load_script_impl(const std::string_view script, const std::string_view source_name) {
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
    loading_script_ = true;
    const bool loaded = execute_at_stack_top("load");
    loading_script_ = false;
    return loaded;
}

bool LuaMod::invoke_impl(const std::string_view callback) {
    if (state_ == nullptr || disabled_callbacks_.contains(std::string(callback))) {
        return false;
    }
    lua_pushglobaltable(state_);
    lua_pushlstring(state_, callback.data(), callback.size());
    lua_rawget(state_, -2);
    lua_remove(state_, -2);
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

void LuaMod::advance_timers_impl(const std::uint64_t ticks) {
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

LuaEventDispatchResult LuaMod::dispatch_event_impl(
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
        const bool terminal_match_handle =
            event_name == "match.ended" && name == "match" &&
            std::holds_alternative<GameObjectHandle>(value) &&
            std::get<GameObjectHandle>(value).kind == GameObjectKind::Match;
        if (name.empty() || name == "name" || name == "cancelled" ||
            (std::holds_alternative<GameObjectHandle>(value) &&
             (!terminal_match_handle &&
              (options_.object_registry == nullptr ||
              options_.object_registry->resolve(
                  std::get<GameObjectHandle>(value),
                  std::get<GameObjectHandle>(value).kind) == nullptr)))) {
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
    const LuaReference event_owner(state_, event_reference);

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
    // Payload inspection is outside the protected callback. Never invoke a
    // mod-controlled __index metamethod here, even for a deleted field.
    lua_pushliteral(state_, "cancelled");
    lua_rawget(state_, -2);
    result.cancelled = cancellable && lua_isboolean(state_, -1) && lua_toboolean(state_, -1) != 0;
    lua_pop(state_, 1);
    result.fields.reserve(fields.size());
    for (const auto& [name, original] : fields) {
        LuaEventValue updated = original;
        bool valid = true;
        if (!std::holds_alternative<GameObjectHandle>(original)) {
            lua_pushlstring(state_, name.data(), name.size());
            lua_rawget(state_, -2);
            if (std::holds_alternative<bool>(original)) {
                valid = lua_isboolean(state_, -1);
                if (valid) updated = lua_toboolean(state_, -1) != 0;
            } else if (std::holds_alternative<std::int64_t>(original)) {
                valid = lua_isinteger(state_, -1);
                if (valid) updated = static_cast<std::int64_t>(lua_tointeger(state_, -1));
            } else if (std::holds_alternative<double>(original)) {
                valid = lua_type(state_, -1) == LUA_TNUMBER &&
                    std::isfinite(static_cast<double>(lua_tonumber(state_, -1)));
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
        mod->instructions_remaining_ = 0;
        mod->budget_failure_ = "CPU instruction budget exceeded";
        luaL_error(state, "CPU instruction budget exceeded");
        return;
    }
    mod->instructions_remaining_ -= static_cast<std::uint64_t>(kInstructionHookInterval);
    if (std::chrono::steady_clock::now() >= mod->deadline_) {
        mod->budget_failure_ = "callback time limit exceeded";
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

// btd5.towers.register{ id = "laser", base = "DartMonkey", cost = 120,
//     name = "Laser Monkey", description = "...", shop = "last",
//     progression = "open", restricted_modes = "base",
//     stats = { range = 140, cooldown = 0.5, pierce = 3, damage = 2, camo = true, ... },
//     weapons = { { TargetRange = 140, CooldownTime = 0.5, Tasks = { ... } } },
//     upgrades = { { <4 tiers> }, { <4 tiers> } },
//     on_place = function(tower, event) end, on_upgrade = ..., on_sell = ... }
// (see custom_tower_content.hpp for stats, weapons and upgrade tiers)
// registers "<mod id>:laser" while the mod script loads, which is before the
// game first reads its tower list and text.
// btd5.sounds.register{ id = "zap", files = { "assets/sounds/zap.wav" }, cooldown = 0.1 }
// registers "<mod id>:zap" while the mod script loads; the game picks one of
// the files at random each time the sound plays.
int LuaMod::api_sounds_register(lua_State* state) {
    LuaMod* mod = from_upvalue(state);
    luaL_checktype(state, 1, LUA_TTABLE);
    if (!mod->loading_script_) return luaL_error(state, "sounds can only be registered while the mod script loads");
    lua_getfield(state, 1, "id");
    const std::string id = luaL_checkstring(state, -1);
    lua_getfield(state, 1, "cooldown");
    const double cooldown = luaL_optnumber(state, -1, 0.1);
    lua_getfield(state, 1, "files");
    luaL_checktype(state, -1, LUA_TTABLE);
    std::vector<std::filesystem::path> files;
    const auto count = static_cast<lua_Integer>(lua_rawlen(state, -1));
    for (lua_Integer i = 1; i <= count; ++i) {
        lua_rawgeti(state, -1, i);
        const std::string relative = luaL_checkstring(state, -1);
        lua_pop(state, 1);
        // Only files inside the package's assets/ folder.
        const std::filesystem::path path(relative);
        if (path.is_absolute() || relative.find("..") != std::string::npos || !relative.starts_with("assets/")) {
            return luaL_error(state, "sound files must be paths under the package's assets/ folder");
        }
        files.push_back(mod->options_.resource_directory / path);
    }
    if (!mod->options_.register_sound) return luaL_error(state, "sounds are unavailable");
    const std::string error = mod->options_.register_sound(mod->options_.mod_id + ":" + id, std::move(files), cooldown);
    if (!error.empty()) return luaL_error(state, "%s", error.c_str());
    lua_pushboolean(state, 1);
    return 1;
}

// btd5.sounds.play("zap") plays one of this mod's sounds (or "<mod id>:<id>")
// in the current match; returns true, or false and the reason.
int LuaMod::api_sounds_play(lua_State* state) {
    LuaMod* mod = from_upvalue(state);
    std::string id = luaL_checkstring(state, 1);
    if (id.find(':') == std::string::npos) id = mod->options_.mod_id + ":" + id;
    const std::string error = mod->options_.play_sound ? mod->options_.play_sound(id) : "sounds are unavailable";
    lua_pushboolean(state, error.empty() ? 1 : 0);
    if (error.empty()) return 1;
    lua_pushstring(state, error.c_str());
    return 2;
}

int LuaMod::api_towers_register(lua_State* state) {
    LuaMod* mod = from_upvalue(state);
    luaL_checktype(state, 1, LUA_TTABLE);
    if (!mod->loading_script_) return luaL_error(state, "towers can only be registered while the mod script loads");
    lua_getfield(state, 1, "id");
    const char* id = luaL_checkstring(state, -1);
    CustomTowerSpec spec;
    lua_getfield(state, 1, "base");
    spec.base = luaL_optstring(state, -1, "DartMonkey");
    lua_getfield(state, 1, "cost");
    if (!lua_isnil(state, -1)) {
        int is_integer = 0;
        const lua_Integer value = lua_tointegerx(state, -1, &is_integer);
        if (is_integer == 0) return luaL_error(state, "tower cost must be an integer");
        spec.cost = value;
    }
    lua_getfield(state, 1, "name");
    spec.name = luaL_optstring(state, -1, "");
    lua_getfield(state, 1, "description");
    spec.description = luaL_optstring(state, -1, "");
    lua_getfield(state, 1, "shop");
    const std::string shop = luaL_optstring(state, -1, "last");
    if (shop != "first" && shop != "last") return luaL_error(state, "tower shop must be \"first\" or \"last\"");
    spec.shop_first = shop == "first";
    lua_getfield(state, 1, "progression");
    const std::string progression = luaL_optstring(state, -1, "open");
    if (progression != "open" && progression != "xp") {
        return luaL_error(state, "tower progression must be \"open\" or \"xp\"");
    }
    spec.xp_progression = progression == "xp";
    lua_getfield(state, 1, "unlock_rank");
    if (!lua_isnil(state, -1)) {
        int is_integer = 0;
        const lua_Integer rank = lua_tointegerx(state, -1, &is_integer);
        if (is_integer == 0) return luaL_error(state, "tower unlock_rank must be an integer");
        spec.unlock_rank = rank;
    }
    lua_getfield(state, 1, "restricted_modes");
    const std::string restricted = luaL_optstring(state, -1, "base");
    if (restricted == "base") {
        spec.restricted_modes = RestrictedModes::base;
    } else if (restricted == "always") {
        spec.restricted_modes = RestrictedModes::always;
    } else if (restricted == "never") {
        spec.restricted_modes = RestrictedModes::never;
    } else {
        return luaL_error(state, "tower restricted_modes must be \"base\", \"always\" or \"never\"");
    }
    const auto convert = [state](const char* field, nlohmann::ordered_json& out) -> std::string {
        lua_getfield(state, 1, field);
        std::string error;
        if (!lua_isnil(state, -1)) error = lua_to_json(state, -1, out);
        lua_pop(state, 1);
        return error.empty() ? std::string() : std::string("tower ") + field + " " + error;
    };
    std::string content_error;
    nlohmann::ordered_json stats;
    if (content_error = convert("stats", stats); content_error.empty() && !stats.is_null()) {
        content_error = parse_tower_stats(stats, spec.stats);
    }
    nlohmann::ordered_json weapons;
    if (content_error.empty()) content_error = convert("weapons", weapons);
    if (content_error.empty() && !weapons.is_null()) {
        if (!weapons.is_array()) content_error = "tower weapons must be a list";
        for (std::size_t i = 0; content_error.empty() && weapons.is_array() && i < weapons.size(); ++i) {
            nlohmann::ordered_json weapon;
            content_error = parse_tower_weapon(weapons[i], weapon);
            if (content_error.empty()) {
                spec.weapons.push_back(std::move(weapon));
            } else {
                content_error = "tower weapon " + std::to_string(i + 1) + ": " + content_error;
            }
        }
    }
    nlohmann::ordered_json upgrades;
    if (content_error.empty()) content_error = convert("upgrades", upgrades);
    if (content_error.empty() && !upgrades.is_null()) {
        if (!upgrades.is_array() || upgrades.size() > 2) content_error = "tower upgrades must be a list of up to two paths";
        for (std::size_t path = 0; content_error.empty() && path < upgrades.size(); ++path) {
            const auto& tiers = upgrades[path];
            if (tiers.is_object() && tiers.empty()) continue;  // {} keeps the base tower's path
            if (!tiers.is_array()) {
                content_error = "tower upgrade path " + std::to_string(path + 1) + " must be a list of tiers";
                break;
            }
            for (const auto& entry : tiers) {
                CustomUpgrade tier;
                content_error = parse_upgrade_tier(entry, tier);
                if (!content_error.empty()) break;
                spec.upgrades[path].push_back(std::move(tier));
            }
        }
    }
    nlohmann::ordered_json sprite;
    if (content_error.empty()) content_error = convert("sprite", sprite);
    if (content_error.empty() && !sprite.is_null()) {
        content_error = parse_sprite(sprite, spec.sprite, spec.sprite_frames, spec.sprite_length);
        if (!content_error.empty()) content_error = "tower " + content_error;
    }
    for (const auto& [field, out] : {std::pair<const char*, std::string*>{"icon", &spec.icon},
             {"avatar", &spec.avatar}}) {
        if (!content_error.empty()) break;
        lua_getfield(state, 1, field);
        if (!lua_isnil(state, -1)) {
            if (lua_type(state, -1) != LUA_TSTRING) {
                content_error = std::string("tower ") + field + " must be a sprite cell name";
            } else {
                *out = lua_tostring(state, -1);
            }
        }
        lua_pop(state, 1);
    }
    nlohmann::ordered_json textures;
    if (content_error.empty()) content_error = convert("textures", textures);
    if (content_error.empty() && !textures.is_null()) {
        if (!textures.is_array()) content_error = "tower textures must be a list of sheet names";
        for (std::size_t i = 0; content_error.empty() && textures.is_array() && i < textures.size(); ++i) {
            if (!textures[i].is_string()) {
                content_error = "tower textures must be a list of sheet names";
                break;
            }
            // Sheets live in the package's assets/textures/ folder.
            CustomTexture texture;
            content_error = load_custom_texture(mod->options_.resource_directory / "assets" / "textures",
                textures[i].get<std::string>(), texture);
            if (content_error.empty()) spec.textures.push_back(std::move(texture));
        }
    }
    if (!content_error.empty()) return luaL_error(state, "%s", content_error.c_str());
    // Callbacks run as event handlers filtered to this tower's type, under the
    // same sandbox and handler limits as btd5.events.on.
    constexpr std::array<std::pair<const char*, const char*>, 3> kCallbacks{{
        {"on_place", "tower.placed"}, {"on_upgrade", "tower.upgraded"}, {"on_sell", "tower.selling"}}};
    for (const auto& [field, event] : kCallbacks) {
        lua_getfield(state, 1, field);
        const bool given = !lua_isnil(state, -1);
        if (given && !lua_isfunction(state, -1)) return luaL_error(state, "tower %s must be a function", field);
        lua_pop(state, 1);
    }
    if (!mod->options_.register_tower) return luaL_error(state, "custom towers are unavailable");
    const std::string full_id = mod->options_.mod_id + ":" + id;
    const std::string error = mod->options_.register_tower(full_id, std::move(spec));
    if (!error.empty()) return luaL_error(state, "%s", error.c_str());
    static constexpr char kFilter[] =
        "local id, callback = ...\n"
        "return function(event)\n"
        "  local tower = event.tower\n"
        "  if tower ~= nil and tower:type() == id then return callback(tower, event) end\n"
        "end\n";
    for (const auto& [field, event] : kCallbacks) {
        lua_getfield(state, 1, field);
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            continue;
        }
        const int callback = lua_gettop(state);
        lua_pushlightuserdata(state, mod);
        lua_pushcclosure(state, &guarded_api<&LuaMod::api_event_on>, 1);
        lua_pushstring(state, event);
        if (luaL_loadbufferx(state, kFilter, sizeof(kFilter) - 1, "=tower_callback", "t") != LUA_OK) {
            return lua_error(state);
        }
        lua_pushstring(state, full_id.c_str());
        lua_pushvalue(state, callback);
        lua_call(state, 2, 1);
        lua_call(state, 2, 1);
        lua_pop(state, 2);
    }
    lua_pushboolean(state, 1);
    return 1;
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
    LuaReference owner(state, reference);
    mod->timers_.push_back({mod->current_tick_ + static_cast<std::uint64_t>(ticks), reference});
    owner.release();
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
    LuaReference owner(state, reference);
    const std::uint64_t token = mod->next_event_token_++;
    mod->event_handlers_.push_back({token, event_name, reference, true});
    owner.release();
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

int LuaMod::api_game_object_type(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    void* const resolved = object->registry == nullptr
                               ? nullptr
                               : object->registry->resolve(object->handle, object->handle.kind);
    if (resolved == nullptr) return luaL_error(state, "game object is stale");
    if (object->handle.kind != GameObjectKind::Tower &&
        object->handle.kind != GameObjectKind::Bloon) {
        return luaL_error(state, "type is available only on tower and bloon objects");
    }
    if (object->owner == nullptr || !object->owner->options_.game_object_text_get) {
        return luaL_error(state, "object type is unavailable on this game build");
    }
    const auto value = object->owner->options_.game_object_text_get(
        object->handle.kind, resolved, "type");
    if (!value) return luaL_error(state, "object type could not be read");
    if (value->empty()) {
        lua_pushnil(state);
    } else {
        lua_pushlstring(state, value->data(), value->size());
    }
    return 1;
}

int LuaMod::api_game_object_upgrade_tiers(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    void* const resolved = object->registry == nullptr
                               ? nullptr
                               : object->registry->resolve(object->handle, object->handle.kind);
    if (resolved == nullptr) return luaL_error(state, "game object is stale");
    if (object->handle.kind != GameObjectKind::Tower) {
        return luaL_error(state, "upgrade_tiers is available only on tower objects");
    }
    if (object->owner == nullptr || !object->owner->options_.game_object_integer_get) {
        return luaL_error(state, "tower upgrade_tiers is unavailable on this game build");
    }
    const auto& get = object->owner->options_.game_object_integer_get;
    const auto left = get(object->handle.kind, resolved, "upgrade_tier_left");
    const auto right = get(object->handle.kind, resolved, "upgrade_tier_right");
    if (!left || !right) return luaL_error(state, "tower upgrade_tiers could not be read");
    lua_pushinteger(state, static_cast<lua_Integer>(*left));
    lua_pushinteger(state, static_cast<lua_Integer>(*right));
    return 2;
}

int LuaMod::api_game_object_number(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    void* const resolved = object->registry == nullptr
                               ? nullptr
                               : object->registry->resolve(object->handle, object->handle.kind);
    if (resolved == nullptr) return luaL_error(state, "game object is stale");
    if (object->handle.kind != GameObjectKind::Round) {
        return luaL_error(state, "number is available only on round objects");
    }
    if (object->owner == nullptr || !object->owner->options_.game_object_integer_get) {
        return luaL_error(state, "round number is unavailable on this game build");
    }
    const auto value = object->owner->options_.game_object_integer_get(
        object->handle.kind, resolved, "number");
    if (!value) return luaL_error(state, "round number could not be read");
    lua_pushinteger(state, static_cast<lua_Integer>(*value));
    return 1;
}

int LuaMod::api_game_object_set_cash(lua_State* state) {
    const auto* object = static_cast<LuaGameObject*>(
        luaL_checkudata(state, 1, kGameObjectMetatable));
    void* const resolved = object->registry == nullptr
                               ? nullptr
                               : object->registry->resolve(object->handle, object->handle.kind);
    if (resolved == nullptr) return luaL_error(state, "game object is stale");
    if (object->handle.kind != GameObjectKind::Match) {
        return luaL_error(state, "set_cash is available only on match objects");
    }
    if (lua_type(state, 2) != LUA_TNUMBER) {
        return luaL_error(state, "match cash must be a number");
    }
    const double requested = static_cast<double>(lua_tonumber(state, 2));
    if (!std::isfinite(requested) || requested < 0.0 ||
        requested > static_cast<double>((std::numeric_limits<std::int32_t>::max)())) {
        return luaL_error(state, "match cash is outside the supported range");
    }
    if (object->owner == nullptr || !object->owner->options_.game_object_number_set) {
        return luaL_error(state, "match cash is unavailable on this game build");
    }
    std::string error;
    if (!object->owner->options_.game_object_number_set(
            object->handle.kind, resolved, "cash", requested, error)) {
        return luaL_error(
            state, "%s", error.empty() ? "match cash could not be changed" : error.c_str());
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
    // Suspended threads and user finalizers cannot outlive protected execution.
    // Coroutines are unavailable until the host owns their resume/close budgets.
    lua_pushcfunction(state_, &LuaMod::api_setmetatable);
    lua_setglobal(state_, "setmetatable");

    set_nil(state_, "dofile");
    set_nil(state_, "loadfile");
    set_nil(state_, "load");
    set_nil(state_, "collectgarbage");
}

int LuaMod::api_setmetatable(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    if (!lua_isnil(state, 2)) {
        luaL_checktype(state, 2, LUA_TTABLE);
        for (const char* field : {"__gc", "__close"}) {
            lua_pushstring(state, field);
            lua_rawget(state, 2);
            if (!lua_isnil(state, -1)) {
                return luaL_error(state, "mod finalizers are unavailable");
            }
            lua_pop(state, 1);
        }
    }
    if (luaL_getmetafield(state, 1, "__metatable") != LUA_TNIL) {
        return luaL_error(state, "cannot change a protected metatable");
    }
    lua_settop(state, 2);
    lua_setmetatable(state, 1);
    return 1;
}

void LuaMod::register_api() {
    if (luaL_newmetatable(state_, kGameObjectMetatable) != 0) {
        lua_newtable(state_);
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_is_valid>);
        lua_setfield(state_, -2, "is_valid");
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_id>);
        lua_setfield(state_, -2, "id");
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_kind>);
        lua_setfield(state_, -2, "kind");
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_pop_count>);
        lua_setfield(state_, -2, "pop_count");
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_set_pop_count>);
        lua_setfield(state_, -2, "set_pop_count");
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_sell_price>);
        lua_setfield(state_, -2, "sell_price");
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_set_sell_price>);
        lua_setfield(state_, -2, "set_sell_price");
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_health>);
        lua_setfield(state_, -2, "health");
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_set_health>);
        lua_setfield(state_, -2, "set_health");
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_type>);
        lua_setfield(state_, -2, "type");
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_upgrade_tiers>);
        lua_setfield(state_, -2, "upgrade_tiers");
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_number>);
        lua_setfield(state_, -2, "number");
        lua_pushcfunction(state_, &guarded_api<&LuaMod::api_game_object_set_cash>);
        lua_setfield(state_, -2, "set_cash");
        lua_setfield(state_, -2, "__index");
        lua_pushliteral(state_, "BTD5 game object v1");
        lua_setfield(state_, -2, "__metatable");
    }
    lua_pop(state_, 1);

    lua_newtable(state_);
    set_api_function(state_, this, "log", &guarded_api<&LuaMod::api_log>);

    lua_newtable(state_);
    set_api_function(state_, this, "get", &guarded_api<&LuaMod::api_config_get>);
    lua_setfield(state_, -2, "config");

    lua_newtable(state_);
    set_api_function(state_, this, "get", &guarded_api<&LuaMod::api_storage_get>);
    set_api_function(state_, this, "set", &guarded_api<&LuaMod::api_storage_set>);
    lua_setfield(state_, -2, "storage");

    lua_newtable(state_);
    set_api_function(state_, this, "get", &guarded_api<&LuaMod::api_localize>);
    lua_setfield(state_, -2, "localization");

    lua_newtable(state_);
    set_api_function(state_, this, "read_text", &guarded_api<&LuaMod::api_read_resource>);
    lua_setfield(state_, -2, "resource");

    lua_newtable(state_);
    set_api_function(state_, this, "after", &guarded_api<&LuaMod::api_timer_after>);
    lua_setfield(state_, -2, "timer");

    lua_newtable(state_);
    set_api_function(state_, this, "register", &guarded_api<&LuaMod::api_towers_register>);
    lua_setfield(state_, -2, "towers");

    lua_newtable(state_);
    set_api_function(state_, this, "register", &guarded_api<&LuaMod::api_sounds_register>);
    set_api_function(state_, this, "play", &guarded_api<&LuaMod::api_sounds_play>);
    lua_setfield(state_, -2, "sounds");

    lua_newtable(state_);
    set_api_function(state_, this, "on", &guarded_api<&LuaMod::api_event_on>);
    set_api_function(state_, this, "off", &guarded_api<&LuaMod::api_event_off>);
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
    const bool outermost = callback_depth_ == 0;
    if (outermost) {
        instructions_remaining_ = options_.instruction_budget;
        deadline_ = std::chrono::steady_clock::now() + options_.callback_time_limit;
        budget_failure_ = nullptr;
    }
    if (std::chrono::steady_clock::now() >= deadline_) {
        budget_failure_ = "callback time limit exceeded";
    }
    if (budget_failure_ != nullptr) {
        lua_pop(state_, argument_count + 1);
        report_error(callback, budget_failure_);
        return false;
    }

    int result = LUA_OK;
    {
        struct ExecutionScope final {
            lua_State* state;
            std::size_t& depth;
            bool outermost;
            lua_Hook hook;
            int mask;
            int count;
            ~ExecutionScope() {
                --depth;
                if (outermost) lua_sethook(state, hook, mask, count);
            }
        } scope{state_, callback_depth_, outermost, lua_gethook(state_),
                lua_gethookmask(state_), lua_gethookcount(state_)};
        ++callback_depth_;
        if (outermost) {
            lua_sethook(state_, &LuaMod::instruction_hook, LUA_MASKCOUNT, kInstructionHookInterval);
        }
        // Nested callbacks share the remaining allowance, deadline and hook
        // countdown. Reinstalling the hook here would replenish partial ticks.
        result = lua_pcall(state_, argument_count, 0, 0);
    }
    if (std::chrono::steady_clock::now() >= deadline_) {
        budget_failure_ = "callback time limit exceeded";
    }
    if (result != LUA_OK) {
        const char* message = lua_type(state_, -1) == LUA_TSTRING ? lua_tostring(state_, -1) : nullptr;
        report_error(callback, message != nullptr ? message : "unknown Lua callback error");
        lua_pop(state_, 1);
        return false;
    }
    if (budget_failure_ != nullptr) {
        report_error(callback, budget_failure_);
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
        try {
            options_.log("error", last_error_);
        } catch (const std::exception&) {
            // A failed diagnostic sink must not replace the original Lua error.
        }
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
