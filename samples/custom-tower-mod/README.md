# Custom Towers

[All samples](../README.md) · [Complete tower API guide](../../docs/custom-towers.md)

A custom-tower example. At load it registers three towers with
`btd5.towers.register`, each built from a vanilla base tower:

## Actual artwork

| Base Plasma Monkey | Plasma Storm: path 1, tier 4 | Plasma Cannon: path 2, tier 4 |
| --- | --- | --- |
| ![Base Plasma Monkey](assets/previews/plasma-monkey.png) | ![Plasma Storm](assets/previews/plasma-storm.png) | ![Plasma Cannon](assets/previews/plasma-cannon.png) |

![Loop of the three actual Plasma Monkey firing frames](assets/previews/plasma-monkey-firing.gif)

The GIF loops the packaged firing frames for preview. In game, the animation
runs when the tower fires. Images are enlarged with nearest-neighbor scaling;
these are original sample assets rather than gameplay screenshots.

## Plasma Monkey upgrades

| Tier | Path 1 | Base cost | Path 2 | Base cost |
| --- | --- | ---: | --- | ---: |
| 1 | Focused Coils | 150 | Targeting Visor | 200 |
| 2 | Hot Plasma | 300 | Wide Lens | 250 |
| 3 | Rapid Discharge | 700 | Overcharge | 900 |
| 4 | Plasma Storm | 3500 | Plasma Cannon | 4000 |

| ID | Base | Cost | Shop name | What it shows |
| --- | --- | --- | --- | --- |
| `example.custom-towers:plasma-monkey` | `DartMonkey` | 350 | Plasma Monkey | Its own art, weapon, two upgrade paths, targeting and callbacks; leads the shop |
| `example.custom-towers:sharp-dart` | `DartMonkey` | 150 | Sharp Dart Monkey | Base upgrades unlocked with the tower's own XP (`progression = "xp"`) |
| `example.custom-towers:frost-cannon` | `IceTower` | 400 | Frost Cannon | Base upgrades, all open (the default) |

## Run it

1. Import `custom-towers.btd5mod`, enable it in a dedicated offline profile, and
   launch from the manager.
2. Enter a fresh match and find Plasma Monkey at the front of the shop. Buy it
   when you have enough cash; inspect the custom icon, avatar, and body.
3. Start a round and watch firing animation and splash attacks. Its placement
   callback requests the packaged plasma sound; inspect the log if it is unavailable.
4. Buy upgrades on either path, inspect tier art and callback logs, then sell it.
   Use another tower for the other tier-4 path under the game's crosspath rules.
5. Enter another match and repeat. Sharp Dart demonstrates separate custom XP;
   Frost Cannon inherits base art/attacks and opens upgrades by default.

Source: [manifest](mod.json), [Lua](lua/main.lua),
[atlas](assets/textures/plasma_monkey.xml). Validate the final archive with
`--game` to check vanilla art names as well as packaged assets.

## Defining a tower

```lua
btd5.towers.register{
    id = "plasma-monkey",            -- becomes "<mod id>:plasma-monkey"
    base = "DartMonkey",             -- art, icon, button and anything not overridden
    cost = 350, name = "...", description = "...",
    shop = "first",                  -- or "last" (default)
    progression = "open",            -- or "xp"
    restricted_modes = "base",       -- or "always" / "never"
    unlock_rank = 0,                 -- player rank that unlocks it in the shop
    textures = { "plasma_monkey" },  -- assets/textures/plasma_monkey.png + .xml
    icon = "plasma_monkey_icon",     -- shop button
    avatar = "plasma_monkey_avatar", -- selected-tower picture
    sprite = "plasma_monkey_body",   -- the tower in the match, or
    -- sprite = { frames = { "rest", "fire_1", "fire_2" }, length = 0.2 } to animate while firing
    stats = { range = 150, cooldown = 0.6, pierce = 3, damage = 2, projectile_speed = 900,
              footprint = 9, camo = false, targeting = "Strong", placement = "land" },
    weapons = { { TargetRange = 150, CooldownTime = 0.6, Tasks = { ... } } },
    upgrades = { { tier1, tier2, tier3, tier4 }, { tier1, tier2, tier3, tier4 } },
    on_place = function(tower, event) end,
    on_upgrade = function(tower, event) end,
    on_sell = function(tower, event) end,
}
```

