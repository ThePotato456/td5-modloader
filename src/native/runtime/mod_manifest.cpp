// SPDX-License-Identifier: GPL-3.0-only
#include "mod_manifest.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <queue>
#include <regex>
#include <set>
#include <tuple>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace btd5loader::runtime {
namespace {

const std::regex kModIdPattern(R"(^[a-z][a-z0-9]*(\.[a-z0-9][a-z0-9-]*)+$)");
const std::unordered_set<std::string> kAllowedCapabilities{
    "gameplay.events",
    "gameplay.mutate",
    "content.towers",
    "content.assets",
    "storage",
};
const std::unordered_set<std::string> kAllowedManifestFields{
    "$schema",
    "id",
    "name",
    "author",
    "version",
    "entry_point",
    "loader_api",
    "supported_game_builds",
    "dependencies",
    "load_order",
    "capabilities",
    "configuration_defaults",
    "localization",
    "documentation",
};

bool valid_id(const std::string& id) {
    return id.size() <= 128 && std::regex_match(id, kModIdPattern);
}

bool valid_relative_path(const std::string& path) {
    if (path.empty() || path.size() > 240 || path.front() == '/' ||
        path.find('\\') != std::string::npos || path.find(':') != std::string::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        const auto component = path.substr(start, end - start);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return true;
}

bool read_identifier_array(
    const nlohmann::json& source,
    const char* field,
    std::vector<std::string>& destination,
    std::string& error) {
    if (!source.contains(field)) {
        error = std::string(field) + " must be declared";
        return false;
    }
    try {
        destination = source.at(field).get<std::vector<std::string>>();
    } catch (const nlohmann::json::exception& exception) {
        error = std::string(field) + ": " + exception.what();
        return false;
    }
    std::unordered_set<std::string> unique;
    for (const auto& id : destination) {
        if (!valid_id(id) || !unique.insert(id).second) {
            error = std::string(field) + " contains an invalid or duplicate mod ID";
            return false;
        }
    }
    return true;
}

std::size_t profile_index(
    const std::string& id,
    const std::unordered_map<std::string, std::size_t>& profile_order) {
    const auto iterator = profile_order.find(id);
    return iterator == profile_order.end() ? (std::numeric_limits<std::size_t>::max)() :
                                             iterator->second;
}

}  // namespace

std::optional<SemanticVersion> parse_semantic_version(const std::string_view text) {
    static const std::regex pattern(
        R"(^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$)");
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_match(text.begin(), text.end(), match, pattern)) {
        return std::nullopt;
    }
    SemanticVersion version;
    const auto parse_number = [&match](const std::size_t index, unsigned int& output) {
        const std::string value(match[index].first, match[index].second);
        const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
        return result.ec == std::errc{} && result.ptr == value.data() + value.size();
    };
    if (!parse_number(1, version.major) || !parse_number(2, version.minor) ||
        !parse_number(3, version.patch)) {
        return std::nullopt;
    }
    if (match[4].matched) {
        version.prerelease.assign(match[4].first, match[4].second);
    }
    return version;
}

bool version_satisfies(
    const SemanticVersion& version,
    const std::string_view requirement) {
    if (requirement == "*") {
        return true;
    }
    std::string_view version_text = requirement;
    enum class Operator { Exact, GreaterOrEqual, Compatible } operation = Operator::Exact;
    if (requirement.starts_with(">=")) {
        operation = Operator::GreaterOrEqual;
        version_text.remove_prefix(2);
    } else if (requirement.starts_with('^')) {
        operation = Operator::Compatible;
        version_text.remove_prefix(1);
    }
    const auto expected = parse_semantic_version(version_text);
    if (!expected) {
        return false;
    }
    const auto numeric = [](const SemanticVersion& value) {
        return std::tuple(value.major, value.minor, value.patch);
    };
    if (operation == Operator::Exact) {
        return version == *expected;
    }
    if (operation == Operator::GreaterOrEqual) {
        return numeric(version) >= numeric(*expected);
    }
    if (numeric(version) < numeric(*expected)) {
        return false;
    }
    if (expected->major > 0) {
        return version.major == expected->major;
    }
    if (expected->minor > 0) {
        return version.major == 0 && version.minor == expected->minor;
    }
    return version.major == 0 && version.minor == 0 && version.patch == expected->patch;
}

