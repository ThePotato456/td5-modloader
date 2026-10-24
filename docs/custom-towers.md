# Custom towers, weapons, art, and sounds

[Documentation home](README.md) · [API signatures](../lua-api/reference.md) ·
[Complete source example](../samples/custom-tower-mod/lua/main.lua)

Custom towers are implemented in API 1. You define Lua tables; the runtime
generates the game's tower data and serves it without patching `BTD5.jet`.
Start with a base tower, then add one feature at a time. The Custom Towers
sample includes three towers and demonstrates every section below.

| Firing frames | Base appearance | Path 1, tier 4 | Path 2, tier 4 |
| --- | --- | --- | --- |
| ![Loop of the actual Plasma Monkey firing frames](../samples/custom-tower-mod/assets/previews/plasma-monkey-firing.gif) | ![Base Plasma Monkey sprite](../samples/custom-tower-mod/assets/previews/plasma-monkey.png) | ![Plasma Storm sprite](../samples/custom-tower-mod/assets/previews/plasma-storm.png) | ![Plasma Cannon sprite](../samples/custom-tower-mod/assets/previews/plasma-cannon.png) |

The PNGs and GIF use the sample's actual atlas cells at twice their native
display size. The GIF loops the three firing frames; in game the animation is
triggered by firing. These are artwork previews, not gameplay captures.

## 1. Register a base variant

Call registration at script top level, **before** `on_load`:

```lua
btd5.towers.register{
    id = "practice-dart",
    base = "DartMonkey",
    name = "Practice Dart Monkey",
    description = "A Dart Monkey variant for learning the API.",
    cost = 250,
    stats = { range = 150, cooldown = 0.5, pierce = 3, camo = true },
}
```

For manifest ID `yourname.first-mod`, its public type is
`yourname.first-mod:practice-dart`. `tower:type()` returns that full ID. Keep
both IDs stable across releases to preserve saved content identities.

Registration returns `true` or raises a contained Lua error. Invalid definitions
reject script loading; validate the archive before installing it.

## Registration fields

| Field | Default / accepted value |
| --- | --- |
| `id` | Required, starts with a lowercase letter, then lowercase letters/digits/`_`/`-`, at most 64 characters; combined public ID at most 200 bytes |
| `base` | `"DartMonkey"`; must be a known vanilla type, e.g. `IceTower`, `SuperMonkey`, `MonkeySub` |
| `cost` | Inherit base cost, or integer `0..1000000` |
| `name`, `description` | Inherit base text if omitted; each at most 200 bytes, no control characters |
| `shop` | `"last"` after vanilla towers; `"first"` puts it at the front |
| `progression` | `"open"` opens upgrades; `"xp"` uses base upgrade gates with separate custom XP |
| `unlock_rank` | `0`; integer `0..100` |
| `restricted_modes` | `"base"` follows base availability; `"always"` allows it in restricted modes; `"never"` excludes it there |
| `stats` | Optional supported overrides below |
| `weapons` | Optional list of 1–8 weapons replacing the base defaults; requires both custom upgrade paths |
| `upgrades` | Up to two paths; each replacement has exactly four tiers; `{}` keeps that base path |
| `textures` | List of packaged sheet names, without extensions |
| `icon`, `avatar` | Sprite cell names; omitted fields inherit base art |
| `sprite` | Packaged cell name or animation table; omitted inherits base sprite |
| `on_place`, `on_upgrade`, `on_sell` | Optional `function(tower, event)` callbacks |

