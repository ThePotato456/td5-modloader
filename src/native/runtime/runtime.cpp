#include "mod_dispatch.hpp"
#include <Windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

#include <btd5loader/runtime_api.hpp>
#include <btd5loader/version.hpp>

#include <nlohmann/json.hpp>

#include "active_profile.hpp"
#include "launch_ticket.hpp"
#include "bloon_action_hook.hpp"
#include "cash_capture.hpp"
#include "compatibility.hpp"
#include "frame_hook.hpp"
#include "exit_hook.hpp"
#include "hook_transaction.hpp"
#include "lives_hook.hpp"
#include "lives_write_hook.hpp"
#include "logger.hpp"
#include "lua_mod.hpp"
#include "custom_tower_saves.hpp"
#include "custom_tower_art.hpp"
#include "match_hook.hpp"
#include "round_hook.hpp"
#include "mod_package.hpp"
#include "mod_lifecycle.hpp"
#include "native_event_hook.hpp"
#include "object_events.hpp"
#include "object_lifetime_hook.hpp"
#include "object_types.hpp"
#include "runtime_state.hpp"
#include "symbol_resolver.hpp"
#include "teardown_ledger.hpp"
#include "tower_placement_hook.hpp"
#include "tower_purchase_hook.hpp"
#include "tower_sale_hook.hpp"
#include "tower_upgrade_hook.hpp"
#include "type_watch.hpp"
#include "custom_tower_hooks.hpp"
#include "tower_progress.hpp"
#include "tower_registry.hpp"

extern "C" void WINAPI BTD5Loader_Shutdown();
static void shutdown_runtime(bool normal_exit);

namespace {

btd5loader::runtime::StateMachine g_state;
btd5loader::runtime::Logger g_logger;
btd5loader::runtime::TypeWatch g_type_watch;
btd5loader::runtime::TowerRegistry g_tower_registry;
btd5loader::runtime::CustomTowerHooks g_custom_tower_hooks;
btd5loader::runtime::CustomTowerProgress g_tower_progress;
std::filesystem::path g_game_directory;
std::filesystem::path g_session_directory;
std::optional<btd5loader::runtime::LaunchTicket> g_launch_ticket;
std::vector<std::unique_ptr<btd5loader::runtime::LuaMod>> g_mods;
btd5loader::runtime::ModDispatchGate g_dispatch_gate;
std::mutex g_lifecycle_mutex;
bool g_shutdown_started{}; // Protected by g_lifecycle_mutex.

btd5loader::runtime::GameObjectRegistry g_game_objects;
btd5loader::runtime::GameObjectEvents g_object_events{g_game_objects};
btd5loader::runtime::BloonActionHook g_bloon_action_hook;
btd5loader::runtime::FrameHook g_frame_hook;
btd5loader::runtime::ExitHook g_exit_hook;
btd5loader::runtime::LivesHook g_lives_hook;
btd5loader::runtime::LivesWriteHook g_lives_write_hook;
btd5loader::runtime::MatchHook g_match_hook;
btd5loader::runtime::RoundHook g_round_hook;
btd5loader::runtime::NativeEventHook g_native_event_hook;
btd5loader::runtime::ObjectLifetimeHook g_object_lifetime_hook;
btd5loader::runtime::TowerPlacementHook g_tower_placement_hook;
btd5loader::runtime::TowerPurchaseHook g_tower_purchase_hook;
btd5loader::runtime::TowerSaleHook g_tower_sale_hook;
btd5loader::runtime::TowerUpgradeHook g_tower_upgrade_hook;
btd5loader::runtime::HookTransaction g_hooks;

using TowerPopCountGetter = std::int32_t(__thiscall*)(void*);
using TowerPopCountSetter = void(__thiscall*)(void*, std::int32_t);
TowerPopCountGetter g_tower_pop_count_get{};
TowerPopCountSetter g_tower_pop_count_set{};
bool g_tower_sell_price_verified{};
bool g_bloon_health_verified{};
// Identity fields and the cash helpers; see
// docs/native-internals.md#cash-code-flow.
bool g_object_identity_verified{};
void* g_cash_add{};
void* g_cash_spend{};

constexpr std::size_t kTowerSellPriceOffset = 0x144;
constexpr std::size_t kBloonHealthOffset = 0x224;
constexpr std::size_t kTowerTypeOffset = 0xD0;
constexpr std::size_t kTowerUpgradeTierOffset = 0x138;
constexpr std::int32_t kMaximumUpgradeTier = 4;
constexpr std::size_t kBloonDefinitionOffset = 0x1FC;
constexpr std::size_t kBloonDefinitionTypeOffset = 0x28;
constexpr std::size_t kRoundNumberOffset = 0x14;
constexpr std::size_t kScreenEconomyOffset = 0x53C;
constexpr std::size_t kEconomyCashOffset = 0x90;

template<class T>
[[nodiscard]] T read_field(const void* const object, const std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}

// The native add and spend helpers take the economy in ECX and the amount in
// XMM1, then raise the money-updated event themselves.
__declspec(naked) void __stdcall invoke_cash_helper(
    void* /*helper*/, void* /*economy*/, const double* /*amount*/) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ecx, [ebp + 12]
        mov eax, [ebp + 16]
        movsd xmm1, qword ptr [eax]
        call dword ptr [ebp + 8]
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 12
    }
}

// High-volume gameplay events are counted per match and summarized at match
// end instead of producing one log record each.
std::map<std::string, std::uint64_t, std::less<>> g_event_counts;

bool high_volume_event(const std::string_view name) noexcept {
    return name.starts_with("cash.") || name.starts_with("bloon.");
}

void log_event(const std::string_view name) {
    if (high_volume_event(name)) {
        const auto found = g_event_counts.find(name);
        if (found != g_event_counts.end()) {
            ++found->second;
        } else {
            g_event_counts.emplace(std::string(name), 1);
        }
        return;
    }
    g_logger.info("events", std::string(name));
}

// A cancelled high-volume event can re-fire every frame (a cancelled leak), so
// only its first cancellation per match is logged; the rest are counted.
void log_cancelled_event(const std::string_view name) {
    auto cancelled = std::string(name) + ":cancelled";
    if (!high_volume_event(name)) {
        g_logger.info("events", cancelled);
        return;
    }
    const auto [found, first] = g_event_counts.try_emplace(cancelled, 0);
    ++found->second;
    if (first) {
        g_logger.info("events", cancelled);
    }
}

void dispatch_game_event(
    const std::string_view name,
    const btd5loader::runtime::LuaEventFields& fields = {}) {
    using btd5loader::runtime::State;

    if (g_state.current() != State::GameReady) {
        return;
    }
    auto dispatch = g_dispatch_gate.enter();
    if (!dispatch) return;
    log_event(name);
    for (const auto& mod : g_mods) {
        (void)mod->dispatch_event(name, fields);
    }
}

bool deliver_game_event(
    const std::string_view name,
    const btd5loader::runtime::LuaEventFields& fields) {
    dispatch_game_event(name, fields);
    return false;
}

void dispatch_match_event(const std::string_view name, void* const match, const bool ended = false) {
    g_object_events.match_event(name, match, ended, &deliver_game_event);
}

btd5loader::runtime::CashCaptureStack g_cash_captures;

void dispatch_cash_event(const std::string_view name, const void* const captured) {
    const auto balance = g_cash_captures.value(captured);
    if (!balance.has_value()) {
        dispatch_game_event(name);
        return;
    }
    dispatch_game_event(name, {{"cash", *balance}});
}

void dispatch_round_event(const std::string_view name, const bool ended = false) {
    g_object_events.round_event(name, ended, &deliver_game_event);
}

btd5loader::runtime::GameObjectHandle player_handle(void* const state) {
    return g_object_events.player(state);
}

void dispatch_lives_event(
    const std::string_view name,
    void* const state,
    const std::int32_t before,
    const std::int32_t after) {
    const auto player = player_handle(state);
    dispatch_game_event(
        name,
        {
            {"old_lives", static_cast<std::int64_t>(before)},
            {"new_lives", static_cast<std::int64_t>(after)},
            {"player", player},
        });
}

