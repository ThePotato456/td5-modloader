# Phase 5 manager and live Lua validation

Date: 2026-08-28

The Release build, native tests, managed integration tests, MSVC analysis,
managed analyzers, formatting checks, bundle staging, and WPF startup smoke test
all passed before the local game acceptance run.

## Local copied-game acceptance

The test used only the ignored `.local/game` copy. Existing loader artifacts
were moved into an ignored, recoverable backup before the manager installation
service installed the staged Release artifacts and recorded their ownership.
The game executable and asset archive were not replaced or patched.

The smoke workflow then performed the same core operations exposed by the WPF
manager:

1. validated the copied Steam Win32 4.8 installation;
2. installed the loader from the staged bundle;
3. validated and installed `lifecycle-sample.btd5mod`;
4. created the `Live Smoke` profile;
5. inherited the package's `greeting` configuration default;
6. enabled the sample after dependency and compatibility validation;
7. wrote the active-profile handoff and launched the copied executable; and
8. closed only the exact process started by the smoke harness after evidence was
   observed.

The live runtime log recorded:

- `supported_build=steam-win32-4.8`;
- all four mandatory symbols resolving;
- `Hello from Lua (launch 1)` from the sample's Lua `on_load` callback;
- `sample.lifecycle:loaded`; and
- `mods_loaded_waiting_for_game_ready_hook`.

The mod-owned storage file contained a launch counter of `1`. The game process
remained stable during the check and no test process remained afterward.

## Integrity and refusal evidence

After the live run, the copied proprietary files retained the documented hashes:

| File | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70` |
| `Assets/BTD5.jet` | `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8` |

Managed integration tests separately exercised missing-file repair, modified-file
preservation, ownership-limited uninstall, pre-existing proxy refusal, missing
dependencies, incompatible upgrades, dependency-protected disable/uninstall,
deterministic reordering, launch blocking, log status, and redacted diagnostics.

This validation does not claim game-ready events, timer advancement, rendering,
network enforcement, or save protection. Those remain later phase gates.
