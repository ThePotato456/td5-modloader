# BTD5 Mod Loader — Implementation Action Plan

## Tracker rules

- `[ ]` means not started, `[~]` means in progress, and `[x]` means complete.
- Complete phases in order. Do not begin the next phase until the current phase's implementation gate is checked.
- A gate passes only when its listed automated tests pass and its manual acceptance check has been recorded.
- Do not commit or redistribute BTD5 executables, game assets, saves, Steam account identifiers, API keys, installed mods, backups, or machine-specific paths.

## Locked v1 decisions

- [x] Target the 32-bit Windows Steam build of Bloons TD 5.
- [x] Implement the bootstrap, hooks, game integration, networking guard, and Lua host in C++20.
- [x] Make Lua 5.4 the only public mod programming language; do not expose a native DLL plugin ABI in v1.
- [x] Build the player-facing manager with C# and WPF.
- [x] Support local `.btd5mod` packages containing Lua, configuration, localization, and assets.
- [x] Include new towers as a first-release feature, alongside gameplay events and balance changes.
- [x] Support named profiles, shared vanilla saves with pre-launch backups, offline-only modded play, and automatic recovery from startup crashes.
- [x] Support only explicitly fingerprinted game builds and fail closed on unknown builds.
- [x] License the loader under GPL-3.0-only so GPL-covered NKHook5 code can be adapted with attribution and corresponding source.
- [x] Treat BTD5-Decomp as research-only; do not copy or adapt its unlicensed source.

Initial supported build fingerprints:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

---

## Phase 1 — Repository and reproducible build foundation

- [x] Create a CMake-based C++20 solution for x86 bootstrap/runtime code.
- [x] Create the .NET WPF manager solution and shared test projects.
- [x] Establish directories for runtime code, manager code, Lua API definitions, build-specific symbol maps, package schemas, samples, tests, and documentation.
- [x] Pin third-party source dependencies and record their licenses. Include Lua 5.4, a hooking library, JSON parsing, ZIP handling, and the C++ test framework.
- [x] Add build presets for Debug and Release x86 runtime artifacts.
- [x] Add formatting, static-analysis, and test commands that do not require BTD5 to be installed.
- [x] Add ignore rules for build output, local game paths, extracted mods, logs, backups, and test artifacts.
- [x] Add a local, untracked configuration template for locating an installed Steam copy during integration testing.

### Phase 1 implementation gate

- [x] A clean checkout builds the C++ x86 targets and WPF manager using documented commands.
- [x] All empty-project tests run successfully without game files.
- [x] No generated artifact or machine-specific path appears in version control.

---

## Phase 2 — Reversible bootstrap and runtime lifecycle

- [x] Implement a 32-bit `wininet.dll` proxy that loads the genuine DLL from `SysWOW64` and correctly forwards the WinINet API imported by BTD5.
- [x] Keep the proxy minimal; load a separate loader runtime DLL for all substantial behavior.
- [x] Locate the game directory from the current process rather than hard-coding a Steam path.
- [x] Initialize structured file logging before installing hooks or loading mods.
- [x] Add runtime states for bootstrap, compatibility check, hooks ready, mods loading, game ready, shutting down, and failed.
- [x] Make loader installation reversible without modifying `BTD5-Win.exe` or `BTD5.jet`.
- [x] Create a fixture executable that imports `wininet.dll` and exercises every forwarded function used by the game.

### Phase 2 implementation gate

- [x] The fixture runs identically with and without the proxy present.
- [x] Installing and removing the proxy leaves fixture and game binaries byte-for-byte unchanged.
- [x] Runtime initialization failures produce a readable log and return control without recursive loading or an unexplained crash.

---

## Phase 3 — Build detection, symbols, and hook safety

- [x] Hash the executable and `BTD5.jet` before any game hook is installed.
- [x] Define a versioned symbol-map format containing build hashes, named functions/data, signatures or offsets, validation bytes, and hook prerequisites.
- [x] Add the initial map for the confirmed 4.8 build.
- [x] Resolve game locations by stable names internally so Lua APIs never expose raw addresses.
- [x] Validate every resolved location against expected module ranges and instruction/data patterns.
- [x] Install hooks transactionally: if a required hook fails, remove hooks already installed and abort mod loading.
- [x] Add a developer-only diagnostics report for resolved and unresolved symbols without dumping proprietary game code.

### Phase 3 implementation gate

- [x] The supported build resolves every mandatory bootstrap symbol and reaches a stable no-mod launch.
- [x] Altered or unknown hashes prevent all game hooks and Lua mods from loading.
- [x] Injected resolver and hook failures roll back cleanly and explain the failed symbol in the log.

---

