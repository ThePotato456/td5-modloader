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
        !string.Equals(args[4], "--expect-tower-actions", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-bloon-actions", StringComparison.Ordinal)))
{
    Console.Error.WriteLine(
        "Usage: BTD5ModLoader.LiveSmoke <game-directory> <artifact-directory> " +
        "<package> <state-root> " +
        "[--expect-match|--expect-match-exit|--expect-round|--expect-cash|" +
        "--expect-cash-action|--expect-lives-loss|--expect-tower-actions|" +
        "--expect-bloon-actions]");
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
var expectTowerActions = args.Length == 5 &&
    string.Equals(args[4], "--expect-tower-actions", StringComparison.Ordinal);
var expectBloonActions = args.Length == 5 &&
    string.Equals(args[4], "--expect-bloon-actions", StringComparison.Ordinal);
const string profileName = "Live Smoke";
Process? gameProcess = null;
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
        expectMatchExit || expectRound || expectLivesLoss || expectTowerActions || expectBloonActions
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
        var log = await File.ReadAllTextAsync(logPath);
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
        var livesChanged = log.Contains("Lifecycle Sample observed lives.changed", StringComparison.Ordinal);
        var placedTower = Regex.Match(log, "Lifecycle Sample observed tower\\.placed id=(\\d+)");
        var upgradedTower = Regex.Match(log, "Lifecycle Sample observed tower\\.upgraded id=(\\d+)");
        var soldTower = Regex.Match(log, "Lifecycle Sample observed tower\\.sold id=(\\d+)");
        var towerActions = placedTower.Success && upgradedTower.Success && soldTower.Success &&
            placedTower.Groups[1].Value == upgradedTower.Groups[1].Value &&
            placedTower.Groups[1].Value == soldTower.Groups[1].Value &&
            log.Contains("Lifecycle Sample confirmed sold tower became stale", StringComparison.Ordinal);
        var bloonActions = log.Contains("Lifecycle Sample observed bloon.spawned", StringComparison.Ordinal) &&
            log.Contains("Lifecycle Sample observed bloon.popped", StringComparison.Ordinal) &&
            log.Contains("Lifecycle Sample observed bloon.leaked", StringComparison.Ordinal);
        if (lifecycleReady && (!expectMatch || matchReady) && (!expectMatchExit || matchExited) &&
            (!expectRound || (matchReady && roundCompleted)) && (!expectCash || (matchReady && cashChanged)) &&
            (!expectLivesLoss || (matchReady && livesChanged)) &&
            (!expectTowerActions || (matchReady && towerActions)) &&
            (!expectBloonActions || (matchReady && bloonActions)))
        {
            Console.WriteLine("LIVE_SMOKE_PASS");
            Console.WriteLine(expectBloonActions
                ? "Lua observed bloon spawn, pop, and leak notifications in BTD5."
                : expectTowerActions
                ? "Lua observed tower placement, upgrade, and sale notifications in BTD5."
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
        ? "Timed out waiting for tower placement, upgrade, sale, and Lua event evidence."
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
