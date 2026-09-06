# Offline firewall helper review — 2026-09-06

Reviewed the pending `scripts/offline-game.ps1` rewrite and corrected:

- Missing JSON properties under StrictMode and empty mandatory rule collections.
- Rule ownership: repair/removal require both the exact display name and guard
  group, preserving unrelated same-name rules. Queries explicitly use the local
  persistent store and propagate access/provider failures.
- Validation now includes all-profile coverage, expected direction, enabled block
  action, exact executable path, and exactly one owned rule per direction.
- Removed nested confirmation during Enable. WhatIf cannot remove/create rules;
  Disable reports success only after confirmation and successful removal.
- Removal errors propagate. Partial enable failures terminate rather than report
  success; a subsequent Enable repairs the owned pair. This is not a transactional
  firewall update; the game must remain closed during configuration changes.
- Disable no longer depends on valid game configuration.
- Status describes configured rules instead of claiming effective network blocking
  or unrestricted access. Firewall service, active policy, and other rules are
  outside that configuration check. Steam is a separate process.

`tests/scripts/offline-game.tests.ps1` parses and loads the actual production
helpers, replaces firewall/process commands with in-memory mocks, and checks
creation, idempotent repair, ownership preservation, duplicate/path/profile
rejection, WhatIf, failures, running-game refusal, and configuration handling.
It changes no system firewall rules.

Mocked regression checks and PowerShell parsing pass. Real Status/WhatIf validation
was attempted but Windows denied Get-NetFirewallRule even outside the sandbox.
No real firewall rules were changed, and live traffic blocking is not claimed.
An elevated Windows session with firewall permissions is needed to exercise the
real provider. This script remains an external helper, not completion of the
manager/runtime offline protection phase.
