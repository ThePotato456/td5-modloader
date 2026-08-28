#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <btd5loader/runtime_api.hpp>
#include <btd5loader/version.hpp>

#include <nlohmann/json.hpp>

#include "active_profile.hpp"
#include "logger.hpp"
#include "compatibility.hpp"
#include "lua_mod.hpp"
#include "mod_package.hpp"
#include "runtime_state.hpp"
#include "symbol_resolver.hpp"

namespace {

btd5loader::runtime::StateMachine g_state;
btd5loader::runtime::Logger g_logger;
std::filesystem::path g_game_directory;
std::filesystem::path g_session_directory;
std::vector<std::unique_ptr<btd5loader::runtime::LuaMod>> g_mods;
std::mutex g_mods_mutex;

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

    const auto handoff_path = environment_value(L"BTD5ML_ACTIVE_PROFILE");
    if (handoff_path.empty()) {
        g_logger.info("mods", "no_active_profile");
        return true;
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
    if (!g_state.transition_to(State::HooksReady)) {
        g_logger.error("runtime", "invalid_hooks_ready_transition");
        (void)g_state.transition_to(State::Failed);
        return ERROR_INVALID_STATE;
    }
    g_logger.info("runtime", "hooks_ready_no_hooks_registered");
    if (!load_active_mods(detection.build->id)) {
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