The profile-wide identity registry has a hard ceiling of 8,191 custom towers.
This is an encoding limit, not a performance guarantee. The
[native identity reference](native-internals.md#custom-tower-identities)
explains the encoding. Callback registrations still obey each mod's 256-handler
limit.

### Stats

These are registration-time absolute values, not methods on live towers.
Weapon overrides target the first default weapon and its supported projectile
tasks; they are not a general rewrite of every attack a complex base uses.

| Key | Type and range | Meaning |
| --- | --- | --- |
| `range` | Number `1..2000` | Weapon targeting range |
| `cooldown` | Number `0.01..60` | Seconds between shots |
| `pierce` | Integer `1..1000` | Projectile persist count |
| `damage` | Integer `0..1000` | Projectile damage amount |
| `projectile_speed` | Number `1..10000` | Projectile movement speed |
| `footprint` | Number `1..200` | Placement radius |
| `camo` | Boolean | Can target camo |
| `targeting` | `"First"`, `"Last"`, `"Strong"`, `"Close"` | Initial targeting mode |
| `placement` | `"land"`, `"water"`, `"both"` | Allowed terrain |

Numbers must be finite. Unsupported `stats` keys are rejected. Base-specific
weapon structure can limit an override; use a simple base and inspect the
generated behavior before expanding to more complex towers.

## 2. Define weapons and upgrades

Weapons use the game's case-sensitive `.weapon` vocabulary. The public lower
case `stats` and upgrade shortcuts are distinct from these native field names.
A minimal projectile weapon is:

```lua
local function dart_weapon(range, cooldown)
    return {
        TargetRange = range,
        CooldownTime = cooldown,
        Tasks = {
            {
                Type = "Projectile",
                GraphicName = "dart_monkey_dart",
                NumPersists = 3,
                TerminateOnZeroPersists = true,
                CollisionType = "Once",
                Movement = { Type = "Forward", Speed = 900, CutOffDistance = range + 60 },
                TasksToProcessOnCollision = { 0 },
                Tasks = { { Type = "Damage", DamageType = "Piercing", Amount = 1 } },
            },
        },
    }
end
```

Each weapon requires positive `TargetRange` and `CooldownTime` and a nonempty
`Tasks` list. `Type` defaults to `DartMonkey`, and `FireDelayTime` to `0`.
Lua lists are 1-based; native selector values such as
`TasksToProcessOnCollision` and `SlotIndices` use the game's zero-based indices.
For example `{ 0 }` selects the first collision subtask, not a Lua table index.
Damage types are `Piercing`, `Fire`, `Explosive`, and `Plasma`; collision types
are `Once`, `Continual`, and `None`.

Supported task types are `Projectile`, `MultiFire`, `Damage`, `StatusEffect`,
`RemoveStatusEffect`, `AreaOfEffect`, `RandomFire`, `TimerFire`, `Effect`,
`TextEffect`, `BloonSpawnedEvent`, `RayIntersect`, `ChainTasks`, `Collectable`,
`LaunchAircraft`, `TowerModifier`, `Harpoon`, `Sacrifice`, `AddChildSprite`,
`ChangeTerrain`, `CreateTower`, `ResetPopCount`, `EnableNextWeaponSlot`,
`EnableWeaponSlot`, `ResetCooldown`, `RedeployTower`, `ChangeSubtaskEnabled`,
`CollectCollectables`, `RestoreAmmo`, `FireTaskAtLocation`, `OverclockTask`,
`RBEProjectile`, `TrapBloon`, and `DamageSpread`.

Movement types are `Forward`, `RotateAroundTower`, `GoToTarget`, `Target`,
`MotionCurve`, `ReturnToSender`, `MoveToTouch`, `BeeMovement`, and
`BeeSwarmMovement`. Recognizing a type does not guarantee every arbitrary
combination is meaningful. Follow a known working structure and verify it in
game. Each weapon is bounded to 64 KiB of serialized JSON and nesting depth 16.

### Upgrade path contract

Every replaced path needs exactly **four** tiers. Omit paths to inherit the
base's upgrades, or use `{}` for one inherited path. If you replace `weapons`,
provide both paths: vanilla upgrades may reference weapon tasks that no longer
exist. Tier names must be unique within the tower.

```lua
local function path(prefix)
    return {
        { name = prefix .. " 1", cost = 100, range = 10 },
        { name = prefix .. " 2", cost = 200, pierce = 1 },
        { name = prefix .. " 3", cost = 400, cooldown = -0.05 },
        { name = prefix .. " 4", cost = 800, camo = true },
    }
end

btd5.towers.register{
    id = "training-dart", base = "DartMonkey", cost = 300,
    weapons = { dart_weapon(150, 0.5) },
    upgrades = { path("Focus"), path("Practice") },
}
```

| Tier field | Meaning |
| --- | --- |
| `name`, `cost` | Required; name at most 64 bytes, cost integer `0..1000000` |
| `description` | Optional text, at most 200 bytes |
| `range` | Additive delta `-2000..2000` to target range |
| `cooldown` | Additive delta `-60..60` seconds; negative makes firing faster |
| `pierce` | Additive integer delta `-1000..1000` |
| `camo` | Boolean replacement for camo targeting |
| `weapon` | Complete replacement of the first weapon slot |
| `tower_upgrade`, `weapon_upgrade` | Advanced native upgrade tables, using the game's field vocabulary |
| `icon`, `avatar`, `sprite` | Art replacement; sprite can be animated |

Keep resulting ranges, pierce, and cooldowns meaningful after upgrades and
crosspaths. Native upgrade tables are advanced escape hatches with structural
checks, not a promise that every possible operation has live acceptance.

## 3. Add callbacks

```lua
-- Add these fields to a registration table:
on_place = function(tower, event)
    btd5.log("info", "Placed " .. tostring(tower:type()))
end,
on_upgrade = function(tower, event)
    local left, right = tower:upgrade_tiers()
    btd5.log("info", "Upgrade tiers " .. left .. "," .. right)
end,
on_sell = function(tower, event)
    -- This is tower.selling, before sale side effects.
    -- Set event.cancelled = true here to reject the sale when needed.
    btd5.log("info", "Sale requested for " .. tostring(tower:type()))
end,
```

These are filtered subscriptions to `tower.placed`, `tower.upgraded`, and
`tower.selling`. They receive a live tower and the normal event table, consume
the same handler budgets, and count toward the 256-handler limit. `on_sell`
means a sale attempt, not a completed sale. For confirmation use `tower.sold`.
Never assume the wrapper remains valid after a later timer or scene transition.

## 4. Add textures and animation

1. Create an original PNG sheet and its matching XML atlas under
   `assets/textures/`, for example `yourname_practice.png` and
   `yourname_practice.xml`.
2. Use the same sheet name in the atlas:

   ```xml
   <FrameInformation name="yourname_practice">
     <Cell name="yourname_practice_body" x="0" y="0" w="64" h="64"/>
   </FrameInformation>
   ```

3. Register `textures = { "yourname_practice" }` and
   `sprite = "yourname_practice_body"`. `icon` and `avatar` also name cells.
4. For firing animation, provide
   `sprite = { frames = { "rest_cell", "fire_cell" }, length = 0.2 }`, using
   cells from your packaged sheets. The first frame is shown at rest.
5. If needed, add a complete lower-quality PNG/XML pair under
   `assets/textures/Tablet/`. Without the pair the Ultra files are used.
6. Validate with `--game`, check warnings, and inspect both quality settings,
   placement, selection, firing, and each upgrade in the game.

Sheet/cell names use letters, digits, `_`, and `-`, at most 64 characters.
Sheets use the game's Ultra sizing convention. A PNG is limited to 16 MiB;
the atlas must name the sheet and contain cells. Animation accepts 1–16 frames,
length `0.01..10` seconds (default `0.2`). Prefix sheet and cell names to avoid
collisions: sheet names are shared by mods even though tower IDs are namespaced.

`GraphicName` may reference vanilla or packaged cells. `SpriteFile` references
existing game `WeaponSprites` files. The current pipeline does not define a
general asset format, memory-budget estimator, pivot editor, or arbitrary effect
and audio import system. See the [sample's real atlas](../samples/custom-tower-mod/assets/textures/plasma_monkey.xml)
for a complete file rather than relying on the minimal shape above.

## 5. Register and play sounds

```lua
-- Script top level; files must already exist in the package.
btd5.sounds.register{
    id = "place", files = { "assets/sounds/place.wav" }, cooldown = 0.2,
}

-- During a match, for example inside on_place:
local played, reason = btd5.sounds.play("place")
if not played then
    btd5.log("info", "Sound unavailable: " .. tostring(reason))
end
```

An effect has 1–8 `.wav` files under `assets/`, each at most 8 MiB, and cooldown
`0..10` seconds (default `0.1`). Several files provide randomly selected
variants. Mono 16-bit PCM at 22,050 Hz matches the supplied sample and game's
effects; registration checks paths and file sizes, not every decoder property.

Registration returns `true`. `play` returns `true` when dispatched or `false,
reason` when unavailable, unknown, or not ready. It uses the game's in-match
sound manager; do not depend on playback in the menu. Local IDs are expanded
with the calling mod's ID; a full `modId:soundId` is also accepted.

## Verify a finished tower

Validate the final archive and art catalog, import it into a dedicated offline
profile, and test shop name/icon, cost, placement, attack, targeting, both paths
and crosspaths, animation, sound, sale, and a second match. Test rank/restricted
mode rules if used. Save and resume with the same mod enabled, then confirm the
missing-content behavior with your own backed-up test save. Keep live results
separate from standalone validation results.

Custom XP and save IDs are described in [compatibility](compatibility.md#saves-and-mod-identities).
Removing a mod is not a general undo of its gameplay effects.