std::optional<ModManifest> parse_mod_manifest(
    const std::string_view json_text,
    std::string& error) {
    try {
        const auto document = nlohmann::json::parse(json_text);
        if (!document.is_object()) {
            error = "manifest root must be an object";
            return std::nullopt;
        }
        for (const auto& [field, value] : document.items()) {
            static_cast<void>(value);
            if (!kAllowedManifestFields.contains(field)) {
                error = "manifest contains an unknown field: " + field;
                return std::nullopt;
            }
        }
        ModManifest manifest;
        manifest.id = document.at("id").get<std::string>();
        manifest.name = document.at("name").get<std::string>();
        manifest.author = document.at("author").get<std::string>();
        manifest.version_text = document.at("version").get<std::string>();
        manifest.entry_point = document.at("entry_point").get<std::string>();
        manifest.loader_api = document.at("loader_api").get<unsigned int>();
        manifest.supported_game_builds =
            document.at("supported_game_builds").get<std::vector<std::string>>();
        manifest.capabilities = document.at("capabilities").get<std::vector<std::string>>();
        const auto parsed_version = parse_semantic_version(manifest.version_text);
        if (!parsed_version) {
            error = "version is not valid semantic versioning";
            return std::nullopt;
        }
        manifest.version = *parsed_version;
        if (!valid_id(manifest.id) || manifest.name.empty() || manifest.name.size() > 128 ||
            manifest.author.empty() || manifest.author.size() > 128 ||
            !valid_relative_path(manifest.entry_point) || manifest.loader_api != 1 ||
            manifest.supported_game_builds.empty()) {
            error = "manifest identity, entry point, API version, or supported builds are invalid";
            return std::nullopt;
        }
        std::unordered_set<std::string> supported_builds;
        for (const auto& build : manifest.supported_game_builds) {
            if (build.empty() || build.size() > 128 || !supported_builds.insert(build).second) {
                error = "supported game builds contain an invalid or duplicate value";
                return std::nullopt;
            }
        }

        std::unordered_set<std::string> capabilities;
        for (const auto& capability : manifest.capabilities) {
            if (!kAllowedCapabilities.contains(capability) ||
                !capabilities.insert(capability).second) {
                error = "manifest contains an unsupported or duplicate capability";
                return std::nullopt;
            }
        }
        std::unordered_set<std::string> dependency_ids;
        for (const auto& input : document.at("dependencies")) {
                ModDependency dependency{
                    input.at("id").get<std::string>(),
                    input.at("version").get<std::string>()};
                if (!valid_id(dependency.id) || dependency.id == manifest.id ||
                    !dependency_ids.insert(dependency.id).second ||
                    !(dependency.version_requirement == "*" ||
                      version_satisfies(SemanticVersion{}, dependency.version_requirement) ||
                      parse_semantic_version(dependency.version_requirement) ||
                      (dependency.version_requirement.starts_with(">=") &&
                       parse_semantic_version(
                           std::string_view(dependency.version_requirement).substr(2))) ||
                      (dependency.version_requirement.starts_with('^') &&
                       parse_semantic_version(
                           std::string_view(dependency.version_requirement).substr(1))))) {
                    error = "dependency has an invalid ID or version requirement";
                    return std::nullopt;
                }
                manifest.dependencies.push_back(std::move(dependency));
        }
        const auto& load_order = document.at("load_order");
        if (!read_identifier_array(load_order, "before", manifest.load_before, error) ||
            !read_identifier_array(load_order, "after", manifest.load_after, error)) {
            return std::nullopt;
        }
        if (std::find(manifest.load_before.begin(), manifest.load_before.end(), manifest.id) !=
                manifest.load_before.end() ||
            std::find(manifest.load_after.begin(), manifest.load_after.end(), manifest.id) !=
                manifest.load_after.end()) {
            error = "a mod cannot order itself before or after itself";
            return std::nullopt;
        }
        return manifest;
    } catch (const nlohmann::json::exception& exception) {
        error = std::string("malformed manifest: ") + exception.what();
        return std::nullopt;
    }
}

LoadOrderResult resolve_load_order(
    const std::vector<ModManifest>& manifests,
    const std::unordered_map<std::string, std::size_t>& profile_order) {
    LoadOrderResult result;
    std::unordered_map<std::string, const ModManifest*> by_id;
    for (const auto& manifest : manifests) {
        if (!by_id.emplace(manifest.id, &manifest).second) {
            result.error = "duplicate mod ID: " + manifest.id;
            return result;
        }
    }

    std::unordered_map<std::string, std::set<std::string>> outgoing;
    std::unordered_map<std::string, std::size_t> indegree;
    for (const auto& manifest : manifests) {
        indegree.emplace(manifest.id, 0);
    }
    const auto add_edge = [&outgoing, &indegree](const std::string& from, const std::string& to) {
        if (from != to && outgoing[from].insert(to).second) {
            ++indegree[to];
        }
    };

    for (const auto& manifest : manifests) {
        for (const auto& dependency : manifest.dependencies) {
            if (dependency.id == manifest.id) {
                result.error = manifest.id + " cannot depend on itself";
                return result;
            }
            const auto installed = by_id.find(dependency.id);
            if (installed == by_id.end()) {
                result.error = manifest.id + " requires missing dependency " + dependency.id;
                return result;
            }
            if (!version_satisfies(installed->second->version, dependency.version_requirement)) {
                result.error = manifest.id + " has an incompatible dependency " + dependency.id;
                return result;
            }
            add_edge(dependency.id, manifest.id);
        }
        for (const auto& before : manifest.load_before) {
            if (by_id.contains(before)) {
                add_edge(manifest.id, before);
            }
        }
        for (const auto& after : manifest.load_after) {
            if (by_id.contains(after)) {
                add_edge(after, manifest.id);
            }
        }
    }

    const auto compare = [&profile_order](const std::string& left, const std::string& right) {
        const auto left_index = profile_index(left, profile_order);
        const auto right_index = profile_index(right, profile_order);
        return left_index != right_index ? left_index > right_index : left > right;
    };
    std::priority_queue<std::string, std::vector<std::string>, decltype(compare)> ready(compare);
    for (const auto& [id, count] : indegree) {
        if (count == 0) {
            ready.push(id);
        }
    }
    while (!ready.empty()) {
        auto id = ready.top();
        ready.pop();
        result.ordered_ids.push_back(id);
        for (const auto& dependent : outgoing[id]) {
            if (--indegree[dependent] == 0) {
                ready.push(dependent);
            }
        }
    }
    if (result.ordered_ids.size() != manifests.size()) {
        result.ordered_ids.clear();
        result.error = "dependency or explicit load-order cycle detected";
    }
    return result;
}

}  // namespace btd5loader::runtime
