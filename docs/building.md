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

