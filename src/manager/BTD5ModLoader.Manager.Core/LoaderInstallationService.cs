using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace BTD5ModLoader.Manager.Core;

public sealed record InstalledFile(string RelativePath, string Sha256);

public sealed record LoaderInstallationRecord(
    int SchemaVersion,
    string GameDirectory,
    string BuildId,
    string LoaderVersion,
    IReadOnlyList<InstalledFile> Files);

public sealed record InstallationResult(
    bool Success,
    string Message,
    IReadOnlyList<string> Conflicts);

public enum LoaderHealthState
{
    UnsupportedGame,
    NotInstalled,
    Healthy,
    Repairable,
    Conflict,
    ArtifactUnavailable,
    InvalidRecord
}

public enum LoaderFileState
{
    Healthy,
    Missing,
    Modified,
    Foreign,
    SourceUnavailable
}

public sealed record LoaderFileHealth(string RelativePath, LoaderFileState State);

public sealed record LoaderHealthResult(
    LoaderHealthState State,
    string Message,
    string? BuildId,
    IReadOnlyList<LoaderFileHealth> Files);

public sealed class LoaderInstallationService
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    private readonly string stateRoot;
    private readonly IReadOnlyList<KnownGameBuild> knownBuilds;

    public LoaderInstallationService(
        string stateRoot,
        IReadOnlyList<KnownGameBuild>? knownBuilds = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(stateRoot);
        this.stateRoot = Path.GetFullPath(stateRoot);
        this.knownBuilds = knownBuilds ?? GameDiscovery.SupportedBuilds;
    }

    public async Task<InstallationResult> InstallAsync(
        string gameDirectory,
        string artifactDirectory,
        CancellationToken cancellationToken = default)
    {
        var installation = await GameDiscovery.ValidateAsync(
            gameDirectory, knownBuilds, cancellationToken).ConfigureAwait(false);
        if (!installation.Supported || installation.BuildId is null)
        {
            return new(false, installation.Error ?? "Game validation failed.", []);
        }

        var recordPath = GetRecordPath(installation.Directory);
        if (File.Exists(recordPath))
        {
            return new(false, "The loader is already recorded as installed.", []);
        }

        var artifacts = EnumerateArtifacts(artifactDirectory);
        if (artifacts.Count == 0)
        {
            return new(false, "No loader artifacts were found.", []);
        }
        var conflicts = artifacts
            .Select(artifact => artifact.RelativePath)
            .Where(relative => File.Exists(ToGamePath(installation.Directory, relative)))
            .ToArray();
        if (conflicts.Length != 0)
        {
            return new(false, "Existing files conflict with loader installation.", conflicts);
        }

        var prepared = new List<(string RelativePath, string SourcePath, string Sha256)>();
        foreach (var artifact in artifacts)
        {
            cancellationToken.ThrowIfCancellationRequested();
            prepared.Add((
                artifact.RelativePath,
                artifact.SourcePath,
                await GameDiscovery.HashFileAsync(artifact.SourcePath, cancellationToken).ConfigureAwait(false)));
        }

        var installed = new List<InstalledFile>();
        var recordWritten = false;
        try
        {
            foreach (var artifact in prepared)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var target = ToGamePath(installation.Directory, artifact.RelativePath);
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                CopyAtomically(artifact.SourcePath, target);
                var installedFile = new InstalledFile(artifact.RelativePath, artifact.Sha256);
                installed.Add(installedFile);
                var installedHash = await GameDiscovery.HashFileAsync(target, cancellationToken)
                    .ConfigureAwait(false);
                if (!string.Equals(installedHash, artifact.Sha256, StringComparison.OrdinalIgnoreCase))
                {
                    throw new IOException($"The copied loader file failed verification: {artifact.RelativePath}");
                }
            }

            var record = new LoaderInstallationRecord(
                1,
                installation.Directory,
                installation.BuildId,
                ProductInfo.Version,
                installed);
            Directory.CreateDirectory(Path.GetDirectoryName(recordPath)!);
            await WriteAllTextAtomicallyAsync(
                recordPath,
                JsonSerializer.Serialize(record, JsonOptions),
                false,
                cancellationToken).ConfigureAwait(false);
            recordWritten = true;
            var verification = await VerifyAsync(installation.Directory, cancellationToken)
                .ConfigureAwait(false);
            if (!verification.Success)
            {
                throw new IOException("The loader did not pass verification after installation: " +
                    verification.Message);
            }
            return new(true, "Loader installed successfully.", []);
        }
        catch (OperationCanceledException)
        {
            await RollBackCopiesAsync(installation.Directory, installed).ConfigureAwait(false);
            if (recordWritten)
            {
                _ = TryDelete(recordPath);
            }
            throw;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            var preserved = await RollBackCopiesAsync(installation.Directory, installed).ConfigureAwait(false);
            if (recordWritten && !TryDelete(recordPath))
            {
                preserved.Add("installation record");
            }
            var message = preserved.Count == 0
                ? "Loader installation failed and all changes were rolled back."
                : "Loader installation failed. Files changed during rollback were preserved for manual recovery.";
            return new(false, message + " " + exception.Message, preserved);
        }
    }

    public async Task<LoaderHealthResult> InspectAsync(
        string gameDirectory,
        string artifactDirectory,
        CancellationToken cancellationToken = default)
    {
        var installation = await GameDiscovery.ValidateAsync(
            gameDirectory, knownBuilds, cancellationToken).ConfigureAwait(false);
        if (!installation.Supported || installation.BuildId is null)
        {
            return new(
                LoaderHealthState.UnsupportedGame,
                installation.Error ?? "Game validation failed.",
                null,
                []);
        }

        LoaderInstallationRecord? record;
        try
        {
            record = await ReadRecordAsync(gameDirectory, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is JsonException or InvalidDataException)
        {
            return new(
                LoaderHealthState.InvalidRecord,
                "The loader installation record is damaged or unsupported. It was preserved for recovery.",
                installation.BuildId,
                []);
        }

        var artifacts = EnumerateArtifacts(artifactDirectory)
            .ToDictionary(value => value.RelativePath, StringComparer.OrdinalIgnoreCase);
        if (record is null)
        {
            if (artifacts.Count == 0)
            {
                return new(
                    LoaderHealthState.ArtifactUnavailable,
                    "Loader release files were not found beside the manager.",
                    installation.BuildId,
                    []);
            }
            var foreign = artifacts.Keys
                .Where(relative => File.Exists(ToGamePath(installation.Directory, relative)))
                .Select(relative => new LoaderFileHealth(relative, LoaderFileState.Foreign))
                .ToArray();
            return foreign.Length == 0
                ? new(LoaderHealthState.NotInstalled, "The loader is not installed.", installation.BuildId, [])
                : new(
                    LoaderHealthState.Conflict,
                    "Files not owned by this manager conflict with loader installation.",
                    installation.BuildId,
                    foreign);
        }

        if (!string.Equals(record.BuildId, installation.BuildId, StringComparison.Ordinal))
        {
            return new(
                LoaderHealthState.Conflict,
                "The recorded loader build does not match this game build.",
                installation.BuildId,
                []);
        }

        var files = new List<LoaderFileHealth>();
        foreach (var expected in record.Files)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var target = ToGamePath(record.GameDirectory, expected.RelativePath);
            if (!File.Exists(target))
            {
                var sourceAvailable = artifacts.TryGetValue(expected.RelativePath, out var source) &&
                    string.Equals(
                        await GameDiscovery.HashFileAsync(source.SourcePath, cancellationToken).ConfigureAwait(false),
                        expected.Sha256,
                        StringComparison.OrdinalIgnoreCase);
                files.Add(new(
                    expected.RelativePath,
                    sourceAvailable ? LoaderFileState.Missing : LoaderFileState.SourceUnavailable));
                continue;
            }
            var hash = await GameDiscovery.HashFileAsync(target, cancellationToken).ConfigureAwait(false);
            files.Add(new(
                expected.RelativePath,
                string.Equals(hash, expected.Sha256, StringComparison.OrdinalIgnoreCase)
                    ? LoaderFileState.Healthy
                    : LoaderFileState.Modified));
        }

        if (files.Any(file => file.State == LoaderFileState.Modified))
        {
            return new(
                LoaderHealthState.Conflict,
                "Modified loader-owned files were found and will not be overwritten.",
                installation.BuildId,
                files);
        }
        if (files.Any(file => file.State == LoaderFileState.SourceUnavailable))
        {
            return new(
                LoaderHealthState.ArtifactUnavailable,
                "Missing loader files cannot be repaired because matching release files are unavailable.",
                installation.BuildId,
                files);
        }
        if (files.Any(file => file.State == LoaderFileState.Missing))
        {
            return new(
                LoaderHealthState.Repairable,
                "Loader files are missing and can be repaired.",
                installation.BuildId,
                files);
        }
        return new(
            LoaderHealthState.Healthy,
            "The loader is installed and healthy.",
            installation.BuildId,
            files);
    }

    public async Task<InstallationResult> VerifyAsync(
        string gameDirectory,
        CancellationToken cancellationToken = default)
    {
        var record = await ReadRecordAsync(gameDirectory, cancellationToken).ConfigureAwait(false);
        if (record is null)
        {
            return new(false, "No loader installation record exists.", []);
        }

        var installation = await GameDiscovery.ValidateAsync(
            gameDirectory, knownBuilds, cancellationToken).ConfigureAwait(false);
        if (!installation.Supported ||
            !string.Equals(installation.BuildId, record.BuildId, StringComparison.Ordinal))
        {
            return new(false, installation.Error ?? "The recorded game build no longer matches.", []);
        }

        var conflicts = new List<string>();
        foreach (var installed in record.Files)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var target = ToGamePath(record.GameDirectory, installed.RelativePath);
            if (!File.Exists(target) ||
                !string.Equals(
                    await GameDiscovery.HashFileAsync(target, cancellationToken).ConfigureAwait(false),
                    installed.Sha256,
                    StringComparison.OrdinalIgnoreCase))
            {
                conflicts.Add(installed.RelativePath);
            }
        }

        return conflicts.Count == 0
            ? new(true, "Loader installation verified.", [])
            : new(false, "Loader installation has missing or modified files.", conflicts);
    }

    public async Task<InstallationResult> RepairAsync(
        string gameDirectory,
        string artifactDirectory,
        CancellationToken cancellationToken = default)
    {
        var record = await ReadRecordAsync(gameDirectory, cancellationToken).ConfigureAwait(false);
        if (record is null)
        {
            return new(false, "No loader installation record exists.", []);
        }
        var installation = await GameDiscovery.ValidateAsync(
            gameDirectory, knownBuilds, cancellationToken).ConfigureAwait(false);
        if (!installation.Supported ||
            !string.Equals(installation.BuildId, record.BuildId, StringComparison.Ordinal))
        {
            return new(false, installation.Error ?? "The recorded game build no longer matches.", []);
        }
        var artifacts = EnumerateArtifacts(artifactDirectory)
            .ToDictionary(artifact => artifact.RelativePath, StringComparer.OrdinalIgnoreCase);
        var conflicts = new List<string>();
        var pending = new List<(InstalledFile Installed, string SourcePath)>();
        foreach (var installed in record.Files)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var target = ToGamePath(record.GameDirectory, installed.RelativePath);
            if (File.Exists(target))
            {
                var currentHash = await GameDiscovery.HashFileAsync(target, cancellationToken).ConfigureAwait(false);
                if (!string.Equals(currentHash, installed.Sha256, StringComparison.OrdinalIgnoreCase))
                {
                    conflicts.Add(installed.RelativePath);
                }
                continue;
            }
            if (!artifacts.TryGetValue(installed.RelativePath, out var artifact) ||
                !string.Equals(
                    await GameDiscovery.HashFileAsync(artifact.SourcePath, cancellationToken).ConfigureAwait(false),
                    installed.Sha256,
                    StringComparison.OrdinalIgnoreCase))
            {
                conflicts.Add(installed.RelativePath);
                continue;
            }
            pending.Add((installed, artifact.SourcePath));
        }
        if (conflicts.Count != 0)
        {
            return new(
                false,
                "Repair made no changes because modified or unavailable loader files require recovery.",
                conflicts);
        }

        var restored = new List<InstalledFile>();
        try
        {
            foreach (var repair in pending)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var target = ToGamePath(record.GameDirectory, repair.Installed.RelativePath);
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                CopyAtomically(repair.SourcePath, target);
                restored.Add(repair.Installed);
                var restoredHash = await GameDiscovery.HashFileAsync(target, cancellationToken)
                    .ConfigureAwait(false);
                if (!string.Equals(
                        restoredHash,
                        repair.Installed.Sha256,
                        StringComparison.OrdinalIgnoreCase))
                {
                    throw new IOException(
                        $"The repaired loader file failed verification: {repair.Installed.RelativePath}");
                }
            }

            var verification = await VerifyAsync(gameDirectory, cancellationToken).ConfigureAwait(false);
            if (verification.Success)
            {
                return new(true, "Loader installation repaired and verified.", []);
            }

            var preserved = await RollBackCopiesAsync(record.GameDirectory, restored).ConfigureAwait(false);
            return new(
                false,
                preserved.Count == 0
                    ? "Repair failed verification and the pre-repair state was restored."
                    : "Repair failed verification. Changed files were preserved for manual recovery.",
                verification.Conflicts.Concat(preserved).Distinct(StringComparer.OrdinalIgnoreCase).ToArray());
        }
        catch (OperationCanceledException)
        {
            await RollBackCopiesAsync(record.GameDirectory, restored).ConfigureAwait(false);
            throw;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            var preserved = await RollBackCopiesAsync(record.GameDirectory, restored).ConfigureAwait(false);
            return new(
                false,
                preserved.Count == 0
                    ? "Repair failed and the pre-repair state was restored. " + exception.Message
                    : "Repair failed. Changed files were preserved for manual recovery. " + exception.Message,
                preserved);
        }
    }

    public async Task<InstallationResult> UninstallAsync(
        string gameDirectory,
        CancellationToken cancellationToken = default)
    {
        var recordPath = GetRecordPath(gameDirectory);
        var record = await ReadRecordAsync(gameDirectory, cancellationToken).ConfigureAwait(false);
        if (record is null)
        {
            return new(false, "No loader installation record exists.", []);
        }
        var conflicts = new List<string>();
        foreach (var installed in record.Files.Reverse())
        {
            var target = ToGamePath(record.GameDirectory, installed.RelativePath);
            if (!File.Exists(target))
            {
                continue;
            }
            var currentHash = await GameDiscovery.HashFileAsync(target, cancellationToken).ConfigureAwait(false);
            if (!string.Equals(currentHash, installed.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                conflicts.Add(installed.RelativePath);
                continue;
            }
            File.Delete(target);
            var parent = Path.GetDirectoryName(target);
            if (parent is not null && !string.Equals(parent, record.GameDirectory, StringComparison.OrdinalIgnoreCase) &&
                Directory.Exists(parent) && !Directory.EnumerateFileSystemEntries(parent).Any())
            {
                Directory.Delete(parent);
            }
        }
        if (conflicts.Count == 0)
        {
            File.Delete(recordPath);
            return new(true, "Loader uninstalled successfully.", []);
        }
        var remaining = record with
        {
            Files = record.Files.Where(file => conflicts.Contains(
                file.RelativePath, StringComparer.OrdinalIgnoreCase)).ToArray()
        };
        await WriteAllTextAtomicallyAsync(
            recordPath,
            JsonSerializer.Serialize(remaining, JsonOptions),
            true,
            cancellationToken).ConfigureAwait(false);
        return new(false, "Modified loader-owned files were preserved.", conflicts);
    }

    public string GetRecordPath(string gameDirectory)
    {
        var normalized = Path.GetFullPath(gameDirectory).TrimEnd(Path.DirectorySeparatorChar).ToUpperInvariant();
        var key = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(normalized)));
        return Path.Combine(stateRoot, "installations", key + ".json");
    }

    private async Task<LoaderInstallationRecord?> ReadRecordAsync(
        string gameDirectory,
        CancellationToken cancellationToken)
    {
        var path = GetRecordPath(gameDirectory);
        if (!File.Exists(path))
        {
            return null;
        }
        using var stream = File.OpenRead(path);
        var record = await JsonSerializer.DeserializeAsync<LoaderInstallationRecord>(
            stream, JsonOptions, cancellationToken).ConfigureAwait(false);
        if (record is null)
        {
            throw new InvalidDataException("The installation record is empty.");
        }
        var requestedDirectory = Path.GetFullPath(gameDirectory).TrimEnd(Path.DirectorySeparatorChar);
        var recordedDirectory = Path.GetFullPath(record.GameDirectory).TrimEnd(Path.DirectorySeparatorChar);
        if (record.SchemaVersion != 1 ||
            !string.Equals(requestedDirectory, recordedDirectory, StringComparison.OrdinalIgnoreCase) ||
            string.IsNullOrWhiteSpace(record.BuildId) ||
            record.Files.Count == 0 ||
            record.Files.Select(file => file.RelativePath).Distinct(StringComparer.OrdinalIgnoreCase).Count() !=
                record.Files.Count ||
            record.Files.Any(file => string.IsNullOrWhiteSpace(file.Sha256) || file.Sha256.Length != 64))
        {
            throw new InvalidDataException("The installation record is invalid.");
        }
        foreach (var file in record.Files)
        {
            _ = ToGamePath(recordedDirectory, file.RelativePath);
        }
        return record;
    }

    private static List<(string RelativePath, string SourcePath)> EnumerateArtifacts(string artifactDirectory)
    {
        var root = Path.GetFullPath(artifactDirectory);
        var required = new[] { "wininet.dll", "btd5loader_runtime.dll" };
        var artifacts = new List<(string, string)>();
        foreach (var name in required)
        {
            var source = Path.Combine(root, name);
            if (!File.Exists(source))
            {
                return [];
            }
            artifacts.Add((name, source));
        }
        var symbols = Path.Combine(root, "symbols");
        if (!Directory.Exists(symbols))
        {
            return [];
        }
        var symbolArtifacts = Directory.EnumerateFiles(symbols, "*.json")
            .Order(StringComparer.OrdinalIgnoreCase)
            .Select(path => (Path.Combine("symbols", Path.GetFileName(path)), path))
            .ToArray();
        if (symbolArtifacts.Length == 0)
        {
            return [];
        }
        artifacts.AddRange(symbolArtifacts);
        return artifacts;
    }

    private static string ToGamePath(string gameDirectory, string relativePath)
    {
        var root = Path.GetFullPath(gameDirectory).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var target = Path.GetFullPath(Path.Combine(root, relativePath));
        if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("Installation record contains an unsafe path.");
        }
        return target;
    }

    private static void CopyAtomically(string source, string target)
    {
        var temporary = target + ".btd5ml-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            File.Copy(source, temporary, false);
            File.Move(temporary, target, false);
        }
        finally
        {
            File.Delete(temporary);
        }
    }

    private static async Task<List<string>> RollBackCopiesAsync(
        string gameDirectory,
        IEnumerable<InstalledFile> copiedFiles)
    {
        var preserved = new List<string>();
        foreach (var copied in copiedFiles.Reverse())
        {
            var target = ToGamePath(gameDirectory, copied.RelativePath);
            if (!File.Exists(target))
            {
                continue;
            }
            try
            {
                var hash = await GameDiscovery.HashFileAsync(target, CancellationToken.None)
                    .ConfigureAwait(false);
                if (!string.Equals(hash, copied.Sha256, StringComparison.OrdinalIgnoreCase))
                {
                    preserved.Add(copied.RelativePath);
                    continue;
                }
                File.Delete(target);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                preserved.Add(copied.RelativePath);
            }
        }
        return preserved;
    }

    private static bool TryDelete(string path)
    {
        try
        {
            File.Delete(path);
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static async Task WriteAllTextAtomicallyAsync(
        string target,
        string contents,
        bool replace,
        CancellationToken cancellationToken)
    {
        var temporary = target + ".btd5ml-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            await File.WriteAllTextAsync(temporary, contents, cancellationToken).ConfigureAwait(false);
            File.Move(temporary, target, replace);
        }
        finally
        {
            File.Delete(temporary);
        }
    }
}