## Phase 4 — Lua host, sandbox, and mod package contract

- [x] Embed Lua 5.4 in the runtime and create one isolated Lua state per enabled mod.
- [x] Expose lifecycle callbacks for `on_load`, `on_ready`, and `on_shutdown`.
- [x] Remove unrestricted `io`, `os`, `package`, `debug`, `loadfile`, `dofile`, native-library loading, process launch, and networking from mod environments.
- [x] Provide sandboxed APIs for logging, configuration, deterministic timers, mod-owned storage, localization, and packaged-resource lookup.
- [x] Enforce CPU instruction budgets, callback time limits, memory limits, and recursion limits per mod.
- [x] Catch Lua errors at every host boundary, annotate them with mod ID and callback, and disable only the failing callback when continuing is safe.
- [x] Define `.btd5mod` as a ZIP package with `mod.json`, Lua entry points, assets, localization, configuration defaults, and optional documentation.
- [x] Require manifests to declare stable ID, name, author, semantic version, entry point, loader API version, supported game builds, dependencies, load-order constraints, and capabilities.
- [x] Reject path traversal, duplicate IDs, malformed archives, unsupported API versions, dependency cycles, and files outside package limits.
- [x] Dispatch mods deterministically by dependency order, explicit ordering rules, profile order, and finally mod ID.

### Phase 4 implementation gate

- [x] A sample Lua mod loads, logs, stores configuration, receives lifecycle callbacks, and shuts down cleanly.
- [x] Sandbox tests prove that a Lua mod cannot access arbitrary files, processes, networking, Windows APIs, native DLLs, or another mod's storage.
- [x] Malformed packages and dependency cycles are rejected without starting the game runtime.
- [x] A deliberately failing or runaway Lua callback is contained without corrupting another Lua state.

---

## Phase 5 — Manager, installation, profiles, and package workflow

- [x] Discover Steam libraries and validate candidate BTD5 installations by executable metadata and hashes.
- [x] Implement loader install, verify, repair, and uninstall using a manager-owned installation manifest.
- [x] Never overwrite a pre-existing proxy DLL silently; identify conflicts and provide a non-destructive recovery path.
- [x] Implement drag-and-drop and file-picker installation for local `.btd5mod` packages.
- [x] Show package identity, version, dependencies, capabilities, supported game builds, and validation errors.
- [x] Implement named profiles with enabled mods, deterministic load order, configuration, and launch history.
- [x] Add enable, disable, reorder, upgrade, downgrade, and uninstall operations with dependency checks.
- [x] Add modded launch, vanilla launch, log viewing, diagnostics export, and loader status views.
- [x] Keep installed packages and manager state under `%LocalAppData%\BTD5ModLoader`, not in the repository.

### Phase 5 implementation gate

- [x] A new user can locate BTD5, install the loader, install a sample package, create a profile, and launch it without manual file editing.
- [x] Repair restores missing loader-owned files, and uninstall removes only files recorded as loader-owned.
- [x] Conflicting, incompatible, or dependency-broken profiles cannot launch and display an actionable explanation.

---

> **Phase 5R passed on 2026-08-30.** Real-use testing exposed workflow and recovery problems
> after the original Phase 5 gate; the revamp below resolved them and passed a disposable
> fresh-state manager walkthrough. Forward feature work may resume with the remaining Phase 6 work.

## Phase 5R — Manager workflow and reliability revamp

- [x] Audit the existing setup, package, profile, configuration, launch, and recovery flows.
- [x] Define the user-facing state model: an installed package is separate from profile
  membership, enabled state, selected version, and per-profile configuration.
- [x] Persist the selected game copy and current profile with atomic, validated manager settings.
- [x] Replace the artifact-folder picker with automatic discovery of release files shipped beside
  the manager; keep an explicit developer override outside the normal workflow.
- [x] Replace separate install/verify/repair buttons with one loader health inspection and one
  context-aware primary action. Keep uninstall in an advanced recovery area.
- [x] Report loader files individually as healthy, missing, modified, unavailable, or foreign;
  distinguish a missing record, invalid record, unsupported game, and partial installation.
- [x] Make install and repair transactional, automatically verify their result, preserve foreign or
  modified files, and leave an actionable recovery result after interruption or partial failure.
  - [x] Preflight every repair before writing so conflicts and unavailable release files leave the
    existing installation unchanged.
  - [x] Verify each copied file and the completed operation; roll back loader-owned copies after
    cancellation, I/O failure, or failed post-operation verification.
  - [x] Persist and reconcile an operation journal when the manager process terminates mid-operation.
- [x] Replace the dual-list profile editor with a current-profile mod list containing an enable
  toggle, selected version, dependency status, load order, Configure action, and Remove action.
