using System.Diagnostics;
using BTD5ModLoader.Manager.Core;

if (args.Length != 4)
{
    Console.Error.WriteLine(
        "Usage: BTD5ModLoader.LiveSmoke <game-directory> <artifact-directory> <package> <state-root>");
    return 2;
}

var gameDirectory = Path.GetFullPath(args[0]);
var artifactDirectory = Path.GetFullPath(args[1]);
var packagePath = Path.GetFullPath(args[2]);
var stateRoot = Path.GetFullPath(args[3]);
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
    var deadline = DateTimeOffset.UtcNow.AddSeconds(20);
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
        if (log.Contains("Hello from Lua (launch ", StringComparison.Ordinal) &&
            log.Contains("sample.lifecycle:loaded", StringComparison.Ordinal) &&
            log.Contains("game_ready_frame_hook", StringComparison.Ordinal) &&
            log.Contains("Lifecycle Sample is ready", StringComparison.Ordinal) &&
            log.Contains("deterministic timer fired", StringComparison.Ordinal))
        {
            Console.WriteLine("LIVE_SMOKE_PASS");
            Console.WriteLine("Lua on_load, on_ready, and deterministic timers executed in BTD5.");
            return 0;
        }
    }
    return Fail("Timed out waiting for live Lua on_load evidence.");
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
