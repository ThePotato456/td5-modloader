using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace BTD5ModLoader.Manager.Core;

public sealed record KnownGameBuild(
    string Id,
    string ExecutableSha256,
    string AssetsSha256);

public sealed record GameInstallation(
    string Directory,
    string? BuildId,
    string ExecutableSha256,
    string AssetsSha256,
    bool Supported,
    string? Error);

public static partial class GameDiscovery
{
    public static readonly IReadOnlyList<KnownGameBuild> SupportedBuilds =
    [
        new(
            "steam-win32-4.8",
            "bdc4f4aec679f51b8763ff7fe517a2556e392d99576045ece117fcafdda27b70",
            "906aa89d690c27664ce47a1a2e3eac756d7cf551fe3e1669ec22ae814346b9a8")
    ];

    public static IEnumerable<string> DiscoverGameDirectories(string steamDirectory)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(steamDirectory);
        var libraries = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            Path.GetFullPath(steamDirectory)
        };
        var libraryFile = Path.Combine(steamDirectory, "steamapps", "libraryfolders.vdf");
        if (File.Exists(libraryFile))
        {
            foreach (Match match in LibraryPathPattern().Matches(File.ReadAllText(libraryFile)))
            {
                var decoded = match.Groups[1].Value.Replace("\\\\", "\\", StringComparison.Ordinal);
                if (!string.IsNullOrWhiteSpace(decoded))
                {
                    libraries.Add(Path.GetFullPath(decoded));
                }
            }
        }

        return libraries
            .Select(path => Path.Combine(path, "steamapps", "common", "BloonsTD5"))
            .Where(Directory.Exists)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .Order(StringComparer.OrdinalIgnoreCase);
    }

    public static async Task<GameInstallation> ValidateAsync(
        string gameDirectory,
        IReadOnlyList<KnownGameBuild>? knownBuilds = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(gameDirectory);
        knownBuilds ??= SupportedBuilds;
        var fullDirectory = Path.GetFullPath(gameDirectory);
        var executable = Path.Combine(fullDirectory, "BTD5-Win.exe");
        var assets = Path.Combine(fullDirectory, "Assets", "BTD5.jet");
        if (!File.Exists(executable) || !File.Exists(assets))
        {
            return new(fullDirectory, null, string.Empty, string.Empty, false, "Required game files are missing.");
        }
        if (!await IsWin32PeAsync(executable, cancellationToken).ConfigureAwait(false))
        {
            return new(
                fullDirectory,
                null,
                string.Empty,
                string.Empty,
                false,
                "BTD5-Win.exe is not a 32-bit Windows executable.");
        }

        var executableHash = await HashFileAsync(executable, cancellationToken).ConfigureAwait(false);
        var assetsHash = await HashFileAsync(assets, cancellationToken).ConfigureAwait(false);
        var build = knownBuilds.FirstOrDefault(candidate =>
            string.Equals(candidate.ExecutableSha256, executableHash, StringComparison.OrdinalIgnoreCase) &&
            string.Equals(candidate.AssetsSha256, assetsHash, StringComparison.OrdinalIgnoreCase));
        return build is null
            ? new(fullDirectory, null, executableHash, assetsHash, false, "The game build is not supported.")
            : new(fullDirectory, build.Id, executableHash, assetsHash, true, null);
    }

    public static async Task<string> HashFileAsync(
        string path,
        CancellationToken cancellationToken = default)
    {
        using var stream = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            64 * 1024,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        var digest = await SHA256.HashDataAsync(stream, cancellationToken).ConfigureAwait(false);
        return Convert.ToHexStringLower(digest);
    }

    [GeneratedRegex("\\\"path\\\"\\s*\\\"([^\\\"]+)\\\"", RegexOptions.CultureInvariant)]
    private static partial Regex LibraryPathPattern();

    private static async Task<bool> IsWin32PeAsync(
        string path,
        CancellationToken cancellationToken)
    {
        using var stream = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            4096,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        var dosHeader = new byte[64];
        if (await stream.ReadAtLeastAsync(
                dosHeader, dosHeader.Length, false, cancellationToken).ConfigureAwait(false) != dosHeader.Length ||
            dosHeader[0] != (byte)'M' || dosHeader[1] != (byte)'Z')
        {
            return false;
        }

        var peOffset = BinaryPrimitives.ReadInt32LittleEndian(dosHeader.AsSpan(0x3c, 4));
        if (peOffset < dosHeader.Length || peOffset > stream.Length - 6)
        {
            return false;
        }
        stream.Seek(peOffset, SeekOrigin.Begin);
        var peHeader = new byte[6];
        return await stream.ReadAtLeastAsync(
                   peHeader, peHeader.Length, false, cancellationToken).ConfigureAwait(false) == peHeader.Length &&
               BinaryPrimitives.ReadUInt32LittleEndian(peHeader) == 0x00004550 &&
               BinaryPrimitives.ReadUInt16LittleEndian(peHeader.AsSpan(4, 2)) == 0x014c;
    }
}