btd5loader::runtime::LivesChangeDecision dispatch_lives_changing(
    void* const state,
    const std::int32_t before,
    const std::int32_t after) {
    using btd5loader::runtime::State;

    if (g_state.current() != State::GameReady) {
        return {false, after};
    }
    auto dispatch = g_dispatch_gate.enter();
    if (!dispatch) return {false, after};
    g_logger.info("events", "lives.changing");
    const auto decision = btd5loader::runtime::dispatch_profile_lives(
        g_mods,
        before,
        after,
        [] { g_logger.error("events", "lives.changing:new_lives_out_of_range"); },
        player_handle(state));
    if (decision.cancelled) {
        g_logger.info("events", "lives.changing:cancelled");
    }
    return decision;
}

void* capture_game_object_event(void* const event) noexcept {
    return event != nullptr ? static_cast<void**>(event)[1] : nullptr;
}

// Delivers a tower or bloon event to every mod in profile order.
bool dispatch_object_event(
    const btd5loader::runtime::GameObjectKind kind,
    const std::string_view name,
    void* const object,
    const bool invalidate_after,
    const bool cancellable,
    btd5loader::runtime::LuaEventFields extra = {}) {
    using btd5loader::runtime::GameObjectKind;
    using btd5loader::runtime::State;

    if (object == nullptr || g_state.current() != State::GameReady) {
        return false;
    }
    auto dispatch = g_dispatch_gate.enter();
    if (!dispatch) return false;
    const bool tower = kind == GameObjectKind::Tower;
    const auto cancelled = g_object_events.object_event(
        kind, tower ? "tower" : "bloon", object, name, invalidate_after, std::move(extra),
        [cancellable](const std::string_view event,
                      const btd5loader::runtime::LuaEventFields& fields) {
            log_event(event);
            const bool result =
                btd5loader::runtime::dispatch_profile_event(g_mods, event, fields, cancellable);
            if (result) log_cancelled_event(event);
            return result;
        });
    if (!cancelled.has_value()) {
        g_logger.error("events", tower ? "tower_handle_unavailable" : "bloon_handle_unavailable");
        return false;
    }
    return *cancelled;
}

bool dispatch_tower_event(
    const std::string_view name,
    void* const tower,
    const bool invalidate_after,
    const bool cancellable = false,
    const std::optional<bool> purchase = std::nullopt) {
    btd5loader::runtime::LuaEventFields extra;
    if (purchase.has_value()) extra.emplace_back("purchase", *purchase);
    return dispatch_object_event(btd5loader::runtime::GameObjectKind::Tower, name, tower,
                                 invalidate_after, cancellable, std::move(extra));
}

bool dispatch_bloon_event(
    const std::string_view name,
    void* const bloon,
    const bool invalidate_after,
    const bool cancellable = false) {
    return dispatch_object_event(btd5loader::runtime::GameObjectKind::Bloon, name, bloon,
                                 invalidate_after, cancellable);
}

// bloon.spawning is cancellable only for round-emitter spawns (`from_round`).
// A cancelled spawn never joins the game, so its wrapper is invalidated at once.
bool dispatch_bloon_spawning(void* const bloon, const bool from_round) {
    using btd5loader::runtime::State;

    if (bloon == nullptr || g_state.current() != State::GameReady) {
        return false;
    }
    auto dispatch = g_dispatch_gate.enter();
    if (!dispatch) return false;
    const auto cancelled = g_object_events.rejectable_object_event(
        btd5loader::runtime::GameObjectKind::Bloon, "bloon", bloon, "bloon.spawning",
        {{"from_round", from_round}},
        [from_round](const std::string_view event,
                     const btd5loader::runtime::LuaEventFields& fields) {
            log_event(event);
            const bool result =
                btd5loader::runtime::dispatch_profile_event(g_mods, event, fields, from_round);
            if (result) log_cancelled_event(event);
            return result;
        });
    if (!cancelled.has_value()) {
        g_logger.error("events", "bloon_handle_unavailable");
        return false;
    }
    return *cancelled;
}

// Native destructor invalidations per kind within the current match scene.
std::array<std::uint64_t, 7> g_destroyed_objects{};
// Creation sites of tracked attacks/projectiles and the fate of teardown survivors.
btd5loader::runtime::TeardownLedger g_teardown_ledger;
std::uintptr_t g_image_base{};

bool any_mod_handles(const std::string_view name) noexcept {
    return std::any_of(g_mods.begin(), g_mods.end(),
        [name](const auto& mod) { return mod->has_event_handlers(name); });
}

// High-volume lifetime events are dispatched only when a mod subscribes and are
// not logged individually; registry tracking itself runs for every object.
bool dispatch_lifetime_event(
    const std::string_view name,
    const btd5loader::runtime::LuaEventFields& fields) {
    using btd5loader::runtime::State;

    if (g_state.current() != State::GameReady) return false;
    auto dispatch = g_dispatch_gate.enter();
    if (!dispatch) return false;
    if (!any_mod_handles(name)) return false;
    (void)btd5loader::runtime::dispatch_profile_event(g_mods, name, fields, false);
    return false;
}

void object_created(
    const btd5loader::runtime::GameObjectKind kind,
    void* const object,
    const void* const caller) {
    using btd5loader::runtime::GameObjectEvents;
    using btd5loader::runtime::State;

    g_teardown_ledger.constructed(object);
    if (g_state.current() != State::GameReady) return;
    const auto tracking = g_object_events.object_created(kind, object,
        [object, caller](const std::string_view name,
                         const btd5loader::runtime::LuaEventFields& fields) {
            const auto address = reinterpret_cast<std::uintptr_t>(caller);
            g_teardown_ledger.record_site(object,
                address > g_image_base ? static_cast<std::uint32_t>(address - g_image_base) : 0);
            return dispatch_lifetime_event(name, fields);
        });
    if (tracking == GameObjectEvents::Tracking::Unavailable) {
        g_logger.error("events", std::string(btd5loader::runtime::game_object_kind_name(kind)) + "_handle_unavailable");
    }
}

void object_destroying(const btd5loader::runtime::GameObjectKind kind, void* const object) {
    if (!g_game_objects.find(kind, object).has_value()) {
        g_teardown_ledger.destroyed(object);
        return;
    }
    ++g_destroyed_objects[static_cast<std::size_t>(kind)];
    g_teardown_ledger.forget(object);
    (void)g_object_events.object_destroying(kind, object, &dispatch_lifetime_event);
}

void dispatch_frame(HDC) {
    using btd5loader::runtime::State;

    thread_local bool dispatching = false;
    if (dispatching) {
        return;
    }
    dispatching = true;
    const auto reset_dispatching = []() { dispatching = false; };

    {
        auto dispatch = g_dispatch_gate.enter();
        if (!dispatch) {
            reset_dispatching();
            return;
        }
        if (g_state.current() == State::ModsLoading) {
            if (!g_state.transition_to(State::GameReady)) {
                g_logger.error("runtime", "invalid_game_ready_transition");
                reset_dispatching();
                return;
            }
            g_logger.info("runtime", "game_ready_frame_hook");
            for (const auto& mod : g_mods) {
                (void)mod->invoke("on_ready");
            }
        }
        if (g_state.current() == State::GameReady) {
            for (const auto& mod : g_mods) {
                mod->advance_timers(1);
            }
        }
    }
    reset_dispatching();
}

// Development-only registration until btd5.towers.register exists: a
// comma-separated list of "<modId>:<towerId>[@<base>[@<cost>[@open|xp[@<name>[@base|always|never]]]]]"
// entries in BTD5ML_DEV_CUSTOM_TOWERS.
void register_dev_custom_towers();
std::filesystem::path data_root();
std::string register_custom_tower(std::string_view id, btd5loader::runtime::CustomTowerSpec spec);

void log_type_watch(const std::vector<std::string>& lines) {
    for (const auto& line : lines) g_logger.info("diag", line);
}

std::wstring environment_value(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0 || written >= required) {
        return {};
    }
    value.resize(written);
    return value;
}

