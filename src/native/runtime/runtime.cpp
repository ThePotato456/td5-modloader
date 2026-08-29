#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <chrono>

#include <btd5loader/runtime_api.hpp>
#include <btd5loader/version.hpp>

#include <nlohmann/json.hpp>

#include "active_profile.hpp"
#include "bloon_action_hook.hpp"
#include "compatibility.hpp"
#include "frame_hook.hpp"
#include "hook_transaction.hpp"
#include "lives_hook.hpp"
#include "lives_write_hook.hpp"
#include "logger.hpp"
#include "lua_mod.hpp"
#include "match_hook.hpp"
#include "mod_package.hpp"
#include "native_event_hook.hpp"
#include "runtime_state.hpp"
#include "symbol_resolver.hpp"
#include "tower_placement_hook.hpp"
#include "tower_sale_hook.hpp"
#include "tower_upgrade_hook.hpp"

namespace {

btd5loader::runtime::StateMachine g_state;
btd5loader::runtime::Logger g_logger;
std::filesystem::path g_game_directory;
std::filesystem::path g_session_directory;
std::vector<std::unique_ptr<btd5loader::runtime::LuaMod>> g_mods;
std::mutex g_mods_mutex;
btd5loader::runtime::GameObjectRegistry g_game_objects;
btd5loader::runtime::BloonActionHook g_bloon_action_hook;
btd5loader::runtime::FrameHook g_frame_hook;
btd5loader::runtime::LivesHook g_lives_hook;
btd5loader::runtime::LivesWriteHook g_lives_write_hook;
btd5loader::runtime::MatchHook g_match_hook;
btd5loader::runtime::NativeEventHook g_native_event_hook;
btd5loader::runtime::TowerPlacementHook g_tower_placement_hook;
btd5loader::runtime::TowerSaleHook g_tower_sale_hook;
btd5loader::runtime::TowerUpgradeHook g_tower_upgrade_hook;
btd5loader::runtime::HookTransaction g_hooks;

void dispatch_game_event(
    const std::string_view name,
    const btd5loader::runtime::LuaEventFields& fields = {}) {
    using btd5loader::runtime::State;

    if (g_state.current() != State::GameReady) {
        return;
    }
    std::scoped_lock lock(g_mods_mutex);
    g_logger.info("events", std::string(name));
    for (const auto& mod : g_mods) {
        (void)mod->dispatch_event(name, fields);
    }
}

void dispatch_lives_event(
    const std::string_view name,
    const std::int32_t before,
    const std::int32_t after) {
    dispatch_game_event(
        name,
        {
            {"old_lives", static_cast<std::int64_t>(before)},
            {"new_lives", static_cast<std::int64_t>(after)},
        });
}

bool dispatch_lives_changing(
    const std::int32_t before,
    const std::int32_t after) {
    using btd5loader::runtime::State;

    if (g_state.current() != State::GameReady) {
        return false;
    }
    const btd5loader::runtime::LuaEventFields fields{
        {"old_lives", static_cast<std::int64_t>(before)},
        {"new_lives", static_cast<std::int64_t>(after)},
    };
    bool cancelled = false;
    std::scoped_lock lock(g_mods_mutex);
    g_logger.info("events", "lives.changing");
    for (const auto& mod : g_mods) {
        cancelled = mod->dispatch_event("lives.changing", fields, true).cancelled || cancelled;
    }
    if (cancelled) {
        g_logger.info("events", "lives.changing:cancelled");
    }
    return cancelled;
}

void* capture_game_object_event(void* const event) noexcept {
    return event != nullptr ? static_cast<void**>(event)[1] : nullptr;
}

void dispatch_tower_event(
    const std::string_view name,
    void* const tower,
    const bool invalidate_after) {
    using btd5loader::runtime::GameObjectKind;

    if (tower == nullptr) {
        return;
    }
    const auto handle = g_game_objects.find_or_add(GameObjectKind::Tower, tower);
    if (handle.id == 0) {
        g_logger.error("events", "tower_handle_unavailable");
        return;
    }
    try {
        dispatch_game_event(name, {{"tower", handle}});
    } catch (...) {
        if (invalidate_after) {
            (void)g_game_objects.invalidate(handle);
        }
        throw;
    }
    if (invalidate_after) {
        (void)g_game_objects.invalidate(handle);
    }
}

void dispatch_bloon_event(
    const std::string_view name,
    void* const bloon,
    const bool invalidate_after) {
    using btd5loader::runtime::GameObjectKind;

    if (bloon == nullptr) {
        return;
    }
    const auto handle = g_game_objects.find_or_add(GameObjectKind::Bloon, bloon);
    if (handle.id == 0) {
        g_logger.error("events", "bloon_handle_unavailable");
        return;
    }
    try {
        dispatch_game_event(name, {{"bloon", handle}});
    } catch (...) {
        if (invalidate_after) {
            (void)g_game_objects.invalidate(handle);
        }
        throw;
    }
    if (invalidate_after) {
        (void)g_game_objects.invalidate(handle);
    }
}

void dispatch_frame(HDC) {
    using btd5loader::runtime::State;

    thread_local bool dispatching = false;
    if (dispatching) {
        return;
    }
    dispatching = true;
    const auto reset_dispatching = []() { dispatching = false; };

    {
        std::scoped_lock lock(g_mods_mutex);
        if (g_state.current() == State::ModsLoading) {
            if (!g_state.transition_to(State::GameReady)) {
                g_logger.error("runtime", "invalid_game_ready_transition");
                reset_dispatching();
                return;
            }
            g_logger.info("runtime", "game_ready_frame_hook");
            for (const auto& mod : g_mods) {
                (void)mod->invoke("on_ready");
            }
        }
        if (g_state.current() == State::GameReady) {
            for (const auto& mod : g_mods) {
                mod->advance_timers(1);
            }
        }
    }
    reset_dispatching();
}

std::wstring environment_value(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required) {
        return {};
    }
    value.resize(written);
    return value;
}

