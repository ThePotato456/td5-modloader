using System.Diagnostics;
using System.Text.Json;

namespace BTD5ModLoader.Manager.Core;

public sealed record GameProcessRequest(
    string FileName,
    string? WorkingDirectory,
    bool UseShellExecute,
    IReadOnlyDictionary<string, string> Environment,
    IReadOnlyList<string>? Arguments = null);

public interface IGameProcessLauncher
{
    int Start(GameProcessRequest request);
    bool IsGameRunning();
    long GetProcessStartTime(int processId);
    Task<int?> AwaitRuntimeAsync(string ticketPath, CancellationToken cancellationToken);
    Task<int?> AwaitGameProcessAsync(string gameDirectory, DateTime startedAfter, CancellationToken cancellationToken);
}

public sealed class SystemGameProcessLauncher : IGameProcessLauncher
{
    public long GetProcessStartTime(int processId)
    {
        using var process = Process.GetProcessById(processId);
        return process.StartTime.ToUniversalTime().ToFileTimeUtc();
    }
    public async Task<int?> AwaitGameProcessAsync(
        string gameDirectory, DateTime startedAfter, CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow.AddSeconds(30);
        while (DateTimeOffset.UtcNow < deadline)
        {
            foreach (var process in Process.GetProcessesByName("BTD5-Win"))
            {
                using (process)
                {
                    if (process.StartTime.ToUniversalTime() >= startedAfter &&
                        string.Equals(process.MainModule?.FileName,
                            Path.Combine(gameDirectory, "BTD5-Win.exe"), StringComparison.OrdinalIgnoreCase))
                        return process.Id;
                }
            }
            await Task.Delay(100, cancellationToken).ConfigureAwait(false);
        }
        return null;
    }
    public bool IsGameRunning()
    {
        var processes = Process.GetProcessesByName("BTD5-Win");
        try { return processes.Length != 0; }
        finally { foreach (var process in processes) process.Dispose(); }
    }

    public async Task<int?> AwaitRuntimeAsync(string ticketPath, CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow.AddSeconds(30);
        while (DateTimeOffset.UtcNow < deadline)
        {
            cancellationToken.ThrowIfCancellationRequested();
            try
            {
                if (File.Exists(ticketPath + ".started") && int.TryParse(
                    await File.ReadAllTextAsync(ticketPath + ".started", cancellationToken)
                        .ConfigureAwait(false), out var processId)) return processId;
            }
            catch (IOException) { /* The acknowledgment may still be being flushed. */ }
            await Task.Delay(100, cancellationToken).ConfigureAwait(false);
        }
        return null;
    }

    public int Start(GameProcessRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        var startInfo = new ProcessStartInfo
        {
            FileName = request.FileName,
            UseShellExecute = request.UseShellExecute
        };
        if (request.WorkingDirectory is not null)
        {
            startInfo.WorkingDirectory = request.WorkingDirectory;
        }
        if (!request.UseShellExecute)
        {
            foreach (var (name, value) in request.Environment)
            {
                startInfo.Environment[name] = value;
            }
        }
        foreach (var argument in request.Arguments ?? []) startInfo.ArgumentList.Add(argument);
        using var process = Process.Start(startInfo) ??
            throw new InvalidOperationException("Windows did not start the game process.");
        return process.Id;
    }
}

public sealed record GameLaunchResult(
    bool Success,
    string Message,
    int? ProcessId,
    IReadOnlyList<string> Errors);

public enum ReadinessAction
{
    ChooseGame,
    InstallLoader,
    RepairLoader,
    UpdateLoader,
    RecoverLoader,
    RestoreReleaseFiles,
    SelectProfile,
    InstallMod,
    ConfigureMod,
    ResolveDependencies,
    ReviewProfile
}

public sealed record ReadinessProblem(
    string Code,
    string Message,
    string Correction,
    ReadinessAction Action,
    string? ModId = null);

public sealed record LoaderStatusResult(
    bool GameSupported,
    bool LoaderVerified,
    bool ProfileValid,
    string? BuildId,
    string RuntimeState,
    IReadOnlyList<ReadinessProblem> Issues)
{
    public IReadOnlyList<string> Problems => Issues.Select(issue => issue.Message).ToArray();
}

