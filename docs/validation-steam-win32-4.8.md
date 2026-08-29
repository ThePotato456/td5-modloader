# Steam Win32 4.8 validation record

Date: 2026-08-29

The developer-only inspector performed a read-only check of a legally obtained
Steam installation. It did not launch or modify the game.

| Input | SHA-256 |
| --- | --- |
| `BTD5-Win.exe` | `bdc4f4aec679f51b8763ff7fe517a2556e392d99576045ece117fcafdda27b70` |
| `Assets/BTD5.jet` | `906aa89d690c27664ce47a1a2e3eac756d7cf551fe3e1669ec22ae814346b9a8` |

The following stable names resolved uniquely inside the executable's `.text`
section and passed their validation patterns:

- `game.main`
- `game.load_assets`
- `screen.game.init`
- `screen.game.uninit`
- `event.manager.dispatch`
- `event.round.started.vtable`
- `event.round.ended.vtable`
- `event.money.updated.vtable`
- `event.tower.spawned.vtable`
- `event.tower.upgraded.vtable`
- `event.tower.sold.vtable`
- `event.bloon.spawned.vtable`
- `event.bloon.popped.vtable`
- `event.bloon.escaped.vtable`
- `player.lives.gain.handler`
- `player.lives.loss.handler`
- `player.lives.gain.write`
- `player.lives.loss.write`
- `tower.factory.constructor`
- `weapon.factory.constructor`

## Isolated-copy launch check

A byte-identical copy of the installation was placed under the ignored
`.local/game` directory. Only that copy received the loader proxy, runtime,
symbol map, and a development-only `steam_appid.txt` containing App ID 306020.
The registered Steam installation was not modified.

The copied executable authenticated against the running Steam client, resolved
all mandatory symbols, reached `HooksReady`, and remained running for the full
15-second startup stability window. The exact test process was then closed.

This satisfies the Phase 3 supported-build no-mod launch gate. Automated tests
separately verify unknown-build rejection, named resolver failures, and
reverse-order transactional hook rollback.
