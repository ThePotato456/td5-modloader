#include <Windows.h>

#include <array>
#include <filesystem>
#include <string>

#include <btd5loader/runtime_api.hpp>

namespace {

using InternetGetConnectedStateFunction = BOOL(WINAPI*)(LPDWORD, DWORD);

INIT_ONCE g_wininet_once = INIT_ONCE_STATIC_INIT;
HMODULE g_real_wininet = nullptr;
InternetGetConnectedStateFunction g_internet_get_connected_state = nullptr;
HMODULE g_bootstrap_module = nullptr;

std::filesystem::path module_path(HMODULE module) {
    std::wstring buffer(512, L'\0');
    for (;;) {
        const DWORD written = GetModuleFileNameW(
            module,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (written < buffer.size() - 1) {
            buffer.resize(written);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

void write_bootstrap_failure(const DWORD error) noexcept {
    const auto game_directory = module_path(nullptr).parent_path();
    const auto log_path = game_directory / L"btd5loader-bootstrap.log";
    const HANDLE file = CreateFileW(
        log_path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    const std::string line =
        "{\"level\":\"error\",\"component\":\"bootstrap\","
        "\"message\":\"runtime_load_failed\",\"win32Error\":" +
        std::to_string(error) + "}\r\n";
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
}

BOOL CALLBACK initialize_real_wininet(PINIT_ONCE, PVOID, PVOID*) {
    std::array<wchar_t, MAX_PATH> system_directory{};
    const UINT length = GetSystemDirectoryW(
        system_directory.data(),
        static_cast<UINT>(system_directory.size()));
    if (length == 0 || length >= system_directory.size()) {
        return FALSE;
    }

    const auto path = std::filesystem::path(system_directory.data()) / L"wininet.dll";
    g_real_wininet = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (g_real_wininet == nullptr) {
        return FALSE;
    }

    g_internet_get_connected_state =
        reinterpret_cast<InternetGetConnectedStateFunction>(
            GetProcAddress(g_real_wininet, "InternetGetConnectedState"));
    return g_internet_get_connected_state != nullptr;
}

void load_runtime() noexcept {
    const auto game_directory = module_path(nullptr).parent_path();
    const auto bootstrap_directory = module_path(g_bootstrap_module).parent_path();
    const auto runtime_path = bootstrap_directory / L"btd5loader_runtime.dll";

    const HMODULE runtime = LoadLibraryExW(
        runtime_path.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (runtime == nullptr) {
        write_bootstrap_failure(GetLastError());
        return;
    }

    const auto initialize = reinterpret_cast<btd5loader::runtime::InitializeFunction>(
        GetProcAddress(runtime, btd5loader::runtime::kInitializeExport));
    if (initialize == nullptr || !initialize(game_directory.c_str())) {
        write_bootstrap_failure(initialize == nullptr ? ERROR_PROC_NOT_FOUND : ERROR_DLL_INIT_FAILED);
    }
}

}  // namespace

extern "C" BOOL WINAPI InternetGetConnectedState(LPDWORD flags, DWORD reserved) {
    if (!InitOnceExecuteOnce(
            &g_wininet_once,
            initialize_real_wininet,
            nullptr,
            nullptr)) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    return g_internet_get_connected_state(flags, reserved);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_bootstrap_module = instance;
        DisableThreadLibraryCalls(instance);
        load_runtime();
    }
    return TRUE;
}