- [x] Add profile create, select, rename, duplicate, and delete workflows with a visible current
  profile and safe fallback when the current profile is removed.
- [x] Add a practical per-profile configuration editor inferred from package defaults for booleans,
  numbers, and strings, with validation plus per-setting and whole-mod reset controls.
- [x] Automatically inspect loader, game, and current-profile readiness at startup and before launch;
  route failures to the exact corrective action instead of a generic status string.
- [x] Migrate existing profile and installation state without losing configuration or launch history.
- [x] Update manager documentation and screenshots after the workflow stabilizes.

### Phase 5R implementation gate

- [x] Automated tests cover settings persistence/recovery, profile lifecycle, configuration edits,
  every loader health state, interrupted operations, post-operation verification, and migrations.
- [x] From a fresh state, a player can choose BTD5 once, install the loader with one primary action,
  install a mod, enable and configure it in the current profile, and launch without editing files.
- [x] Restarting the manager preserves the game and current profile; invalid saved selections recover
  to an explicit safe state without silently selecting an unrelated profile.
- [x] Missing loader files can be repaired, while modified or foreign files are preserved and shown
  with a clear manual recovery path; uninstall removes only verified loader-owned files.
- [x] A profile cannot launch with broken dependencies or invalid configuration, and the manager
  identifies the affected mod and corrective action.

---

## Phase 6 — Core gameplay object model and mutable events

- [x] Install a transactional render-frame hook without patching the game executable on disk.
- [x] Dispatch Lua `on_ready()` once from the live game render thread and advance deterministic timers once per rendered frame.
- [x] Prove the lifecycle bridge in the copied Steam build with a rerunnable `on_load`/`on_ready`/timer smoke test.
- [x] Dispatch live match start/end events around verified game-screen initialization and teardown boundaries.
- [x] Define versioned Lua wrappers with runtime lifetime checks for matches, rounds, players, towers, attacks, projectiles, and bloons.
- [x] Prevent wrappers from accessing game objects after those objects have been destroyed or their scene has changed.
- [x] Implement event subscription/unsubscription and deterministic handler ordering.
- [x] Add match start/end events.
- [x] Add round start/end events.
- [x] Add cash change events.
- [x] Add a post-change lives notification for verified gains and losses.
- [x] Add a verified lives pre-change event at the exact native write boundary.
- [x] Make `lives.changing` cancellable so Lua can skip a verified gain or loss write.
- [x] Add post-action tower placed, upgraded, and sold notifications.
- [x] Attach stable, lifetime-checked tower wrappers to live tower notifications.
- [x] Add a verified tower placing pre-event immediately before manager ownership.
- [x] Add a verified tower upgrading pre-event after eligibility and before mutation.
- [x] Add a verified tower selling pre-event before sale side effects begin.
- [x] Add post-action bloon spawned, popped, and leaked notifications.
- [x] Attach stable, lifetime-checked bloon wrappers to live bloon notifications.
- [x] Add verified bloon spawning, popping, and leaking pre-events.
- [ ] Make verified pre-events cancellable and mutable.
- [ ] Make supported live-object properties mutable through validated setters; reject invalid types, ranges, phases, and stale objects.
- [x] Guard against recursive event loops when a mod mutation triggers another game event.
- [ ] Document which fields are mutable, when changes take effect, and which mutations may be rejected.

### Phase 6 implementation gate

- [ ] Automated mock-host tests cover every event, mutable field, cancellation path, invalid mutation, stale wrapper, and recursive dispatch case.
- [ ] In-game smoke tests confirm event order and mutations across a complete match without hooks firing twice or surviving scene teardown.
- [ ] Disabling the sample event mod restores unmodified gameplay on the next launch.

---

## Phase 7 — Custom tower content API

- [ ] Add `btd5.towers.register(definition)` with stable namespaced tower IDs.
- [ ] Define the v1 tower schema for display/localization keys, base cost, range, footprint, targeting modes, base statistics, shop placement, and packaged visual/audio resources.
- [ ] Define attacks as composable data: cooldown, targeting, projectile, pierce, damage, damage types, area effects, status effects, and child attacks.
- [ ] Allow Lua callbacks for behavior that cannot be represented declaratively while applying the same sandbox and callback limits.
- [ ] Support two BTD5-style upgrade paths, tier prerequisites, cross-path restrictions, cost, text, stat mutations, behavior additions, and visual changes.
- [ ] Register custom towers into the in-game tower menu without replacing a vanilla tower ID.
- [ ] Create and destroy custom tower instances through the normal match lifecycle.
- [ ] Keep custom unlocks, settings, and mod-specific progression in the mod-owned store; do not write custom identifiers into vanilla progression data.
- [ ] Define missing-mod behavior: refuse to load mod-owned session data and report the missing tower/mod rather than substituting or corrupting state.
- [ ] Detect duplicate tower IDs, invalid upgrade graphs, missing assets, unsupported behavior combinations, and shop-capacity conflicts before entering a match.

