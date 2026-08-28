#include <Windows.h>
#include <wininet.h>

#include <array>
#include <filesystem>

namespace {

using InternetGetConnectedStateFunction = BOOL(WINAPI*)(LPDWORD, DWORD);

}  // namespace

int main() {
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
    return imported_result == real_result && imported_flags == real_flags ? 0 : 13;
}

