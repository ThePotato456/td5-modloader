# Troubleshooting and recovery

[Documentation home](README.md) · [Player setup](getting-started.md)

Start by closing the game and reading the manager's concrete readiness error.
Export diagnostics before clearing logs. Change one thing, relaunch, and check
the result so the cause stays clear.

Open **Options → Runtime log → Show console** and compare your log with the
sample's expected messages:

| Message from Hello World | What it confirms |
| --- | --- |
| `Hello World loaded` | The script loaded and `on_load` ran |
| `Hello World is ready` | The first render-ready callback ran |
| `Hello World timer fired after 60 rendered frames` | The scheduled frame timer ran |

Missing all three points toward profile activation or launch authorization.
A callback error points to the named mod and callback instead.

## Installation and launch

| Symptom | Check and next step |
| --- | --- |
| Game not found | Select the installation containing `BTD5-Win.exe` and `Assets/` in Options; check the [supported build](compatibility.md) |
| Unsupported game or hash mismatch | Verify the actual Steam files and selected copy; provide diagnostics if the build needs research |
| Missing manager artifacts | Extract a complete release; keep the DLLs, validator, and `symbols/` beside the manager |
| Missing owned loader file | Use **Repair loader** with the matching release bundle |
| Loader version mismatch | Use **Update loader** with a complete current bundle |
| Conflict or modified file | Preserve the reported file and ownership record; compare with the original release or other loader before taking action |
| No current profile | Create or explicitly select one; the manager does not guess after deletion |
| Missing dependency or order cycle | Install and enable the required version; remove contradictory `before`/`after` rules or choose compatible mods |
| Launch blocked by offline requirement | Put Steam in Offline Mode, then acknowledge the requirement in Options |
| Game already running | Exit the existing process, then retry the launch from the manager |
| Steam opens a different copy | Verify the selected path and Steam installation; copied development installs have separate harness requirements |

Install/repair/uninstall preserve foreign and modified files. Do not delete
ownership records to hide a conflict: that discards evidence needed for recovery.
For exact health states see [manager recovery](manager-revamp.md#loader-health-and-recovery).

## A mod is installed but does not run

1. Confirm its switch is on in the selected profile and the intended version is
   selected. Package installation alone does not enable anything.
2. Confirm the selected game and loader are healthy and the profile is ready.
3. Launch **from the manager**. A normal Steam launch has no mod authorization
   ticket and stays vanilla.
4. Open the runtime console in Options. Look for the mod ID, script errors, and
   callback names; the normal log is under
   `%LocalAppData%\BTD5ModLoader\logs\runtime.jsonl`.
5. If the log says `vanilla_no_ticket`, close the game and retry from the manager.
   Do not create `runtime/active-profile.json` or set `BTD5ML_ACTIVE_PROFILE`;
   those old handoffs are ignored.
6. `launch_ticket_missing_or_consumed`, `launch_ticket_binding_or_expiry_invalid`,
   and `launch_ticket_malformed` indicate a rejected handoff. Retry a fresh
   manager launch after closing the game; export diagnostics if it repeats.

Tickets bind a profile to one exact process and expire. Reusing a ticket cannot
enable a different launch. See [launch storage](manager-storage.md#runtime-handoff-and-launch).

## Package or script rejected

| Error area | Typical correction |
| --- | --- |
| Manifest | Check required fields, exact case, stable dotted ID, API 1, and supported build |
| Archive layout | Put `mod.json` at the ZIP root; remove extra parent folders, unsupported roots, and duplicate paths |
| Entry point | Use `lua/main.lua` with forward slashes; confirm it exists in the archive |
| Existing ID/version | Increment the version after edits, then select that version in the profile |
| Tower registration timing | Call `btd5.towers.register` at script top level, before `on_load` |
| Upgrade path | Provide exactly four tiers for each replaced path; own weapons require both paths |
| Unknown art | Check cell and sheet names, PNG/XML pairs, and validate with `--game`; review warnings |
| Sound registration | Use existing `.wav` files below `assets/`, 1–8 per effect, at most 8 MiB each |
| Missing translation | Ship `localization/en-US.json` as a flat string-to-string object; missing keys return the key |

Run `btd5loader_validate.exe` against the final archive. Its success covers
script loading and registration, not runtime callback behavior. See
[author validation steps](mod-authoring.md#5-validate-before-launch).

## Callback errors or unexpected behavior

- **Stale object:** a tower was sold, bloon ended, or scene changed. Use
  `is_valid()` before accessing a retained wrapper. `match.ended` already has a
  stale match; save primitive values while the object is valid.
- **No effect from cancellation:** check the [event contract](gameplay-events.md).
  Cash events are notifications; only documented pre-action events consume
  `cancelled`. Purchases need `purchase = true`; spawns need `from_round = true`.
- **Repeated leak cancellation:** the same live bloon can retry. Bound attempts
  and eventually allow the leak, as in [Bloon Rules](../samples/bloon-rules-mod/README.md).
- **Recursive cash callbacks:** `match:set_cash()` dispatches cash events before
  returning. Set a guard before calling it, or set cash from a round handler as
  in [Round Budget](../samples/round-budget-mod/README.md).
- **Budget exceeded:** shorten the handler, bound retained data and logging, and
  avoid per-projectile timers. A timer still runs on the game thread.
- **`require`, `io`, or `os` missing:** these are unavailable in the sandbox;
  use the public APIs and one entry script.
- **Setting does not change:** parse configuration strings explicitly and restart
  after changing profile settings.

## Recover a working setup

1. Close the game. Duplicate the affected profile before changing its setup.
2. Disable the most recently added mod, along with dependents if required, and
   retry. If needed, use a new profile with Hello World alone.
3. Reintroduce mods one at a time. Record the first version combination that
   reproduces the failure.
4. For a saved match with missing custom content, restore its original mod IDs
   and versions before trying to resume. Use your backups for broader save
   recovery; the loader has no general automatic restore facility.
5. Use **Launch vanilla** in Options to check the installation without a mod
   ticket. If installation health blocks it, repair or remove the loader using
   the manager's reported action.

## Report a bug

Include the loader version, game build ID, mod IDs/versions, enabled order,
reproduction steps, expected versus actual result, and the relevant log or
diagnostics export. Mention whether it reproduces in a fresh profile and vanilla.
Review your own log messages for personal data before sharing. Exclude game
files, saves, account information, credentials, and local-only research dumps.
