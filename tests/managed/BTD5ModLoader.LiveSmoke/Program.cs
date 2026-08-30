using System.Diagnostics;
using System.Text.RegularExpressions;
using BTD5ModLoader.Manager.Core;

if (args.Length is < 4 or > 5 ||
    (args.Length == 5 &&
        !string.Equals(args[4], "--expect-match", StringComparison.Ordinal) &&
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
        !string.Equals(args[4], "--expect-tower-actions", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-bloon-actions", StringComparison.Ordinal)))
{
    Console.Error.WriteLine(
        "Usage: BTD5ModLoader.LiveSmoke <game-directory> <artifact-directory> " +
        "<package> <state-root> " +
        "[--expect-match|--expect-match-exit|--expect-round|--expect-cash|" +
        "--expect-cash-action|--expect-lives-loss|--expect-lives-cancel|--expect-lives-mutation|" +
        "--expect-tower-pop-count|--expect-tower-cancellation|--expect-tower-actions|" +
        "--expect-bloon-actions|--expect-direct-properties]");
    return 2;
}

var gameDirectory = Path.GetFullPath(args[0]);
var artifactDirectory = Path.GetFullPath(args[1]);
var packagePath = Path.GetFullPath(args[2]);
var stateRoot = Path.GetFullPath(args[3]);
var expectMatch = args.Length == 5;
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
var expectTowerActions = args.Length == 5 &&
    string.Equals(args[4], "--expect-tower-actions", StringComparison.Ordinal);
var expectBloonActions = args.Length == 5 &&
    string.Equals(args[4], "--expect-bloon-actions", StringComparison.Ordinal);
const string profileName = "Live Smoke";
Process? gameProcess = null;
DateTimeOffset? livesCancellationObservedAt = null;
DateTimeOffset? towerCancellationObservedAt = null;
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
    if (expectLivesCancel || expectLivesMutation || expectTowerPopCount || expectTowerCancellation ||
        expectDirectProperties)
    {
        var profile = profileChange.Profile!;
        var configuredMods = profile.Mods.Select(mod => mod.Id != package.Package.Id
            ? mod
            : mod with
            {
                Configuration = new Dictionary<string, System.Text.Json.JsonElement>(mod.Configuration)
                {
                    [expectLivesCancel
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
    if (File.Exists(logPath))
    {
        File.Delete(logPath);
    }
    var launch = await new GameLaunchService(stateRoot)
        .LaunchModdedAsync(gameDirectory, profileName, true);
    if (!launch.Success || launch.ProcessId is null)
    {
        return Fail("Modded launch failed: " + launch.Message + Format(launch.Errors));
    }
    gameProcess = Process.GetProcessById(launch.ProcessId.Value);
    var deadline = DateTimeOffset.UtcNow.AddSeconds(
        expectMatchExit || expectRound || expectLivesLoss || expectLivesCancel || expectLivesMutation ||
            expectTowerPopCount || expectTowerActions || expectBloonActions
            || expectTowerCancellation || expectDirectProperties
            ? 240
            : expectMatch || expectCash ? 180 : 20);
    while (DateTimeOffset.UtcNow < deadline)
    {
        await Task.Delay(250);
        gameProcess.Refresh();
        if (gameProcess.HasExited)
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
            await using var stream = new FileStream(
                logPath,
                FileMode.Open,
                FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete,
                4096,
                FileOptions.Asynchronous | FileOptions.SequentialScan);
            using var reader = new StreamReader(stream);
            log = await reader.ReadToEndAsync();
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
        var towerActions = placingTower.Success && placedTower.Success && upgradingTower.Success &&
            upgradedTower.Success && sellingTower.Success && soldTower.Success &&
            placingTower.Index < placedTower.Index &&
            upgradingTower.Index < upgradedTower.Index &&
            sellingTower.Index < soldTower.Index &&
            placingTower.Groups[1].Value == placedTower.Groups[1].Value &&
            placedTower.Groups[1].Value == upgradingTower.Groups[1].Value &&
            placedTower.Groups[1].Value == upgradedTower.Groups[1].Value &&
            upgradedTower.Groups[1].Value == sellingTower.Groups[1].Value &&
            placedTower.Groups[1].Value == soldTower.Groups[1].Value &&
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
        var poppingBloon = Regex.Match(log, "Lifecycle Sample observed bloon\\.popping id=(\\d+)");
        var poppedBloon = Regex.Match(log, "Lifecycle Sample observed bloon\\.popped id=(\\d+)");
        var leakingBloon = Regex.Match(log, "Lifecycle Sample observed bloon\\.leaking id=(\\d+)");
        var leakedBloon = Regex.Match(log, "Lifecycle Sample observed bloon\\.leaked id=(\\d+)");
        var bloonActions = spawnedBloons.Count > 0 &&
            spawnedBloons.All(spawningBloons.Contains) &&
            poppingBloon.Success && poppedBloon.Success && leakingBloon.Success && leakedBloon.Success &&
            poppingBloon.Index < poppedBloon.Index && leakingBloon.Index < leakedBloon.Index &&
            poppingBloon.Groups[1].Value == poppedBloon.Groups[1].Value &&
            leakingBloon.Groups[1].Value == leakedBloon.Groups[1].Value &&
            spawnedBloons.Contains(poppedBloon.Groups[1].Value) &&
            spawnedBloons.Contains(leakedBloon.Groups[1].Value) &&
            log.Contains("Lifecycle Sample confirmed popped bloon became stale", StringComparison.Ordinal) &&
            log.Contains("Lifecycle Sample confirmed leaked bloon became stale", StringComparison.Ordinal);
        var bloonHealthMutation = Regex.Match(
            log,
            "Lifecycle Sample mutated bloon\\.health ([0-9.]+)->([0-9.]+)");
        var directProperties = towerSellPriceMutation.Success && bloonHealthMutation.Success;
        if (lifecycleReady && (!expectMatch || matchReady) && (!expectMatchExit || matchExited) &&
            (!expectRound || (matchReady && roundCompleted)) && (!expectCash || (matchReady && cashChanged)) &&
            (!expectLivesLoss || (matchReady && livesLifecycle)) &&
            (!expectLivesCancel || (matchReady && livesCancellation)) &&
            (!expectLivesMutation || (matchReady && livesMutation)) &&
            (!expectTowerPopCount || (matchReady && towerPopCountMutation.Success)) &&
            (!expectTowerCancellation || (matchReady && towerCancellation)) &&
            (!expectDirectProperties || (matchReady && directProperties)) &&
            (!expectTowerActions || (matchReady && towerActions)) &&
            (!expectBloonActions || (matchReady && bloonActions)))
        {
            Console.WriteLine("LIVE_SMOKE_PASS");
            Console.WriteLine(expectBloonActions
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
    return Fail(expectBloonActions
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
