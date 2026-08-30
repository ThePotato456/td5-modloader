using System.Diagnostics;
using System.Text.Json;

namespace BTD5ModLoader.Manager.Core;

public sealed record GameProcessRequest(
    string FileName,
    string? WorkingDirectory,
    bool UseShellExecute,
    IReadOnlyDictionary<string, string> Environment);

public interface IGameProcessLauncher
{
    int Start(GameProcessRequest request);
}

public sealed class SystemGameProcessLauncher : IGameProcessLauncher
{
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

    public GameLaunchService(
        string managerStateRoot,
        IReadOnlyList<KnownGameBuild>? knownBuilds = null,
        IGameProcessLauncher? processLauncher = null,
        string? artifactDirectory = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(managerStateRoot);
        this.managerStateRoot = Path.GetFullPath(managerStateRoot);
        this.knownBuilds = knownBuilds ?? GameDiscovery.SupportedBuilds;
        this.processLauncher = processLauncher ?? new SystemGameProcessLauncher();
        this.artifactDirectory = Path.GetFullPath(artifactDirectory ?? AppContext.BaseDirectory);
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
        var handoffPath = await WriteRuntimeHandoffAsync(
            profile, status.BuildId, validation.OrderedPackages, cancellationToken).ConfigureAwait(false);
        var gamePath = Path.Combine(Path.GetFullPath(gameDirectory), "BTD5-Win.exe");
        try
        {
            var processId = processLauncher.Start(new(
                gamePath,
                Path.GetDirectoryName(gamePath),
                false,
                new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
                {
                    ["BTD5ML_DATA_ROOT"] = managerStateRoot,
                    ["BTD5ML_ACTIVE_PROFILE"] = handoffPath
                }));
            await profileService.RecordLaunchAsync(
                profileName, "modded", true, $"Started process {processId}.", cancellationToken)
                .ConfigureAwait(false);
            return new(true, "Modded game process started.", processId, []);
        }
        catch (Exception exception) when (exception is InvalidOperationException or IOException)
        {
            await profileService.RecordLaunchAsync(
                profileName, "modded", false, exception.Message, cancellationToken).ConfigureAwait(false);
            return new(false, "Windows could not start the modded game process.", null, [exception.Message]);
        }
    }

    public async Task<GameLaunchResult> LaunchVanillaAsync(
        string? profileName = null,
        CancellationToken cancellationToken = default)
    {
        try
        {
            var processId = processLauncher.Start(new(
                "steam://run/306020",
                null,
                true,
                new Dictionary<string, string>()));
            if (!string.IsNullOrWhiteSpace(profileName))
            {
                await new ProfileService(managerStateRoot).RecordLaunchAsync(
                    profileName, "vanilla", true, $"Started process {processId}.", cancellationToken)
                    .ConfigureAwait(false);
            }
            return new(true, "Vanilla launch was handed to Steam.", processId, []);
        }
        catch (Exception exception) when (exception is InvalidOperationException or IOException)
        {
            if (!string.IsNullOrWhiteSpace(profileName))
            {
                await new ProfileService(managerStateRoot).RecordLaunchAsync(
                    profileName, "vanilla", false, exception.Message, cancellationToken)
                    .ConfigureAwait(false);
            }
            return new(false, "Steam could not be opened.", null, [exception.Message]);
        }
    }

    public async Task<IReadOnlyList<string>> ReadRuntimeLogAsync(
        int maximumLines = 500,
        CancellationToken cancellationToken = default)
    {
        if (maximumLines is < 1 or > 10_000)
        {
            throw new ArgumentOutOfRangeException(nameof(maximumLines));
        }
        var path = Path.Combine(managerStateRoot, "logs", "runtime.jsonl");
        if (!File.Exists(path))
        {
            return [];
        }
        var lines = await File.ReadAllLinesAsync(path, cancellationToken).ConfigureAwait(false);
        return lines.TakeLast(maximumLines).ToArray();
    }

    private async Task<string> WriteRuntimeHandoffAsync(
        ModProfile profile,
        string selectedBuildId,
        IReadOnlyList<ModPackageInfo> orderedPackages,
        CancellationToken cancellationToken)
    {
        var profileEntries = profile.Mods.ToDictionary(mod => mod.Id, StringComparer.Ordinal);
        var document = new
        {
            schemaVersion = 1,
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
        var path = Path.Combine(managerStateRoot, "runtime", "active-profile.json");
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + ".btd5ml-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            await File.WriteAllTextAsync(
                temporary, JsonSerializer.Serialize(document, JsonOptions), cancellationToken)
                .ConfigureAwait(false);
            File.Move(temporary, path, true);
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
