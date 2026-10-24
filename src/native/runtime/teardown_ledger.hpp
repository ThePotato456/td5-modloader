// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "game_object.hpp"

namespace btd5loader::runtime {

// Accounts for natively constructed attack and projectile objects that are
// still tracked when a match scene is torn down. Each tracked object records
// the image-relative address its constructor returns to. At teardown the
// survivors are grouped by that site and watched until a later native
// destructor, a constructor reusing their address, or the next settlement.
class TeardownLedger final {
public:
    void record_site(void* object, std::uint32_t site);
    void forget(void* object) noexcept;

    // Moves the given teardown survivors into the watch set and returns a
    // summary such as "teardown_survivors attack@0x1F3773=12".
    [[nodiscard]] std::string capture(
        const std::vector<std::pair<GameObjectKind, void*>>& survivors);

    // Reports native lifetime boundaries for objects no longer tracked.
    void destroyed(void* object) noexcept;
    void constructed(void* object) noexcept;

    // Summarizes the watched survivors' fate, such as
    // "teardown_fate attack:destroyed=0,reused=0,live=12", and clears the
    // watch set. Returns an empty string when nothing was watched.
    [[nodiscard]] std::string settle();

    // Counts every survivor captured at any teardown that has since reached
    // neither a native destructor nor a reusing constructor, such as
    // "teardown_outstanding attack=12". Unlike the watch set, this persists
    // across settlements, so it grows if each match leaves new survivors.
    [[nodiscard]] std::string outstanding() const;

private:
    struct Watched final {
        GameObjectKind kind{GameObjectKind::Attack};
        std::uint32_t site{};
    };
    struct Fate final {
        std::uint64_t destroyed{};
        std::uint64_t reused{};
    };

    mutable std::mutex mutex_;
    std::unordered_map<void*, std::uint32_t> sites_;
    std::unordered_map<void*, Watched> watched_;
    std::unordered_map<void*, GameObjectKind> outstanding_;
    std::map<GameObjectKind, std::uint64_t> captured_;
    std::map<GameObjectKind, Fate> fates_;
};

}  // namespace btd5loader::runtime
