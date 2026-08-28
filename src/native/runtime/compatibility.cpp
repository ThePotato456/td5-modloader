// SPDX-License-Identifier: GPL-3.0-only
#include "compatibility.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "pattern.hpp"

namespace btd5loader::runtime {
namespace {

class AlgorithmHandle final {
public:
    ~AlgorithmHandle() {
        if (value != nullptr) {
            BCryptCloseAlgorithmProvider(value, 0);
        }
    }
    BCRYPT_ALG_HANDLE value{};
};

class HashHandle final {
public:
    ~HashHandle() {
        if (value != nullptr) {
            BCryptDestroyHash(value);
        }
    }
    BCRYPT_HASH_HANDLE value{};
};

bool valid_digest(const std::string& digest) {
    return digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](const char value) {
        return std::isxdigit(static_cast<unsigned char>(value)) != 0;
    });
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool parse_symbol(
    const nlohmann::json& input,
    SymbolDefinition& output,
    std::string& error) {
    try {
        output.name = input.at("name").get<std::string>();
        output.module = input.at("module").get<std::string>();
        output.section = input.at("section").get<std::string>();
        output.pattern = input.at("pattern").get<std::string>();
        output.result_offset = input.value("result_offset", 0);
        output.required = input.value("required", true);
        output.prerequisites = input.value("prerequisites", std::vector<std::string>{});
        if (input.contains("validation")) {
            const auto& validation = input.at("validation");
            output.validation_offset = validation.value("offset", 0);
            output.validation_pattern = validation.at("pattern").get<std::string>();
        }
    } catch (const nlohmann::json::exception& exception) {
        error = std::string("invalid symbol entry: ") + exception.what();
        return false;
    }

    if (output.name.empty() || output.module.empty() || output.section.empty()) {
        error = "symbol name, module, and section cannot be empty";
        return false;
    }
    std::string pattern_error;
    if (!parse_pattern(output.pattern, pattern_error)) {
        error = output.name + ": " + pattern_error;
        return false;
    }
    if (!output.validation_pattern.empty() &&
        !parse_pattern(output.validation_pattern, pattern_error)) {
        error = output.name + " validation: " + pattern_error;
        return false;
    }
    return true;
}

}  // namespace

std::optional<std::string> sha256_file(
    const std::filesystem::path& path,
    std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open file for hashing: " + path.string();
        return std::nullopt;
    }

    AlgorithmHandle algorithm;
    if (BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        error = "BCryptOpenAlgorithmProvider failed";
        return std::nullopt;
    }

    DWORD object_size = 0;
    DWORD result_size = 0;
    if (BCryptGetProperty(
            algorithm.value,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_size),
            sizeof(object_size),
            &result_size,
            0) < 0) {
        error = "cannot query SHA-256 object length";
        return std::nullopt;
    }

    std::vector<UCHAR> hash_object(object_size);
    HashHandle hash;
    if (BCryptCreateHash(
            algorithm.value,
            &hash.value,
            hash_object.data(),
            static_cast<ULONG>(hash_object.size()),
            nullptr,
            0,
            0) < 0) {
        error = "BCryptCreateHash failed";
        return std::nullopt;
    }

    std::vector<char> buffer(64 * 1024);
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        if (count > 0 && BCryptHashData(
                             hash.value,
                             reinterpret_cast<PUCHAR>(buffer.data()),
                             static_cast<ULONG>(count),
                             0) < 0) {
            error = "BCryptHashData failed";
            return std::nullopt;
        }
    }
    if (!stream.eof()) {
        error = "failed while reading file for hashing: " + path.string();
        return std::nullopt;
    }

    std::array<UCHAR, 32> digest{};
    if (BCryptFinishHash(hash.value, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        error = "BCryptFinishHash failed";
        return std::nullopt;
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const UCHAR value : digest) {
        output << std::setw(2) << static_cast<unsigned int>(value);
    }
    return output.str();
}

std::optional<BuildDefinition> load_build_definition(
    const std::filesystem::path& path,
    std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open symbol map: " + path.string();
        return std::nullopt;
    }

    try {
        const auto document = nlohmann::json::parse(stream);
        if (document.at("schema_version").get<int>() != 1) {
            error = "unsupported symbol-map schema version";
            return std::nullopt;
        }

        const auto& build = document.at("build");
        BuildDefinition definition;
        definition.id = build.at("id").get<std::string>();
        definition.display_name = build.at("display_name").get<std::string>();
        definition.executable_sha256 = lowercase(build.at("executable_sha256").get<std::string>());
        definition.assets_sha256 = lowercase(build.at("assets_sha256").get<std::string>());
        definition.source_path = path;
        if (definition.id.empty() || !valid_digest(definition.executable_sha256) ||
            !valid_digest(definition.assets_sha256)) {
            error = "symbol map has an invalid build identity or digest";
            return std::nullopt;
        }

        for (const auto& input : document.at("symbols")) {
            SymbolDefinition symbol;
            if (!parse_symbol(input, symbol, error)) {
                return std::nullopt;
            }
            const auto duplicate = std::find_if(
                definition.symbols.begin(),
                definition.symbols.end(),
                [&symbol](const SymbolDefinition& existing) { return existing.name == symbol.name; });
            if (duplicate != definition.symbols.end()) {
                error = "duplicate symbol name: " + symbol.name;
                return std::nullopt;
            }
            definition.symbols.push_back(std::move(symbol));
        }
        return definition;
    } catch (const nlohmann::json::exception& exception) {
        error = std::string("invalid symbol map: ") + exception.what();
        return std::nullopt;
    }
}

DetectionResult detect_build(
    const std::filesystem::path& game_directory,
    const std::filesystem::path& symbol_map_directory) {
    DetectionResult result;
    auto executable = sha256_file(game_directory / L"BTD5-Win.exe", result.error);
    if (!executable) {
        return result;
    }
    auto assets = sha256_file(game_directory / L"Assets" / L"BTD5.jet", result.error);
    if (!assets) {
        return result;
    }
    result.fingerprints = {*executable, *assets};

    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(symbol_map_directory, filesystem_error)) {
        result.error = "symbol-map directory not found: " + symbol_map_directory.string();
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(symbol_map_directory, filesystem_error)) {
        if (filesystem_error) {
            result.error = "cannot enumerate symbol-map directory";
            return result;
        }
        if (!entry.is_regular_file() || entry.path().extension() != L".json") {
            continue;
        }
        std::string map_error;
        auto definition = load_build_definition(entry.path(), map_error);
        if (!definition) {
            result.error = map_error;
            return result;
        }
        if (definition->executable_sha256 == result.fingerprints.executable_sha256 &&
            definition->assets_sha256 == result.fingerprints.assets_sha256) {
            result.build = std::move(definition);
            return result;
        }
    }
    result.error = "unsupported game build";
    return result;
}

}  // namespace btd5loader::runtime