### Phase 7 implementation gate

- [ ] A sample Lua mod adds a distinct tower with a shop entry, base attack, targeting choices, two upgrade paths, and custom behavior.
- [ ] The sample tower can be placed, selected, upgraded, sold, and cleaned up across repeated matches without stale hooks or leaked objects.
- [ ] Conflicting or invalid tower definitions fail package/profile validation with actionable errors.
- [ ] Vanilla towers and vanilla progression remain unchanged when the custom-tower mod is disabled.

---

## Phase 8 — Asset, localization, and content packaging pipeline

- [ ] Support packaged tower portraits, shop icons, world sprites/animations, projectile sprites, effects, and audio in documented formats and size limits.
- [ ] Decode resources outside time-critical hooks and cache them per mod with bounded memory use.
- [ ] Give every loaded resource a namespaced ID and track ownership for cleanup.
- [ ] Support localization tables with a required fallback language and safe formatting placeholders.
- [ ] Define resolution, pivot, animation-frame, audio-format, and fallback requirements for tower assets.
- [ ] Release cached content on profile shutdown and scene transitions where appropriate.
- [ ] Extend package validation to report missing references, unsupported formats, excessive dimensions, and memory-budget estimates.

### Phase 8 implementation gate

- [ ] The sample custom tower renders its own menu art, in-game animation, projectile/effect art, localized text, and sound.
- [ ] Missing optional assets use documented fallbacks; missing required assets block the profile before launch.
- [ ] Repeated matches and profile changes do not cause unbounded asset-memory growth.

---

## Phase 9 — Offline enforcement, saves, and crash recovery

- [ ] Before each modded launch, discover every Steam `userdata/*/306020` folder and create a timestamped, integrity-checked backup of relevant local data, remote files, and Steam metadata.
- [ ] Default to retaining ten backups while allowing users to configure retention.
- [ ] Implement preview and explicit restoration; never restore a backup automatically.
- [ ] Install fail-closed guards over the game's WinINet, Winsock, Steam matchmaking, and leaderboard paths before loading mods.
- [ ] Deny modded launch if any mandatory network guard cannot be installed.
- [ ] Document that the external Steam client may still synchronize shared saves and recommend disabling Steam Cloud while using progression-changing mods.
- [ ] Record launch states and the last installed/enabled mod in a crash-safe journal.
- [ ] If the game exits before the ready state, automatically disable the most recently enabled mod, falling back to the newest installed mod, and show the preserved diagnostics on the next manager start.
- [ ] Provide a one-click vanilla launch and a manual all-mods-disabled recovery option.

### Phase 9 implementation gate

- [ ] Backup and restore round-trip tests preserve timestamps, contents, metadata, and integrity across realistic save fixtures.
- [ ] Network tests verify blocked outbound game connections and fail-closed behavior when a guard is unavailable.
- [ ] Forced startup crashes disable the expected mod while retaining its package, configuration, and logs.
- [ ] Vanilla launch remains functional after modded crashes and after loader uninstall.

---

## Phase 10 — Hardening, documentation, and v1 release

- [ ] Fuzz package parsing, manifest validation, Lua/C++ boundaries, event payloads, and asset decoders.
- [ ] Run long-session tests with multiple event-heavy and content-heavy mods.
- [ ] Measure startup time, callback cost, memory growth, and asset-loading pauses; define and meet release budgets.
- [ ] Test clean install, upgrade, repair, rollback, and uninstall on a fresh Windows account.
- [ ] Test unsupported builds, missing Steam data, read-only folders, antivirus quarantine, partial installs, corrupted packages, and interrupted backups.
- [ ] Publish the Lua API reference, tower schema, package specification, tutorials, troubleshooting guide, recovery guide, and compatibility policy.
- [ ] Include at least one minimal event mod and one complete custom-tower mod as source examples.
- [ ] Produce checksummed release artifacts without any Ninja Kiwi or user-owned files.

### Phase 10 implementation gate

- [ ] All automated suites and the documented manual compatibility matrix pass from a clean checkout.
- [ ] A new mod author can build both sample mods using only published documentation and SDK files.
- [ ] A new player can install, use, recover, and uninstall the loader without modifying game files manually.
- [ ] Release artifacts contain no game binaries, proprietary assets, saves, credentials, account identifiers, or local paths.
- [ ] Tag v1 only after every earlier phase gate remains checked on the release candidate.
