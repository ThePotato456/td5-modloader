# Steam Win32 4.8 validation record

Date: 2026-08-28

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
- `tower.factory.constructor`
- `weapon.factory.constructor`

This record satisfies the offline fingerprint and symbol-resolution check. It
does not satisfy the Phase 3 no-mod launch gate, which remains unchecked until
the reversible loader package is tested in the game process.
