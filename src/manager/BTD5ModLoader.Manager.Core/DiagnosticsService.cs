using System.IO.Compression;
using System.Text;
using System.Text.Json;

namespace BTD5ModLoader.Manager.Core;

public sealed record DiagnosticsExportResult(bool Success, string Message, string? Path);

public sealed class DiagnosticsService
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };
    private readonly string managerStateRoot;
    private readonly IReadOnlyList<KnownGameBuild> knownBuilds;

    public DiagnosticsService(
        string managerStateRoot,
        IReadOnlyList<KnownGameBuild>? knownBuilds = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(managerStateRoot);
        this.managerStateRoot = System.IO.Path.GetFullPath(managerStateRoot);
        this.knownBuilds = knownBuilds ?? GameDiscovery.SupportedBuilds;
    }

    public async Task<DiagnosticsExportResult> ExportAsync(
        string destinationPath,
        string gameDirectory,
        string? profileName = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(destinationPath);
        ArgumentException.ThrowIfNullOrWhiteSpace(gameDirectory);
        var destination = System.IO.Path.GetFullPath(destinationPath);
        if (!string.Equals(System.IO.Path.GetExtension(destination), ".zip", StringComparison.OrdinalIgnoreCase))
        {
            return new(false, "Diagnostics must be exported as a .zip file.", null);
        }
        if (File.Exists(destination))
        {
            return new(false, "The destination file already exists.", null);
        }

        var game = await GameDiscovery.ValidateAsync(
            gameDirectory, knownBuilds, cancellationToken).ConfigureAwait(false);
        var installation = await new LoaderInstallationService(managerStateRoot, knownBuilds)
            .VerifyAsync(gameDirectory, cancellationToken).ConfigureAwait(false);
        ProfileValidationResult? profile = null;
        if (game.BuildId is not null && !string.IsNullOrWhiteSpace(profileName))
        {
            profile = await new ProfileModService(managerStateRoot, game.BuildId)
                .ValidateAsync(profileName, cancellationToken).ConfigureAwait(false);
        }
        var summary = new
        {
            generatedAtUtc = DateTimeOffset.UtcNow,
            product = ProductInfo.Name,
            loaderVersion = ProductInfo.Version,
            game = new
            {
                game.BuildId,
                game.Supported,
                game.ExecutableSha256,
                game.AssetsSha256,
                game.Error
            },
            installation = new
            {
                installation.Success,
                installation.Message,
                installation.Conflicts
            },
            profile = profileName is null ? null : new
            {
                name = profileName,
                valid = profile?.Valid,
                errors = profile?.Errors,
                loadOrder = profile?.OrderedPackages.Select(package => new
                {
                    package.Id,
                    package.Version
                })
            }
        };

        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(destination)!);
        var temporary = destination + ".btd5ml-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var archive = await ZipFile.OpenAsync(
                       temporary, ZipArchiveMode.Create, cancellationToken).ConfigureAwait(false))
            {
                await WriteEntryAsync(
                    archive,
                    "summary.json",
                    JsonSerializer.Serialize(summary, JsonOptions),
                    cancellationToken).ConfigureAwait(false);
                var runtimeLogPath = System.IO.Path.Combine(managerStateRoot, "logs", "runtime.jsonl");
                if (File.Exists(runtimeLogPath))
                {
                    var log = await File.ReadAllTextAsync(runtimeLogPath, cancellationToken)
                        .ConfigureAwait(false);
                    await WriteEntryAsync(
                        archive,
                        "runtime.jsonl",
                        Redact(log, gameDirectory),
                        cancellationToken).ConfigureAwait(false);
                }
                await WriteEntryAsync(
                    archive,
                    "README.txt",
                    "This archive contains loader diagnostics only. Game binaries, assets, saves, " +
                    "account identifiers, mod archives, and machine-specific paths are excluded.\r\n",
                    cancellationToken).ConfigureAwait(false);
            }
            File.Move(temporary, destination, false);
            return new(true, "Diagnostics exported successfully.", destination);
        }
        finally
        {
            File.Delete(temporary);
        }
    }

    private string Redact(string text, string gameDirectory)
    {
        return text
            .Replace(System.IO.Path.GetFullPath(gameDirectory), "[GAME_DIRECTORY]", StringComparison.OrdinalIgnoreCase)
            .Replace(managerStateRoot, "[MANAGER_DATA]", StringComparison.OrdinalIgnoreCase);
    }

    private static async Task WriteEntryAsync(
        ZipArchive archive,
        string name,
        string contents,
        CancellationToken cancellationToken)
    {
        var entry = archive.CreateEntry(name, CompressionLevel.Optimal);
        using var stream = await entry.OpenAsync(cancellationToken).ConfigureAwait(false);
        using var writer = new StreamWriter(stream, new UTF8Encoding(false), leaveOpen: false);
        await writer.WriteAsync(contents.AsMemory(), cancellationToken).ConfigureAwait(false);
    }
}
