// SPDX-License-Identifier: GPL-3.0-only
#include <filesystem>
#include <iostream>
#include <iomanip>

#include "../../src/native/runtime/compatibility.hpp"
#include "../../src/native/runtime/symbol_resolver.hpp"

int wmain(const int argument_count, wchar_t** arguments) {
    if (argument_count != 3) {
        std::cerr << "usage: btd5loader_symbol_inspector <game-directory> <symbol-map-directory>\n";
        return 2;
    }
    const std::filesystem::path game_directory(arguments[1]);
    const auto detection = btd5loader::runtime::detect_build(game_directory, arguments[2]);
    if (!detection.build) {
        std::cerr << "compatibility: " << detection.error << '\n';
        return 3;
    }
    std::cout << "build: " << detection.build->id << '\n';
    const auto report = btd5loader::runtime::resolve_symbols_from_image(
        *detection.build, game_directory / L"BTD5-Win.exe");
    for (const auto& symbol : report.resolved) {
        std::cout << "resolved: " << symbol.name << " rva=0x"
                  << std::hex << std::uppercase << symbol.relative_virtual_address
                  << std::dec << '\n';
    }
    for (const auto& diagnostic : report.diagnostics) {
        std::cout << (diagnostic.required ? "required" : "optional") << ": "
                  << diagnostic.name << ": " << diagnostic.message << '\n';
    }
    return report.success ? 0 : 4;
}
