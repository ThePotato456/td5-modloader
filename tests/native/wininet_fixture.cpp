#include <Windows.h>
#include <wininet.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <nlohmann/json.hpp>

#include "../../src/native/include/btd5loader/runtime_api.hpp"

namespace {

using InternetGetConnectedStateFunction = BOOL(WINAPI*)(LPDWORD, DWORD);

}  // namespace

int main(int count, char**) {
    if (count > 1) {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 16;
        const auto ticks = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        const auto root = std::filesystem::current_path();
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const nlohmann::json ticket{{"schemaVersion", 1}, {"launchProtocol", 1}, {"mode", "modded"},
            {"processId", GetCurrentProcessId()}, {"processStartedAt", std::to_string(ticks)},
            {"issuedAt", now}, {"expiresAt", now + 60}, {"gameDirectory", root.generic_string()},
            {"dataRoot", (root / "local" / "BTD5ModLoader").generic_string()}, {"profile", "fixture"},
            {"buildId", "fixture"}, {"mods", nlohmann::json::array()}};
        std::filesystem::create_directories(root / ".btd5ml-launch");
        std::ofstream(root / ".btd5ml-launch" / (std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(ticks) + ".json")) << ticket.dump();
    }
    std::array<wchar_t, MAX_PATH> system_directory{};
    const UINT length = GetSystemDirectoryW(
        system_directory.data(),
        static_cast<UINT>(system_directory.size()));
    if (length == 0 || length >= system_directory.size()) {
        return 10;
    }

    const auto path = std::filesystem::path(system_directory.data()) / L"wininet.dll";
    const HMODULE real_module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (real_module == nullptr) {
        return 11;
    }

    const auto real_function = reinterpret_cast<InternetGetConnectedStateFunction>(
        GetProcAddress(real_module, "InternetGetConnectedState"));
    if (real_function == nullptr) {
        FreeLibrary(real_module);
        return 12;
    }

    DWORD imported_flags = 0;
    DWORD real_flags = 0;
    const BOOL imported_result = InternetGetConnectedState(&imported_flags, 0);
    const BOOL real_result = real_function(&real_flags, 0);
    FreeLibrary(real_module);

    Sleep(250);
    if (const HMODULE runtime = GetModuleHandleW(L"btd5loader_runtime.dll")) {
        const auto shutdown = reinterpret_cast<void(WINAPI*)()>(
            GetProcAddress(runtime, "BTD5Loader_Shutdown"));
        const auto state = reinterpret_cast<btd5loader::runtime::State(WINAPI*)()>(
            GetProcAddress(runtime, "BTD5Loader_GetState"));
        if (shutdown == nullptr || state == nullptr) return 14;
        shutdown();
        shutdown();
        if (state() != btd5loader::runtime::State::ShuttingDown) return 15;
    }
    return imported_result == real_result && imported_flags == real_flags ? 0 : 13;
}
