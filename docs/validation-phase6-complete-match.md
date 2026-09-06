# Complete-match lifecycle acceptance — 2026-09-06

The user played a fresh Easy match to the victory screen and confirmed victory,
then returned to the menu and entered a second match. The copied-game
`--expect-complete-match` harness reported `LIVE_SMOKE_PASS`.

## Observed evidence

- First match entered at 22:25:26 UTC and exited at 22:36:46 UTC.
- Exactly 50 each of round.starting, round.started, round.ending, and round.ended;
  all 200 sample round notifications occurred in that repeating order.
- Exactly two match.starting/started pairs and one match.ending/ended pair before
  the harness closed the game following second entry at 22:36:58 UTC.
- The sample retained tower and bloon handles across teardown and verified both
  invalid, including rejection of a stale tower property read.
- 22 tower placements and 54 upgrades were observed. No tower sale occurred.
- No runtime error records. Normal shutdown completed at 22:36:58 UTC.
- Raw logs are preserved in ignored `.local/match-acceptance/victory-runtime.jsonl`
  and `victory-result.txt`; they are not release content.
- Game executable SHA-256 remained
  `BDC4F4AEC679F51B8763FF7FE517A2556E392D99576045ECE117FCAFDDA27B70`.
- Asset archive SHA-256 remained
  `906AA89D690C27664CE47A1A2E3EAC756D7CF551FE3E1669EC22AE814346B9A8`.

## Scope

This accepts the observed full-match round ordering, single lifecycle dispatch,
scene invalidation, and second-match entry scenario. It does not prove all native
destruction paths, future object wrappers, or gameplay mutations across a full
match: mutation toggles were disabled. Tower sale has separate historical live
evidence, but was not exercised in this run. The broader ACTIONPLAN Phase 6 gate
remains partial; native life-gain acceptance also remains open.
