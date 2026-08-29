# Lifecycle Sample

This publishable sample uses only the sandboxed Lua API. It logs each lifecycle
stage, persists a launch counter in mod-owned storage, reads configuration and
localization, schedules a deterministic timer, and logs all live match and
round lifecycle events plus cash, lives, and tower placed/upgraded/sold
notifications. The tower handlers demonstrate stable wrapper identity and
post-sale invalidation.
