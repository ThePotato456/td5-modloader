---@meta

---Documentation-only Lua Language Server definitions for BTD5 Mod Loader API v1.
---Do not include this file in a .btd5mod package or execute it at runtime.

---@alias BTD5LogLevel "info"|"error"|string
---@alias BTD5ObjectKind "match"|"round"|"player"|"tower"|"attack"|"projectile"|"bloon"
---@alias BTD5EventName
---| "match.starting"
---| "match.started"
---| "match.ending"
---| "match.ended"
---| "round.starting"
---| "round.started"
---| "round.ending"
---| "round.ended"
---| "cash.changing"
---| "cash.changed"
---| "lives.changing"
---| "lives.changed"
---| "tower.placing"
---| "tower.placed"
---| "tower.upgrading"
---| "tower.upgraded"
---| "tower.selling"
---| "tower.sold"
---| "bloon.spawning"
---| "bloon.spawned"
---| "bloon.popping"
---| "bloon.popped"
---| "bloon.leaking"
---| "bloon.leaked"
---| "attack.created"
---| "attack.destroyed"
---| "projectile.created"
---| "projectile.destroyed"

---@class BTD5GameObject
local GameObject = {}

---@return boolean
function GameObject:is_valid() end

---@return integer
function GameObject:id() end

---@return BTD5ObjectKind
function GameObject:kind() end

---@class BTD5Tower: BTD5GameObject
local Tower = {}

---@return integer
function Tower:pop_count() end

---@param value integer # Accepted range is 0..2147483647.
---@return boolean
function Tower:set_pop_count(value) end

---@return integer
function Tower:sell_price() end

---@param value integer # Accepted range is 0..2147483647.
---@return boolean
function Tower:set_sell_price(value) end

---Vanilla type name or custom modId:towerId; nil when unnamed.
---@return string|nil
function Tower:type() end

---Left and right upgrade path tiers, each 0..4.
---@return integer left
---@return integer right
function Tower:upgrade_tiers() end

---@class BTD5Bloon: BTD5GameObject
local Bloon = {}

---@return number
function Bloon:health() end

---@param value number # Must be finite and nonnegative.
---@return boolean
function Bloon:set_health(value) end

---Bloon type name such as "Red" or "MOAB"; nil when unnamed.
---@return string|nil
function Bloon:type() end

---@class BTD5Round: BTD5GameObject
local Round = {}

---Round number being played; 1 for the first standard round.
---@return integer
function Round:number() end

---@class BTD5Match: BTD5GameObject
local Match = {}

---Sets cash through the native helpers; cash events run before it returns.
---@param value number # Finite, 0..2147483647.
---@return boolean
function Match:set_cash(value) end

---@class BTD5Event
---@field name BTD5EventName
---@field cancelled boolean

---@class BTD5LivesEvent: BTD5Event
---@field player BTD5GameObject
---Read-only value captured immediately before the pending native write.
---@field old_lives integer
---Mutable on lives.changing only; accepted range is 0..2147483647.
---@field new_lives integer

---@class BTD5MatchEvent: BTD5Event
---@field match BTD5Match

---@class BTD5RoundEvent: BTD5Event
---@field round BTD5Round

---Delivered by cash.changing and cash.changed.
---@class BTD5CashEvent: BTD5Event
---Read-only balance the game has just committed.
---@field cash number

---@class BTD5TowerEvent: BTD5Event
---@field tower BTD5Tower
---tower.placing only: true at the player's cancellable purchase decision.
---@field purchase boolean?

---@class BTD5BloonEvent: BTD5Event
---@field bloon BTD5Bloon
---bloon.spawning only: true for a spawn from the round's bloon queue, the only
---cancellable kind.
---@field from_round boolean?

---Delivered by attack.created and attack.destroyed.
---@class BTD5AttackEvent: BTD5Event
---@field attack BTD5GameObject

---Delivered by projectile.created and projectile.destroyed.
---@class BTD5ProjectileEvent: BTD5Event
---@field projectile BTD5GameObject

---@class BTD5ConfigApi
---@field get fun(key: string): string?

