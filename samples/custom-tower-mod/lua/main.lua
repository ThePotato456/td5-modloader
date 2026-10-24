-- Custom towers must be registered while this script loads, before the game
-- reads its tower list and text. Each starts from a vanilla base tower's
-- definition, art, upgrades and weapons.
btd5.towers.register{
    id = "sharp-dart",
    base = "DartMonkey",
    cost = 150,
    name = "Sharp Dart Monkey",
    description = "A pricier Dart Monkey that earns its own XP to unlock upgrades.",
    progression = "xp",
}
btd5.towers.register{
    id = "frost-cannon",
    base = "IceTower",
    cost = 400,
    name = "Frost Cannon",
    description = "An Ice Tower variant with every upgrade open from the start.",
}

-- A tower with its own weapon, upgrades and callbacks. Weapons use the game's
-- .weapon vocabulary: a Projectile carries a Damage task, and an
-- AreaOfEffect child attack splashes on each hit.
local function plasma_bolt(range, cooldown, pierce, damage)
    return {
        TargetRange = range,
        CooldownTime = cooldown,
        Tasks = {
            {
                Type = "Projectile",
                GraphicName = "dart_monkey_dart",
                NumPersists = pierce,
                TerminateOnZeroPersists = true,
                CollisionType = "Once",
                Movement = { Type = "Forward", Speed = 900, CutOffDistance = range + 60 },
                TasksToProcessOnCollision = { 0, 1 },
                Tasks = {
                    { Type = "Damage", DamageType = "Plasma", Amount = damage },
                    {
                        Type = "AreaOfEffect", Range = 30, MaxTargets = 3,
                        Tasks = { { Type = "Damage", DamageType = "Explosive", Amount = 1 } },
                    },
                },
            },
        },
    }
end

-- A sound the game can play during a match (a WAV under assets/).
btd5.sounds.register{ id = "plasma-zap", files = { "assets/sounds/plasma_zap.wav" }, cooldown = 0.2 }

local function tier(name, cost, description, fields)
    fields = fields or {}
    fields.name, fields.cost, fields.description = name, cost, description
    return fields
end

btd5.towers.register{
    id = "plasma-monkey",
    base = "DartMonkey",
    cost = 350,
    name = "Plasma Monkey",
    description = "Fires plasma bolts that splash nearby bloons.",
    shop = "first",
    stats = { targeting = "Strong", footprint = 9 },
    -- Art from assets/textures/plasma_monkey.png and its cell atlas.
    textures = { "plasma_monkey" },
    icon = "plasma_monkey_icon",
    avatar = "plasma_monkey_avatar",
    -- The body at rest, then two glowing frames while it fires.
    sprite = { frames = { "plasma_monkey_body", "plasma_monkey_fire_1", "plasma_monkey_fire_2" }, length = 0.2 },
    weapons = { plasma_bolt(150, 0.6, 3, 2) },
    upgrades = {
        {
            tier("Focused Coils", 150, "Longer range.", { range = 30 }),
            tier("Hot Plasma", 300, "Bolts pierce one more bloon.", { pierce = 1 }),
            tier("Rapid Discharge", 700, "Fires much faster.", { cooldown = -0.25 }),
            tier("Plasma Storm", 3500, "A storm of heavy bolts.", { weapon = plasma_bolt(200, 0.12, 6, 4), sprite = "plasma_monkey_storm" }),
        },
        {
            tier("Targeting Visor", 200, "Sees camo bloons.", { camo = true }),
            tier("Wide Lens", 250, "Even longer range.", { range = 40 }),
            tier("Overcharge", 900, "Bolts pierce two more bloons.", { pierce = 2 }),
            tier("Plasma Cannon", 4000, "Slow, devastating bolts.", { weapon = plasma_bolt(260, 0.9, 20, 10), sprite = "plasma_monkey_cannon" }),
        },
    },
    on_place = function(tower)
        btd5.log("info", "Custom Towers sample plasma placed")
        local played, reason = btd5.sounds.play("plasma-zap")
        btd5.log("info", "Custom Towers sample plasma zap " .. (played and "played" or tostring(reason)))
    end,
    on_upgrade = function(tower)
        local left, right = tower:upgrade_tiers()
        btd5.log("info", "Custom Towers sample plasma upgraded " .. tostring(left) .. "," .. tostring(right))
    end,
    on_sell = function(tower)
        btd5.log("info", "Custom Towers sample plasma selling")
    end,
}

btd5.events.on("tower.placed", function(event)
    -- tower:type() returns "example.custom-towers:sharp-dart" for a custom tower.
    btd5.log("info", "Custom Towers sample placed " .. tostring(event.tower:type()))
end)
