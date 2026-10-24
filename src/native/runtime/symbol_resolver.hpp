// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "compatibility.hpp"

namespace btd5loader::runtime {

struct ResolvedSymbol final {
    std::string name;
    std::uint32_t relative_virtual_address{};
};

struct SymbolDiagnostic final {
    std::string name;
    bool required{};
    std::string message;
};

struct ResolutionReport final {
    bool success{};
    std::vector<ResolvedSymbol> resolved;
    std::vector<SymbolDiagnostic> diagnostics;
};

[[nodiscard]] ResolutionReport resolve_symbols_from_image(
    const BuildDefinition& build,
    const std::filesystem::path& executable_path);

}  // namespace btd5loader::runtime
