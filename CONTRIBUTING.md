# Contributing

Start at the [documentation wiki](docs/README.md),
[architecture guide](docs/architecture.md), and
[native internals reference](docs/native-internals.md). Documentation should
describe implemented behavior and identify unsupported operations clearly.

## Make a change

1. Start from an up-to-date checkout and inspect existing local changes.
2. Create a descriptive branch named `feature/feature-name`, using lowercase
   words separated by hyphens.
3. Keep the change focused. Explain the user-visible behavior and any API or
   compatibility implications.
4. Follow [upstream research and provenance](docs/upstream-code-provenance.md).
   Keep game binaries, assets, saves, account data, dumps, logs, and local paths
   outside version control.
5. Update the relevant guides and samples alongside API changes. Keep session
   notes, plans, audits, and test-run reports outside the public source tree.

## Validate proportionately

Follow [building](docs/building.md) for the exact commands. Native code is
Win32/x86. The ordinary native/managed suites do not require the game.

```powershell
.\scripts\test.ps1 -Configuration Release
.\scripts\check-format.ps1
.\scripts\stage.ps1 -Configuration Release
.\scripts\check-docs.ps1
```

For docs and samples, check relative links, manifest JSON, all staged packages,
and callback behavior relevant to new samples. The standalone validator only
loads entry scripts; it is not an in-game acceptance test. For gameplay and
hook changes, exercise the supported copied game as described in
[building](docs/building.md#live-game-acceptance).

Keep callbacks short, native hook installation transactional, invalid objects
stale, and error handling contained. Add meaningful tests for new behavior;
avoid checks that merely duplicate implementation text.

## Submit for review

Describe the problem, resulting behavior, tests run, and material limitations.
Include mod IDs/versions and game build IDs for reproducible gameplay issues.
Do not claim a live test when only fixtures or script validation ran.

Documentation-only improvements, focused Lua examples, clear bug reports, and
compatibility research with reproducible evidence are all useful contributions.
