# Game symbol maps

Build-specific symbol maps use `schemas/symbol-map.schema.json`. Runtime
compatibility requires both the executable and asset archive hashes to match a
single map before any symbol is resolved. Maps contain signatures, offsets,
and validation metadata only—never copied game code or assets.
