using System.Diagnostics;
using BTD5ModLoader.Manager.Core;

if (args.Length is < 4 or > 5 ||
    (args.Length == 5 &&
        !string.Equals(args[4], "--expect-match", StringComparison.Ordinal) &&
        !string.Equals(args[4], "--expect-match-exit", StringComparison.Ordinal)))
{
    Console.Error.WriteLine(
        "Usage: BTD5ModLoader.LiveSmoke <game-directory> <artifact-directory> " +
        "<package> <state-root> [--expect-match|--expect-match-exit]");
    return 2;
}

var gameDirectory = Path.GetFullPath(args[0]);
var artifactDirectory = Path.GetFullPath(args[1]);
var packagePath = Path.GetFullPath(args[2]);
var stateRoot = Path.GetFullPath(args[3]);
var expectMatch = args.Length == 5;
var expectMatchExit = args.Length == 5 &&
    string.Equals(args[4], "--expect-match-exit", StringComparison.Ordinal);
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
    var deadline = DateTimeOffset.UtcNow.AddSeconds(expectMatchExit ? 240 : expectMatch ? 180 : 20);
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
        if (lifecycleReady && (!expectMatch || matchReady) && (!expectMatchExit || matchExited))
        {
            Console.WriteLine("LIVE_SMOKE_PASS");
            Console.WriteLine(expectMatchExit
                ? "Lua observed the complete match entry and exit lifecycle in BTD5."
                : expectMatch
                    ? "Lua observed match.starting and match.started in BTD5."
                    : "Lua on_load, on_ready, and deterministic timers executed in BTD5.");
            return 0;
        }
    }
    return Fail(expectMatchExit
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
