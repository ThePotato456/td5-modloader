using System.Text.Json;

namespace BTD5ModLoader.Manager.Core;

public sealed record ManagerSettings(
    int SchemaVersion,
    string? GameDirectory,
    string? CurrentProfile);

public sealed record ManagerSettingsLoadResult(
    ManagerSettings Settings,
    bool Recovered,
    string? Warning);

public sealed class ManagerSettingsService
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };

    private readonly string settingsPath;

    public ManagerSettingsService(string managerStateRoot)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(managerStateRoot);
        settingsPath = Path.Combine(Path.GetFullPath(managerStateRoot), "manager.json");
    }

    public async Task<ManagerSettingsLoadResult> LoadAsync(
        CancellationToken cancellationToken = default)
    {
        if (!File.Exists(settingsPath))
        {
            return new(Default(), false, null);
        }

        try
        {
            using var stream = File.OpenRead(settingsPath);
            var settings = await JsonSerializer.DeserializeAsync<ManagerSettings>(
                stream, JsonOptions, cancellationToken).ConfigureAwait(false);
            Validate(settings);
            return new(settings!, false, null);
        }
        catch (Exception exception) when (exception is JsonException or InvalidDataException)
        {
            return new(
                Default(),
                true,
                "Manager settings could not be read. The saved file was preserved and no game or profile was selected.");
        }
    }

    public Task SaveAsync(
        ManagerSettings settings,
        CancellationToken cancellationToken = default)
    {
        Validate(settings);
        return WriteAtomicallyAsync(
            JsonSerializer.Serialize(settings, JsonOptions), cancellationToken);
    }

    public async Task<ManagerSettings> SetGameDirectoryAsync(
        string? gameDirectory,
        CancellationToken cancellationToken = default)
    {
        var current = (await LoadAsync(cancellationToken).ConfigureAwait(false)).Settings;
        var normalized = string.IsNullOrWhiteSpace(gameDirectory)
            ? null
            : Path.GetFullPath(gameDirectory).TrimEnd(Path.DirectorySeparatorChar);
        var updated = current with { GameDirectory = normalized };
        await SaveAsync(updated, cancellationToken).ConfigureAwait(false);
        return updated;
    }

    public async Task<ManagerSettings> SetCurrentProfileAsync(
        string? profileName,
        CancellationToken cancellationToken = default)
    {
        var current = (await LoadAsync(cancellationToken).ConfigureAwait(false)).Settings;
        var updated = current with
        {
            CurrentProfile = string.IsNullOrWhiteSpace(profileName) ? null : profileName
        };
        await SaveAsync(updated, cancellationToken).ConfigureAwait(false);
        return updated;
    }

    public static ManagerSettings Default() => new(1, null, null);

    private static void Validate(ManagerSettings? settings)
    {
        if (settings is null || settings.SchemaVersion != 1)
        {
            throw new InvalidDataException("The manager settings version is unsupported.");
        }
        if (settings.GameDirectory is not null &&
            (!Path.IsPathFullyQualified(settings.GameDirectory) || settings.GameDirectory.Any(char.IsControl)))
        {
            throw new InvalidDataException("The saved game directory is invalid.");
        }
        if (settings.CurrentProfile is not null &&
            (settings.CurrentProfile.Length is < 1 or > 64 || settings.CurrentProfile.Any(char.IsControl)))
        {
            throw new InvalidDataException("The saved current profile is invalid.");
        }
    }

    private async Task WriteAtomicallyAsync(
        string contents,
        CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(settingsPath)!);
        var temporary = settingsPath + ".btd5ml-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            await File.WriteAllTextAsync(temporary, contents, cancellationToken).ConfigureAwait(false);
            File.Move(temporary, settingsPath, true);
        }
        finally
        {
            File.Delete(temporary);
        }
    }
}