std::filesystem::path data_root() {
    const auto override_path = environment_value(L"BTD5ML_DATA_ROOT");
    if (!override_path.empty()) {
        return override_path;
    }
    const auto local_app_data = environment_value(L"LOCALAPPDATA");
    return local_app_data.empty()
               ? std::filesystem::path{}
               : std::filesystem::path(local_app_data) / L"BTD5ModLoader";
}

std::filesystem::path path_from_utf8(const std::string_view text) {
    return std::filesystem::path(
        std::u8string(
            reinterpret_cast<const char8_t*>(text.data()),
            reinterpret_cast<const char8_t*>(text.data() + text.size())));
}

bool read_text_file(
    const std::filesystem::path& path,
    std::string& output,
    std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "file could not be opened: " + path.filename().string();
        return false;
    }
    output.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) {
        error = "file could not be read: " + path.filename().string();
        return false;
    }
    return true;
}

bool load_localization(
    const std::filesystem::path& resource_directory,
    std::unordered_map<std::string, std::string>& localization,
    std::string& error) {
    const auto path = resource_directory / L"localization" / L"en-US.json";
    if (!std::filesystem::is_regular_file(path)) {
        return true;
    }
    std::string text;
    if (!read_text_file(path, text, error)) {
        return false;
    }
    try {
        localization = nlohmann::json::parse(text)
                           .get<std::unordered_map<std::string, std::string>>();
        return true;
    } catch (const nlohmann::json::exception& exception) {
        error = std::string("localization is malformed: ") + exception.what();
        return false;
    }
}