// Lets tower registration check art names against the game's atlases and
// weapon sprites, loaded on first use from the game directory.
void install_tower_art_check() {
    g_tower_registry.set_art_check([](const bool weapon_sprite, const std::string_view name) {
        static btd5loader::runtime::ArtCatalog catalog;
        static std::once_flag once;
        std::call_once(once, [] {
            std::wstring executable(MAX_PATH, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
            if (length == 0 || length >= executable.size()) return;
            executable.resize(length);
            std::string error;
            if (!catalog.load(std::filesystem::path(executable).parent_path(), error)) {
                g_logger.info("towers", "custom_tower_art_check_skipped " + error);
            }
        });
        // Without the game's catalog, names are left for the game to resolve.
        if (!catalog.loaded()) return true;
        return weapon_sprite ? catalog.has_weapon_sprite(name) : catalog.has_cell(name);
    });
}

void register_dev_custom_towers() {
    const auto value = environment_value(L"BTD5ML_DEV_CUSTOM_TOWERS");
    // Valid IDs are ASCII; anything else becomes '?' and fails validation.
    std::string list;
    for (const wchar_t c : value) list.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    std::size_t start = 0;
    while (start < list.size()) {
        const auto end = (std::min)(list.find(',', start), list.size());
        const std::string id = list.substr(start, end - start);
        start = end + 1;
        if (id.empty()) continue;
        // "<id>[@<base>[@<cost>[@open|xp[@<name>[@base|always|never[@<unlock rank>]]]]]]". Dev
        // towers lead the shop so smoke tests can buy them from the first slot;
        // "xp" turns on XP progression (otherwise every upgrade is open); the
        // last field is the restricted-mode rule (default "base").
        std::vector<std::string> fields;
        for (std::size_t from = 0;;) {
            const auto at = id.find('@', from);
            fields.push_back(id.substr(from, at == std::string::npos ? std::string::npos : at - from));
            if (at == std::string::npos) break;
            from = at + 1;
        }
        btd5loader::runtime::CustomTowerSpec spec;
        spec.shop_first = true;
        spec.xp_progression = fields.size() > 3 && fields[3] == "xp";
        if (fields.size() > 1) spec.base = fields[1];
        if (fields.size() > 4) spec.name = fields[4];
        if (fields.size() > 5 && fields[5] == "always") spec.restricted_modes = btd5loader::runtime::RestrictedModes::always;
        if (fields.size() > 5 && fields[5] == "never") spec.restricted_modes = btd5loader::runtime::RestrictedModes::never;
        if (fields.size() > 6) {
            try {
                spec.unlock_rank = std::stoll(fields[6]);
            } catch (...) {
                g_logger.error("towers", "invalid dev tower unlock rank in '" + id + "'");
                continue;
            }
        }
        if (fields.size() > 2) {
            try {
                spec.cost = std::stoll(fields[2]);
            } catch (...) {
                g_logger.error("towers", "invalid dev tower cost in '" + id + "'");
                continue;
            }
        }
        (void)register_custom_tower(fields[0], std::move(spec));
    }
}

// The game's Data/Docs folder for the Steam account that is running it:
// <Steam>/userdata/<account>/306020/local/Data/Docs.
std::filesystem::path steam_docs_directory() {
    wchar_t steam[MAX_PATH]{};
    DWORD size = sizeof(steam);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam, &size) !=
        ERROR_SUCCESS) {
        return {};
    }
    DWORD account = 0;
    size = sizeof(account);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser", RRF_RT_REG_DWORD,
            nullptr, &account, &size) != ERROR_SUCCESS ||
        account == 0) {
        return {};
    }
    return std::filesystem::path(steam) / L"userdata" / std::to_wstring(account) / L"306020" / L"local" / L"Data" /
           L"Docs";
}

// Holds match saves that name custom towers no enabled mod registers, and
// restores them once those towers are back (see custom_tower_saves.hpp).
// Runs once mods have registered their towers, before the menus can open a
// save.
void guard_custom_tower_saves() {
    try {
        const auto docs = steam_docs_directory();
        std::error_code ignored;
        if (docs.empty() || !std::filesystem::is_directory(docs, ignored)) {
            g_logger.info("towers", "custom_tower_saves_skipped reason=no_save_folder");
            return;
        }
        for (const auto& line : btd5loader::runtime::guard_match_saves(
                 docs, data_root() / L"storage" / L"custom-towers", g_tower_registry)) {
            g_logger.info("towers", line);
        }
    } catch (const std::exception& exception) {
        g_logger.error("towers", std::string("custom_tower_saves_failed ") + exception.what());
    }
}

std::string register_custom_tower(const std::string_view id, btd5loader::runtime::CustomTowerSpec spec) {
    const std::string error = g_tower_registry.add(id, std::move(spec));
    if (!error.empty()) {
        g_logger.error("towers", error);
        return error;
    }
    const auto* const tower = g_tower_registry.find_id(id);
    char flag[24];
    std::snprintf(flag, sizeof(flag), "0x%016llX", static_cast<unsigned long long>(tower->flag));
    g_logger.info("towers", "custom_tower id=" + tower->id + ";native=" + tower->native_name +
                                ";base=" + tower->spec.base + ";index=" + std::to_string(tower->index) +
                                ";flag=" + flag);
    return {};
}

std::filesystem::path data_root() {
    const auto override_path = environment_value(L"BTD5ML_DATA_ROOT");
    if (!override_path.empty()) {
        return override_path;
    }
    const auto local_app_data = environment_value(L"LOCALAPPDATA");
    return local_app_data.empty()
               ? std::filesystem::path{}
               : std::filesystem::path(local_app_data) / L"BTD5ModLoader";
}

std::filesystem::path path_from_utf8(const std::string_view text) {
    return std::filesystem::path(
        std::u8string(
            reinterpret_cast<const char8_t*>(text.data()),
            reinterpret_cast<const char8_t*>(text.data() + text.size())));
}

bool read_text_file(
    const std::filesystem::path& path,
    std::string& output,
    std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "file could not be opened: " + path.filename().string();
        return false;
    }
    output.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) {
        error = "file could not be read: " + path.filename().string();
        return false;
    }
    return true;
}

bool load_localization(
    const std::filesystem::path& resource_directory,
    std::unordered_map<std::string, std::string>& localization,
    std::string& error) {
    const auto path = resource_directory / L"localization" / L"en-US.json";
    if (!std::filesystem::is_regular_file(path)) {
        return true;
    }
    std::string text;
    if (!read_text_file(path, text, error)) {
        return false;
    }
    try {
        localization = nlohmann::json::parse(text)
                           .get<std::unordered_map<std::string, std::string>>();
        return true;
    } catch (const nlohmann::json::exception& exception) {
        error = std::string("localization is malformed: ") + exception.what();
        return false;
    }
}

