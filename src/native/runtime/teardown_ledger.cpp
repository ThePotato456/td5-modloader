// SPDX-License-Identifier: GPL-3.0-only
#include "teardown_ledger.hpp"

#include <cstdio>

namespace btd5loader::runtime {

namespace {

std::string hex_site(const std::uint32_t site) {
    char buffer[16]{};
    (void)std::snprintf(buffer, sizeof(buffer), "0x%X", static_cast<unsigned>(site));
    return buffer;
}

}  // namespace

void TeardownLedger::record_site(void* const object, const std::uint32_t site) {
    if (object == nullptr) return;
    std::scoped_lock lock(mutex_);
    sites_[object] = site;
}

void TeardownLedger::forget(void* const object) noexcept {
    std::scoped_lock lock(mutex_);
    sites_.erase(object);
}

std::string TeardownLedger::capture(
    const std::vector<std::pair<GameObjectKind, void*>>& survivors) {
    std::scoped_lock lock(mutex_);
    std::map<std::pair<GameObjectKind, std::uint32_t>, std::uint64_t> groups;
    for (const auto& [kind, object] : survivors) {
        if (object == nullptr) continue;
        const auto site = sites_.find(object);
        const std::uint32_t rva = site == sites_.end() ? 0 : site->second;
        ++groups[{kind, rva}];
        watched_[object] = {kind, rva};
        outstanding_[object] = kind;
        ++captured_[kind];
    }
    sites_.clear();
    std::string summary = "teardown_survivors";
    if (groups.empty()) return summary + " none";
    char separator = ' ';
    for (const auto& [key, count] : groups) {
        summary += separator;
        summary += std::string(game_object_kind_name(key.first)) + "@" +
                   (key.second == 0 ? std::string("unknown") : hex_site(key.second)) + "=" +
                   std::to_string(count);
        separator = ';';
    }
    return summary;
}

void TeardownLedger::destroyed(void* const object) noexcept {
    std::scoped_lock lock(mutex_);
    if (watched_.empty() && outstanding_.empty()) return;
    outstanding_.erase(object);
    const auto found = watched_.find(object);
    if (found == watched_.end()) return;
    ++fates_[found->second.kind].destroyed;
    watched_.erase(found);
}

void TeardownLedger::constructed(void* const object) noexcept {
    std::scoped_lock lock(mutex_);
    if (watched_.empty() && outstanding_.empty()) return;
    outstanding_.erase(object);
    const auto found = watched_.find(object);
    if (found == watched_.end()) return;
    ++fates_[found->second.kind].reused;
    watched_.erase(found);
}

std::string TeardownLedger::settle() {
    std::scoped_lock lock(mutex_);
    if (captured_.empty()) return {};
    std::map<GameObjectKind, std::uint64_t> live;
    for (const auto& [object, watched] : watched_) {
        (void)object;
        ++live[watched.kind];
    }
    std::string summary = "teardown_fate";
    char separator = ' ';
    for (const auto& [kind, total] : captured_) {
        (void)total;
        const Fate fate = fates_[kind];
        summary += separator;
        summary += std::string(game_object_kind_name(kind)) +
                   ":destroyed=" + std::to_string(fate.destroyed) +
                   ",reused=" + std::to_string(fate.reused) +
                   ",live=" + std::to_string(live[kind]);
        separator = ';';
    }
    watched_.clear();
    captured_.clear();
    fates_.clear();
    return summary;
}

std::string TeardownLedger::outstanding() const {
    std::scoped_lock lock(mutex_);
    std::map<GameObjectKind, std::uint64_t> counts;
    for (const auto& [object, kind] : outstanding_) {
        (void)object;
        ++counts[kind];
    }
    std::string summary = "teardown_outstanding";
    if (counts.empty()) return summary + " none";
    char separator = ' ';
    for (const auto& [kind, count] : counts) {
        summary += separator;
        summary += std::string(game_object_kind_name(kind)) + "=" + std::to_string(count);
        separator = ';';
    }
    return summary;
}

}  // namespace btd5loader::runtime
