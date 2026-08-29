// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

namespace btd5loader::runtime {

enum class GameObjectKind : std::uint8_t {
    Match,
    Round,
    Player,
    Tower,
    Attack,
    Projectile,
    Bloon,
};

struct GameObjectHandle final {
    std::uint32_t id{};
    std::uint32_t generation{};
    std::uint64_t scene{};
    GameObjectKind kind{GameObjectKind::Match};

    [[nodiscard]] bool operator==(const GameObjectHandle&) const noexcept = default;
};

class GameObjectRegistry final {
public:
    [[nodiscard]] GameObjectHandle add(GameObjectKind kind, void* object);
    [[nodiscard]] GameObjectHandle find_or_add(GameObjectKind kind, void* object);
    [[nodiscard]] bool invalidate(const GameObjectHandle& handle) noexcept;
    void begin_scene() noexcept;

    [[nodiscard]] void* resolve(
        const GameObjectHandle& handle,
        GameObjectKind expected_kind) const noexcept;
    [[nodiscard]] std::uint64_t scene() const noexcept;

private:
    struct Slot final {
        std::uint32_t generation{1};
        std::uint64_t scene{};
        GameObjectKind kind{GameObjectKind::Match};
        void* object{};
    };

    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_ids_;
    std::uint64_t scene_{1};
};

[[nodiscard]] std::string_view game_object_kind_name(GameObjectKind kind) noexcept;

}  // namespace btd5loader::runtime
