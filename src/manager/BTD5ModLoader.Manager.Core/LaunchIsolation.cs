using Microsoft.Win32;

namespace BTD5ModLoader.Manager.Core;

public sealed record SteamLaunchTarget(string GameDirectory, string SteamExecutable);

internal static class LaunchIsolation
{
    public static SteamLaunchTarget? DiscoverSteamTarget()
    {
        if (!OperatingSystem.IsWindows())
        {
            return null;
        }
        using var key = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam");
        var root = key?.GetValue("SteamPath") as string;
        if (string.IsNullOrWhiteSpace(root))
            root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam");
        if (!Directory.Exists(root))
        {
            return null;
        }
        var executable = Path.Combine(root, "steam.exe");
        var candidates = GameDiscovery.DiscoverRegisteredGameDirectories(root).ToArray();
        return File.Exists(executable) && candidates.Length == 1
            ? new(candidates[0], executable)
            : null;
    }

    public static FileStream Acquire(string gameDirectory)
    {
        var directory = Path.Combine(gameDirectory, ".btd5ml-launch");
        Directory.CreateDirectory(directory);
        return new FileStream(Path.Combine(directory, "request.lock"), FileMode.OpenOrCreate,
            FileAccess.ReadWrite, FileShare.None);
    }
}
