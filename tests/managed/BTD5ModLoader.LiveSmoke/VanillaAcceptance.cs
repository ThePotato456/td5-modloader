using System.Diagnostics;
using BTD5ModLoader.Manager.Core;

internal static class VanillaAcceptance
{
    public static async Task<int> RunAsync(string gameDirectory, string artifacts, string package, string stateRoot,
        bool vanillaOnly = false)
    {
        gameDirectory = Path.GetFullPath(gameDirectory);
        artifacts = Path.GetFullPath(artifacts);
        stateRoot = Path.GetFullPath(stateRoot);
        const string profile = "FIXUP Vanilla Acceptance";
        Process? game = null;
        string? orphanTicket = null;
        try
        {
            var files = new[] { Path.Combine(gameDirectory, "BTD5-Win.exe"), Path.Combine(gameDirectory, "Assets", "BTD5.jet") };
            var before = await Task.WhenAll(files.Select(path => GameDiscovery.HashFileAsync(path)));
            var loader = new LoaderInstallationService(stateRoot);
            if (File.Exists(loader.GetRecordPath(gameDirectory)))
            {
                var verified = await loader.VerifyAsync(gameDirectory);
                if (!verified.Success) throw new InvalidOperationException(verified.Message);
                var removed = await loader.UninstallAsync(gameDirectory);
                if (!removed.Success) throw new InvalidOperationException(removed.Message);
            }
            var installation = await loader.InstallAsync(gameDirectory, artifacts);
            if (!installation.Success) throw new InvalidOperationException(installation.Message);
            var inspected = await ModPackageService.InspectAsync(package, "steam-win32-4.8");
            if (!inspected.Valid || inspected.Id is null || inspected.Version is null)
                throw new InvalidOperationException("Invalid acceptance package.");
            var packages = await ModPackageService.ListInstalledAsync(stateRoot, "steam-win32-4.8");
            if (!packages.Any(value => value.Id == inspected.Id && value.Version == inspected.Version))
            {
                var installed = await ModPackageService.InstallAsync(package, stateRoot, "steam-win32-4.8");
                if (!installed.Success) throw new InvalidOperationException(installed.Message);
            }
            var profiles = new ProfileService(stateRoot);
            if (await profiles.LoadAsync(profile) is null) await profiles.CreateAsync(profile);
            var enabled = await new ProfileModService(stateRoot, "steam-win32-4.8")
                .EnableAsync(profile, inspected.Id, inspected.Version);
            if (!enabled.Success) throw new InvalidOperationException("Cannot enable acceptance sample.");
            var service = new GameLaunchService(stateRoot, artifactDirectory: artifacts);
            var logPath = Path.Combine(stateRoot, "logs", "runtime.jsonl");
            RuntimeLogFiles.Delete(logPath);
            var launchTime = DateTimeOffset.UtcNow;
            if (!vanillaOnly)
            {
                var modded = await service.LaunchModdedAsync(gameDirectory, profile, true);
                if (!modded.Success || modded.ProcessId is null)
                    throw new InvalidOperationException(modded.Message + " " + string.Join("; ", modded.Errors));
                game = Process.GetProcessById(modded.ProcessId.Value);
                _ = game.SafeHandle;
                var processStartedAt = game.StartTime.ToUniversalTime().ToFileTimeUtc();
                Console.WriteLine("Modded runtime acknowledged; waiting for rendered-frame timer.");
                await WaitForAsync(async () => (await RuntimeLogFiles.ReadAsync(logPath)).Contains("deterministic timer fired", StringComparison.Ordinal));
                await CloseNormallyAsync(game);
                var moddedLog = await RuntimeLogFiles.ReadAsync(logPath);
                if (!moddedLog.Contains("Lifecycle Sample shut down cleanly", StringComparison.Ordinal))
                    throw new InvalidOperationException("Missing modded shutdown callback.");
                game.Dispose();
                game = null;
                // Simulate interrupted cleanup after a previous process has exited.
                var installedSample = (await ModPackageService.ListInstalledAsync(stateRoot, "steam-win32-4.8"))
                    .Single(value => value.Id == inspected.Id && value.Version == inspected.Version);
                var orphanPath = Path.Combine(gameDirectory, ".btd5ml-launch",
                    modded.ProcessId.Value.ToString(System.Globalization.CultureInfo.InvariantCulture) + "-" +
                    processStartedAt.ToString(System.Globalization.CultureInfo.InvariantCulture) + ".json");
                var issued = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
                var orphan = new
                {
                    schemaVersion = 1,
                    launchProtocol = 1,
                    mode = "modded",
                    processId = modded.ProcessId.Value,
                    processStartedAt = processStartedAt.ToString(System.Globalization.CultureInfo.InvariantCulture),
                    issuedAt = issued,
                    expiresAt = issued + 60,
                    gameDirectory,
                    dataRoot = stateRoot,
                    profile,
                    buildId = "steam-win32-4.8",
                    mods = new[] { new { id = inspected.Id, version = inspected.Version,
                        archivePath = installedSample.PackagePath,
                        configuration = new Dictionary<string, string> { ["greeting"] = "Hello from Lua" } } }
                };
                await using (var output = new FileStream(orphanPath, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    orphanTicket = orphanPath;
                    await System.Text.Json.JsonSerializer.SerializeAsync(output, orphan);
                }
                Console.WriteLine("Modded game closed normally; launching vanilla through Steam.");
                // Start the vanilla phase with an empty log so only its records are checked.
                RuntimeLogFiles.Delete(logPath);
            }
            var vanilla = await service.LaunchVanillaAsync(profile);
            if (!vanilla.Success || vanilla.ProcessId is null)
                throw new InvalidOperationException(vanilla.Message + " " + string.Join("; ", vanilla.Errors));
            if (DateTimeOffset.UtcNow - launchTime >= TimeSpan.FromSeconds(60))
                throw new InvalidOperationException("The immediate-launch regression exceeded the former fallback window.");
            game = Process.GetProcessById(vanilla.ProcessId.Value);
            _ = game.SafeHandle;
            Console.WriteLine("Vanilla game process observed; checking runtime exclusion.");
            await WaitForAsync(async () => (await RuntimeLogFiles.ReadAsync(logPath)).Contains("vanilla_no_ticket", StringComparison.Ordinal));
            await Task.Delay(2000);
            await CloseNormallyAsync(game);
            var vanillaLog = await RuntimeLogFiles.ReadAsync(logPath);
            if (vanillaLog.Contains("\"component\":\"lua\"", StringComparison.Ordinal) ||
                vanillaLog.Contains("hooks_ready", StringComparison.Ordinal) ||
                vanillaLog.Contains("mods_loaded", StringComparison.Ordinal))
                throw new InvalidOperationException("Vanilla launched Lua or installed game hooks.");
            if (!vanillaOnly)
            {
                if (!File.Exists(orphanTicket) || File.Exists(orphanTicket + ".claimed"))
                    throw new InvalidOperationException("Vanilla consumed the interrupted launch's ticket.");
                game.Dispose();
                game = null;
                RuntimeLogFiles.Delete(logPath);
                var resumed = await service.LaunchModdedAsync(gameDirectory, profile, true);
                if (!resumed.Success || resumed.ProcessId is null)
                    throw new InvalidOperationException("Modded launch did not recover after vanilla.");
                game = Process.GetProcessById(resumed.ProcessId.Value);
                _ = game.SafeHandle;
                await WaitForAsync(async () => (await RuntimeLogFiles.ReadAsync(logPath)).Contains("deterministic timer fired", StringComparison.Ordinal));
                await CloseNormallyAsync(game);
            }
            var after = await Task.WhenAll(files.Select(path => GameDiscovery.HashFileAsync(path)));
            if (!before.SequenceEqual(after)) throw new InvalidOperationException("Game hashes changed.");
            Console.WriteLine(vanillaOnly
                ? "VANILLA_ONLY_PASS: Steam vanilla, no Lua/hooks, unchanged game hashes; immediate modded-to-vanilla gate still requires its separate run."
                : "VANILLA_ISOLATION_PASS: Steam modded then vanilla within 60 seconds, no Lua/hooks, orphan ticket ignored, modded relaunch passed, unchanged game hashes.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception.Message);
            return 1;
        }
        finally
        {
            if (orphanTicket is not null) File.Delete(orphanTicket);
            if (game is not null)
            {
                if (!game.HasExited) { game.Kill(); await game.WaitForExitAsync(); }
                game.Dispose();
            }
        }
    }

    private static async Task CloseNormallyAsync(Process game)
    {
        await WaitForAsync(() => { game.Refresh(); return Task.FromResult(game.MainWindowHandle != IntPtr.Zero); });
        if (!game.CloseMainWindow()) throw new InvalidOperationException("Normal window close failed.");
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
        await game.WaitForExitAsync(timeout.Token);
        if (game.ExitCode != 0) throw new InvalidOperationException("Game did not exit normally.");
    }

    private static async Task WaitForAsync(Func<Task<bool>> condition)
    {
        var deadline = DateTimeOffset.UtcNow.AddSeconds(30);
        while (DateTimeOffset.UtcNow < deadline)
        {
            if (await condition()) return;
            await Task.Delay(100);
        }
        throw new TimeoutException("Acceptance condition was not observed.");
    }
}
