---@meta

---Documentation-only Lua Language Server definitions for BTD5 Mod Loader API v1.
---Do not include this file in a .btd5mod package or execute it at runtime.

---@alias BTD5LogLevel "info"|"error"|string
---@alias BTD5ObjectKind "tower"|"bloon"
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

---@class BTD5GameObject
local GameObject = {}

---@return boolean
function GameObject:is_valid() end

---@return integer
function GameObject:id() end

---@return BTD5ObjectKind
function GameObject:kind() end

---@class BTD5Event
---@field name BTD5EventName
---@field cancelled boolean

---@class BTD5LivesEvent: BTD5Event
---Read-only value captured immediately before the pending native write.
---@field old_lives integer
---Mutable on lives.changing only; accepted range is 0..2147483647.
---@field new_lives integer

---@class BTD5TowerEvent: BTD5Event
---@field tower BTD5GameObject

---@class BTD5BloonEvent: BTD5Event
---@field bloon BTD5GameObject

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

---@class BTD5Api
---@field log fun(level: BTD5LogLevel, message: string)
---@field config BTD5ConfigApi
---@field storage BTD5StorageApi
---@field localization BTD5LocalizationApi
---@field resource BTD5ResourceApi
---@field timer BTD5TimerApi
---@field events BTD5EventsApi

---@type BTD5Api
btd5 = {}
