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

public sealed record LoaderStatusResult(
    bool GameSupported,
    bool LoaderVerified,
    bool ProfileValid,
    string? BuildId,
    string RuntimeState,
    IReadOnlyList<string> Problems);

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

    public GameLaunchService(
        string managerStateRoot,
        IReadOnlyList<KnownGameBuild>? knownBuilds = null,
        IGameProcessLauncher? processLauncher = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(managerStateRoot);
        this.managerStateRoot = Path.GetFullPath(managerStateRoot);
        this.knownBuilds = knownBuilds ?? GameDiscovery.SupportedBuilds;
        this.processLauncher = processLauncher ?? new SystemGameProcessLauncher();
    }

    public async Task<LoaderStatusResult> GetStatusAsync(
        string gameDirectory,
        string profileName,
        CancellationToken cancellationToken = default)
    {
        var problems = new List<string>();
        var game = await GameDiscovery.ValidateAsync(
            gameDirectory, knownBuilds, cancellationToken).ConfigureAwait(false);
        if (!game.Supported)
        {
            problems.Add(game.Error ?? "Game validation failed.");
        }
        var loader = await new LoaderInstallationService(managerStateRoot, knownBuilds)
            .VerifyAsync(gameDirectory, cancellationToken).ConfigureAwait(false);
        if (!loader.Success)
        {
            problems.Add(loader.Message + FormatItems(loader.Conflicts));
        }
        ProfileValidationResult? profile = null;
        if (game.BuildId is not null)
        {
            profile = await new ProfileModService(managerStateRoot, game.BuildId)
                .ValidateAsync(profileName, cancellationToken).ConfigureAwait(false);
            problems.AddRange(profile.Errors);
        }
        return new(
            game.Supported,
            loader.Success,
            profile?.Valid == true,
            game.BuildId,
            await ReadLatestRuntimeStateAsync(cancellationToken).ConfigureAwait(false),
            problems);
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

    private static string FormatItems(IReadOnlyList<string> items) =>
        items.Count == 0 ? string.Empty : " (" + string.Join(", ", items) + ")";
}
