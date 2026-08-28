// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lua_State;
struct lua_Debug;

namespace btd5loader::runtime {

struct LuaModOptions final {
    std::string mod_id;
    std::filesystem::path storage_directory;
    std::filesystem::path resource_directory;
    std::unordered_map<std::string, std::string> configuration;
    std::unordered_map<std::string, std::string> localization;
    std::size_t memory_limit_bytes{16U * 1024U * 1024U};
    std::uint64_t instruction_budget{250'000};
    std::chrono::milliseconds callback_time_limit{100};
    std::size_t callback_recursion_limit{32};
    std::function<void(std::string_view, std::string_view)> log;
};

class LuaMod final {
public:
    explicit LuaMod(LuaModOptions options);
    ~LuaMod();

    LuaMod(const LuaMod&) = delete;
    LuaMod& operator=(const LuaMod&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool load_script(std::string_view script, std::string_view source_name);
    [[nodiscard]] bool invoke(std::string_view callback);
    void advance_timers(std::uint64_t ticks);

    [[nodiscard]] bool callback_disabled(std::string_view callback) const;
    [[nodiscard]] std::string_view last_error() const noexcept;
    [[nodiscard]] std::size_t memory_used() const noexcept;

private:
    struct MemoryState final {
        std::size_t used{};
        std::size_t limit{};
    };

    struct Timer final {
        std::uint64_t due_tick{};
        int function_reference{};
    };

    static void* allocate(void* user_data, void* pointer, std::size_t old_size, std::size_t new_size);
    static void instruction_hook(lua_State* state, lua_Debug* debug_record);
    static int api_log(lua_State* state);
    static int api_config_get(lua_State* state);
    static int api_storage_get(lua_State* state);
    static int api_storage_set(lua_State* state);
    static int api_localize(lua_State* state);
    static int api_read_resource(lua_State* state);
    static int api_timer_after(lua_State* state);

    static LuaMod* from_upvalue(lua_State* state);
    void open_sandbox();
    void register_api();
    void load_storage();
    [[nodiscard]] bool save_storage();
    [[nodiscard]] bool execute_at_stack_top(std::string_view callback);
    void report_error(std::string_view callback, std::string_view message);
    [[nodiscard]] bool safe_resource_path(
        std::string_view requested,
        std::filesystem::path& resolved) const;

    LuaModOptions options_;
    MemoryState memory_;
    lua_State* state_{};
    std::unordered_map<std::string, std::string> storage_;
    std::unordered_set<std::string> disabled_callbacks_;
    std::vector<Timer> timers_;
    std::uint64_t current_tick_{};
    std::uint64_t instructions_remaining_{};
    std::chrono::steady_clock::time_point deadline_{};
    std::size_t callback_depth_{};
    std::string last_error_;
};

}  // namespace btd5loader::runtime
