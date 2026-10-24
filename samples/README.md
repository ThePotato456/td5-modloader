# Sample mod catalog

[Documentation home](../docs/README.md) · [First mod tutorial](../docs/mod-authoring.md)

Ten source examples demonstrate the implemented public Lua API. Start with
Hello World, then choose one focused example. Lifecycle Sample is the acceptance
fixture; API Debug Console is the broader diagnostic. Read each sample's README
before enabling its gameplay-changing options.

| Learn | Source | Staged archive | Default effect |
| --- | --- | --- | --- |
| Lifecycle, logging, frame timers | [Hello World](hello-world-mod/README.md) | `hello-world.btd5mod` | Logs only |
| Events, counters, IDs, unsubscription, storage | [Event Monitor](event-monitor-mod/README.md) | `event-monitor.btd5mod` | Observes and stores counters |
| Protect a lives floor at a native write | [Lives Guardian](lives-guardian-mod/README.md) | `lives-guardian.btd5mod` | Cancels losses below 1 life |
| Round numbers, match handles, cash setter | [Round Budget](round-budget-mod/README.md) | `round-budget.btd5mod` | Logs load; mutation opt-in |
| Purchase/upgrade/sale cancellation and tower setters | [Tower Rules](tower-rules-mod/README.md) | `tower-rules.btd5mod` | Observes towers; mutation opt-in |
| Spawn/pop/leak cancellation and bloon health | [Bloon Rules](bloon-rules-mod/README.md) | `bloon-rules.btd5mod` | Mutation opt-in |
| Config, resources, localization, persistence, optional order | [Resource Notes](resource-notes-mod/README.md) | `resource-notes.btd5mod` | Logs and stores a launch counter |
| Three custom towers, weapons, paths, art, animation, sound | [Custom Towers](custom-tower-mod/README.md) | `custom-towers.btd5mod` | Adds shop content |
| All 28 events and acceptance assertions | [Lifecycle Sample](lifecycle-mod/README.md) | `lifecycle-sample.btd5mod` | Verbose diagnostic; special tests opt-in |
| Bounded event inspection and supported mutations | [API Debug Console](api-debug-mod/README.md) | `api-debug-console.btd5mod` | Compact read-only summaries; mutations opt-in |

## Visual examples

### Custom tower artwork

![Plasma Monkey firing animation from the sample's actual sprite sheet](custom-tower-mod/assets/previews/plasma-monkey-firing.gif)

The GIF previews the packaged firing frames. See [Custom Towers](custom-tower-mod/README.md#actual-artwork)
for static PNG appearances and a Markdown table of both upgrade paths.

### Compare supported changes

| Example situation | Sample | Result when its setting is enabled |
| --- | --- | --- |
| A life loss crosses the configured floor | [Lives Guardian](lives-guardian-mod/README.md#life-loss-decision) | Reject the complete pending lives write |
| A round starts below or above the cash target | [Round Budget](round-budget-mod/README.md#synchronous-cash-update) | Set the absolute cash target |
| A tower purchase, upgrade, or sale is attempted | [Tower Rules](tower-rules-mod/README.md#action-rules) | Reject the first eligible attempt of each selected action per match |
| A bloon spawns, receives a lethal hit, or leaks | [Bloon Rules](bloon-rules-mod/README.md#bounded-effects) | Apply only the selected, globally bounded operation |

The sample guides use Mermaid flow/sequence diagrams, Markdown tables, and
selectable example logs. Their charts explain the contract; live acceptance
results remain separate in the validation archive.

## Build and run

1. With the loader toolchain installed, run from the repository root:

   ```powershell
   .\scripts\build.ps1 -Configuration Release
   .\scripts\stage.ps1 -Configuration Release
   .\scripts\check-docs.ps1
   ```

2. Find the ten archives under `out/stage/release/samples/`. Release bundles
   already contain their packaged copies; players do not need to build them.
3. Run the validator on an archive before importing it:

   ```powershell
   .\out\stage\release\btd5loader_validate.exe .\out\stage\release\samples\round-budget.btd5mod
   ```

4. Close BTD5, import the archive, enable it in a dedicated test profile, and
   launch offline from the manager. Follow the sample's run steps and inspect
   the runtime log in Options.
5. Change one sample setting at a time and relaunch. Use a dedicated profile
   for each example; overlapping cancellation samples can affect each other's
   expected results.

## Adapt an example

Copy its source folder, change the manifest ID/name/author, and keep API 1 and
the supported build declaration. Increment the version for subsequent imports.
Package the folder's contents with `mod.json` at the archive root, following
the [authoring tutorial](../docs/mod-authoring.md).

Manifest capabilities describe each example's intent. Dependencies order and
require other packages; they do not share Lua states or import scripts. Resource
Notes shows a conditional `after` rule; the [package reference](../docs/mod-packages.md)
also demonstrates required dependency versions.

## Checking a sample

Every sample is included in staging and release archive checks. Standalone
validation proves package intake and script registration, not a complete match.
The focused sample fixture tests exercise relevant callbacks without the game.
Follow each sample's run steps in the supported game to check its gameplay
behavior; see [building](../docs/building.md#live-game-acceptance) for the
optional game-test tools.
