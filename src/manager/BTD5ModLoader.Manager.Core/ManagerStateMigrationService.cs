using System.Text.Json;

namespace BTD5ModLoader.Manager.Core;

public sealed record ManagerStateMigrationResult(
    bool Migrated,
    int ProfilesPreserved,
    int InstallationsPreserved,
    int PackagesPreserved,
    string? InferredGameDirectory,
    string? InferredCurrentProfile,
    string? Warning);

public sealed class ManagerStateMigrationService
{
    private const string MigrationId = "revamped-manager-v1";
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };

    private readonly string stateRoot;
    private readonly string markerPath;
    private readonly string backupRoot;

    public ManagerStateMigrationService(string managerStateRoot)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(managerStateRoot);
        stateRoot = Path.GetFullPath(managerStateRoot);
        markerPath = Path.Combine(stateRoot, "migrations", MigrationId + ".json");
        backupRoot = Path.Combine(stateRoot, "backups", MigrationId);
    }

    public async Task<ManagerStateMigrationResult> MigrateAsync(
        CancellationToken cancellationToken = default)
    {
        if (File.Exists(markerPath))
        {
            return new(false, 0, 0, 0, null, null, null);
        }

        var profilePaths = EnumerateStateFiles("profiles");
        var installationPaths = EnumerateStateFiles("installations");
        var packageCount = Directory.Exists(Path.Combine(stateRoot, "packages"))
            ? Directory.EnumerateFiles(
                Path.Combine(stateRoot, "packages"), "*.btd5mod", SearchOption.AllDirectories).Count()
            : 0;

        var profiles = new List<ModProfile>();
        foreach (var path in profilePaths)
        {
            profiles.Add(await ReadProfileAsync(path, cancellationToken).ConfigureAwait(false));
        }

        var installations = new List<LoaderInstallationRecord>();
        foreach (var path in installationPaths)
        {
            installations.Add(await ReadInstallationAsync(path, cancellationToken).ConfigureAwait(false));
        }

        foreach (var path in profilePaths.Concat(installationPaths))
        {
            await PreserveOriginalAsync(path, cancellationToken).ConfigureAwait(false);
        }

        string? inferredProfile = null;
        string? inferredGameDirectory = null;
        var settingsPath = Path.Combine(stateRoot, "manager.json");
        var hadSettings = File.Exists(settingsPath);
        if (!hadSettings)
        {
            inferredProfile = profiles.Count == 1 ? profiles[0].Name : null;
            var existingGameDirectories = installations
                .Select(record => Path.GetFullPath(record.GameDirectory).TrimEnd(Path.DirectorySeparatorChar))
                .Where(Directory.Exists)
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToArray();
            inferredGameDirectory = existingGameDirectories.Length == 1
                ? existingGameDirectories[0]
                : null;
            await new ManagerSettingsService(stateRoot).SaveAsync(
                new(1, inferredGameDirectory, inferredProfile), cancellationToken).ConfigureAwait(false);
        }

        var warnings = new List<string>();
        if (profiles.Count > 1 && !hadSettings)
        {
            warnings.Add("Choose a current profile; multiple existing profiles were preserved.");
        }
        if (installations.Count > 1 && inferredGameDirectory is null && !hadSettings)
        {
            warnings.Add("Choose a game copy; multiple installation records were preserved.");
        }

        var marker = new MigrationMarker(
            1,
            MigrationId,
            DateTimeOffset.UtcNow,
            profiles.Count,
            installations.Count,
            packageCount,
            inferredGameDirectory,
            inferredProfile);
        await WriteAtomicallyAsync(markerPath, JsonSerializer.Serialize(marker, JsonOptions), cancellationToken)
            .ConfigureAwait(false);

        return new(
            true,
            profiles.Count,
            installations.Count,
            packageCount,
            inferredGameDirectory,
            inferredProfile,
            warnings.Count == 0 ? null : string.Join(" ", warnings));
    }

    private string[] EnumerateStateFiles(string directoryName)
    {
        var directory = Path.Combine(stateRoot, directoryName);
        return Directory.Exists(directory)
            ? Directory.EnumerateFiles(directory, "*.json", SearchOption.TopDirectoryOnly)
                .Order(StringComparer.OrdinalIgnoreCase)
                .ToArray()
            : [];
    }

    private static async Task<ModProfile> ReadProfileAsync(
        string path,
        CancellationToken cancellationToken)
    {
        await using var stream = File.OpenRead(path);
        var profile = await JsonSerializer.DeserializeAsync<ModProfile>(
            stream, JsonOptions, cancellationToken).ConfigureAwait(false) ??
            throw new InvalidDataException($"The existing profile is empty: {Path.GetFileName(path)}");
        if (profile.SchemaVersion != 1 || string.IsNullOrWhiteSpace(profile.Name) ||
            profile.Mods is null || profile.LaunchHistory is null || profile.LaunchHistory.Count > 100 ||
            profile.Mods.Any(mod => mod is null || string.IsNullOrWhiteSpace(mod.Id) ||
                string.IsNullOrWhiteSpace(mod.Version) || mod.Order < 0 || mod.Configuration is null) ||
            profile.Mods.Select(mod => mod.Id).Distinct(StringComparer.Ordinal).Count() != profile.Mods.Count)
        {
            throw new InvalidDataException($"The existing profile is invalid: {Path.GetFileName(path)}");
        }
        return profile;
    }

    private static async Task<LoaderInstallationRecord> ReadInstallationAsync(
        string path,
        CancellationToken cancellationToken)
    {
        await using var stream = File.OpenRead(path);
        var record = await JsonSerializer.DeserializeAsync<LoaderInstallationRecord>(
            stream, JsonOptions, cancellationToken).ConfigureAwait(false) ??
            throw new InvalidDataException($"The existing installation record is empty: {Path.GetFileName(path)}");
        if (record.SchemaVersion != 1 || string.IsNullOrWhiteSpace(record.GameDirectory) ||
            string.IsNullOrWhiteSpace(record.BuildId) || record.Files is null || record.Files.Count == 0 ||
            record.Files.Any(file => file is null || string.IsNullOrWhiteSpace(file.RelativePath) ||
                string.IsNullOrWhiteSpace(file.Sha256) || file.Sha256.Length != 64) ||
            record.Files.Select(file => file.RelativePath).Distinct(StringComparer.OrdinalIgnoreCase).Count() !=
                record.Files.Count)
        {
            throw new InvalidDataException(
                $"The existing installation record is invalid: {Path.GetFileName(path)}");
        }
        return record;
    }

    private async Task PreserveOriginalAsync(string source, CancellationToken cancellationToken)
    {
        var relative = Path.GetRelativePath(stateRoot, source);
        var destination = Path.Combine(backupRoot, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        if (File.Exists(destination))
        {
            var sourceBytes = await File.ReadAllBytesAsync(source, cancellationToken).ConfigureAwait(false);
            var backupBytes = await File.ReadAllBytesAsync(destination, cancellationToken).ConfigureAwait(false);
            if (!sourceBytes.AsSpan().SequenceEqual(backupBytes))
            {
                throw new IOException($"The migration backup conflicts with current state: {relative}");
            }
            return;
        }

        var temporary = destination + ".btd5ml-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.Copy(source, temporary, false);
            File.Move(temporary, destination, false);
        }
        finally
        {
            File.Delete(temporary);
        }
    }

    private static async Task WriteAtomicallyAsync(
        string destination,
        string contents,
        CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        var temporary = destination + ".btd5ml-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            await File.WriteAllTextAsync(temporary, contents, cancellationToken).ConfigureAwait(false);
            File.Move(temporary, destination, false);
        }
        finally
        {
            File.Delete(temporary);
        }
    }

    private sealed record MigrationMarker(
        int SchemaVersion,
        string MigrationId,
        DateTimeOffset CompletedAtUtc,
        int ProfilesPreserved,
        int InstallationsPreserved,
        int PackagesPreserved,
        string? InferredGameDirectory,
        string? InferredCurrentProfile);
}
