// SPDX-License-Identifier: GPL-3.0-only
#include "symbol_resolver.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_set>

#include "pattern.hpp"

namespace btd5loader::runtime {
namespace {

struct ImageSection final {
    std::string name;
    std::uint32_t virtual_address{};
    std::span<const std::byte> bytes;
};

template <typename Value>
std::optional<Value> read_structure(
    const std::span<const std::byte> bytes,
    const std::size_t offset) {
    if (offset > bytes.size() || sizeof(Value) > bytes.size() - offset) {
        return std::nullopt;
    }
    Value value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

std::optional<std::vector<std::byte>> read_image(
    const std::filesystem::path& path,
    std::string& error) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = "cannot open executable image";
        return std::nullopt;
    }
    const auto length = stream.tellg();
    if (length <= 0 || static_cast<unsigned long long>(length) >
                           (std::numeric_limits<std::size_t>::max)()) {
        error = "invalid executable image size";
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    stream.seekg(0, std::ios::beg);
    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(length));
    if (!stream) {
        error = "cannot read executable image";
        return std::nullopt;
    }
    return bytes;
}

std::optional<std::vector<ImageSection>> read_sections(
    const std::span<const std::byte> image,
    std::string& error) {
    const auto dos = read_structure<IMAGE_DOS_HEADER>(image, 0);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) {
        error = "image has no valid DOS header";
        return std::nullopt;
    }
    const auto nt_offset = static_cast<std::size_t>(dos->e_lfanew);
    const auto signature = read_structure<DWORD>(image, nt_offset);
    const auto file_header = read_structure<IMAGE_FILE_HEADER>(image, nt_offset + sizeof(DWORD));
    if (!signature || *signature != IMAGE_NT_SIGNATURE || !file_header ||
        file_header->Machine != IMAGE_FILE_MACHINE_I386) {
        error = "image is not a valid 32-bit PE file";
        return std::nullopt;
    }
    const auto optional_offset = nt_offset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
    const auto optional_magic = read_structure<WORD>(image, optional_offset);
    if (!optional_magic || *optional_magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        error = "image has no valid PE32 optional header";
        return std::nullopt;
    }

    const auto section_offset = optional_offset + file_header->SizeOfOptionalHeader;
    std::vector<ImageSection> sections;
    sections.reserve(file_header->NumberOfSections);
    for (WORD index = 0; index < file_header->NumberOfSections; ++index) {
        const auto header_offset = section_offset +
                                   static_cast<std::size_t>(index) * sizeof(IMAGE_SECTION_HEADER);
        const auto header = read_structure<IMAGE_SECTION_HEADER>(image, header_offset);
        if (!header) {
            error = "image section table is truncated";
            return std::nullopt;
        }
        const auto raw_offset = static_cast<std::size_t>(header->PointerToRawData);
        const auto raw_size = static_cast<std::size_t>(header->SizeOfRawData);
        if (raw_offset > image.size() || raw_size > image.size() - raw_offset) {
            error = "image section data is outside the file";
            return std::nullopt;
        }
        std::array<char, IMAGE_SIZEOF_SHORT_NAME + 1> name{};
        std::memcpy(name.data(), header->Name, IMAGE_SIZEOF_SHORT_NAME);
        sections.push_back({
            std::string(name.data()),
            header->VirtualAddress,
            image.subspan(raw_offset, raw_size)});
    }
    return sections;
}

bool matches_at(
    const std::span<const std::byte> bytes,
    const std::size_t offset,
    const Pattern& pattern) {
    if (offset > bytes.size() || pattern.size() > bytes.size() - offset) {
        return false;
    }
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        if (pattern[index] &&
            std::to_integer<std::uint8_t>(bytes[offset + index]) != *pattern[index]) {
            return false;
        }
    }
    return true;
}

}  // namespace

ResolutionReport resolve_symbols_from_image(
    const BuildDefinition& build,
    const std::filesystem::path& executable_path) {
    ResolutionReport report{true};
    std::string image_error;
    const auto image = read_image(executable_path, image_error);
    if (!image) {
        report.success = false;
        report.diagnostics.push_back({"<image>", true, image_error});
        return report;
    }
    const auto sections = read_sections(*image, image_error);
    if (!sections) {
        report.success = false;
        report.diagnostics.push_back({"<image>", true, image_error});
        return report;
    }

    std::unordered_set<std::string> resolved_names;
    for (const auto& symbol : build.symbols) {
        const bool prerequisites_met = std::all_of(
            symbol.prerequisites.begin(),
            symbol.prerequisites.end(),
            [&resolved_names](const std::string& name) { return resolved_names.contains(name); });
        if (!prerequisites_met) {
            report.diagnostics.push_back({symbol.name, symbol.required, "prerequisite unresolved"});
            report.success = report.success && !symbol.required;
            continue;
        }
        if (_stricmp(symbol.module.c_str(), "BTD5-Win.exe") != 0) {
            report.diagnostics.push_back({symbol.name, symbol.required, "unsupported module"});
            report.success = report.success && !symbol.required;
            continue;
        }
        const auto section = std::find_if(
            sections->begin(),
            sections->end(),
            [&symbol](const ImageSection& candidate) { return candidate.name == symbol.section; });
        if (section == sections->end()) {
            report.diagnostics.push_back({symbol.name, symbol.required, "section not found"});
            report.success = report.success && !symbol.required;
            continue;
        }

        std::string pattern_error;
        const auto pattern = parse_pattern(symbol.pattern, pattern_error);
        const auto matches = pattern ? find_pattern(section->bytes, *pattern) :
                                       std::vector<std::size_t>{};
        if (!pattern || matches.size() != 1) {
            const std::string message = !pattern ? pattern_error :
                (matches.empty() ? "pattern not found" : "pattern is not unique");
            report.diagnostics.push_back({symbol.name, symbol.required, message});
            report.success = report.success && !symbol.required;
            continue;
        }

        const auto base_offset = static_cast<std::ptrdiff_t>(matches.front());
        const auto result_offset = base_offset + symbol.result_offset;
        const auto validation_offset = result_offset + symbol.validation_offset;
        if (result_offset < 0 || validation_offset < 0 ||
            static_cast<std::size_t>(result_offset) >= section->bytes.size()) {
            report.diagnostics.push_back({symbol.name, symbol.required, "resolved location outside section"});
            report.success = report.success && !symbol.required;
            continue;
        }
        if (!symbol.validation_pattern.empty()) {
            const auto validation = parse_pattern(symbol.validation_pattern, pattern_error);
            if (!validation || !matches_at(
                                   section->bytes,
                                   static_cast<std::size_t>(validation_offset),
                                   *validation)) {
                report.diagnostics.push_back({symbol.name, symbol.required, "validation pattern mismatch"});
                report.success = report.success && !symbol.required;
                continue;
            }
        }
        const auto rva64 = static_cast<std::uint64_t>(section->virtual_address) +
                           static_cast<std::size_t>(result_offset);
        if (rva64 > (std::numeric_limits<std::uint32_t>::max)()) {
            report.diagnostics.push_back({symbol.name, symbol.required, "resolved RVA overflow"});
            report.success = report.success && !symbol.required;
            continue;
        }
        report.resolved.push_back({symbol.name, static_cast<std::uint32_t>(rva64)});
        resolved_names.insert(symbol.name);
    }
    return report;
}

}  // namespace btd5loader::runtime
