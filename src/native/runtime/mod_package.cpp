// SPDX-License-Identifier: GPL-3.0-only
#include "mod_package.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string_view>
#include <unordered_set>

#include <miniz.h>

namespace btd5loader::runtime {
namespace {

struct FileCloser final {
    void operator()(std::FILE* file) const noexcept {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

class ZipReader final {
public:
    explicit ZipReader(std::FILE* file, const std::uint64_t size) {
        initialized_ = mz_zip_reader_init_cfile(&archive_, file, size, 0) == MZ_TRUE;
    }
    ~ZipReader() {
        if (initialized_) {
            mz_zip_reader_end(&archive_);
        }
    }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] mz_zip_archive* get() noexcept { return &archive_; }

private:
    mz_zip_archive archive_{};
    bool initialized_{};
};

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool valid_archive_path(const std::string_view path, bool directory) {
    if (path.empty() || path.size() > 240 || path.front() == '/' ||
        path.find('\\') != std::string_view::npos || path.find(':') != std::string_view::npos ||
        path.find('\0') != std::string_view::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto component = path.substr(start, end - start);
        if (component.empty() || component == "." || component == "..") {
            return directory && end == std::string_view::npos && component.empty();
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
        if (start == path.size()) {
            return directory;
        }
    }
    return true;
}

bool allowed_package_file(const std::string& path) {
    if (path == "mod.json" || path == "README.md" || path == "CHANGELOG.md" ||
        path == "LICENSE" || path == "LICENSE.md") {
        return true;
    }
    const auto separator = path.find('/');
    if (separator == std::string::npos) {
        return false;
    }
    const auto root = path.substr(0, separator);
    return root == "lua" || root == "assets" || root == "localization" ||
           root == "config" || root == "docs";
}

bool is_symbolic_link(const mz_zip_archive_file_stat& statistics) {
    constexpr std::uint32_t kUnixFileTypeMask = 0170000U;
    constexpr std::uint32_t kUnixSymbolicLink = 0120000U;
    return ((statistics.m_external_attr >> 16U) & kUnixFileTypeMask) == kUnixSymbolicLink;
}

}  // namespace

std::optional<ModPackage> validate_mod_package(
    const std::filesystem::path& archive_path,
    std::string& error,
    const ModPackageLimits& limits) {
    if (lowercase(archive_path.extension().string()) != ".btd5mod") {
        error = "mod package must use the .btd5mod extension";
        return std::nullopt;
    }
    std::error_code filesystem_error;
    const auto archive_size = std::filesystem::file_size(archive_path, filesystem_error);
    if (filesystem_error || archive_size == 0 || archive_size > limits.maximum_archive_bytes) {
        error = "mod package is missing, empty, or exceeds the archive size limit";
        return std::nullopt;
    }

    std::FILE* raw_file = nullptr;
    if (_wfopen_s(&raw_file, archive_path.c_str(), L"rb") != 0 || raw_file == nullptr) {
        error = "mod package could not be opened";
        return std::nullopt;
    }
    std::unique_ptr<std::FILE, FileCloser> file(raw_file);
    ZipReader reader(file.get(), archive_size);
    if (!reader.initialized()) {
        error = "mod package is not a valid ZIP archive";
        return std::nullopt;
    }

    const mz_uint entry_count = mz_zip_reader_get_num_files(reader.get());
    if (entry_count == 0 || entry_count > limits.maximum_entries) {
        error = "mod package entry count is outside the allowed limit";
        return std::nullopt;
    }

    ModPackage package;
    package.archive_path = archive_path;
    std::unordered_set<std::string> normalized_paths;
    std::uint64_t total_uncompressed = 0;
    bool has_manifest = false;
    for (mz_uint index = 0; index < entry_count; ++index) {
        mz_zip_archive_file_stat statistics{};
        if (!mz_zip_reader_file_stat(reader.get(), index, &statistics)) {
            error = "mod package contains an unreadable ZIP entry";
            return std::nullopt;
        }
        const std::size_t filename_length = std::strlen(statistics.m_filename);
        if (filename_length == 0 || filename_length >= MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE - 1) {
            error = "mod package contains an empty or overlong path";
            return std::nullopt;
        }
        const std::string path(statistics.m_filename, filename_length);
        if (!valid_archive_path(path, statistics.m_is_directory == MZ_TRUE) ||
            statistics.m_is_encrypted == MZ_TRUE || statistics.m_is_supported != MZ_TRUE ||
            is_symbolic_link(statistics)) {
            error = "mod package contains an unsafe or unsupported entry: " + path;
            return std::nullopt;
        }
        const auto normalized = lowercase(path);
        if (!normalized_paths.insert(normalized).second) {
            error = "mod package contains a duplicate path: " + path;
            return std::nullopt;
        }
        if (statistics.m_is_directory == MZ_TRUE) {
            continue;
        }
        if (!allowed_package_file(path)) {
            error = "mod package contains a file outside allowed roots: " + path;
            return std::nullopt;
        }
        if (statistics.m_uncomp_size > limits.maximum_file_uncompressed_bytes ||
            statistics.m_uncomp_size >
                (std::numeric_limits<std::uint64_t>::max)() - total_uncompressed) {
            error = "mod package file exceeds its uncompressed size limit";
            return std::nullopt;
        }
        total_uncompressed += statistics.m_uncomp_size;
        if (total_uncompressed > limits.maximum_total_uncompressed_bytes) {
            error = "mod package exceeds the total uncompressed size limit";
            return std::nullopt;
        }
        package.files.push_back(path);
        has_manifest = has_manifest || path == "mod.json";
    }
    if (!has_manifest) {
        error = "mod package has no root mod.json";
        return std::nullopt;
    }

    std::size_t manifest_size = 0;
    void* manifest_data = mz_zip_reader_extract_file_to_heap(
        reader.get(), "mod.json", &manifest_size, 0);
    if (manifest_data == nullptr || manifest_size == 0 || manifest_size > 1024U * 1024U) {
        if (manifest_data != nullptr) {
            mz_free(manifest_data);
        }
        error = "mod.json is missing, empty, or exceeds 1 MiB";
        return std::nullopt;
    }
    const std::string manifest_text(static_cast<const char*>(manifest_data), manifest_size);
    mz_free(manifest_data);
    auto manifest = parse_mod_manifest(manifest_text, error);
    if (!manifest) {
        return std::nullopt;
    }
    if (std::find(package.files.begin(), package.files.end(), manifest->entry_point) ==
        package.files.end()) {
        error = "manifest entry point is not present in the package";
        return std::nullopt;
    }
    package.manifest = std::move(*manifest);
    return package;
}

bool extract_mod_package(
    const ModPackage& package,
    const std::filesystem::path& destination,
    std::string& error) {
    std::error_code filesystem_error;
    if (std::filesystem::exists(destination, filesystem_error) &&
        (!std::filesystem::is_directory(destination, filesystem_error) ||
         !std::filesystem::is_empty(destination, filesystem_error))) {
        error = "package extraction destination must be an empty directory";
        return false;
    }
    std::filesystem::create_directories(destination, filesystem_error);
    if (filesystem_error) {
        error = "package extraction destination could not be created";
        return false;
    }

    const auto archive_size = std::filesystem::file_size(package.archive_path, filesystem_error);
    if (filesystem_error) {
        error = "validated package is no longer available";
        return false;
    }
    std::FILE* raw_file = nullptr;
    if (_wfopen_s(&raw_file, package.archive_path.c_str(), L"rb") != 0 || raw_file == nullptr) {
        error = "validated package could not be reopened";
        return false;
    }
    std::unique_ptr<std::FILE, FileCloser> file(raw_file);
    ZipReader reader(file.get(), archive_size);
    if (!reader.initialized()) {
        error = "validated package ZIP could not be reopened";
        return false;
    }

    for (const auto& relative_text : package.files) {
        const std::filesystem::path relative(relative_text);
        const auto output_path = destination / relative;
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error || std::filesystem::exists(output_path, filesystem_error)) {
            error = "package extraction target already exists or cannot be created: " + relative_text;
            return false;
        }
        std::size_t extracted_size = 0;
        void* extracted = mz_zip_reader_extract_file_to_heap(
            reader.get(), relative_text.c_str(), &extracted_size, 0);
        if (extracted == nullptr && extracted_size != 0) {
            error = "package file could not be extracted: " + relative_text;
            return false;
        }
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            if (extracted != nullptr) {
                mz_free(extracted);
            }
            error = "package extraction output could not be opened: " + relative_text;
            return false;
        }
        if (extracted_size > 0) {
            output.write(static_cast<const char*>(extracted), static_cast<std::streamsize>(extracted_size));
        }
        if (extracted != nullptr) {
            mz_free(extracted);
        }
        if (!output.good()) {
            error = "package extraction output could not be written: " + relative_text;
            return false;
        }
    }
    return true;
}

}  // namespace btd5loader::runtime