- `stats` changes the base tower: `range`, `cooldown`, `pierce`, `damage` and
  `projectile_speed` apply to its first weapon; `footprint` (placement radius),
  `camo`, `targeting` (`First`, `Last`, `Strong`, `Close`) and `placement`
  (`land`, `water`, `both`) to the tower.
- `weapons` replaces the base tower's weapons. Each is a `.weapon` definition
  in the game's own vocabulary: `TargetRange`, `CooldownTime`, optional `Type`
  (default `DartMonkey`) and a `Tasks` tree of the game's task types
  (`Projectile`, `Damage`, `AreaOfEffect`, `StatusEffect`, `MultiFire`, ...)
  with damage types `Piercing`, `Fire`, `Explosive` and `Plasma`. Child attacks
  nest through a task's own `Tasks`. A tower with its own weapons must define
  both upgrade paths.
- `upgrades` replaces either path (use `{}` to keep the base tower's). A path
  has exactly four tiers: `{ name = "Range", cost = 100, range = 10 }` is a
  tier example; range, cooldown, and pierce are additive deltas, and `weapon`
  swaps the first weapon slot (native index 0)
  for that weapon. `tower_upgrade` and `weapon_upgrade` pass the game's own
  `TowerUpgrade` / `WeaponUpgrade` entries through.
- `textures` lists sprite sheets the package ships in `assets/textures/`: a PNG
  and its cell atlas in the game's format (`<FrameInformation name="<sheet>">`
  with `<Cell name=... x= y= w= h=/>` entries), drawn at the game's Ultra
  size. Optional `assets/textures/Tablet/<sheet>.*` copies are used at the
  lower texture quality. `icon`, `avatar` and `sprite` name cells (icons may
  also name vanilla cells); upgrade tiers take `icon`, `avatar` and `sprite`
  too, and a tier's sprite shows once that tier is bought (the highest tier
  wins). `sprite` can also be `{ frames = {...}, length = seconds }`: the first
  frame shows at rest and the others in turn each time the tower fires. Sheet
  names are shared by every mod, so prefix them with your own.
- Art names are checked when the tower registers: `GraphicName` in weapons and
  `icon`/`avatar` must be a cell of the game's atlases or the mod's sheets, and
  `SpriteFile` must be one of the game's `WeaponSprites` files.
- `on_place`, `on_upgrade` and `on_sell` run for this tower only, as event
  handlers with the same sandbox and limits as `btd5.events.on`.

## Sounds

```lua
btd5.sounds.register{ id = "plasma-zap", files = { "assets/sounds/plasma_zap.wav" }, cooldown = 0.2 }
-- later, during a match (for example in on_place):
local played, reason = btd5.sounds.play("plasma-zap")
```

Sounds register while the script loads, from WAV files under the package's
`assets/` folder (the game's own are mono 16-bit 22,050 Hz); with several files
the game picks one at random. They join the game's in-match effect list, so
`btd5.sounds.play` works during a match and returns `false` and a reason
otherwise.

Invalid definitions stop the mod's script with an error naming the field (for
example `upgrade path 1 needs exactly 4 tiers` or `weapon 1 task 1 has unknown
Type 'Laser'`), before any menu or match.

## How it works

Custom towers follow the vanilla towers in the shop (`shop = "first"` puts one
at the front) and keep whatever they do not override from their base tower. The
loader generates their game files and text at run time under
`btd5loader_generated/` in the game directory; nothing in `Assets` changes.
Upgrades are open by default. A tower registered with `progression = "xp"`
keeps its base tower's upgrade gates and earns its own XP, which the loader
keeps under its data root in `storage/custom-towers/progress.json`, never in
the game's `Profile.save`; the loader installs its XP hooks only when some
tower asks for this.
In modes that limit the shop's towers, a custom tower is available when its
base tower is; `restricted_modes = "always"` keeps it available everywhere and
`"never"` leaves it out of every such mode.
Placing one logs its type, which is the tower's public ID.

Current limits: projectile graphics name existing cells and sprite files
(mod sheets work for `GraphicName`); mod sounds play only during a match and
only when a mod calls `btd5.sounds.play`; and only English-style plural and
capitalised names are derived from `name`.