public sealed class GameLaunchService
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };
    private readonly string managerStateRoot;
    private readonly IReadOnlyList<KnownGameBuild> knownBuilds;
    private readonly IGameProcessLauncher processLauncher;
    private readonly string artifactDirectory;
    private readonly SteamLaunchTarget? steamTarget;

    public GameLaunchService(
        string managerStateRoot,
        IReadOnlyList<KnownGameBuild>? knownBuilds = null,
        IGameProcessLauncher? processLauncher = null,
        string? artifactDirectory = null,
        SteamLaunchTarget? steamTarget = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(managerStateRoot);
        this.managerStateRoot = Path.GetFullPath(managerStateRoot);
        this.knownBuilds = knownBuilds ?? GameDiscovery.SupportedBuilds;
        this.processLauncher = processLauncher ?? new SystemGameProcessLauncher();
        this.artifactDirectory = Path.GetFullPath(artifactDirectory ?? AppContext.BaseDirectory);
        this.steamTarget = steamTarget ?? LaunchIsolation.DiscoverSteamTarget();
    }

    public async Task<LoaderStatusResult> GetStatusAsync(
        string gameDirectory,
        string profileName,
        CancellationToken cancellationToken = default)
    {
        var issues = new List<ReadinessProblem>();
        var game = await GameDiscovery.ValidateAsync(
            gameDirectory, knownBuilds, cancellationToken).ConfigureAwait(false);
        if (!game.Supported)
        {
            issues.Add(new(
                "game.unsupported",
                game.Error ?? "Game validation failed.",
                "Choose the supported Steam Win32 game folder in Options.",
                ReadinessAction.ChooseGame));
        }
        LoaderHealthResult? loader = null;
        if (game.Supported)
        {
            loader = await new LoaderInstallationService(managerStateRoot, knownBuilds)
                .InspectAsync(gameDirectory, artifactDirectory, cancellationToken).ConfigureAwait(false);
            if (loader.State != LoaderHealthState.Healthy)
            {
                issues.Add(LoaderIssue(loader));
            }
        }
        ProfileValidationResult? profile = null;
        if (game.BuildId is not null)
        {
            if (await new ProfileService(managerStateRoot).LoadAsync(profileName, cancellationToken)
                    .ConfigureAwait(false) is null)
            {
                issues.Add(new(
                    "profile.missing",
                    "The selected profile no longer exists.",
                    "Choose or create a profile.",
                    ReadinessAction.SelectProfile));
            }
            else
            {
                profile = await new ProfileModService(managerStateRoot, game.BuildId)
                    .ValidateAsync(profileName, cancellationToken).ConfigureAwait(false);
                issues.AddRange(profile.Errors.Select(ProfileIssue));
            }
        }
        return new(
            game.Supported,
            loader?.State == LoaderHealthState.Healthy,
            profile?.Valid == true,
            game.BuildId,
            await ReadLatestRuntimeStateAsync(cancellationToken).ConfigureAwait(false),
            issues);
    }

    public async Task<GameLaunchResult> LaunchModdedAsync(
        string gameDirectory,
        string profileName,
        bool offlineRiskAcknowledged,
        CancellationToken cancellationToken = default)
    {
        try
        {
            using var launchLock = LaunchIsolation.Acquire(gameDirectory);
            if (processLauncher.IsGameRunning())
                return new(false, "Close the running game before starting another launch.", null, []);
            return await LaunchModdedCoreAsync(gameDirectory, profileName, offlineRiskAcknowledged,
                cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return new(false, "Launch isolation could not be acquired. Another launch may be in progress.",
                null, [exception.Message]);
        }
    }

    private async Task<GameLaunchResult> LaunchModdedCoreAsync(
        string gameDirectory,
        string profileName,
        bool offlineRiskAcknowledged,
        CancellationToken cancellationToken = default)
    {
        if (!offlineRiskAcknowledged)
        {
            return new(
                false,
                "Modded launch requires acknowledging that network enforcement is not implemented yet.",
                null,
                ["Use Steam Offline Mode and avoid all online, multiplayer, ranked, and leaderboard features."]);
        }
        var status = await GetStatusAsync(gameDirectory, profileName, cancellationToken)
            .ConfigureAwait(false);
        if (!status.GameSupported || !status.LoaderVerified || !status.ProfileValid || status.BuildId is null)
        {
            return new(false, "Modded launch was blocked by validation.", null, status.Problems);
        }

        var profileService = new ProfileService(managerStateRoot);
        var profile = await profileService.LoadAsync(profileName, cancellationToken).ConfigureAwait(false) ??
            throw new InvalidOperationException("The profile does not exist.");
        var validation = await new ProfileModService(managerStateRoot, status.BuildId)
            .ValidateAsync(profileName, cancellationToken).ConfigureAwait(false);
        string? handoffPath = null;
        var gamePath = Path.Combine(Path.GetFullPath(gameDirectory), "BTD5-Win.exe");
        try
        {
            var throughSteam = steamTarget is not null && string.Equals(
                Path.GetFullPath(gameDirectory), Path.GetFullPath(steamTarget.GameDirectory),
                StringComparison.OrdinalIgnoreCase);
            var startedAfter = DateTime.UtcNow;
            _ = processLauncher.Start(new(
                throughSteam ? steamTarget!.SteamExecutable : gamePath,
                Path.GetDirectoryName(gamePath),
                false,
                throughSteam ? new Dictionary<string, string>() : new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
                {
                    ["SteamAppId"] = "306020",
                    ["SteamGameId"] = "306020"
                },
                throughSteam ? ["-applaunch", "306020"] : []));
            var observedProcessId = await processLauncher.AwaitGameProcessAsync(gameDirectory, startedAfter,
                cancellationToken).ConfigureAwait(false) ?? throw new InvalidOperationException("The intended game process did not start.");
            handoffPath = await WriteRuntimeHandoffAsync(gameDirectory, observedProcessId,
                processLauncher.GetProcessStartTime(observedProcessId), profile, status.BuildId,
                validation.OrderedPackages, cancellationToken).ConfigureAwait(false);
            var processId = await processLauncher.AwaitRuntimeAsync(handoffPath, cancellationToken)
                .ConfigureAwait(false) ?? throw new InvalidOperationException(
                    "The runtime did not acknowledge the launch ticket. Check startup logs; no reusable intent remains.");
            if (processId != observedProcessId) throw new InvalidOperationException("Launch acknowledgment came from a different process.");
            await profileService.RecordLaunchAsync(
                profileName, "modded", true, $"Started process {processId}.", cancellationToken)
                .ConfigureAwait(false);
            return new(true, "Modded game process started.", processId, []);
        }
        catch (Exception exception) when (exception is InvalidOperationException or IOException or System.ComponentModel.Win32Exception)
        {
            await profileService.RecordLaunchAsync(
                profileName, "modded", false, exception.Message, cancellationToken).ConfigureAwait(false);
            return new(false, "Modded launch could not be confirmed.", null, [exception.Message]);
        }
        finally
        {
            if (handoffPath is not null)
            {
                File.Delete(handoffPath);
                File.Delete(handoffPath + ".claimed");
                File.Delete(handoffPath + ".started");
                File.Delete(handoffPath + ".started.tmp");
            }
        }
    }

    public async Task<GameLaunchResult> LaunchVanillaAsync(
        string? profileName = null,
        CancellationToken cancellationToken = default)
    {
        try
        {
            if (steamTarget is null)
                return new(false, "The registered Steam installation could not be identified.", null, []);
            using var launchLock = LaunchIsolation.Acquire(steamTarget.GameDirectory);
            if (processLauncher.IsGameRunning())
                return new(false, "Close the running game before starting another launch.", null, []);
            var game = await GameDiscovery.ValidateAsync(steamTarget.GameDirectory, knownBuilds,
                cancellationToken).ConfigureAwait(false);
            if (!game.Supported)
                return new(false, "The registered Steam game could not be validated.", null, [game.Error ?? "Unknown build."]);
            var health = await new LoaderInstallationService(managerStateRoot, knownBuilds)
                .InspectAsync(steamTarget.GameDirectory, artifactDirectory, cancellationToken).ConfigureAwait(false);
            if (health.State is not (LoaderHealthState.Healthy or LoaderHealthState.NotInstalled))
                return new(false, "Vanilla isolation requires the current verified loader or no loader. Repair or uninstall it first.",
                    null, [health.Message]);
            if (health.State == LoaderHealthState.Healthy && !RuntimeCapabilities.HasLaunchIsolation(
                Path.Combine(steamTarget.GameDirectory, "btd5loader_runtime.dll")))
                return new(false, "This loader predates vanilla isolation. Update or uninstall it before a vanilla launch.", null, []);
            var startedAfter = DateTime.UtcNow;
            _ = processLauncher.Start(new(
                steamTarget.SteamExecutable,
                null,
                false,
                new Dictionary<string, string>(), ["-applaunch", "306020"]));
            var processId = await processLauncher.AwaitGameProcessAsync(steamTarget.GameDirectory, startedAfter,
                cancellationToken).ConfigureAwait(false) ?? throw new InvalidOperationException(
                    "Steam did not start the verified game installation.");
            if (!string.IsNullOrWhiteSpace(profileName))
            {
                await new ProfileService(managerStateRoot).RecordLaunchAsync(
                    profileName, "vanilla", true, $"Started process {processId}.", cancellationToken)
                    .ConfigureAwait(false);
            }
            return new(true, "Vanilla game started with mod loading excluded.", processId, []);
        }
        catch (Exception exception) when (exception is InvalidOperationException or IOException or UnauthorizedAccessException or System.ComponentModel.Win32Exception)
        {
            if (!string.IsNullOrWhiteSpace(profileName))
            {
                await new ProfileService(managerStateRoot).RecordLaunchAsync(
                    profileName, "vanilla", false, exception.Message, cancellationToken)
                    .ConfigureAwait(false);
            }
            return new(false, "Vanilla launch could not be confirmed.", null, [exception.Message]);
        }
    }

    public Task<IReadOnlyList<string>> ReadRuntimeLogAsync(
        int maximumLines = 500,
        CancellationToken cancellationToken = default) =>
        RuntimeLogReader.ReadAsync(Path.Combine(managerStateRoot, "logs", "runtime.jsonl"),
            maximumLines, cancellationToken);
    public async Task ClearRuntimeLogAsync(CancellationToken cancellationToken = default)
    {
        var path = Path.Combine(managerStateRoot, "logs", "runtime.jsonl");
        if (!File.Exists(path))
        {
            return;
        }

        var stream = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Write,
            FileShare.ReadWrite | FileShare.Delete,
            4096,
            FileOptions.Asynchronous);
        await using (stream.ConfigureAwait(false))
        {
            stream.SetLength(0);
            await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
        }
    }

    private async Task<string> WriteRuntimeHandoffAsync(
        string gameDirectory,
        int processId,
        long processStartedAt,
        ModProfile profile,
        string selectedBuildId,
        IReadOnlyList<ModPackageInfo> orderedPackages,
        CancellationToken cancellationToken)
    {
        var profileEntries = profile.Mods.ToDictionary(mod => mod.Id, StringComparer.Ordinal);
        var issuedAt = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        var document = new
        {
            schemaVersion = 1,
            launchProtocol = 1,
            mode = "modded",
            processId,
            processStartedAt = processStartedAt.ToString(System.Globalization.CultureInfo.InvariantCulture),
            issuedAt,
            expiresAt = issuedAt + 60,
            gameDirectory = Path.GetFullPath(gameDirectory),
            dataRoot = managerStateRoot,
            profile = profile.Name,
            buildId = selectedBuildId,
            mods = orderedPackages.Select(package => new
            {
                id = package.Id,
                version = package.Version,
                archivePath = package.PackagePath,
                configuration = profileEntries[package.Id!].Configuration.ToDictionary(
                    value => value.Key,
                    value => value.Value.ValueKind == JsonValueKind.String
                        ? value.Value.GetString() ?? string.Empty
                        : value.Value.GetRawText(),
                    StringComparer.Ordinal)
            }).ToArray()
        };
        var identity = processId.ToString(System.Globalization.CultureInfo.InvariantCulture) + "-" +
            processStartedAt.ToString(System.Globalization.CultureInfo.InvariantCulture);
        var path = Path.Combine(Path.GetFullPath(gameDirectory), ".btd5ml-launch", identity + ".json");
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + ".btd5ml-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            await File.WriteAllTextAsync(
                temporary, JsonSerializer.Serialize(document, JsonOptions), cancellationToken)
                .ConfigureAwait(false);
            File.Move(temporary, path, false);
        }
        finally
        {
            File.Delete(temporary);
        }
        return path;
    }

    private async Task<string> ReadLatestRuntimeStateAsync(CancellationToken cancellationToken)
    {
        foreach (var line in (await ReadRuntimeLogAsync(200, cancellationToken).ConfigureAwait(false)).Reverse())
        {
            try
            {
                using var document = JsonDocument.Parse(line);
                var root = document.RootElement;
                if (root.TryGetProperty("component", out var component) &&
                    component.GetString() == "runtime" &&
                    root.TryGetProperty("message", out var message))
                {
                    return message.GetString() ?? "Unknown";
                }
            }
            catch (JsonException)
            {
                // Ignore an incomplete final log line from a concurrently running game.
            }
        }
        return "No runtime log available";
    }

    private static string FormatItems(string[] items) =>
        items.Length == 0 ? string.Empty : " (" + string.Join(", ", items) + ")";

    private static ReadinessProblem LoaderIssue(LoaderHealthResult loader)
    {
        var files = loader.Files.Where(file => file.State != LoaderFileState.Healthy)
            .Select(file => file.RelativePath).ToArray();
        var message = loader.Message + FormatItems(files);
        return loader.State switch
        {
            LoaderHealthState.NotInstalled => new(
                "loader.not_installed", message, "Open Options and install the loader.",
                ReadinessAction.InstallLoader),
            LoaderHealthState.Repairable => new(
                "loader.repairable", message, "Open Options and repair the loader.",
                ReadinessAction.RepairLoader),
            LoaderHealthState.VersionMismatch => new(
                "loader.version_mismatch", message,
                "Open Options and reinstall the bundled loader version.",
                ReadinessAction.UpdateLoader),
            LoaderHealthState.ArtifactUnavailable => new(
                "loader.release_files_missing", message,
                "Restore the release files beside the manager, then recheck.",
                ReadinessAction.RestoreReleaseFiles),
            _ => new(
                "loader.recovery_required", message,
                "Open Options for the preserved-file recovery details.",
                ReadinessAction.RecoverLoader)
        };
    }

    private static ReadinessProblem ProfileIssue(string message)
    {
        var modId = message.Split(' ', StringSplitOptions.RemoveEmptyEntries).FirstOrDefault();
        if (message.Contains("configuration", StringComparison.OrdinalIgnoreCase) ||
            message.Contains("setting", StringComparison.OrdinalIgnoreCase))
        {
            return new(
                "profile.configuration", message,
                "Select the affected mod and correct its configuration.",
                ReadinessAction.ConfigureMod,
                modId);
        }
        if (message.Contains("not installed or compatible", StringComparison.OrdinalIgnoreCase))
        {
            return new(
                "profile.package_missing", message,
                "Install the required version or select an installed compatible version.",
                ReadinessAction.InstallMod,
                modId);
        }
        if (message.Contains("requires", StringComparison.OrdinalIgnoreCase) ||
            message.Contains("cycle", StringComparison.OrdinalIgnoreCase))
        {
            return new(
                "profile.dependencies", message,
                "Enable compatible dependencies or adjust the profile load order.",
                ReadinessAction.ResolveDependencies,
                modId);
        }
        return new(
            "profile.invalid", message,
            "Review the affected package and profile settings.",
            ReadinessAction.ReviewProfile,
            modId);
    }
}
