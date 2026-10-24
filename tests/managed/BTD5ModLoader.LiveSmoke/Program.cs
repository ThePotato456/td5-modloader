using System.Diagnostics;
using System.Text.RegularExpressions;
using BTD5ModLoader.Manager.Core;

if (args.Length == 5 && args[4] == "--expect-vanilla-isolation")
    return await VanillaAcceptance.RunAsync(args[0], args[1], args[2], args[3]);
if (args.Length == 5 && args[4] == "--expect-vanilla-only")
    return await VanillaAcceptance.RunAsync(args[0], args[1], args[2], args[3], true);

if (args.Length is < 4 or > 5 ||
    (args.Length == 5 &&
        !string.Equals(args[4], "--expect-match", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-shutdown", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-match-exit", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-round", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-cash", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-cash-action", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-lives-loss", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-lives-cancel", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-lives-mutation", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-tower-pop-count", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-tower-cancellation", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-direct-properties", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-identity-cash", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-pop-cancellation", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-spawn-cancellation", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-tower-actions", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-complete-match", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-match-cycles", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-mutation-match", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-native-gains", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-bloon-cancellation", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-bloon-actions", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-attack-projectile", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-placement-cancellation", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-disabled", StringComparison.Ordinal)))
{
    Console.Error.WriteLine(
        "Usage: BTD5ModLoader.LiveSmoke <game-directory> <artifact-directory> " +
        "<package> <state-root> " +
        "[--expect-match|--expect-match-exit|--expect-round|--expect-cash|" +
        "--expect-cash-action|--expect-lives-loss|--expect-lives-cancel|--expect-lives-mutation|" +
        "--expect-tower-pop-count|--expect-tower-cancellation|--expect-tower-actions|" +
        "--expect-bloon-actions|--expect-attack-projectile|--expect-placement-cancellation|--expect-complete-match|--expect-match-cycles|--expect-mutation-match|--expect-native-gains|--expect-bloon-cancellation|--expect-direct-properties|--expect-identity-cash|--expect-pop-cancellation|--expect-spawn-cancellation|--expect-disabled|--expect-shutdown]");
    return 2;
}

var gameDirectory = Path.GetFullPath(args[0]);
var artifactDirectory = Path.GetFullPath(args[1]);
var packagePath = Path.GetFullPath(args[2]);
var stateRoot = Path.GetFullPath(args[3]);
var expectDisabled = args.Length == 5 &&
    string.Equals(args[4], "--expect-disabled", StringComparison.Ordinal);
var expectShutdown = args.Length == 5 &&
    string.Equals(args[4], "--expect-shutdown", StringComparison.Ordinal);
var expectMatch = args.Length == 5 && !expectDisabled && !expectShutdown;
var closeRequested = false;
var expectMatchExit = args.Length == 5 &&
    string.Equals(args[4], "--expect-match-exit", StringComparison.Ordinal);
var expectRound = args.Length == 5 &&
    string.Equals(args[4], "--expect-round", StringComparison.Ordinal);
var expectCash = args.Length == 5 &&
    (string.Equals(args[4], "--expect-cash", StringComparison.Ordinal) ||
        string.Equals(args[4], "--expect-cash-action", StringComparison.Ordinal));
var expectCashAction = args.Length == 5 &&
    string.Equals(args[4], "--expect-cash-action", StringComparison.Ordinal);
var expectLivesLoss = args.Length == 5 &&
    string.Equals(args[4], "--expect-lives-loss", StringComparison.Ordinal);
var expectLivesCancel = args.Length == 5 &&
    string.Equals(args[4], "--expect-lives-cancel", StringComparison.Ordinal);
var expectLivesMutation = args.Length == 5 &&
    string.Equals(args[4], "--expect-lives-mutation", StringComparison.Ordinal);
var expectTowerPopCount = args.Length == 5 &&
    string.Equals(args[4], "--expect-tower-pop-count", StringComparison.Ordinal);
var expectTowerCancellation = args.Length == 5 &&
    string.Equals(args[4], "--expect-tower-cancellation", StringComparison.Ordinal);
var expectDirectProperties = args.Length == 5 &&
    string.Equals(args[4], "--expect-direct-properties", StringComparison.Ordinal);
// Identity getters on a Ten-mode match, plus a cash raise and restore.
var expectIdentityCash = args.Length == 5 && args[4] == "--expect-identity-cash";
// A cancelled lethal hit keeps the bloon alive until a later hit pops it.
var expectPopCancellation = args.Length == 5 && args[4] == "--expect-pop-cancellation";
// Cancelled round spawns never join the game and the round still ends.
var expectSpawnCancellation = args.Length == 5 && args[4] == "--expect-spawn-cancellation";
var expectTowerActions = args.Length == 5 &&
    string.Equals(args[4], "--expect-tower-actions", StringComparison.Ordinal);
var expectCompleteMatch = args.Length == 5 && args[4] == "--expect-complete-match";
// Several enter/exit cycles, to see whether native survivors grow per match.
const int matchCycles = 3;
var expectMatchCycles = args.Length == 5 && args[4] == "--expect-match-cycles";
// Every implemented mutation enabled from match entry to exit, then re-entry.
var expectMutationMatch = args.Length == 5 && args[4] == "--expect-mutation-match";
var expectNativeGains = args.Length == 5 && args[4] == "--expect-native-gains";
var expectBloonCancellation = args.Length == 5 && args[4] == "--expect-bloon-cancellation";
var expectBloonActions = args.Length == 5 &&
    string.Equals(args[4], "--expect-bloon-actions", StringComparison.Ordinal);
var expectPlacementCancellation = args.Length == 5 &&
    string.Equals(args[4], "--expect-placement-cancellation", StringComparison.Ordinal);
var expectAttackProjectile = args.Length == 5 &&
    string.Equals(args[4], "--expect-attack-projectile", StringComparison.Ordinal);
