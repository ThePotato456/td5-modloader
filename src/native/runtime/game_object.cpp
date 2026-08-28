// SPDX-License-Identifier: GPL-3.0-only
#include "game_object.hpp"

#include <limits>

namespace btd5loader::runtime {

GameObjectHandle GameObjectRegistry::add(const GameObjectKind kind, void* const object) {
    if (object == nullptr) {
        return {};
    }
    std::scoped_lock lock(mutex_);
    std::uint32_t id = 0;
    if (free_ids_.empty()) {
        if (slots_.size() >= (std::numeric_limits<std::uint32_t>::max)()) {
            return {};
        }
        slots_.push_back({});
        id = static_cast<std::uint32_t>(slots_.size());
    } else {
        id = free_ids_.back();
        free_ids_.pop_back();
    }
    auto& slot = slots_[id - 1];
    slot.scene = scene_;
    slot.kind = kind;
    slot.object = object;
    return {id, slot.generation, slot.scene, kind};
}

bool GameObjectRegistry::invalidate(const GameObjectHandle& handle) noexcept {
    std::scoped_lock lock(mutex_);
    if (handle.id == 0 || handle.id > slots_.size()) {
        return false;
    }
    auto& slot = slots_[handle.id - 1];
    if (slot.object == nullptr || slot.generation != handle.generation ||
        slot.scene != handle.scene || slot.kind != handle.kind) {
        return false;
    }
    slot.object = nullptr;
    ++slot.generation;
    if (slot.generation == 0) {
        slot.generation = 1;
    }
    free_ids_.push_back(handle.id);
    return true;
}

void GameObjectRegistry::begin_scene() noexcept {
    std::scoped_lock lock(mutex_);
    ++scene_;
    if (scene_ == 0) {
        scene_ = 1;
    }
    free_ids_.clear();
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        auto& slot = slots_[index];
        slot.object = nullptr;
        ++slot.generation;
        if (slot.generation == 0) {
            slot.generation = 1;
        }
        free_ids_.push_back(static_cast<std::uint32_t>(index + 1));
    }
}

void* GameObjectRegistry::resolve(
    const GameObjectHandle& handle,
    const GameObjectKind expected_kind) const noexcept {
    std::scoped_lock lock(mutex_);
    if (handle.id == 0 || handle.id > slots_.size() || handle.kind != expected_kind) {
        return nullptr;
    }
    const auto& slot = slots_[handle.id - 1];
    return slot.object != nullptr && slot.generation == handle.generation &&
                   slot.scene == handle.scene && slot.scene == scene_ &&
                   slot.kind == handle.kind
               ? slot.object
               : nullptr;
}

std::uint64_t GameObjectRegistry::scene() const noexcept {
    std::scoped_lock lock(mutex_);
    return scene_;
}

std::string_view game_object_kind_name(const GameObjectKind kind) noexcept {
    switch (kind) {
    case GameObjectKind::Match:
        return "match";
    case GameObjectKind::Round:
        return "round";
    case GameObjectKind::Player:
        return "player";
    case GameObjectKind::Tower:
        return "tower";
    case GameObjectKind::Attack:
        return "attack";
    case GameObjectKind::Projectile:
        return "projectile";
    case GameObjectKind::Bloon:
        return "bloon";
    }
    return "unknown";
}

}  // namespace btd5loader::runtime