bool load_active_mods(const std::string& build_id) {
    using btd5loader::runtime::LuaMod;
    using btd5loader::runtime::LuaModOptions;
    using btd5loader::runtime::State;

    const auto* active = &g_launch_ticket->profile;
    std::string error;
    if (active->build_id != build_id) return false;
    auto lock = g_dispatch_gate.exclusive();
    if (!g_state.transition_to(State::ModsLoading)) {
        g_logger.error("runtime", "invalid_mods_loading_transition");
        return false;
    }
    const auto root = data_root();
    if (root.empty()) {
        g_logger.error("mods", "manager_data_root_unavailable");
        return false;
    }
    std::error_code filesystem_error;
    const auto packages_root = std::filesystem::weakly_canonical(root / L"packages", filesystem_error);
    if (filesystem_error) {
        g_logger.error("mods", "manager_package_root_unavailable");
        return false;
    }
    g_session_directory = root / L"runtime" / L"sessions" /
                          (std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(g_session_directory, filesystem_error);
    if (filesystem_error) {
        g_logger.error("mods", "session_directory_creation_failed");
        return false;
    }

    const auto rollback = []() {
        btd5loader::runtime::shutdown_mods(g_mods);
        std::error_code cleanup_error;
        if (!g_session_directory.empty()) {
            std::filesystem::remove_all(g_session_directory, cleanup_error);
        }
        g_session_directory.clear();
        return false;
    };
    for (const auto& requested : active->mods) {
        if (g_dispatch_gate.stopping()) return rollback();
        filesystem_error.clear();
        const auto archive_path = std::filesystem::weakly_canonical(
            requested.archive_path, filesystem_error);
        const auto root_end = std::mismatch(
            packages_root.begin(), packages_root.end(), archive_path.begin(), archive_path.end()).first;
        if (filesystem_error || root_end != packages_root.end()) {
            g_logger.error("mods", requested.id + ":package_outside_manager_storage");
            return rollback();
        }
        auto package = btd5loader::runtime::validate_mod_package(archive_path, error);
        if (!package || package->manifest.id != requested.id ||
            package->manifest.version_text != requested.version ||
            std::find(
                package->manifest.supported_game_builds.begin(),
                package->manifest.supported_game_builds.end(),
                build_id) == package->manifest.supported_game_builds.end()) {
            g_logger.error(
                "mods",
                requested.id + ":" + (package ? "package_identity_or_build_mismatch" : error));
            return rollback();
        }
        const auto resource_directory = g_session_directory / path_from_utf8(requested.id);
        if (!btd5loader::runtime::extract_mod_package(*package, resource_directory, error)) {
            g_logger.error("mods", requested.id + ":" + error);
            return rollback();
        }
        std::string script;
        if (!read_text_file(resource_directory / path_from_utf8(package->manifest.entry_point),
                            script,
                            error)) {
            g_logger.error("mods", requested.id + ":" + error);
            return rollback();
        }
        LuaModOptions options;
        options.mod_id = requested.id;
        options.storage_directory = root / L"storage" / path_from_utf8(requested.id);
        options.resource_directory = resource_directory;
        options.configuration = requested.configuration;
        options.register_sound = [](const std::string_view id, std::vector<std::filesystem::path> files,
                                     const double cooldown) {
            const std::string error = g_tower_registry.add_sound(id, std::move(files), cooldown);
            if (error.empty()) g_logger.info("towers", "custom_sound id=" + std::string(id));
            return error;
        };
        options.play_sound = [](const std::string_view id) {
            return btd5loader::runtime::CustomTowerHooks::play_sound(id);
        };
        options.object_registry = &g_game_objects;
        options.game_object_integer_get = [](
            const btd5loader::runtime::GameObjectKind kind,
            void* const object,
            const std::string_view property) -> std::optional<std::int64_t> {
            using btd5loader::runtime::GameObjectKind;
            if (object == nullptr) return std::nullopt;
            if (kind == GameObjectKind::Round) {
                if (property != "number" || !g_object_identity_verified) return std::nullopt;
                const auto number = read_field<std::int32_t>(object, kRoundNumberOffset);
                if (number < 0) return std::nullopt;
                return static_cast<std::int64_t>(number);
            }
            if (kind != GameObjectKind::Tower) return std::nullopt;
            if ((property == "upgrade_tier_left" || property == "upgrade_tier_right") &&
                g_object_identity_verified) {
                const auto tier = read_field<std::int32_t>(object,
                    kTowerUpgradeTierOffset + (property == "upgrade_tier_right" ? 4 : 0));
                if (tier < 0 || tier > kMaximumUpgradeTier) return std::nullopt;
                return static_cast<std::int64_t>(tier);
            }
            if (property == "pop_count" && g_tower_pop_count_get != nullptr) {
                return static_cast<std::int64_t>(g_tower_pop_count_get(object));
            }
            if (property == "sell_price" && g_tower_sell_price_verified) {
                std::int32_t value{};
                std::memcpy(
                    &value,
                    static_cast<const std::byte*>(object) + kTowerSellPriceOffset,
                    sizeof(value));
                return static_cast<std::int64_t>(value);
            }
            return std::nullopt;
        };
        options.game_object_integer_set = [](
            const btd5loader::runtime::GameObjectKind kind,
            void* const object,
            const std::string_view property,
            const std::int64_t value,
            std::string& error) {
            if (kind != btd5loader::runtime::GameObjectKind::Tower || object == nullptr) {
                error = "tower integer property is unavailable on this game build";
                return false;
            }
            if (value < 0 || value > (std::numeric_limits<std::int32_t>::max)()) {
                error = "tower pop_count is outside the supported range";
                return false;
            }
            if (property == "pop_count" && g_tower_pop_count_set != nullptr) {
                g_tower_pop_count_set(object, static_cast<std::int32_t>(value));
                return true;
            }
            if (property == "sell_price" && g_tower_sell_price_verified) {
                const auto stored = static_cast<std::int32_t>(value);
                std::memcpy(
                    static_cast<std::byte*>(object) + kTowerSellPriceOffset,
                    &stored,
                    sizeof(stored));
                return true;
            }
            error = "tower integer property is unavailable on this game build";
            return false;
        };
        options.game_object_number_get = [](
            const btd5loader::runtime::GameObjectKind kind,
            void* const object,
            const std::string_view property) -> std::optional<double> {
            if (kind != btd5loader::runtime::GameObjectKind::Bloon ||
                property != "health" || object == nullptr || !g_bloon_health_verified) {
                return std::nullopt;
            }
            float value{};
            std::memcpy(
                &value,
                static_cast<const std::byte*>(object) + kBloonHealthOffset,
                sizeof(value));
            return static_cast<double>(value);
        };
        options.game_object_number_set = [](
            const btd5loader::runtime::GameObjectKind kind,
            void* const object,
            const std::string_view property,
            const double value,
            std::string& error) {
            if (kind == btd5loader::runtime::GameObjectKind::Match && property == "cash") {
                void* const economy = object == nullptr || g_cash_add == nullptr ||
                                              g_cash_spend == nullptr
                                          ? nullptr
                                          : read_field<void*>(object, kScreenEconomyOffset);
                if (economy == nullptr || !std::isfinite(value) || value < 0.0) {
                    error = "match cash is unavailable or outside the supported range";
                    return false;
                }
                const auto balance = read_field<double>(economy, kEconomyCashOffset);
                if (!std::isfinite(balance)) {
                    error = "match cash could not be read";
                    return false;
                }
                // The helper raises cash.changing and cash.changed through the
                // native money-updated event; that nested dispatch re-enters the gate.
                const double delta = value - balance;
                if (delta > 0.0) {
                    invoke_cash_helper(g_cash_add, economy, &delta);
                } else if (delta < 0.0) {
                    const double amount = -delta;
                    invoke_cash_helper(g_cash_spend, economy, &amount);
                }
                return true;
            }
            if (kind != btd5loader::runtime::GameObjectKind::Bloon ||
                property != "health" || object == nullptr || !g_bloon_health_verified ||
                !std::isfinite(value) || value < 0.0 ||
                value > static_cast<double>((std::numeric_limits<float>::max)())) {
                error = "bloon health is unavailable or outside the supported range";
                return false;
            }
            const auto stored = static_cast<float>(value);
            std::memcpy(
                static_cast<std::byte*>(object) + kBloonHealthOffset,
                &stored,
                sizeof(stored));
            return true;
        };
        options.game_object_text_get = [](
            const btd5loader::runtime::GameObjectKind kind,
            void* const object,
            const std::string_view property) -> std::optional<std::string> {
            using btd5loader::runtime::GameObjectKind;
            if (property != "type" || object == nullptr || !g_object_identity_verified) {
                return std::nullopt;
            }
            if (kind == GameObjectKind::Tower) {
                return std::string(
                    g_tower_registry.type_name(read_field<std::uint64_t>(object, kTowerTypeOffset)));
            }
            if (kind == GameObjectKind::Bloon) {
                const auto* const definition = read_field<const void*>(object, kBloonDefinitionOffset);
                if (definition == nullptr) return std::nullopt;
                return std::string(btd5loader::runtime::type_flag_name(
                    btd5loader::runtime::kBloonTypeNames,
                    read_field<std::uint32_t>(definition, kBloonDefinitionTypeOffset)));
            }
            return std::nullopt;
        };
        if (!load_localization(resource_directory, options.localization, error)) {
            g_logger.error("mods", requested.id + ":" + error);
            return rollback();
        }
        options.register_tower = [](const std::string_view id, btd5loader::runtime::CustomTowerSpec spec) {
            return register_custom_tower(id, std::move(spec));
        };
        options.log = [](const std::string_view level, const std::string_view message) {
            if (level == "error") {
                g_logger.error("lua", std::string(message));
            } else {
                g_logger.info("lua", std::string(message));
            }
        };
        auto mod = std::make_unique<LuaMod>(std::move(options));
        if (!mod->valid() || !mod->load_script(script, package->manifest.entry_point)) {
            g_logger.error("mods", requested.id + ":lua_script_load_failed");
            return rollback();
        }
        if (!mod->invoke("on_load")) {
            g_logger.error("mods", requested.id + ":on_load_disabled_after_error");
        }
        g_mods.push_back(std::move(mod));
        g_logger.info("mods", requested.id + ":loaded");
    }
    g_logger.info("runtime", "mods_loaded_waiting_for_game_ready_hook");
    return true;
}

DWORD WINAPI initialize_worker(LPVOID) {
    using btd5loader::runtime::State;
    std::scoped_lock lifecycle_lock(g_lifecycle_mutex);
    if (g_dispatch_gate.stopping()) return ERROR_CANCELLED;

    std::string launch_error;
    const auto identity = btd5loader::runtime::current_launch_identity();
    const auto pending_path = g_game_directory / L".btd5ml-launch" / (identity + L".json");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!identity.empty() && !g_dispatch_gate.stopping() && std::chrono::steady_clock::now() < deadline) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(pending_path, ec) && !ec) {
            const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            g_launch_ticket = btd5loader::runtime::consume_launch_ticket(g_game_directory, identity, now, launch_error);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (g_launch_ticket) {
        SetEnvironmentVariableW(L"BTD5ML_DATA_ROOT", g_launch_ticket->data_root.c_str());
    } else {
        // Steam may retain a legacy manager environment across launches.
        SetEnvironmentVariableW(L"BTD5ML_DATA_ROOT", nullptr);
    }

    if (!g_logger.initialize()) {
        (void)g_state.transition_to(State::Failed);
        return ERROR_WRITE_FAULT;
    }

    g_logger.info("runtime", "bootstrap_started");
    g_logger.info("runtime", btd5loader::kVersion);

    if (!g_launch_ticket) {
        g_logger.info("launch", launch_error.empty() ? "vanilla_no_ticket" : launch_error);
        return ERROR_SUCCESS;
    }

    if (g_game_directory.empty() ||
        !std::filesystem::is_regular_file(g_game_directory / L"BTD5-Win.exe")) {
        g_logger.error("runtime", "game_executable_not_found");
        (void)g_state.transition_to(State::Failed);
        return ERROR_FILE_NOT_FOUND;
    }

    if (!g_state.transition_to(State::CompatibilityCheck)) {
        g_logger.error("runtime", "invalid_compatibility_transition");
        (void)g_state.transition_to(State::Failed);
        return ERROR_INVALID_STATE;
    }

    const auto detection = btd5loader::runtime::detect_build(
        g_game_directory, g_game_directory / L"symbols");
    if (!detection.build) {
        g_logger.error("compatibility", detection.error);
        g_logger.info(
            "compatibility",
            "exe_sha256=" + detection.fingerprints.executable_sha256 +
                ";assets_sha256=" + detection.fingerprints.assets_sha256);
        (void)g_state.transition_to(State::Failed);
        return ERROR_REVISION_MISMATCH;
    }

    g_logger.info("compatibility", "supported_build=" + detection.build->id);
    const auto resolution = btd5loader::runtime::resolve_symbols_from_image(
        *detection.build, g_game_directory / L"BTD5-Win.exe");
    for (const auto& symbol : resolution.resolved) {
        g_logger.info("symbols", "resolved=" + symbol.name);
    }
    for (const auto& diagnostic : resolution.diagnostics) {
        const auto message = diagnostic.name + ":" + diagnostic.message;
        if (diagnostic.required) {
            g_logger.error("symbols", message);
        } else {
            g_logger.info("symbols", message);
        }
    }
    if (!resolution.success) {
        g_logger.error("symbols", "required_symbol_resolution_failed");
        (void)g_state.transition_to(State::Failed);
        return ERROR_INVALID_ADDRESS;
    }
    const auto exit_call = std::find_if(resolution.resolved.begin(), resolution.resolved.end(),
        [](const auto& symbol) { return symbol.name == "game.exit.call"; });
    const auto exit_predicate = std::find_if(resolution.resolved.begin(), resolution.resolved.end(),
        [](const auto& symbol) { return symbol.name == "game.exit.predicate"; });
    if (exit_call == resolution.resolved.end() || exit_predicate == resolution.resolved.end()) {
        g_logger.error("hooks", "normal_exit_boundary_unavailable");
        (void)g_state.transition_to(State::Failed);
        return ERROR_INVALID_ADDRESS;
    }
    const auto image_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    void* const exit_call_target = reinterpret_cast<void*>(image_base + exit_call->relative_virtual_address);
    const auto exit_predicate_target = reinterpret_cast<btd5loader::runtime::ExitHook::Predicate>(
        image_base + exit_predicate->relative_virtual_address);
    g_hooks.add({"game.exit.call", true,
        [exit_call_target, exit_predicate_target]() {
            std::string error;
            const bool installed = g_exit_hook.install(exit_call_target, exit_predicate_target,
                []() { shutdown_runtime(true); }, error);
            if (!installed) g_logger.error("hooks", error);
            return installed;
        }, []() { g_exit_hook.remove(); }});
    install_tower_art_check();
    register_dev_custom_towers();
    {
        const auto custom_target = [&resolution, image_base](const std::string_view name) -> void* {
            const auto found = std::find_if(resolution.resolved.begin(), resolution.resolved.end(),
                [name](const auto& symbol) { return symbol.name == name; });
            return found == resolution.resolved.end()
                       ? nullptr
                       : reinterpret_cast<void*>(image_base + found->relative_virtual_address);
        };
        const btd5loader::runtime::CustomTowerTargets targets{
            custom_target("custom_tower.tower.type.name_to_flag"),
            custom_target("custom_tower.tower.type.flag_to_name"),
            custom_target("custom_tower.file.open"),
            custom_target("custom_tower.string.construct"),
            custom_target("custom_tower.string.destroy"),
            custom_target("custom_tower.profile.tower_unlocked"),
            custom_target("custom_tower.shop.tower_unlocked"),
            custom_target("custom_tower.shop.tower_available"),
            custom_target("custom_tower.profile.upgrade_unlocked"),
            custom_target("custom_tower.profile.save"),
            custom_target("custom_tower.profile.tower_xp"),
            custom_target("custom_tower.text.lookup"),
            custom_target("custom_tower.xp.award"),
            custom_target("custom_tower.file.exists"),
            custom_target("custom_tower.texture.load_index"),
            custom_target("custom_tower.texture.get_sprite"),
            custom_target("custom_tower.texture.load"),
            custom_target("custom_tower.sfx.instance"),
            custom_target("custom_tower.sfx.play"),
        };
        g_hooks.add({"custom_tower.identity", false,
            [targets]() {
                std::string error;
                std::string progress_error;
                if (!g_tower_progress.load(
                        data_root() / L"storage" / L"custom-towers" / L"progress.json", progress_error)) {
                    g_logger.error("towers", "custom_tower_progress_unreadable " + progress_error);
                }
                const bool installed = g_custom_tower_hooks.install(targets, g_tower_registry, g_tower_progress,
                    [](const std::string_view line) { g_logger.info("towers", line); }, error);
                if (!installed) g_logger.error("towers", error);
                return installed;
            }, []() { g_custom_tower_hooks.remove(); }});
    }
    const auto game_screen_init = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "screen.game.init";
        });
    const auto game_screen_uninit = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "screen.game.uninit";
        });
    const auto event_dispatch = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.manager.dispatch";
        });
    const auto round_started_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.round.started.vtable";
        });
    const auto round_lifecycle_start = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "round.lifecycle.start";
        });
    const auto round_ended_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.round.ended.vtable";
        });
    const auto money_updated_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.money.updated.vtable";
        });
    const auto tower_spawned_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.tower.spawned.vtable";
        });
    const auto tower_upgraded_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.tower.upgraded.vtable";
        });
    const auto tower_sold_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.tower.sold.vtable";
        });
    const auto tower_manager_place = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "tower.manager.place";
        });
    const auto tower_pop_count_get = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "tower.pop_count.get";
        });
    const auto tower_pop_count_set = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "tower.pop_count.set";
        });
    const auto tower_sell_price_read = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "tower.sell_price.read";
        });
    const auto tower_upgrade_commit = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "tower.upgrade.commit";
        });
    const auto tower_sale_commit = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "tower.sale.commit";
        });
    const auto bloon_spawned_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.bloon.spawned.vtable";
        });
    const auto bloon_popped_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.bloon.popped.vtable";
        });
    const auto bloon_escaped_vtable = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "event.bloon.escaped.vtable";
        });
    const auto bloon_spawn_primary = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "bloon.manager.spawn.primary";
        });
    const auto bloon_spawn_secondary = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "bloon.manager.spawn.secondary";
        });
    const auto bloon_pop_commit = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "bloon.pop.commit";
        });
    const auto bloon_health_damage_commit = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "bloon.health.damage.commit";
        });
    const auto bloon_leak_commit = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "bloon.leak.commit";
        });
    const auto lives_gain_handler = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "player.lives.gain.handler";
        });
    const auto lives_loss_handler = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "player.lives.loss.handler";
        });
    const auto lives_gain_write = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "player.lives.gain.write";
        });
    const auto lives_loss_write = std::find_if(
        resolution.resolved.begin(),
        resolution.resolved.end(),
        [](const btd5loader::runtime::ResolvedSymbol& symbol) {
            return symbol.name == "player.lives.loss.write";
        });
    const auto find_symbol = [&resolution](const std::string_view name) {
        return std::find_if(
            resolution.resolved.begin(),
            resolution.resolved.end(),
            [name](const btd5loader::runtime::ResolvedSymbol& symbol) {
                return symbol.name == name;
            });
    };
    const auto attack_constructor = find_symbol("attack.constructor");
    const auto attack_destructor = find_symbol("attack.destructor");
    const auto projectile_constructor_primary = find_symbol("projectile.constructor.primary");
    const auto projectile_constructor_secondary = find_symbol("projectile.constructor.secondary");
    const auto projectile_constructor_tertiary = find_symbol("projectile.constructor.tertiary");
    const auto projectile_destructor = find_symbol("projectile.destructor");
    const auto tower_destructor = find_symbol("tower.destructor");
    const auto bloon_destructor = find_symbol("bloon.destructor");
    const auto tower_purchase_checks = find_symbol("tower.purchase.checks");
    const auto tower_upgrade_tier_increment = find_symbol("tower.upgrade_tier.increment");
    const auto bloon_type_read = find_symbol("bloon.type.read");
    const auto round_number_write = find_symbol("round.number.write");
    const auto cash_add = find_symbol("cash.add");
    const auto cash_spend = find_symbol("cash.spend");
    const auto cash_economy_read = find_symbol("cash.economy.read");
    const auto bloon_pop_decision = find_symbol("bloon.pop.decision");
    const auto bloon_spawn_emitter_return = find_symbol("bloon.spawn.emitter.return");
    const HMODULE executable = GetModuleHandleW(nullptr);
    g_image_base = reinterpret_cast<std::uintptr_t>(executable);
    if (game_screen_init == resolution.resolved.end() ||
        game_screen_uninit == resolution.resolved.end() ||
        event_dispatch == resolution.resolved.end() ||
        round_started_vtable == resolution.resolved.end() ||
        round_lifecycle_start == resolution.resolved.end() ||
        round_ended_vtable == resolution.resolved.end() ||
        money_updated_vtable == resolution.resolved.end() ||
        tower_spawned_vtable == resolution.resolved.end() ||
        tower_upgraded_vtable == resolution.resolved.end() ||
        tower_sold_vtable == resolution.resolved.end() ||
        tower_manager_place == resolution.resolved.end() ||
        tower_pop_count_get == resolution.resolved.end() ||
        tower_pop_count_set == resolution.resolved.end() ||
        tower_sell_price_read == resolution.resolved.end() ||
        tower_upgrade_commit == resolution.resolved.end() ||
        tower_sale_commit == resolution.resolved.end() ||
        bloon_spawned_vtable == resolution.resolved.end() ||
        bloon_popped_vtable == resolution.resolved.end() ||
        bloon_escaped_vtable == resolution.resolved.end() ||
        bloon_spawn_primary == resolution.resolved.end() ||
        bloon_spawn_secondary == resolution.resolved.end() ||
        bloon_pop_commit == resolution.resolved.end() ||
        bloon_health_damage_commit == resolution.resolved.end() ||
        bloon_leak_commit == resolution.resolved.end() ||
        lives_gain_handler == resolution.resolved.end() ||
        lives_loss_handler == resolution.resolved.end() ||
        lives_gain_write == resolution.resolved.end() ||
        lives_loss_write == resolution.resolved.end() ||
        attack_constructor == resolution.resolved.end() ||
        attack_destructor == resolution.resolved.end() ||
        projectile_constructor_primary == resolution.resolved.end() ||
        projectile_constructor_secondary == resolution.resolved.end() ||
        projectile_constructor_tertiary == resolution.resolved.end() ||
        projectile_destructor == resolution.resolved.end() ||
        tower_destructor == resolution.resolved.end() ||
        bloon_destructor == resolution.resolved.end() ||
        tower_purchase_checks == resolution.resolved.end() ||
        tower_upgrade_tier_increment == resolution.resolved.end() ||
        bloon_type_read == resolution.resolved.end() ||
        round_number_write == resolution.resolved.end() ||
        cash_add == resolution.resolved.end() ||
        cash_spend == resolution.resolved.end() ||
        cash_economy_read == resolution.resolved.end() ||
        bloon_pop_decision == resolution.resolved.end() ||
        bloon_spawn_emitter_return == resolution.resolved.end() || executable == nullptr) {
        g_logger.error("hooks", "gameplay lifecycle target unavailable");
        (void)g_state.transition_to(State::Failed);
        return ERROR_INVALID_ADDRESS;
    }
    void* const game_screen_init_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + game_screen_init->relative_virtual_address);
    void* const game_screen_uninit_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + game_screen_uninit->relative_virtual_address);
    void* const round_lifecycle_start_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + round_lifecycle_start->relative_virtual_address);
    void* const event_dispatch_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + event_dispatch->relative_virtual_address);
    void* const round_started_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + round_started_vtable->relative_virtual_address);
    void* const round_ended_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + round_ended_vtable->relative_virtual_address);
    void* const money_updated_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + money_updated_vtable->relative_virtual_address);
    void* const tower_spawned_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_spawned_vtable->relative_virtual_address);
    void* const tower_upgraded_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_upgraded_vtable->relative_virtual_address);
    void* const tower_sold_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_sold_vtable->relative_virtual_address);
    void* const tower_manager_place_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_manager_place->relative_virtual_address);
    g_tower_pop_count_get = reinterpret_cast<TowerPopCountGetter>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_pop_count_get->relative_virtual_address);
    g_tower_pop_count_set = reinterpret_cast<TowerPopCountSetter>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_pop_count_set->relative_virtual_address);
    g_tower_sell_price_verified = true;
    g_bloon_health_verified = true;
    g_object_identity_verified = true;
    g_cash_add = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + cash_add->relative_virtual_address);
    g_cash_spend = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + cash_spend->relative_virtual_address);
    void* const tower_upgrade_commit_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_upgrade_commit->relative_virtual_address);
    void* const tower_sale_commit_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + tower_sale_commit->relative_virtual_address);
    void* const bloon_spawned_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_spawned_vtable->relative_virtual_address);
    void* const bloon_popped_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_popped_vtable->relative_virtual_address);
    void* const bloon_escaped_vtable_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_escaped_vtable->relative_virtual_address);
    void* const bloon_spawn_primary_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_spawn_primary->relative_virtual_address);
    void* const bloon_spawn_secondary_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_spawn_secondary->relative_virtual_address);
    // bloon.popping moved to the lethal hit; bloon.pop.commit stays a required
    // anchor for the bloon field symbols but is no longer hooked.
    void* const bloon_pop_decision_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_pop_decision->relative_virtual_address);
    btd5loader::runtime::BloonActionHook::SpawnRejection bloon_spawn_rejection;
    {
        std::string error;
        if (!btd5loader::runtime::BloonActionHook::resolve_rejection(
                bloon_spawn_primary_target,
                reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(executable) +
                                        bloon_pop_commit->relative_virtual_address),
                reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(executable) +
                                        bloon_spawn_emitter_return->relative_virtual_address),
                bloon_spawn_rejection, error)) {
            g_logger.error("hooks", error);
            (void)g_state.transition_to(State::Failed);
            return ERROR_INVALID_ADDRESS;
        }
    }
    void* const bloon_leak_commit_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + bloon_leak_commit->relative_virtual_address);
    void* const lives_gain_handler_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + lives_gain_handler->relative_virtual_address);
    void* const lives_loss_handler_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + lives_loss_handler->relative_virtual_address);
    void* const lives_gain_write_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + lives_gain_write->relative_virtual_address);
    void* const lives_loss_write_target = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(executable) + lives_loss_write->relative_virtual_address);
    const auto image_target = [executable](const btd5loader::runtime::ResolvedSymbol& symbol) {
        return reinterpret_cast<void*>(
            reinterpret_cast<std::uintptr_t>(executable) + symbol.relative_virtual_address);
    };
    btd5loader::runtime::ObjectLifetimeHook::Targets object_lifetime_targets;
    object_lifetime_targets.attack_constructor = image_target(*attack_constructor);
    object_lifetime_targets.attack_destructor = image_target(*attack_destructor);
    object_lifetime_targets.projectile_constructors = {
        image_target(*projectile_constructor_primary),
        image_target(*projectile_constructor_secondary),
        image_target(*projectile_constructor_tertiary)};
    object_lifetime_targets.projectile_destructor = image_target(*projectile_destructor);
    object_lifetime_targets.tower_destructor = image_target(*tower_destructor);
    object_lifetime_targets.bloon_destructor = image_target(*bloon_destructor);
    g_hooks.add({
        "render.swap_buffers",
        true,
        []() {
            std::string error;
            const bool installed = g_frame_hook.install(&dispatch_frame, error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_frame_hook.remove(); }});
    g_hooks.add({
        "screen.game.lifecycle",
        true,
        [game_screen_init_target, game_screen_uninit_target]() {
            std::string error;
            const bool installed = g_match_hook.install(
                game_screen_init_target,
                game_screen_uninit_target,
                [](void* match) {
                    const auto fate = g_teardown_ledger.settle();
                    if (!fate.empty()) g_logger.info("objects", fate);
                    g_object_events.reset_scene();
                    g_destroyed_objects = {};
                    g_event_counts.clear();
                    dispatch_match_event("match.starting", match);
                },
                [](void* match) { dispatch_match_event("match.started", match); },
                [](void* match) {
                    log_type_watch(g_type_watch.disarm(nullptr));
                    dispatch_match_event("match.ending", match);
                },
                [](void* match) {
                    using btd5loader::runtime::GameObjectKind;
                    const auto count = [](const GameObjectKind kind) {
                        return std::to_string(g_destroyed_objects[static_cast<std::size_t>(kind)]);
                    };
                    g_logger.info("objects",
                        "destroyed_tracked tower=" + count(GameObjectKind::Tower) +
                        ";bloon=" + count(GameObjectKind::Bloon) +
                        ";attack=" + count(GameObjectKind::Attack) +
                        ";projectile=" + count(GameObjectKind::Projectile) +
                        ";remaining=" + std::to_string(g_game_objects.live_count()));
                    std::string remaining = "remaining_live";
                    for (const auto kind : {GameObjectKind::Match, GameObjectKind::Round,
                             GameObjectKind::Player, GameObjectKind::Tower,
                             GameObjectKind::Attack, GameObjectKind::Projectile,
                             GameObjectKind::Bloon}) {
                        remaining += std::string(remaining.back() == 'e' ? " " : ";") +
                                     std::string(btd5loader::runtime::game_object_kind_name(kind)) +
                                     "=" + std::to_string(g_game_objects.live_count(kind));
                    }
                    g_logger.info("objects", remaining);
                    // Scene-scoped match/round/player handles are released by the
                    // scene reset below; native objects still tracked are survivors.
                    std::vector<std::pair<GameObjectKind, void*>> survivors;
                    for (const auto kind : {GameObjectKind::Tower, GameObjectKind::Attack,
                             GameObjectKind::Projectile, GameObjectKind::Bloon}) {
                        for (void* const object : g_game_objects.live_objects(kind)) {
                            survivors.emplace_back(kind, object);
                        }
                    }
                    g_logger.info("objects", g_teardown_ledger.capture(survivors));
                    g_logger.info("objects", g_teardown_ledger.outstanding());
                    std::string counts = "high_volume_counts";
                    for (const auto& [event, total] : g_event_counts) {
                        counts += std::string(counts.back() == 's' ? " " : ";") + event + "=" +
                                  std::to_string(total);
                    }
                    g_logger.info("events", counts);
                    dispatch_match_event("match.ended", match, true);
                },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_match_hook.remove(); }});
    g_hooks.add({
        "round.lifecycle.identity",
        true,
        [round_lifecycle_start_target]() {
            std::string error;
            const bool installed =
                g_round_hook.install(
                    round_lifecycle_start_target,
                    [](void* round) { g_object_events.begin_round(round); },
                    error);
            if (!installed) g_logger.error("hooks", error);
            return installed;
        },
        []() { g_round_hook.remove(); }});
    g_hooks.add({
        "tower.placing",
        true,
        [tower_manager_place_target]() {
            std::string error;
            const bool installed = g_tower_placement_hook.install(
                tower_manager_place_target,
                [](void* tower) {
                    if (g_object_events.take_purchase(tower)) return;
                    dispatch_tower_event("tower.placing", tower, false, false, false);
                },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_tower_placement_hook.remove(); }});
    void* const tower_purchase_target = image_target(*tower_purchase_checks);
    g_hooks.add({
        "tower.purchasing",
        true,
        [tower_purchase_target]() {
            std::string error;
            const bool installed = g_tower_purchase_hook.install(
                tower_purchase_target,
                [](void* tower) {
                    const bool cancelled =
                        dispatch_tower_event("tower.placing", tower, false, true, true);
                    g_object_events.purchase_delivered(tower, cancelled);
                    return cancelled;
                },
                error);
            if (!installed) g_logger.error("hooks", error);
            return installed;
        },
        []() { g_tower_purchase_hook.remove(); }});
    g_hooks.add({
        "tower.upgrading",
        true,
        [tower_upgrade_commit_target]() {
            std::string error;
            const bool installed = g_tower_upgrade_hook.install(
                tower_upgrade_commit_target,
                [](void* tower) {
                    return dispatch_tower_event("tower.upgrading", tower, false, true);
                },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_tower_upgrade_hook.remove(); }});
    g_hooks.add({
        "tower.selling",
        true,
        [tower_sale_commit_target]() {
            std::string error;
            const bool installed = g_tower_sale_hook.install(
                tower_sale_commit_target,
                [](void* tower) {
                    return dispatch_tower_event("tower.selling", tower, false, true);
                },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_tower_sale_hook.remove(); }});
    g_hooks.add({
        "bloon.pre_actions",
        true,
        [bloon_spawn_primary_target,
         bloon_spawn_secondary_target,
         bloon_spawn_rejection,
         bloon_pop_decision_target,
         bloon_leak_commit_target]() {
            std::string error;
            const bool installed = g_bloon_action_hook.install(
                bloon_spawn_primary_target,
                bloon_spawn_secondary_target,
                bloon_spawn_rejection,
                bloon_pop_decision_target,
                bloon_leak_commit_target,
                [](void* bloon, bool from_round) {
                    return dispatch_bloon_spawning(bloon, from_round);
                },
                [](void* bloon) {
                    return dispatch_bloon_event("bloon.popping", bloon, false, true);
                },
                [](void* bloon) {
                    return dispatch_bloon_event("bloon.leaking", bloon, false, true);
                },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_bloon_action_hook.remove(); }});
    g_hooks.add({
        "object.lifetime",
        true,
        [object_lifetime_targets]() {
            std::string error;
            const bool installed = g_object_lifetime_hook.install(
                object_lifetime_targets, &object_created, &object_destroying, error);
            if (!installed) g_logger.error("hooks", error);
            return installed;
        },
        []() { g_object_lifetime_hook.remove(); }});
    g_hooks.add({
        "event.gameplay.lifecycle",
        true,
        [event_dispatch_target,
         round_started_vtable_target,
         round_ended_vtable_target,
         money_updated_vtable_target,
         tower_spawned_vtable_target,
         tower_upgraded_vtable_target,
         tower_sold_vtable_target,
         bloon_spawned_vtable_target,
         bloon_popped_vtable_target,
         bloon_escaped_vtable_target]() {
            std::string error;
            const bool installed = g_native_event_hook.install(
                event_dispatch_target,
                {
                    {
                        round_started_vtable_target,
                        {},
                        [](void*) { dispatch_round_event("round.starting"); },
                        [](void*) { dispatch_round_event("round.started"); },
                    },
                    {
                        round_ended_vtable_target,
                        {},
                        [](void*) { dispatch_round_event("round.ending"); },
                        [](void*) { dispatch_round_event("round.ended", true); },
                    },
                    {
                        money_updated_vtable_target,
                        [](void* event) { return g_cash_captures.capture(event); },
                        [](void* captured) { dispatch_cash_event("cash.changing", captured); },
                        [](void* captured) {
                            struct Release final {
                                void* token;
                                ~Release() { g_cash_captures.release(token); }
                            } release{captured};
                            dispatch_cash_event("cash.changed", captured);
                        },
                    },
                    {
                        tower_spawned_vtable_target,
                        &capture_game_object_event,
                        {},
                        [](void* tower) {
                            dispatch_tower_event("tower.placed", tower, false);
                            const auto armed = g_type_watch.arm(tower);
                            if (!armed.empty()) g_logger.info("diag", armed);
                        },
                    },
                    {
                        tower_upgraded_vtable_target,
                        &capture_game_object_event,
                        {},
                        [](void* tower) { dispatch_tower_event("tower.upgraded", tower, false); },
                    },
                    {
                        tower_sold_vtable_target,
                        &capture_game_object_event,
                        {},
                        [](void* tower) {
                            dispatch_tower_event("tower.sold", tower, true);
                            log_type_watch(g_type_watch.disarm(tower));
                        },
                    },
                    {
                        bloon_spawned_vtable_target,
                        &capture_game_object_event,
                        {},
                        [](void* bloon) { dispatch_bloon_event("bloon.spawned", bloon, false); },
                    },
                    {
                        bloon_popped_vtable_target,
                        &capture_game_object_event,
                        {},
                        [](void* bloon) { dispatch_bloon_event("bloon.popped", bloon, true); },
                    },
                    {
                        bloon_escaped_vtable_target,
                        &capture_game_object_event,
                        {},
                        [](void* bloon) { dispatch_bloon_event("bloon.leaked", bloon, true); },
                    },
                },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_native_event_hook.remove(); }});
    g_hooks.add({
        "player.lives.changing",
        true,
        [lives_gain_write_target, lives_loss_write_target]() {
            std::string error;
            const bool installed = g_lives_write_hook.install(
                lives_gain_write_target,
                lives_loss_write_target,
                [](void* state, const std::int32_t before, const std::int32_t after) {
                    return dispatch_lives_changing(state, before, after);
                },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_lives_write_hook.remove(); }});
    g_hooks.add({
        "player.lives.changed",
        true,
        [lives_gain_handler_target, lives_loss_handler_target]() {
            std::string error;
            const bool installed = g_lives_hook.install(
                lives_gain_handler_target,
                lives_loss_handler_target,
                [](void* state, const std::int32_t before, const std::int32_t after) {
                    dispatch_lives_event("lives.changed", state, before, after);
                },
                error);
            if (!installed) {
                g_logger.error("hooks", error);
            }
            return installed;
        },
        []() { g_lives_hook.remove(); }});
    std::string hook_error;
    if (!g_hooks.commit(hook_error)) {
        g_logger.error("hooks", hook_error);
        (void)g_state.transition_to(State::Failed);
        return ERROR_HOOK_NOT_INSTALLED;
    }
    if (!g_state.transition_to(State::HooksReady)) {
        g_hooks.rollback();
        g_logger.error("runtime", "invalid_hooks_ready_transition");
        (void)g_state.transition_to(State::Failed);
        return ERROR_INVALID_STATE;
    }
    g_logger.info(
        "runtime",
        "hooks_ready=render.swap_buffers,screen.game.init,screen.game.uninit,round.lifecycle.identity,event.gameplay.lifecycle,"
        "tower.placing,tower.upgrading,tower.selling,bloon.spawning,bloon.popping,bloon.leaking,"
        "player.lives.changing,player.lives.changed,object.lifetime,tower.purchasing");
    const bool mods_loaded = load_active_mods(detection.build->id);
    btd5loader::runtime::CustomTowerHooks::finish_registration();
    guard_custom_tower_saves();
    if (!mods_loaded) {
        if (!g_dispatch_gate.stopping()) g_hooks.rollback();
        g_logger.error("runtime", "active_mod_loading_failed");
        (void)g_state.transition_to(State::Failed);
        return ERROR_INVALID_DATA;
    }
    {
        const auto temporary = std::filesystem::path(g_launch_ticket->acknowledgment_path.wstring() + L".tmp");
        {
            std::ofstream acknowledgment(temporary, std::ios::binary);
            acknowledgment << GetCurrentProcessId();
        }
        if (!MoveFileExW(temporary.c_str(), g_launch_ticket->acknowledgment_path.c_str(), MOVEFILE_WRITE_THROUGH)) {
            g_logger.error("launch", "launch_acknowledgment_failed");
        }
    }
    return ERROR_SUCCESS;
}

}  // namespace

extern "C" BOOL WINAPI BTD5Loader_Initialize(
    const wchar_t* game_directory) {
    using btd5loader::runtime::State;

    if (game_directory == nullptr || !g_state.transition_to(State::Bootstrap)) {
        return FALSE;
    }

    g_game_directory = game_directory;
    const HANDLE worker = CreateThread(nullptr, 0, initialize_worker, nullptr, 0, nullptr);
    if (worker == nullptr) {
        (void)g_state.transition_to(State::Failed);
        return FALSE;
    }
    CloseHandle(worker);
    return TRUE;
}

extern "C" btd5loader::runtime::State WINAPI BTD5Loader_GetState() {
    return g_state.current();
}

extern "C" BOOL WINAPI BTD5Loader_LaunchProtocol1() { return TRUE; }

static void shutdown_runtime(const bool normal_exit) {
    using btd5loader::runtime::State;
    if (g_state.current() == State::NotStarted) return;
    (void)g_dispatch_gate.request_stop();
    // Serialize with worker-owned installation/mod loading, then drain Lua work.
    std::scoped_lock lifecycle_lock(g_lifecycle_mutex);
    if (g_shutdown_started) {
        if (normal_exit) g_hooks.rollback();
        return;
    }
    g_shutdown_started = true;
    {
        auto lock = g_dispatch_gate.exclusive();
        (void)g_state.transition_to(State::ShuttingDown);
        g_logger.info("runtime", "shutdown_requested");
        log_type_watch(g_type_watch.disarm(nullptr));
        g_type_watch.shutdown();
        g_tower_pop_count_get = nullptr;
        g_tower_pop_count_set = nullptr;
        g_tower_sell_price_verified = false;
        g_bloon_health_verified = false;
        g_object_identity_verified = false;
        g_cash_add = nullptr;
        g_cash_spend = nullptr;
        const auto fate = g_teardown_ledger.settle();
        if (!fate.empty()) g_logger.info("objects", fate);
        g_object_events.reset_scene();
        btd5loader::runtime::shutdown_mods(g_mods);
    }
    // An explicit request can race a native detour outside its Lua dispatch.
    // Leave inert hooks/trampolines installed until game.main has returned.
    if (normal_exit) g_hooks.rollback();
    std::error_code filesystem_error;
    if (!g_session_directory.empty()) {
        std::filesystem::remove_all(g_session_directory, filesystem_error);
        if (filesystem_error) {
            g_logger.error("runtime", "shutdown_session_cleanup_failed");
            return;
        }
        g_session_directory.clear();
    }
    g_logger.info("runtime", "shutdown_complete");
}

extern "C" void WINAPI BTD5Loader_Shutdown() {
    shutdown_runtime(false);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
