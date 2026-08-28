#include <Windows.h>

#include <filesystem>

#include <btd5loader/runtime_api.hpp>
#include <btd5loader/version.hpp>

#include "logger.hpp"
#include "runtime_state.hpp"

namespace {

btd5loader::runtime::StateMachine g_state;
btd5loader::runtime::Logger g_logger;
std::filesystem::path g_game_directory;

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

    g_logger.info("runtime", "compatibility_check_pending_phase_3");
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
