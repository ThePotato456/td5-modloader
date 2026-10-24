// SPDX-License-Identifier: GPL-3.0-only
#include "custom_tower_art.hpp"

#include <cstdio>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>

#include <miniz.h>

namespace btd5loader::runtime {
namespace {

constexpr std::string_view kWeaponSprites = "Assets/JSON/WeaponSprites/";

struct FileCloser final {
    void operator()(std::FILE* file) const noexcept {
        if (file != nullptr) std::fclose(file);
    }
};

}  // namespace

void ArtCatalog::add_cells_from_atlas(const std::string_view atlas) {
    static const std::regex cell(R"re(<Cell\s[^>]*name\s*=\s*"([^"]*)")re");
    const std::string text(atlas);
    for (auto it = std::sregex_iterator(text.begin(), text.end(), cell); it != std::sregex_iterator(); ++it) {
        cells_.insert((*it)[1].str());
    }
}

bool ArtCatalog::load(const std::filesystem::path& game_directory, std::string& error) {
    cells_.clear();
    weapon_sprites_.clear();
    std::error_code ignored;
    const auto atlases = game_directory / L"Assets" / L"Textures" / L"Ultra";
    if (!std::filesystem::is_directory(atlases, ignored)) {
        error = "texture atlases are missing";
        return false;
    }
    for (const auto& entry : std::filesystem::directory_iterator(atlases, ignored)) {
        if (entry.path().extension() != L".xml") continue;
        std::ifstream stream(entry.path(), std::ios::binary);
        std::stringstream buffer;
        buffer << stream.rdbuf();
        add_cells_from_atlas(buffer.str());
    }

    const auto archive = game_directory / L"Assets" / L"BTD5.jet";
    const auto size = std::filesystem::file_size(archive, ignored);
    std::FILE* raw = nullptr;
    if (ignored || _wfopen_s(&raw, archive.c_str(), L"rb") != 0 || raw == nullptr) {
        error = "asset archive is missing";
        cells_.clear();
        return false;
    }
    const std::unique_ptr<std::FILE, FileCloser> file(raw);
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, file.get(), size, 0)) {
        error = "asset archive is unreadable";
        cells_.clear();
        return false;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint index = 0; index < count; ++index) {
        char name[512]{};
        if (mz_zip_reader_get_filename(&zip, index, name, sizeof(name)) == 0) continue;
        const std::string_view path(name);
        if (path.starts_with(kWeaponSprites) && path.find('/', kWeaponSprites.size()) == std::string_view::npos) {
            weapon_sprites_.insert(std::string(path.substr(kWeaponSprites.size())));
        }
    }
    mz_zip_reader_end(&zip);
    return true;
}

}  // namespace btd5loader::runtime
