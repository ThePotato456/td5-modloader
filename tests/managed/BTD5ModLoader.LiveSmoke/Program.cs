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
    var installation = await new LoaderInstallationService(stateRoot)
        .InstallAsync(gameDirectory, artifactDirectory);
    if (!installation.Success)
    {
        return Fail("Loader install failed: " + installation.Message + Format(installation.Conflicts));
    }
    var package = await ModPackageService.InstallAsync(
        packagePath, stateRoot, "steam-win32-4.8");
    if (!package.Success || package.Package.Id is null || package.Package.Version is null)
    {
        return Fail("Package install failed: " + package.Message + Format(package.Package.Errors));
    }

    var profiles = new ProfileService(stateRoot);
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
        if (log.Contains("Hello from Lua (launch 1)", StringComparison.Ordinal) &&
            log.Contains("sample.lifecycle:loaded", StringComparison.Ordinal) &&
            log.Contains("mods_loaded_waiting_for_game_ready_hook", StringComparison.Ordinal))
        {
            Console.WriteLine("LIVE_SMOKE_PASS");
            Console.WriteLine("Lua on_load executed inside the stable copied game process.");
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
