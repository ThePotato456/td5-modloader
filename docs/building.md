# Building BTD5 Mod Loader

## Requirements

- Windows 10 or later.
- Visual Studio Build Tools with the MSVC x86/x64 workload and bundled CMake.
- The project-local .NET 10.0.400 SDK installed by `scripts/install-dotnet.ps1`,
  or the exact SDK available on `PATH`.
- Git and network access for the first dependency restore.

The game is not required for Phase 1 builds or tests. Never copy game files into
the repository. For later opt-in integration tests, copy `config/local.example.json`
to the ignored `config/local.json` and adjust the path.

## Commands

```powershell
./scripts/install-dotnet.ps1
./scripts/build.ps1 -Configuration Debug
./scripts/test.ps1 -Configuration Debug
./scripts/analyze.ps1
./scripts/check-format.ps1
```

Release builds use `-Configuration Release`. Native output is always Win32/x86
because it loads into the 32-bit game; the WPF manager is architecture-neutral.

After a successful build, assemble a runnable manager bundle and the lifecycle
sample package:

```powershell
./scripts/stage.ps1 -Configuration Debug
```

The result is written to `out/stage/debug`. The manager, proxy, runtime, symbol
maps, and sample package are colocated there so installation does not require
manually collecting build outputs.

## Fresh-state manager acceptance

The final manager workflow can be exercised without touching normal manager
state or the registered Steam installation:

```powershell
./scripts/start-manager-acceptance.ps1 -Configuration Debug
```

The script reads the ignored `config/local.json` by default, creates a
timestamped copy under `.local/manager-acceptance`, excludes existing loader
artifacts, uses an isolated state directory, and starts the staged manager with
automatic Steam discovery disabled. Choose the printed game-copy path in
Options and drag a package from the printed samples directory.

After the modded launch succeeds, close the game and manager and run the printed
verification command. Acceptance copies and state remain ignored and can be
removed manually after review.

## Read-only game compatibility check

After building, inspect a legally obtained installation without launching or
modifying it:

```powershell
.\scripts\inspect-symbols.ps1 -GameDirectory 'C:\Program Files (x86)\Steam\steamapps\common\BloonsTD5'
```