---@class BTD5StorageApi
---@field get fun(key: string): string?
---@field set fun(key: string, value: string)

---@class BTD5LocalizationApi
---@field get fun(key: string): string

---@class BTD5ResourceApi
---@field read_text fun(path: string): string

---@class BTD5TimerApi
---@field after fun(ticks: integer, callback: fun())

---@class BTD5EventsApi
---@field on fun(name: BTD5EventName, callback: fun(event: BTD5Event)): integer
---@field off fun(token: integer): boolean

---@class BTD5TowerStats
---@field range? number # Absolute 1..2000.
---@field cooldown? number # Absolute seconds, 0.01..60.
---@field pierce? integer # Absolute 1..1000.
---@field damage? integer # 0..1000.
---@field projectile_speed? number # 1..10000.
---@field footprint? number # 1..200.
---@field camo? boolean
---@field targeting? "First"|"Last"|"Strong"|"Close"
---@field placement? "land"|"water"|"both"

---@class BTD5Animation
---@field frames string[] # 1..16 packaged cells; first is the resting frame.
---@field length? number # 0.01..10 seconds; default 0.2.

---Native .weapon vocabulary. Nested tasks use case-sensitive game fields.
---@class BTD5Weapon
---@field TargetRange number
---@field CooldownTime number
---@field Type? string
---@field FireDelayTime? number
---@field Tasks table[]

---@class BTD5Upgrade
---@field name string
---@field cost integer # 0..1000000.
---@field description? string
---@field range? number # Additive delta, -2000..2000.
---@field cooldown? number # Additive seconds, -60..60.
---@field pierce? integer # Additive delta, -1000..1000.
---@field camo? boolean
---@field weapon? BTD5Weapon # Replace native weapon slot 0.
---@field tower_upgrade? table
---@field weapon_upgrade? table
---@field icon? string
---@field avatar? string
---@field sprite? string|BTD5Animation

---@class BTD5TowerDefinition
---@field id string # Local ID; public type becomes modId:id.
---@field base? string # Default DartMonkey; known vanilla type.
---@field cost? integer # 0..1000000; default inherits base.
---@field name? string # Maximum 200 bytes.
---@field description? string # Maximum 200 bytes.
---@field shop? "first"|"last"
---@field progression? "open"|"xp"
---@field restricted_modes? "base"|"always"|"never"
---@field unlock_rank? integer # 0..100.
---@field stats? BTD5TowerStats
---@field weapons? BTD5Weapon[] # Own weapons require both custom upgrade paths.
---@field upgrades? BTD5Upgrade[][] # Up to two paths, four tiers per replaced path.
---@field textures? string[] # Sheet names under assets/textures/, no extension.
---@field icon? string
---@field avatar? string
---@field sprite? string|BTD5Animation
---@field on_place? fun(tower: BTD5Tower, event: BTD5TowerEvent)
---@field on_upgrade? fun(tower: BTD5Tower, event: BTD5TowerEvent)
---@field on_sell? fun(tower: BTD5Tower, event: BTD5TowerEvent) # tower.selling; may cancel.

---@class BTD5TowersApi
---Register only while the entry script loads, before on_load.
---@field register fun(definition: BTD5TowerDefinition): boolean

---@class BTD5SoundDefinition
---@field id string
---@field files string[] # 1..8 WAV paths under assets/, each at most 8 MiB.
---@field cooldown? number # Seconds, 0..10; default 0.1.

---@class BTD5SoundsApi
---Register at script load; playback needs a ready in-match sound manager.
---@field register fun(definition: BTD5SoundDefinition): boolean
---@field play fun(id: string): boolean, string?

---@class BTD5Api
---@field log fun(level: BTD5LogLevel, message: string)
---@field config BTD5ConfigApi
---@field storage BTD5StorageApi
---@field localization BTD5LocalizationApi
---@field resource BTD5ResourceApi
---@field timer BTD5TimerApi
---@field events BTD5EventsApi
---@field towers BTD5TowersApi
---@field sounds BTD5SoundsApi

---@type BTD5Api
btd5 = {}
