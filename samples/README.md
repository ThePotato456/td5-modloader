# Samples

These source examples use only the public, sandboxed Lua 5.4 API:

| Source | Purpose |
| --- | --- |
| [`hello-world-mod`](hello-world-mod/README.md) | Minimal lifecycle, logging, and timer starter |
| [`event-monitor-mod`](event-monitor-mod/README.md) | Read-only gameplay events, stable object IDs, configuration, storage, and unsubscription |
| [`lives-guardian-mod`](lives-guardian-mod/README.md) | Configurable cancellation of the supported `lives.changing` event |
| [`lifecycle-mod`](lifecycle-mod/README.md) | Comprehensive validation sample covering every live event |

Running `scripts/stage.ps1` packages all four examples into the staged
`samples` directory. A complete custom-tower example will be added with Phase 7
after the tower-content API exists.