bool load_active_mods(const std::string& build_id) {
    using btd5loader::runtime::LuaMod;
    using btd5loader::runtime::LuaModOptions;
    using btd5loader::runtime::State;

    auto handoff_path = environment_value(L"BTD5ML_ACTIVE_PROFILE");

    if (handoff_path.empty()) {
        const auto fallback_path =
            data_root() / L"runtime" / L"active-profile.json";

        std::error_code ec;

        if (std::filesystem::is_regular_file(fallback_path, ec) && !ec) {
            const auto modified_time =
                std::filesystem::last_write_time(fallback_path, ec);

            if (!ec) {
                const auto now =
                    std::filesystem::file_time_type::clock::now();

                const auto age = now - modified_time;

                if (age <= std::chrono::seconds(60)) {
                    handoff_path = fallback_path.wstring();
                    g_logger.info("mods", "active_profile_fallback");
                } else {
                    g_logger.info("mods", "active_profile_fallback_stale");
                    return true;
                }
            } else {
                g_logger.info("mods", "active_profile_fallback_timestamp_failed");
                return true;
            }
        } else {
            g_logger.info("mods", "no_active_profile");
            return true;
        }
    }
    std::string error;
    const auto active = btd5loader::runtime::load_active_profile(handoff_path, error);
    if (!active || active->build_id != build_id) {
        g_logger.error("mods", active ? "active_profile_build_mismatch" : error);
        return false;
    }
    if (!g_state.transition_to(State::ModsLoading)) {
        g_logger.error("runtime", "invalid_mods_loading_transition");
        return false;
    }
    const auto root = data_root();
    if (root.empty()) {
        g_logger.error("mods", "manager_data_root_unavailable");
        return false;
    }
    std::error_code filesystem_error;
    const auto packages_root = std::filesystem::weakly_canonical(root / L"packages", filesystem_error);
    if (filesystem_error) {
        g_logger.error("mods", "manager_package_root_unavailable");
        return false;
    }
    g_session_directory = root / L"runtime" / L"sessions" /
                          (std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(g_session_directory, filesystem_error);
    if (filesystem_error) {
        g_logger.error("mods", "session_directory_creation_failed");
        return false;
    }

    std::scoped_lock lock(g_mods_mutex);
    const auto rollback = []() {
        for (auto iterator = g_mods.rbegin(); iterator != g_mods.rend(); ++iterator) {
            (void)(*iterator)->invoke("on_shutdown");
        }
        g_mods.clear();
        std::error_code cleanup_error;
        if (!g_session_directory.empty()) {
            std::filesystem::remove_all(g_session_directory, cleanup_error);
        }
        g_session_directory.clear();
        return false;
    };
    for (const auto& requested : active->mods) {
        filesystem_error.clear();
        const auto archive_path = std::filesystem::weakly_canonical(
            requested.archive_path, filesystem_error);
        const auto root_end = std::mismatch(
            packages_root.begin(), packages_root.end(), archive_path.begin(), archive_path.end()).first;
        if (filesystem_error || root_end != packages_root.end()) {
            g_logger.error("mods", requested.id + ":package_outside_manager_storage");
            return rollback();
        }
        auto package = btd5loader::runtime::validate_mod_package(archive_path, error);
        if (!package || package->manifest.id != requested.id ||
            package->manifest.version_text != requested.version ||
            std::find(
                package->manifest.supported_game_builds.begin(),
                package->manifest.supported_game_builds.end(),
                build_id) == package->manifest.supported_game_builds.end()) {
            g_logger.error(
                "mods",
                requested.id + ":" + (package ? "package_identity_or_build_mismatch" : error));
            return rollback();
        }
        const auto resource_directory = g_session_directory / path_from_utf8(requested.id);
        if (!btd5loader::runtime::extract_mod_package(*package, resource_directory, error)) {
            g_logger.error("mods", requested.id + ":" + error);
            return rollback();
        }
        std::string script;
        if (!read_text_file(resource_directory / path_from_utf8(package->manifest.entry_point),
                            script,
                            error)) {
            g_logger.error("mods", requested.id + ":" + error);
            return rollback();
        }
        LuaModOptions options;
        options.mod_id = requested.id;
        options.storage_directory = root / L"storage" / path_from_utf8(requested.id);
        options.resource_directory = resource_directory;
        options.configuration = requested.configuration;
        options.object_registry = &g_game_objects;
        if (!load_localization(resource_directory, options.localization, error)) {
            g_logger.error("mods", requested.id + ":" + error);
            return rollback();
        }
        options.log = [](const std::string_view level, const std::string_view message) {
            if (level == "error") {
                g_logger.error("lua", std::string(message));
            } else {
                g_logger.info("lua", std::string(message));
            }
        };
        auto mod = std::make_unique<LuaMod>(std::move(options));
        if (!mod->valid() || !mod->load_script(script, package->manifest.entry_point)) {
            g_logger.error("mods", requested.id + ":lua_script_load_failed");
            return rollback();
        }
        if (!mod->invoke("on_load")) {
            g_logger.error("mods", requested.id + ":on_load_disabled_after_error");
        }
        g_mods.push_back(std::move(mod));
        g_logger.info("mods", requested.id + ":loaded");
    }
    g_logger.info("runtime", "mods_loaded_waiting_for_game_ready_hook");
    return true;
}

DWORD WINAPI initialize_worker(LPVOID) {
    using btd5loader::runtime::State;

    if (!g_logger.initialize()) {
        (void)g_state.transition_to(State::Failed);
        return ERROR_WRITE_FAULT;
    }

    g_logger.info("runtime", "bootstrap_started");
    g_logger.info("runtime", btd5loader::kVersion);

    if (g_game_directory.empty() ||
        !std::filesystem::is_regular_file(g_game_directory / L"BTD5-Win.exe")) {
        g_logger.error("runtime", "game_executable_not_found");
        (void)g_state.transition_to(State::Failed);
        return ERROR_FILE_NOT_FOUND;
    }

    if (!g_state.transition_to(State::CompatibilityCheck)) {
        g_logger.error("runtime", "invalid_compatibility_transition");
        (void)g_state.transition_to(State::Failed);
        return ERROR_INVALID_STATE;
    }

    const auto detection = btd5loader::runtime::detect_build(
        g_game_directory, g_game_directory / L"symbols");
    if (!detection.build) {
        g_logger.error("compatibility", detection.error);
        g_logger.info(
            "compatibility",
            "exe_sha256=" + detection.fingerprints.executable_sha256 +
                ";assets_sha256=" + detection.fingerprints.assets_sha256);
        (void)g_state.transition_to(State::Failed);
        return ERROR_REVISION_MISMATCH;
    }

    g_logger.info("compatibility", "supported_build=" + detection.build->id);
    const auto resolution = btd5loader::runtime::resolve_symbols_from_image(
        *detection.build, g_game_directory / L"BTD5-Win.exe");
    for (const auto& symbol : resolution.resolved) {
        g_logger.info("symbols", "resolved=" + symbol.name);
    }
    for (const auto& diagnostic : resolution.diagnostics) {
        const auto message = diagnostic.name + ":" + diagnostic.message;
        if (diagnostic.required) {
            g_logger.error("symbols", message);
        } else {
            g_logger.info("symbols", message);
        }
    }
    if (!resolution.success) {
        g_logger.error("symbols", "required_symbol_resolution_failed");
        (void)g_state.transition_to(State::Failed);
        return ERROR_INVALID_ADDRESS;
    }
    const auto game_screen_init = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "screen.game.init";
        });
    const auto game_screen_uninit = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "screen.game.uninit";
        });
    const auto event_dispatch = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.manager.dispatch";
        });
    const auto round_started_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.round.started.vtable";
        });
    const auto round_ended_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.round.ended.vtable";
        });
    const auto money_updated_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.money.updated.vtable";
        });
    const auto tower_spawned_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.tower.spawned.vtable";
        });
    const auto tower_upgraded_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.tower.upgraded.vtable";
        });
    const auto tower_sold_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.tower.sold.vtable";
        });
    const auto tower_manager_place = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "tower.manager.place";
        });
    const auto tower_upgrade_commit = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "tower.upgrade.commit";
        });
    const auto tower_sale_commit = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "tower.sale.commit";
        });
    const auto bloon_spawned_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.bloon.spawned.vtable";
        });
    const auto bloon_popped_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.bloon.popped.vtable";
        });
    const auto bloon_escaped_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.bloon.escaped.vtable";
        });
    const auto bloon_spawn_primary = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "bloon.manager.spawn.primary";
        });
    const auto bloon_spawn_secondary = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "bloon.manager.spawn.secondary";
        });
    const auto bloon_pop_commit = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "bloon.pop.commit";
        });
    const auto bloon_leak_commit = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "bloon.leak.commit";
        });
    const auto lives_gain_handler = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "player.lives.gain.handler";
        });
    const auto lives_loss_handler = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "player.lives.loss.handler";
        });
    const auto lives_gain_write = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "player.lives.gain.write";
        });
    const auto lives_loss_write = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "player.lives.loss.write";
        });
    const HMODULE executable = GetModuleHandleW(nullptr);
    if (game_screen_init == resolution.resolved.end() ||
        game_screen_uninit == resolution.resolved.end() ||
        event_dispatch == resolution.resolved.end() ||
        round_started_vtable == resolution.resolved.end() ||
        round_ended_vtable == resolution.resolved.end() ||
        money_updated_vtable == resolution.resolved.end() ||
        tower_spawned_vtable == resolution.resolved.end() ||
        tower_upgraded_vtable == resolution.resolved.end() ||
        tower_sold_vtable == resolution.resolved.end() ||
        tower_manager_place == resolution.resolved.end() ||
        tower_upgrade_commit == resolution.resolved.end() ||
        tower_sale_commit == resolution.resolved.end() ||
        bloon_spawned_vtable == resolution.resolved.end() ||
        bloon_popped_vtable == resolution.resolved.end() ||
        bloon_escaped_vtable == resolution.resolved.end() ||
        bloon_spawn_primary == resolution.resolved.end() ||
        bloon_spawn_secondary == resolution.resolved.end() ||
        bloon_pop_commit == resolution.resolved.end() ||
        bloon_leak_commit == resolution.resolved.end() ||
        lives_gain_handler == resolution.resolved.end() ||
        lives_loss_handler == resolution.resolved.end() ||
        lives_gain_write == resolution.resolved.end() ||
        lives_loss_write == resolution.resolved.end() || executable == nullptr) {
        g_logger.error("hooks", "gameplay lifecycle target unavailable");
        (void)g_state.transition_to(State::Failed);
        return ERROR_INVALID_ADDRESS;
    }
    void* const game_screen_init_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + game_screen_init->relative_virtual_address);
    void* const game_screen_uninit_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + game_screen_uninit->relative_virtual_address);
    void* const event_dispatch_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + event_dispatch->relative_virtual_address);
    void* const round_started_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + round_started_vtable->relative_virtual_address);
    void* const round_ended_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + round_ended_vtable->relative_virtual_address);
    void* const money_updated_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + money_updated_vtable->relative_virtual_address);
    void* const tower_spawned_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_spawned_vtable->relative_virtual_address);
    void* const tower_upgraded_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_upgraded_vtable->relative_virtual_address);
    void* const tower_sold_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_sold_vtable->relative_virtual_address);
    void* const tower_manager_place_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_manager_place->relative_virtual_address);
    void* const tower_upgrade_commit_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_upgrade_commit->relative_virtual_address);
    void* const tower_sale_commit_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_sale_commit->relative_virtual_address);
    void* const bloon_spawned_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_spawned_vtable->relative_virtual_address);
    void* const bloon_popped_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_popped_vtable->relative_virtual_address);
    void* const bloon_escaped_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_escaped_vtable->relative_virtual_address);
    void* const bloon_spawn_primary_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_spawn_primary->relative_virtual_address);
    void* const bloon_spawn_secondary_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_spawn_secondary->relative_virtual_address);
    void* const bloon_pop_commit_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_pop_commit->relative_virtual_address);
    void* const bloon_leak_commit_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_leak_commit->relative_virtual_address);
    void* const lives_gain_handler_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + lives_gain_handler->relative_virtual_address);
    void* const lives_loss_handler_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + lives_loss_handler->relative_virtual_address);
    void* const lives_gain_write_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + lives_gain_write->relative_virtual_address);
    void* const lives_loss_write_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + lives_loss_write->relative_virtual_address);
    g_hooks.add({
        "render.swap_buffers",
        true,
        []() {
            std::string error;
            const bool installed = g_frame_hook.install(&dispatch_frame, error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_frame_hook.remove(); }});
    g_hooks.add({
        "screen.game.lifecycle",
        true,
        [game_screen_init_target, game_screen_uninit_target]() {
            std::string error;
            const bool installed = g_match_hook.install(
                game_screen_init_target,
                game_screen_uninit_target,
                []() {
                    g_game_objects.begin_scene();
                    dispatch_game_event("match.starting");
                },
                []() { dispatch_game_event("match.started"); },
                []() { dispatch_game_event("match.ending"); },
                []() {
                    g_game_objects.begin_scene();
                    dispatch_game_event("match.ended");
                },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_match_hook.remove(); }});
    g_hooks.add({
        "tower.placing",
        true,
        [tower_manager_place_target]() {
            std::string error;
            const bool installed = g_tower_placement_hook.install(
                tower_manager_place_target,
                [](void* tower) { dispatch_tower_event("tower.placing", tower, false); },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_tower_placement_hook.remove(); }});
    g_hooks.add({
        "tower.upgrading",
        true,
        [tower_upgrade_commit_target]() {
            std::string error;
            const bool installed = g_tower_upgrade_hook.install(
                tower_upgrade_commit_target,
                [](void* tower) { dispatch_tower_event("tower.upgrading", tower, false); },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_tower_upgrade_hook.remove(); }});
    g_hooks.add({
        "tower.selling",
        true,
        [tower_sale_commit_target]() {
            std::string error;
            const bool installed = g_tower_sale_hook.install(
                tower_sale_commit_target,
                [](void* tower) { dispatch_tower_event("tower.selling", tower, false); },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_tower_sale_hook.remove(); }});
    g_hooks.add({
        "bloon.pre_actions",
        true,
        [bloon_spawn_primary_target,
         bloon_spawn_secondary_target,
         bloon_pop_commit_target,
         bloon_leak_commit_target]() {
            std::string error;
            const bool installed = g_bloon_action_hook.install(
                bloon_spawn_primary_target,
                bloon_spawn_secondary_target,
                bloon_pop_commit_target,
                bloon_leak_commit_target,
                [](void* bloon) { dispatch_bloon_event("bloon.spawning", bloon, false); },
                [](void* bloon) { dispatch_bloon_event("bloon.popping", bloon, false); },
                [](void* bloon) { dispatch_bloon_event("bloon.leaking", bloon, false); },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_bloon_action_hook.remove(); }});
    g_hooks.add({
        "event.gameplay.lifecycle",
        true,
        [event_dispatch_target,
         round_started_vtable_target,
         round_ended_vtable_target,
         money_updated_vtable_target,
         tower_spawned_vtable_target,
         tower_upgraded_vtable_target,
         tower_sold_vtable_target,
         bloon_spawned_vtable_target,
         bloon_popped_vtable_target,
         bloon_escaped_vtable_target]() {
            std::string error;
            const bool installed = g_native_event_hook.install(
                event_dispatch_target,
                {
                    {
                        round_started_vtable_target,
                        {},
                        [](void*) { dispatch_game_event("round.starting"); },
                        [](void*) { dispatch_game_event("round.started"); },
                    },
                    {
                        round_ended_vtable_target,
                        {},
                        [](void*) { dispatch_game_event("round.ending"); },
                        [](void*) { dispatch_game_event("round.ended"); },
                    },
                    {
                        money_updated_vtable_target,
                        {},
                        [](void*) { dispatch_game_event("cash.changing"); },
                        [](void*) { dispatch_game_event("cash.changed"); },
                    },
                    {
                        tower_spawned_vtable_target,
                        &capture_game_object_event,
                        {},
                        [](void* tower) { dispatch_tower_event("tower.placed", tower, false); },
                    },
                    {
                        tower_upgraded_vtable_target,
                        &capture_game_object_event,
                        {},
                        [](void* tower) { dispatch_tower_event("tower.upgraded", tower, false); },
                    },
                    {
                        tower_sold_vtable_target,
                        &capture_game_object_event,
                        {},
                        [](void* tower) { dispatch_tower_event("tower.sold", tower, true); },
                    },
                    {
                        bloon_spawned_vtable_target,
                        &capture_game_object_event,
                        {},
                        [](void* bloon) { dispatch_bloon_event("bloon.spawned", bloon, false); },
                    },
                    {
                        bloon_popped_vtable_target,
                        &capture_game_object_event,
                        {},
                        [](void* bloon) { dispatch_bloon_event("bloon.popped", bloon, true); },
                    },
                    {
                        bloon_escaped_vtable_target,
                        &capture_game_object_event,
                        {},
                        [](void* bloon) { dispatch_bloon_event("bloon.leaked", bloon, true); },
                    },
                },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_native_event_hook.remove(); }});
    g_hooks.add({
        "player.lives.changing",
        true,
        [lives_gain_write_target, lives_loss_write_target]() {
            std::string error;
            const bool installed = g_lives_write_hook.install(
                lives_gain_write_target,
                lives_loss_write_target,
                [](const std::int32_t before, const std::int32_t after) {
                    return dispatch_lives_changing(before, after);
                },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_lives_write_hook.remove(); }});
    g_hooks.add({
        "player.lives.changed",
        true,
        [lives_gain_handler_target, lives_loss_handler_target]() {
            std::string error;
            const bool installed = g_lives_hook.install(
                lives_gain_handler_target,
                lives_loss_handler_target,
                [](const std::int32_t before, const std::int32_t after) {
                    dispatch_lives_event("lives.changed", before, after);
                },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_lives_hook.remove(); }});
    std::string hook_error;
    if (!g_hooks.commit(hook_error)) {
        g_logger.error("hooks", hook_error);
        (void)g_state.transition_to(State::Failed);
        return ERROR_HOOK_NOT_INSTALLED;
    }
    if (!g_state.transition_to(State::HooksReady)) {
        g_hooks.rollback();
        g_logger.error("runtime", "invalid_hooks_ready_transition");
        (void)g_state.transition_to(State::Failed);
        return ERROR_INVALID_STATE;
    }
    g_logger.info(
        "runtime",
        "hooks_ready=render.swap_buffers,screen.game.init,screen.game.uninit,event.gameplay.lifecycle,"
        "tower.placing,tower.upgrading,tower.selling,bloon.spawning,bloon.popping,bloon.leaking,"
        "player.lives.changing,player.lives.changed");
    if (!load_active_mods(detection.build->id)) {
        g_hooks.rollback();
        g_logger.error("runtime", "active_mod_loading_failed");
        (void)g_state.transition_to(State::Failed);
        return ERROR_INVALID_DATA;
    }
    return ERROR_SUCCESS;
}

}  // namespace

extern "C" BOOL WINAPI BTD5Loader_Initialize(
    const wchar_t* game_directory) {
    using btd5loader::runtime::State;

    if (game_directory == nullptr || !g_state.transition_to(State::Bootstrap)) {
        return FALSE;
    }

    g_game_directory = game_directory;
    const HANDLE worker = CreateThread(nullptr, 0, initialize_worker, nullptr, 0, nullptr);
    if (worker == nullptr) {
        (void)g_state.transition_to(State::Failed);
        return FALSE;
    }
    CloseHandle(worker);
    return TRUE;
}

extern "C" btd5loader::runtime::State WINAPI BTD5Loader_GetState() {
    return g_state.current();
}

extern "C" void WINAPI BTD5Loader_Shutdown() {
    using btd5loader::runtime::State;
    g_hooks.rollback();
    g_game_objects.begin_scene();
    {
        std::scoped_lock lock(g_mods_mutex);
        for (auto iterator = g_mods.rbegin(); iterator != g_mods.rend(); ++iterator) {
            (void)(*iterator)->invoke("on_shutdown");
        }
        g_mods.clear();
    }
    std::error_code filesystem_error;
    if (!g_session_directory.empty()) {
        std::filesystem::remove_all(g_session_directory, filesystem_error);
        g_session_directory.clear();
    }
    if (g_state.transition_to(State::ShuttingDown)) {
        g_logger.info("runtime", "shutdown_requested");
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
