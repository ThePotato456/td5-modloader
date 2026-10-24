# Building BTD5 Mod Loader

## Requirements

- Windows 10 or later.
- Visual Studio Build Tools with the MSVC x86/x64 workload and bundled CMake.
- The project-local .NET 10.0.400 SDK installed by `scripts/install-dotnet.ps1`,
  or the exact SDK available on `PATH`.
- Git and network access for the first dependency restore.

The game is not required for the ordinary native/managed builds or tests. Do not
commit game files. For opt-in integration tests, copy `config/local.example.json`
to the ignored `config/local.json` and adjust the path. Development game copies
belong under ignored `.local/game/` or outside the checkout.

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

After a successful build, assemble a runnable manager bundle and all ten sample
packages:

```powershell
./scripts/stage.ps1 -Configuration Debug
```

The result is written to `out/stage/debug`. The manager, proxy, runtime, symbol
maps, validator, and sample packages are colocated there so installation does not require
manually collecting build outputs.

## Documentation and sample checks

```powershell
.\scripts\check-docs.ps1
.\scripts\check-docs.ps1 -Configuration Release -ValidatePackages
```

The first command checks current-guide relative links and heading anchors,
sample JSON, and referenced source files without a build or game. The second
command also validates every staged sample archive with the
native validator after staging. It runs entry scripts and content registration,
not lifecycle or gameplay callbacks. Native sample fixture tests exercise
focused callback behavior as part of `scripts/test.ps1`.

## Live game acceptance

For hook/gameplay changes, build, test, and stage Release, then dry-run the
selected scenario before an offline unattended run against the copied game:

```powershell
.\scripts\drive-smoke-match.ps1 -Scenario tower-actions -DryRun
.\scripts\drive-smoke-match.ps1 -Scenario tower-actions
.\scripts\run-smoke-suite.ps1
```

Put Steam in Offline Mode first. The driver controls the mouse/keyboard, backs
up changed test saves under `.local/save-backups`, and stores screenshots under
`.local/shots`. Read the script's parameters before overriding the idle check.
The harness's save backup behavior does not mean the player manager implements
general automatic backups. Keep live acceptance results separate from fixture
results and record the tested revision, game fingerprint, and scenario.

## Update documentation visuals

Edit Mermaid charts and tables directly in their Markdown pages. GitHub renders
the diagrams; a reader without Mermaid support can still follow the adjacent
steps and tables. Expected logs stay in selectable text blocks.

The custom tower's PNG/GIF previews are cropped from its real sprite atlas.
To regenerate them, use Python 3.10 or later with Pillow available:

```powershell
python scripts/render-doc-assets.py
python scripts/render-doc-assets.py --check
.\scripts\check-docs.ps1
```

The normal docs check requires no Python and verifies local image/link targets.
See [visual asset notes](assets/README.md) for provenance and review guidance.

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