const string profileName = "Live Smoke";
Process? gameProcess = null;
DateTimeOffset? livesCancellationObservedAt = null;
DateTimeOffset? towerCancellationObservedAt = null;
DateTimeOffset? disabledRuntimeObservedAt = null;
try
{
    var installationService = new LoaderInstallationService(stateRoot);
    var installationRecord = installationService.GetRecordPath(gameDirectory);
    if (File.Exists(installationRecord))
    {
        var uninstall = await installationService.UninstallAsync(gameDirectory);
        if (!uninstall.Success)
        {
            return Fail("Previous loader uninstall failed: " + uninstall.Message + Format(uninstall.Conflicts));
        }
    }
    var installation = await installationService.InstallAsync(gameDirectory, artifactDirectory);
    if (!installation.Success)
    {
        return Fail("Loader install failed: " + installation.Message + Format(installation.Conflicts));
    }
    var candidate = await ModPackageService.InspectAsync(packagePath, "steam-win32-4.8");
    if (!candidate.Valid || candidate.Id is null || candidate.Version is null)
    {
        return Fail("Package inspection failed: " + Format(candidate.Errors));
    }
    var profiles = new ProfileService(stateRoot);
    var existingProfile = await profiles.LoadAsync(profileName);
    // Extra packages from an earlier run leave the profile; this run adds its own after the sample.
    foreach (var stale in existingProfile?.Mods.Where(mod => mod.Id != candidate.Id).ToList() ?? [])
    {
        var removeStale = await new ProfileModService(stateRoot, "steam-win32-4.8").RemoveAsync(profileName, stale.Id);
        if (!removeStale.Success)
        {
            return Fail("Previous extra package removal failed: " + Format(removeStale.Validation.Errors));
        }
    }
    if (existingProfile?.Mods.Any(mod => mod.Id == candidate.Id) == true)
    {
        var remove = await new ProfileModService(stateRoot, "steam-win32-4.8")
            .RemoveAsync(profileName, candidate.Id);
        if (!remove.Success)
        {
            return Fail("Previous sample removal failed: " + Format(remove.Validation.Errors));
        }
    }
    var installedPackages = await ModPackageService.ListInstalledAsync(stateRoot, "steam-win32-4.8");
    if (installedPackages.Any(package => package.Id == candidate.Id && package.Version == candidate.Version))
    {
        var uninstallPackage = await new ProfileModService(stateRoot, "steam-win32-4.8")
            .UninstallPackageAsync(candidate.Id, candidate.Version);
        if (!uninstallPackage.Success)
        {
            return Fail("Previous sample uninstall failed: " +
                uninstallPackage.Message + Format(uninstallPackage.BlockingProfiles));
        }
    }
    var package = await ModPackageService.InstallAsync(packagePath, stateRoot, "steam-win32-4.8");
    if (!package.Success || package.Package.Id is null || package.Package.Version is null)
    {
        return Fail("Package install failed: " + package.Message + Format(package.Package.Errors));
    }

    if (await profiles.LoadAsync(profileName) is null)
    {
        await profiles.CreateAsync(profileName);
    }
    var profileChange = await new ProfileModService(stateRoot, "steam-win32-4.8")
        .EnableAsync(profileName, package.Package.Id, package.Package.Version);
    if (!profileChange.Success)
    {
        return Fail("Profile enable failed: " + Format(profileChange.Validation.Errors));
    }
    if (profileChange.Profile?.Mods.Single().Configuration.TryGetValue("greeting", out var greeting) != true ||
        greeting.GetString() != "Hello from Lua")
    {
        return Fail("Package configuration defaults were not inherited by the profile.");
    }
    // BTD5ML_SMOKE_EXTRA_PACKAGES: ';'-separated packages enabled after the sample,
    // such as the custom tower sample.
    foreach (var extraPath in (Environment.GetEnvironmentVariable("BTD5ML_SMOKE_EXTRA_PACKAGES") ?? "")
                 .Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
    {
        var extra = await ModPackageService.InspectAsync(Path.GetFullPath(extraPath), "steam-win32-4.8");
        if (!extra.Valid || extra.Id is null || extra.Version is null)
        {
            return Fail("Extra package inspection failed: " + extraPath + Format(extra.Errors));
        }
        var installedExtras = await ModPackageService.ListInstalledAsync(stateRoot, "steam-win32-4.8");
        if (installedExtras.Any(installed => installed.Id == extra.Id && installed.Version == extra.Version))
        {
            var uninstallExtra = await new ProfileModService(stateRoot, "steam-win32-4.8")
                .UninstallPackageAsync(extra.Id, extra.Version);
            if (!uninstallExtra.Success)
            {
                return Fail("Previous extra package uninstall failed: " + uninstallExtra.Message);
            }
        }
        var extraInstall = await ModPackageService.InstallAsync(Path.GetFullPath(extraPath), stateRoot, "steam-win32-4.8");
        if (!extraInstall.Success || extraInstall.Package.Id is null || extraInstall.Package.Version is null)
        {
            return Fail("Extra package install failed: " + extraInstall.Message);
        }
        var extraEnable = await new ProfileModService(stateRoot, "steam-win32-4.8")
            .EnableAsync(profileName, extraInstall.Package.Id, extraInstall.Package.Version);
        if (!extraEnable.Success)
        {
            return Fail("Extra package enable failed: " + Format(extraEnable.Validation.Errors));
        }
        profileChange = extraEnable;
        Console.Error.WriteLine("extra package enabled: " + extraInstall.Package.Id);
    }
    if (expectDisabled)
    {
        var disabled = await new ProfileModService(stateRoot, "steam-win32-4.8")
            .DisableAsync(profileName, package.Package.Id);
        if (!disabled.Success || disabled.Profile?.Mods.Single().Enabled != false)
        {
            return Fail("Sample mod could not be disabled before launch: " +
                Format(disabled.Validation.Errors));
        }
    }
    if (expectSpawnCancellation || expectPopCancellation || expectIdentityCash || expectMutationMatch || expectCompleteMatch || expectNativeGains || expectBloonCancellation || expectLivesCancel || expectLivesMutation || expectTowerPopCount || expectTowerCancellation ||
        expectDirectProperties || expectAttackProjectile || expectPlacementCancellation)
    {
        var profile = profileChange.Profile!;
        var configuredMods = profile.Mods.Select(mod => mod.Id != package.Package.Id
            ? mod
            : mod with
            {
                Configuration = new Dictionary<string, System.Text.Json.JsonElement>(mod.Configuration)
                {
                    [expectSpawnCancellation ? "cancel_bloon_spawns" : expectPopCancellation ? "cancel_bloon_pops" : expectIdentityCash ? "test_identity_cash" : expectMutationMatch ? "test_mutation_match" : expectPlacementCancellation ? "cancel_tower_placement" : expectAttackProjectile ? "observe_attacks_projectiles" : expectCompleteMatch ? "test_complete_match" : expectNativeGains ? "test_native_gains" : expectBloonCancellation ? "cancel_bloon_leaks" : expectLivesCancel
                        ? "cancel_lives_loss"
                        : expectLivesMutation
                            ? "mutate_lives_loss"
                            : expectTowerPopCount
                                ? "mutate_tower_pop_count"
                                : expectTowerCancellation
                                    ? "cancel_tower_actions"
                                    : "mutate_direct_properties"] =
                        System.Text.Json.JsonSerializer.SerializeToElement(true)
                }
            });
        await profiles.SaveModsAsync(profileName, configuredMods);
    }

    var logPath = Path.Combine(stateRoot, "logs", "runtime.jsonl");
    RuntimeLogFiles.Delete(logPath);
    var sessionsPath = Path.Combine(stateRoot, "runtime", "sessions");
    var previousSessions = Directory.Exists(sessionsPath)
        ? Directory.EnumerateFileSystemEntries(sessionsPath).ToHashSet(StringComparer.OrdinalIgnoreCase)
        : new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    var launch = await new GameLaunchService(stateRoot)
        .LaunchModdedAsync(gameDirectory, profileName, true);
    if (!launch.Success || launch.ProcessId is null)
    {
        return Fail("Modded launch failed: " + launch.Message + Format(launch.Errors));
    }
    gameProcess = Process.GetProcessById(launch.ProcessId.Value);
    // Retain a handle while the process is alive so exit-code inspection works.
    _ = gameProcess.SafeHandle;
    var deadline = DateTimeOffset.UtcNow.AddSeconds(expectIdentityCash || expectPopCancellation || expectSpawnCancellation ? 600 : expectCompleteMatch || expectMatchCycles || expectMutationMatch ? 7200 : expectNativeGains ? 3600 : expectBloonCancellation ? 900 :
        expectMatchExit || expectRound || expectLivesLoss || expectLivesCancel || expectLivesMutation ||
            expectTowerPopCount || expectTowerActions || expectBloonActions || expectAttackProjectile ||
            expectPlacementCancellation
            || expectTowerCancellation || expectDirectProperties
            ? 240
            : expectMatch || expectCash ? 180 : expectShutdown ? 90 : expectDisabled ? 30 : 20);
    while (DateTimeOffset.UtcNow < deadline)
    {
        await Task.Delay(250);
        gameProcess.Refresh();
        if (gameProcess.HasExited && !expectShutdown)
        {
            return Fail($"The game exited early with code {gameProcess.ExitCode}.");
        }
        if (!File.Exists(logPath))
        {
            continue;
        }
        string log;
        try
        {
            log = await RuntimeLogFiles.ReadAsync(logPath);
        }
        catch (IOException)
        {
            continue;
        }
        var lifecycleReady = log.Contains("Hello from Lua (launch ", StringComparison.Ordinal) &&
            log.Contains("sample.lifecycle:loaded", StringComparison.Ordinal) &&
            log.Contains("game_ready_frame_hook", StringComparison.Ordinal) &&
            log.Contains("Lifecycle Sample is ready", StringComparison.Ordinal) &&
            log.Contains("deterministic timer fired", StringComparison.Ordinal);
        if (expectShutdown)
        {
            if (gameProcess.HasExited)
            {
                var remainingSessions = Directory.Exists(sessionsPath)
                    ? Directory.EnumerateFileSystemEntries(sessionsPath)
                    : [];
                if (!closeRequested || gameProcess.ExitCode != 0 ||
                    CountOccurrences(log, "Lifecycle Sample shut down cleanly") != 1 ||
                    CountOccurrences(log, "shutdown_requested") != 1 ||
                    CountOccurrences(log, "shutdown_complete") != 1 ||
                    !previousSessions.SetEquals(remainingSessions))
                {
                    return Fail("Normal exit did not produce exactly-once shutdown and session cleanup.");
                }
                Console.WriteLine("LIVE_SMOKE_PASS: normal exit, exactly-once shutdown, session cleanup.");
                return 0;
            }
            if (lifecycleReady && !closeRequested)
            {
                closeRequested = gameProcess.CloseMainWindow();
                if (closeRequested)
                {
                    Console.WriteLine("Normal window close requested; waiting for the game to exit.");
                }
            }
            continue;
        }
        if (expectCompleteMatch)
        {
            if (CountOccurrences(log, "Lifecycle Sample observed match.started") >= 2)
            {
                if (CountOccurrences(log, "Lifecycle Sample observed match.starting") != 2 ||
                    CountOccurrences(log, "Lifecycle Sample observed match.started") != 2 ||
                    CountOccurrences(log, "Lifecycle Sample observed match.ending") != 1 ||
                    CountOccurrences(log, "Lifecycle Sample observed match.ended") != 1 ||
                    CountOccurrences(log, "Complete match test retained objects stale after teardown") != 1)
                    return Fail("Match re-entry did not preserve exactly-once lifecycle and stale-object checks.");
                if (CountOccurrences(log, "teardown_survivors") != 1 ||
                    (!log.Contains("teardown_survivors none", StringComparison.Ordinal) &&
                     CountOccurrences(log, "teardown_fate") != 1))
                    return Fail("Match teardown did not account for surviving native objects by creation site.");
                Console.WriteLine("LIVE_SMOKE_PASS: first match teardown and second match entry; " +
                    "user confirmation of full match completion and event-log review still required.");
                gameProcess.CloseMainWindow();
                await gameProcess.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(15));
                return 0;
            }
            continue;
        }
        if (expectMutationMatch)
        {
            if (CountOccurrences(log, "Lifecycle Sample observed match.started") >= 2)
            {
                if (CountOccurrences(log, "Lifecycle Sample observed match.starting") != 2 ||
                    CountOccurrences(log, "Lifecycle Sample observed match.ending") != 1 ||
                    CountOccurrences(log, "Lifecycle Sample observed match.ended") != 1 ||
                    CountOccurrences(log, "Complete match test retained objects stale after teardown") != 1)
                    return Fail("Mutation match did not preserve exactly-once lifecycle and stale-object checks.");
                var missing = new List<string>();
                void Require(string label, bool met)
                {
                    if (!met) missing.Add(label);
                }
                Require("one cancelled purchase", CountOccurrences(log, "Lifecycle Sample cancelled tower.placing") == 1);
                Require("a placed tower", log.Contains("Lifecycle Sample observed tower.placed id=", StringComparison.Ordinal));
                Require("pop-count replacement", Regex.IsMatch(log, @"Lifecycle Sample mutated tower\.pop_count \d+->123"));
                Require("sell-price replacement", Regex.IsMatch(log, @"Lifecycle Sample mutated tower\.sell_price \d+->777"));
                Require("one cancelled upgrade", CountOccurrences(log, "Lifecycle Sample cancelled tower.upgrading") == 1);
                Require("a completed upgrade", log.Contains("Lifecycle Sample observed tower.upgraded id=", StringComparison.Ordinal));
                Require("one cancelled sale", CountOccurrences(log, "Lifecycle Sample cancelled tower.selling") == 1);
                Require("a completed sale", CountOccurrences(log, "Lifecycle Sample observed tower.sold id=") == 1 &&
                    log.Contains("Lifecycle Sample confirmed sold tower became stale", StringComparison.Ordinal));
                Require("bloon health replacement", log.Contains("Lifecycle Sample mutated bloon.health ", StringComparison.Ordinal));
                var leak = Regex.Match(log, @"Lifecycle Sample cancelled bloon\.leaking id=(\d+)");
                Require("one cancelled leak that later leaked", leak.Success &&
                    CountOccurrences(log, "Lifecycle Sample cancelled bloon.leaking") == 1 &&
                    log.Contains("Mutation match cancelled bloon leaked after continuing id=" + leak.Groups[1].Value,
                        StringComparison.Ordinal));
                Require("one cancelled lives loss", CountOccurrences(log, "Mutation match cancelled lives loss") == 1);
                var replaced = Regex.Match(log, @"Mutation match replaced lives loss (\d+)->(\d+) with (\d+)->(\d+)");
                Require("a replaced lives loss that committed", replaced.Success &&
                    int.Parse(replaced.Groups[4].Value) == int.Parse(replaced.Groups[1].Value) + 1 &&
                    log.Contains("Lifecycle Sample observed lives.changed " + replaced.Groups[3].Value + "->" +
                        replaced.Groups[4].Value, StringComparison.Ordinal));
                if (missing.Count > 0)
                    return Fail("Mutation match is missing: " + string.Join(", ", missing) + ".");
                Console.WriteLine("LIVE_SMOKE_PASS: every implemented mutation applied across one match; " +
                    "retained objects were stale after teardown and the next match started.");
                gameProcess.CloseMainWindow();
                await gameProcess.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(15));
                return 0;
            }
            continue;
        }
        if (expectMatchCycles)
        {
            if (CountOccurrences(log, "Lifecycle Sample observed match.started") > matchCycles)
            {
                if (CountOccurrences(log, "Lifecycle Sample observed match.starting") != matchCycles + 1 ||
                    CountOccurrences(log, "Lifecycle Sample observed match.started") != matchCycles + 1 ||
                    CountOccurrences(log, "Lifecycle Sample observed match.ending") != matchCycles ||
                    CountOccurrences(log, "Lifecycle Sample observed match.ended") != matchCycles)
                    return Fail("Match cycles did not preserve exactly-once lifecycle events.");
                var outstanding = Regex.Matches(log, @"teardown_outstanding [^""]*")
                    .Select(match => match.Value).ToList();
                if (CountOccurrences(log, "teardown_survivors") != matchCycles || outstanding.Count != matchCycles)
                    return Fail("Each match teardown must report its survivors and the outstanding total.");
                for (var cycle = 0; cycle < outstanding.Count; cycle++)
                    Console.WriteLine($"match {cycle + 1}: {outstanding[cycle]}");
                Console.WriteLine($"LIVE_SMOKE_PASS: {matchCycles} match cycles accounted for at teardown; " +
                    "compare the outstanding totals to see whether survivors grow.");
                gameProcess.CloseMainWindow();
                await gameProcess.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(15));
                return 0;
            }
            continue;
        }
        if (expectNativeGains)
        {
            var normal = Regex.Match(log, @"Native gain test normal (\d+)->(\d+)");
            var canceled = Regex.Match(log, @"Native gain test cancelled (\d+)->(\d+)");
            var replaced = Regex.Match(log, @"Native gain test replaced (\d+)->(\d+) with (\d+)->(\d+)");
            if (normal.Success && canceled.Success && replaced.Success)
            {
                var normalTransition = normal.Groups[1].Value + "->" + normal.Groups[2].Value;
                var replacement = replaced.Groups[3].Value + "->" + replaced.Groups[4].Value;
                if (!log.Contains("Lifecycle Sample observed lives.changed " + replacement, StringComparison.Ordinal))
                    continue;
                var cancelWindow = log[canceled.Index..replaced.Index];
                if (!lifecycleReady || normal.Index >= canceled.Index ||
                    !log.Contains("Lifecycle Sample observed lives.changed " + normalTransition, StringComparison.Ordinal) ||
                    !cancelWindow.Contains("lives.changing:cancelled", StringComparison.Ordinal) ||
                    cancelWindow.Contains("Lifecycle Sample observed lives.changed", StringComparison.Ordinal) ||
                    canceled.Groups[1].Value != replaced.Groups[1].Value)
                    return Fail("Native gain sequence failed normal/cancel/replacement checks; avoid losses between gains.");
                Console.WriteLine("LIVE_SMOKE_PASS: native gains normal " + normalTransition +
                    ", cancelled " + canceled.Groups[1].Value + "->" + canceled.Groups[2].Value +
                    ", replacement " + replacement + ".");
                gameProcess.CloseMainWindow();
                await gameProcess.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(15));
                return 0;
            }
            continue;
        }
        if (expectBloonCancellation)
        {
            var canceled = Regex.Match(log, "Lifecycle Sample cancelled bloon\\.leaking id=(\\d+)");
            if (canceled.Success && livesCancellationObservedAt is null)
                livesCancellationObservedAt = DateTimeOffset.UtcNow;
            if (canceled.Success && livesCancellationObservedAt is not null &&
                DateTimeOffset.UtcNow - livesCancellationObservedAt >= TimeSpan.FromSeconds(3))
            {
                var id = canceled.Groups[1].Value;
                if (!lifecycleReady || !log.Contains("bloon.leaking:cancelled", StringComparison.Ordinal) ||
                    !log.Contains("confirmed cancelled bloon remains valid id=" + id, StringComparison.Ordinal) ||
                    log.Contains("Lifecycle Sample observed bloon.leaked", StringComparison.Ordinal) ||
                    log.Contains("Lifecycle Sample observed lives.changing", StringComparison.Ordinal) ||
                    log.Contains("Lifecycle Sample observed lives.changed", StringComparison.Ordinal))
                    return Fail("Leak cancellation did not preserve the bloon and exclude leak/lives events.");
                Console.WriteLine("LIVE_SMOKE_PASS: cancelled bloon " + id +
                    "; handle remained valid; no leak or lives transition during settling window.");
                gameProcess.CloseMainWindow();
                await gameProcess.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(15));
                return 0;
            }
            continue;
        }
        var disabledRuntimeReady =
            log.Contains("hooks_ready=", StringComparison.Ordinal) &&
            log.Contains("mods_loaded_waiting_for_game_ready_hook", StringComparison.Ordinal);
        if (disabledRuntimeReady && disabledRuntimeObservedAt is null)
        {
            disabledRuntimeObservedAt = DateTimeOffset.UtcNow;
        }
        var disabledRuntimeSettled = disabledRuntimeObservedAt is not null &&
            DateTimeOffset.UtcNow - disabledRuntimeObservedAt >= TimeSpan.FromSeconds(2);
        var disabledProfileClean = disabledRuntimeReady && disabledRuntimeSettled &&
            !log.Contains("sample.lifecycle:loaded", StringComparison.Ordinal) &&
            !log.Contains("Hello from Lua", StringComparison.Ordinal) &&
            !log.Contains("Lifecycle Sample", StringComparison.Ordinal);
        var matchReady = log.Contains("Lifecycle Sample observed match.starting", StringComparison.Ordinal) &&
            log.Contains("Lifecycle Sample observed match.started", StringComparison.Ordinal);
        var matchExited = log.Contains("Lifecycle Sample observed match.ending", StringComparison.Ordinal) &&
            log.Contains("Lifecycle Sample observed match.ended", StringComparison.Ordinal);
        var roundCompleted = log.Contains("Lifecycle Sample observed round.starting", StringComparison.Ordinal) &&
            log.Contains("Lifecycle Sample observed round.started", StringComparison.Ordinal) &&
            log.Contains("Lifecycle Sample observed round.ending", StringComparison.Ordinal) &&
            log.Contains("Lifecycle Sample observed round.ended", StringComparison.Ordinal);
        var requiredCashUpdates = expectCashAction ? 2 : 1;
        var cashChanged = CountOccurrences(log, "Lifecycle Sample observed cash.changing") >=
                requiredCashUpdates &&
            CountOccurrences(log, "Lifecycle Sample observed cash.changed") >= requiredCashUpdates;
        var livesChanging = Regex.Match(
            log,
            "Lifecycle Sample observed lives\\.changing (\\d+)->(\\d+)");
        var livesChanged = Regex.Match(
            log,
            "Lifecycle Sample observed lives\\.changed (\\d+)->(\\d+)");
        var livesLifecycle = livesChanging.Success && livesChanged.Success &&
            livesChanging.Groups[1].Value == livesChanged.Groups[1].Value &&
            livesChanging.Groups[2].Value == livesChanged.Groups[2].Value &&
            livesChanging.Index < livesChanged.Index;
        var livesCancelled = Regex.Match(
            log,
            "Lifecycle Sample cancelled lives\\.changing (\\d+)->(\\d+)");
        if (livesCancelled.Success && livesCancellationObservedAt is null)
        {
            livesCancellationObservedAt = DateTimeOffset.UtcNow;
        }
        var cancelledTransition = livesCancelled.Success
            ? livesCancelled.Groups[1].Value + "->" + livesCancelled.Groups[2].Value
            : string.Empty;
        var cancellationSettled = livesCancellationObservedAt is not null &&
            DateTimeOffset.UtcNow - livesCancellationObservedAt >= TimeSpan.FromSeconds(2);
        var livesCancellation = livesCancelled.Success && cancellationSettled &&
            log.Contains("lives.changing:cancelled", StringComparison.Ordinal) &&
            !log.Contains(
                "Lifecycle Sample observed lives.changed " + cancelledTransition,
                StringComparison.Ordinal);
        var livesMutated = Regex.Match(
            log,
            "Lifecycle Sample mutated lives\\.changing (\\d+)->(\\d+) to (\\d+)->(\\d+)");
        var mutatedTransition = livesMutated.Success
            ? livesMutated.Groups[3].Value + "->" + livesMutated.Groups[4].Value
            : string.Empty;
        var livesMutation = livesMutated.Success &&
            livesMutated.Groups[1].Value == livesMutated.Groups[3].Value &&
            livesMutated.Groups[2].Value != livesMutated.Groups[4].Value &&
            log.IndexOf(
                "Lifecycle Sample observed lives.changed " + mutatedTransition,
                livesMutated.Index,
                StringComparison.Ordinal) > livesMutated.Index;
        var placingTower = Regex.Match(log, "Lifecycle Sample observed tower\\.placing id=(\\d+)");
        var placedTower = Regex.Match(log, "Lifecycle Sample observed tower\\.placed id=(\\d+)");
        var upgradingTower = Regex.Match(log, "Lifecycle Sample observed tower\\.upgrading id=(\\d+)");
        var upgradedTower = Regex.Match(log, "Lifecycle Sample observed tower\\.upgraded id=(\\d+)");
        var sellingTower = Regex.Match(log, "Lifecycle Sample observed tower\\.selling id=(\\d+)");
        var soldTower = Regex.Match(log, "Lifecycle Sample observed tower\\.sold id=(\\d+)");
        // Maps may pre-place towers during setup, so evaluate the full action
        // sequence per tower identity rather than by first occurrence.
        static int TowerActionIndex(string log, string action, string id)
        {
            var match = Regex.Match(log,
                "Lifecycle Sample observed tower\\." + action + " id=" + id + "[\" ]");
            return match.Success ? match.Index : -1;
        }
        var towerActions = soldTower.Success &&
            Regex.Matches(log, "Lifecycle Sample observed tower\\.sold id=(\\d+)")
                .Select(match => match.Groups[1].Value)
                .Any(id =>
                {
                    var order = new[] { "placing", "placed", "upgrading", "upgraded", "selling", "sold" }
                        .Select(action => TowerActionIndex(log, action, id))
                        .ToArray();
                    return order.All(index => index >= 0) &&
                        order.Zip(order.Skip(1)).All(pair => pair.First < pair.Second);
                }) &&
            log.Contains("Lifecycle Sample confirmed sold tower became stale", StringComparison.Ordinal);
        var towerPopCountMutation = Regex.Match(
            log,
            "Lifecycle Sample mutated tower\\.pop_count (\\d+)->123");
        var towerSellPriceMutation = Regex.Match(
            log,
            "Lifecycle Sample mutated tower\\.sell_price (\\d+)->777");
        var cancelledTowerUpgrade = Regex.Match(
            log,
            "Lifecycle Sample cancelled tower\\.upgrading id=(\\d+)");
        var cancelledTowerSale = Regex.Match(
            log,
            "Lifecycle Sample cancelled tower\\.selling id=(\\d+)");
        if (cancelledTowerUpgrade.Success && cancelledTowerSale.Success &&
            towerCancellationObservedAt is null)
        {
            towerCancellationObservedAt = DateTimeOffset.UtcNow;
        }
        var towerCancellationSettled = towerCancellationObservedAt is not null &&
            DateTimeOffset.UtcNow - towerCancellationObservedAt >= TimeSpan.FromSeconds(2);
        var towerCancellation = cancelledTowerUpgrade.Success && cancelledTowerSale.Success &&
            cancelledTowerUpgrade.Groups[1].Value == cancelledTowerSale.Groups[1].Value &&
            towerCancellationSettled &&
            log.Contains("tower.upgrading:cancelled", StringComparison.Ordinal) &&
            log.Contains("tower.selling:cancelled", StringComparison.Ordinal) &&
            !log.Contains(
                "Lifecycle Sample observed tower.upgraded id=" +
                    cancelledTowerUpgrade.Groups[1].Value,
                StringComparison.Ordinal) &&
            !log.Contains(
                "Lifecycle Sample observed tower.sold id=" + cancelledTowerSale.Groups[1].Value,
                StringComparison.Ordinal);
        var spawningBloons = Regex.Matches(log, "Lifecycle Sample observed bloon\\.spawning id=(\\d+)")
            .Select(match => match.Groups[1].Value)
            .ToHashSet(StringComparer.Ordinal);
        var spawnedBloons = Regex.Matches(log, "Lifecycle Sample observed bloon\\.spawned id=(\\d+)")
            .Select(match => match.Groups[1].Value)
            .ToHashSet(StringComparer.Ordinal);
        // Popping runs at the lethal hit and popped on the next manager update,
        // so other pops can interleave: pair a popped bloon with its own popping.
        var poppedBloon = Regex.Matches(log, "Lifecycle Sample observed bloon\\.popped id=(\\d+)")
            .FirstOrDefault(popped => Regex.Match(log[..popped.Index],
                "Lifecycle Sample observed bloon\\.popping id=" + popped.Groups[1].Value + "\\b").Success)
            ?? Match.Empty;
        var poppingBloon = poppedBloon.Success
            ? Regex.Match(log[..poppedBloon.Index],
                "Lifecycle Sample observed bloon\\.popping id=(" + poppedBloon.Groups[1].Value + ")\\b")
            : Match.Empty;
        var leakingBloon = Regex.Match(log, "Lifecycle Sample observed bloon\\.leaking id=(\\d+)");
        var leakedBloon = Regex.Match(log, "Lifecycle Sample observed bloon\\.leaked id=(\\d+)");
        var bloonActions = spawnedBloons.Count > 0 &&
            spawnedBloons.All(spawningBloons.Contains) &&
            poppingBloon.Success && poppedBloon.Success && leakingBloon.Success && leakedBloon.Success &&
            poppingBloon.Index < poppedBloon.Index && leakingBloon.Index < leakedBloon.Index &&
            poppingBloon.Groups[1].Value == poppedBloon.Groups[1].Value &&
            leakingBloon.Groups[1].Value == leakedBloon.Groups[1].Value &&
            // Leaks are always logged; the sample asserts their spawn identity in Lua.
            spawnedBloons.Contains(poppedBloon.Groups[1].Value) &&
            log.Contains("Lifecycle Sample confirmed popped bloon became stale", StringComparison.Ordinal) &&
            log.Contains("Lifecycle Sample confirmed leaked bloon became stale", StringComparison.Ordinal);
        var bloonHealthMutation = Regex.Match(
            log,
            "Lifecycle Sample mutated bloon\\.health ([0-9.]+)->([0-9.]+)");
        var directProperties = towerSellPriceMutation.Success && bloonHealthMutation.Success;
        static bool LifetimeObserved(string log, string kind)
        {
            var created = Regex.Matches(log, "Lifecycle Sample observed " + kind + "\\.created id=(\\d+)");
            var destroyed = Regex.Matches(log, "Lifecycle Sample observed " + kind + "\\.destroyed id=(\\d+)");
            return destroyed.Any(end => created.Any(start =>
                    start.Index < end.Index && start.Groups[1].Value == end.Groups[1].Value)) &&
                log.Contains("Lifecycle Sample confirmed destroyed " + kind + " became stale", StringComparison.Ordinal);
        }
        var attackProjectileLifetimes = LifetimeObserved(log, "attack") && LifetimeObserved(log, "projectile");
        var popCancellation = Regex.Matches(log, "Lifecycle Sample cancelled bloon\\.popping id=(\\d+) health=")
            .Select(match => match.Groups[1].Value)
            .Any(id =>
                log.Contains("Lifecycle Sample confirmed cancelled pop kept bloon id=" + id, StringComparison.Ordinal) &&
                log.Contains("Lifecycle Sample popped previously cancelled bloon id=" + id, StringComparison.Ordinal)) &&
            log.Contains("bloon.popping:cancelled", StringComparison.Ordinal);
        var cancelledSpawnIds = Regex.Matches(log, "Lifecycle Sample cancelled bloon\\.spawning id=(\\d+)")
            .Select(match => match.Groups[1].Value)
            .ToList();
        // The game reuses freed bloon memory, so ids recur. The sample asserts
        // that no cancelled bloon reaches bloon.spawned; a failed assertion is a
        // runtime-log error, which fails the drive.
        var spawnCancellation = cancelledSpawnIds.Count > 0 &&
            cancelledSpawnIds.All(id =>
                log.Contains("Lifecycle Sample confirmed cancelled spawn stale id=" + id, StringComparison.Ordinal)) &&
            log.Contains("Lifecycle Sample round ended after cancelled spawns", StringComparison.Ordinal) &&
            log.Contains("bloon.spawning:cancelled", StringComparison.Ordinal);
        var identityCash = false;
        var placedIdentity = Regex.Match(log,
            "Identity test tower\\.placed id=(\\d+) type=DartMonkey tiers=0,0");
        if (placedIdentity.Success)
        {
            var towerId = placedIdentity.Groups[1].Value;
            var rounds = Regex.Matches(log, "Identity test round number=(\\d+)")
                .Select(match => int.Parse(match.Groups[1].Value)).ToList();
            var bloonTypes = Regex.Matches(log, "Identity test bloon type=(\\w+)")
                .Select(match => match.Groups[1].Value).ToHashSet(StringComparer.Ordinal);
            var cashSet = Regex.Match(log, "Identity test set match cash (\\d+)->(\\d+)->(\\d+)");
            identityCash =
                log.Contains("Identity test tower.upgraded id=" + towerId + " type=DartMonkey tiers=1,0",
                    StringComparison.Ordinal) &&
                log.Contains("Identity test tower.upgraded id=" + towerId + " type=DartMonkey tiers=1,1",
                    StringComparison.Ordinal) &&
                rounds.Count >= 2 && rounds[0] >= 1 &&
                rounds.Zip(rounds.Skip(1)).All(pair => pair.Second == pair.First + 1) &&
                bloonTypes.Count >= 2 && bloonTypes.Contains("Red") && !bloonTypes.Contains("nil") &&
                cashSet.Success && cashSet.Groups[1].Value == cashSet.Groups[3].Value &&
                long.Parse(cashSet.Groups[2].Value) == long.Parse(cashSet.Groups[1].Value) + 500 &&
                log.Contains("Identity test observed cash.changed cash=" + cashSet.Groups[2].Value,
                    StringComparison.Ordinal) &&
                log.Contains("Identity test observed cash.changed cash=" + cashSet.Groups[1].Value,
                    StringComparison.Ordinal);
        }
        // A cancelled purchase must not reach placement; the next purchase must
        // place normally with exactly one tower.placing for its identity.
        var cancelledPlacement = Regex.Match(log, "Lifecycle Sample cancelled tower\\.placing id=(\\d+)");
        var placementCancellation = false;
        if (cancelledPlacement.Success &&
            log.Contains("tower.placing:cancelled", StringComparison.Ordinal))
        {
            var afterCancel = log[(cancelledPlacement.Index + cancelledPlacement.Length)..];
            var nextPurchase = Regex.Match(afterCancel,
                "Lifecycle Sample observed tower\\.placing id=(\\d+) purchase=true");
            var firstPlaced = Regex.Match(afterCancel, "Lifecycle Sample observed tower\\.placed id=(\\d+)");
            if (nextPurchase.Success && firstPlaced.Success && nextPurchase.Index < firstPlaced.Index &&
                nextPurchase.Groups[1].Value == firstPlaced.Groups[1].Value)
            {
                var id = firstPlaced.Groups[1].Value;
                placementCancellation = Regex.Matches(afterCancel[..firstPlaced.Index],
                    "Lifecycle Sample observed tower\\.placing id=" + id + " ").Count == 1;
            }
        }
        if ((expectDisabled ? disabledProfileClean : lifecycleReady) &&
            (!expectMatch || matchReady) && (!expectMatchExit || matchExited) &&
            (!expectRound || (matchReady && roundCompleted)) && (!expectCash || (matchReady && cashChanged)) &&
            (!expectLivesLoss || (matchReady && livesLifecycle)) &&
            (!expectLivesCancel || (matchReady && livesCancellation)) &&
            (!expectLivesMutation || (matchReady && livesMutation)) &&
            (!expectTowerPopCount || (matchReady && towerPopCountMutation.Success)) &&
            (!expectTowerCancellation || (matchReady && towerCancellation)) &&
            (!expectDirectProperties || (matchReady && directProperties)) &&
            (!expectIdentityCash || (matchReady && identityCash)) &&
            (!expectPopCancellation || (matchReady && popCancellation)) &&
            (!expectSpawnCancellation || (matchReady && spawnCancellation)) &&
            (!expectTowerActions || (matchReady && towerActions)) &&
            (!expectBloonActions || (matchReady && bloonActions)) &&
            (!expectAttackProjectile || (matchReady && attackProjectileLifetimes)) &&
            (!expectPlacementCancellation || (matchReady && placementCancellation)))
        {
            Console.WriteLine("LIVE_SMOKE_PASS");
            Console.WriteLine(expectDisabled
                ? "The disabled sample was excluded from the next modded launch."
                : expectPlacementCancellation
                ? "Lua cancelled a tower purchase through the native rejection path, then a later purchase placed normally in BTD5."
                : expectAttackProjectile
                ? "Lua observed attack and projectile creation, destruction, and stale wrappers in BTD5."
                : expectBloonActions
                ? "Lua observed bloon pre/post spawn, pop, and leak notifications in BTD5."
                : expectTowerActions
                ? "Lua observed tower pre-placement, placement, upgrade, and sale notifications in BTD5."
                : expectLivesCancel
                ? "Lua cancelled a verified lives loss and the native write did not occur in BTD5."
                : expectLivesMutation
                ? "Lua replaced a verified lives loss and lives.changed observed the mutated value in BTD5."
                : expectTowerPopCount
                ? "Lua changed a live tower pop count through its validated wrapper setter in BTD5."
                : expectTowerCancellation
                ? "Lua cancelled verified tower upgrade and sale attempts before side effects in BTD5."
                : expectDirectProperties
                ? "Lua changed live tower sell price and bloon health through validated wrappers in BTD5."
                : expectIdentityCash
                ? "Lua read tower, bloon, and round identity and set match cash through the native helpers in BTD5."
                : expectPopCancellation
                ? "Lua cancelled a lethal hit; the bloon kept its health and popped on a later hit in BTD5."
                : expectSpawnCancellation
                ? "Lua cancelled round spawns; the bloons never joined the game and the round ended in BTD5."
                : expectLivesLoss
                ? "Lua observed a verified lives loss after match entry in BTD5."
                : expectCashAction
                ? "Lua observed a cash update after match entry in BTD5."
                : expectCash
                ? "Lua observed a complete cash notification lifecycle in BTD5."
                : expectRound
                    ? "Lua observed a complete round lifecycle in BTD5."
                : expectMatchExit
                    ? "Lua observed the complete match entry and exit lifecycle in BTD5."
                : expectMatch
                    ? "Lua observed match.starting and match.started in BTD5."
                    : "Lua on_load, on_ready, and deterministic timers executed in BTD5.");
            return 0;
        }
    }
    return Fail(expectDisabled
        ? "Timed out waiting for a clean runtime startup with the sample disabled."
        : expectPlacementCancellation
        ? "Timed out waiting for a cancelled tower purchase followed by a normal placement; try to place a tower twice."
        : expectAttackProjectile
        ? "Timed out waiting for attack and projectile lifetime evidence; place a tower, let it fire, then sell it."
        : expectBloonActions
        ? "Timed out waiting for bloon spawn, pop, leak, and Lua event evidence."
        : expectTowerActions
        ? "Timed out waiting for tower pre-placement, placement, upgrade, sale, and Lua event evidence."
        : expectLivesCancel
        ? "Timed out waiting for a cancelled lives loss with no post-change notification."
        : expectLivesMutation
        ? "Timed out waiting for a mutated lives loss and matching post-change notification."
        : expectTowerPopCount
        ? "Timed out waiting for a placed tower and validated pop-count mutation."
        : expectTowerCancellation
        ? "Timed out waiting for cancelled tower upgrade and sale attempts."
        : expectDirectProperties
        ? "Timed out waiting for live tower sell-price and bloon-health mutations."
        : expectIdentityCash
        ? "Timed out waiting for identity readings on a placed and upgraded tower, two rounds, two bloon types, and a cash set."
        : expectPopCancellation
        ? "Timed out waiting for a cancelled bloon pop that kept the bloon alive and a later normal pop of it."
        : expectSpawnCancellation
        ? "Timed out waiting for cancelled round spawns that went stale without spawning and a round end."
        : expectLivesLoss
        ? "Timed out waiting for a verified lives loss and Lua event evidence."
        : expectCashAction
        ? "Timed out waiting for a cash update after match entry and Lua event evidence."
        : expectCash
        ? "Timed out waiting for a cash update and Lua event evidence."
        : expectRound
            ? "Timed out waiting for a complete round lifecycle and Lua event evidence."
        : expectMatchExit
            ? "Timed out waiting for a match to start, exit, and emit complete Lua lifecycle evidence."
        : expectMatch
        ? "Timed out waiting for a match to start and emit Lua event evidence."
        : "Timed out waiting for live Lua lifecycle evidence.");
}
catch (Exception exception)
{
    return Fail(exception.ToString());
}
finally
{
    if (gameProcess is not null)
    {
        gameProcess.Refresh();
        if (!gameProcess.HasExited)
        {
            gameProcess.Kill();
            await gameProcess.WaitForExitAsync();
        }
        gameProcess.Dispose();
    }
}

static int Fail(string message)
{
    Console.Error.WriteLine(message);
    return 1;
}

static string Format(IReadOnlyList<string> values) =>
    values.Count == 0 ? string.Empty : " (" + string.Join(", ", values) + ")";

static int CountOccurrences(string value, string search)
{
    var count = 0;
    var start = 0;
    while ((start = value.IndexOf(search, start, StringComparison.Ordinal)) >= 0)
    {
        count++;
        start += search.Length;
    }
    return count;
}
