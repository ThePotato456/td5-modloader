using System.IO.Compression;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace BTD5ModLoader.Manager.Core;

public sealed record ModDependencyInfo(string Id, string Version);

public sealed record ModPackageInfo(
    string PackagePath,
    bool Valid,
    string? Id,
    string? Name,
    string? Author,
    string? Version,
    uint? LoaderApi,
    IReadOnlyList<string> SupportedGameBuilds,
    IReadOnlyList<ModDependencyInfo> Dependencies,
    IReadOnlyList<string> LoadBefore,
    IReadOnlyList<string> LoadAfter,
    IReadOnlyList<string> Capabilities,
    IReadOnlyDictionary<string, JsonElement> ConfigurationDefaults,
    IReadOnlyList<string> Errors);

public sealed record ModPackageInstallResult(
    bool Success,
    string Message,
    string? InstalledPath,
    ModPackageInfo Package);

public static partial class ModPackageService
{
    private const long MaximumArchiveBytes = 64L * 1024 * 1024;
    private const long MaximumTotalBytes = 256L * 1024 * 1024;
    private const long MaximumFileBytes = 32L * 1024 * 1024;
    private const int MaximumManifestBytes = 1024 * 1024;
    private const int MaximumEntries = 4096;

    private static readonly HashSet<string> AllowedRoots = new(StringComparer.OrdinalIgnoreCase)
    {
        "lua", "assets", "localization", "config", "docs"
    };

    private static readonly HashSet<string> AllowedRootFiles = new(StringComparer.OrdinalIgnoreCase)
    {
        "mod.json", "README.md", "CHANGELOG.md", "LICENSE", "LICENSE.md"
    };

    private static readonly HashSet<string> AllowedCapabilities = new(StringComparer.Ordinal)
    {
        "gameplay.events", "gameplay.mutate", "content.towers", "content.assets", "storage"
    };

    private static readonly HashSet<string> AllowedManifestFields = new(StringComparer.Ordinal)
    {
        "$schema", "id", "name", "author", "version", "entry_point", "loader_api",
        "supported_game_builds", "dependencies", "load_order", "capabilities",
        "configuration_defaults", "localization", "documentation"
    };

    public static async Task<ModPackageInfo> InspectAsync(
        string packagePath,
        string? selectedBuildId = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(packagePath);
        var fullPath = Path.GetFullPath(packagePath);
        var errors = new List<string>();
        if (!string.Equals(Path.GetExtension(fullPath), ".btd5mod", StringComparison.OrdinalIgnoreCase))
        {
            errors.Add("The package must use the .btd5mod extension.");
        }
        if (!File.Exists(fullPath))
        {
            errors.Add("The package file does not exist.");
            return EmptyResult(fullPath, errors);
        }
        var fileInfo = new FileInfo(fullPath);
        if (fileInfo.Length == 0 || fileInfo.Length > MaximumArchiveBytes)
        {
            errors.Add("The package is empty or exceeds the 64 MiB archive limit.");
            return EmptyResult(fullPath, errors);
        }

        byte[]? manifestBytes = null;
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        try
        {
            using var archive = await ZipFile.OpenReadAsync(fullPath, cancellationToken)
                .ConfigureAwait(false);
            if (archive.Entries.Count == 0 || archive.Entries.Count > MaximumEntries)
            {
                errors.Add("The package has no entries or exceeds the 4,096-entry limit.");
            }
            long totalBytes = 0;
            foreach (var entry in archive.Entries)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var isDirectory = entry.FullName.EndsWith('/');
                if (!IsSafePackagePath(entry.FullName, isDirectory) ||
                    (!isDirectory && !paths.Add(entry.FullName)))
                {
                    errors.Add($"Unsafe or duplicate package path: {entry.FullName}");
                    continue;
                }
                if (IsSymbolicLink(entry))
                {
                    errors.Add($"Symbolic links are not allowed: {entry.FullName}");
                    continue;
                }
                if (isDirectory)
                {
                    continue;
                }
                if (!IsAllowedLocation(entry.FullName))
                {
                    errors.Add($"File is outside the allowed package layout: {entry.FullName}");
                }
                if (entry.Length > MaximumFileBytes || totalBytes > MaximumTotalBytes - entry.Length)
                {
                    errors.Add($"Package size limit exceeded by: {entry.FullName}");
                    continue;
                }
                totalBytes += entry.Length;
                if (totalBytes > MaximumTotalBytes)
                {
                    errors.Add("The package exceeds the 256 MiB uncompressed limit.");
                    continue;
                }
                var bytes = await ReadEntryAsync(entry, cancellationToken).ConfigureAwait(false);
                if (string.Equals(entry.FullName, "mod.json", StringComparison.Ordinal))
                {
                    if (bytes.Length > MaximumManifestBytes)
                    {
                        errors.Add("mod.json exceeds its 1 MiB limit.");
                    }
                    else
                    {
                        manifestBytes = bytes;
                    }
                }
            }
        }
        catch (InvalidDataException exception)
        {
            errors.Add("The package is not a readable, unencrypted ZIP archive: " + exception.Message);
        }

        return manifestBytes is null
            ? EmptyResult(fullPath, [.. errors, "The package does not contain a root mod.json."])
            : ParseManifest(fullPath, manifestBytes, paths, selectedBuildId, errors);
    }

    public static async Task<ModPackageInstallResult> InstallAsync(
        string packagePath,
        string managerStateRoot,
        string? selectedBuildId = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(managerStateRoot);
        var package = await InspectAsync(packagePath, selectedBuildId, cancellationToken).ConfigureAwait(false);
        if (!package.Valid || package.Id is null || package.Version is null)
        {
            return new(false, "Package validation failed.", null, package);
        }

        var destination = Path.Combine(
            Path.GetFullPath(managerStateRoot), "packages", package.Id, package.Version, "package.btd5mod");
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        if (File.Exists(destination))
        {
            var sourceHash = await GameDiscovery.HashFileAsync(package.PackagePath, cancellationToken)
                .ConfigureAwait(false);
            var destinationHash = await GameDiscovery.HashFileAsync(destination, cancellationToken)
                .ConfigureAwait(false);
            return string.Equals(sourceHash, destinationHash, StringComparison.OrdinalIgnoreCase)
                ? new(true, "This exact package is already installed.", destination, package)
                : new(false, "A different package with the same ID and version is already installed.",
                    destination, package);
        }

        CopyAtomically(package.PackagePath, destination);
        return new(true, "Mod package installed successfully.", destination, package);
    }

    public static async Task<IReadOnlyList<ModPackageInfo>> ListInstalledAsync(
        string managerStateRoot,
        string? selectedBuildId = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(managerStateRoot);
        var packagesRoot = Path.Combine(Path.GetFullPath(managerStateRoot), "packages");
        if (!Directory.Exists(packagesRoot))
        {
            return [];
        }

        var packages = new List<ModPackageInfo>();
        foreach (var path in Directory.EnumerateFiles(
                     packagesRoot, "*.btd5mod", SearchOption.AllDirectories)
                     .Order(StringComparer.OrdinalIgnoreCase))
        {
            var package = await InspectAsync(path, selectedBuildId, cancellationToken)
                .ConfigureAwait(false);
            var versionDirectory = Path.GetDirectoryName(path);
            var idDirectory = versionDirectory is null ? null : Path.GetDirectoryName(versionDirectory);
            var managerCanonicalPackage = string.Equals(
                Path.GetFileName(path), "package.btd5mod", StringComparison.OrdinalIgnoreCase);
            if (managerCanonicalPackage && package.Id is not null && package.Version is not null &&
                (!string.Equals(Path.GetFileName(idDirectory), package.Id, StringComparison.Ordinal) ||
                 !string.Equals(Path.GetFileName(versionDirectory), package.Version, StringComparison.Ordinal)))
            {
                package = package with
                {
                    Valid = false,
                    Errors = [.. package.Errors, "The installed package path does not match its identity."]
                };
            }
            packages.Add(package);
        }
        var duplicateIdentities = packages
            .Where(package => package.Valid && package.Id is not null && package.Version is not null)
            .GroupBy(package => (package.Id!, package.Version!), PackageIdentityComparer.Instance)
            .Where(group => group.Count() > 1)
            .Select(group => group.Key)
            .ToHashSet(PackageIdentityComparer.Instance);
        return packages.Select(package =>
        {
            if (package.Id is null || package.Version is null ||
                !duplicateIdentities.Contains((package.Id, package.Version)))
            {
                return package;
            }
            return package with
            {
                Valid = false,
                Errors = [.. package.Errors,
                    "More than one package file declares this same ID and version. Remove the duplicate copy."]
            };
        }).ToArray();
    }

    private static ModPackageInfo ParseManifest(
        string packagePath,
        byte[] manifestBytes,
        HashSet<string> packagePaths,
        string? selectedBuildId,
        List<string> errors)
    {
        string? id = null;
        string? name = null;
        string? author = null;
        string? version = null;
        uint? loaderApi = null;
        var builds = new List<string>();
        var dependencies = new List<ModDependencyInfo>();
        var loadBefore = new List<string>();
        var loadAfter = new List<string>();
        var capabilities = new List<string>();
        var configurationDefaults = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
        try
        {
            var utf8 = new UTF8Encoding(false, true).GetString(manifestBytes);
            using var document = JsonDocument.Parse(utf8);
            if (document.RootElement.ValueKind != JsonValueKind.Object)
            {
                errors.Add("mod.json must contain a JSON object.");
                return Result();
            }
            var root = document.RootElement;
            foreach (var property in root.EnumerateObject())
            {
                if (!AllowedManifestFields.Contains(property.Name))
                {
                    errors.Add("Unknown manifest field: " + property.Name);
                }
            }

            id = ReadString(root, "id", errors);
            name = ReadString(root, "name", errors);
            author = ReadString(root, "author", errors);
            version = ReadString(root, "version", errors);
            var entryPoint = ReadString(root, "entry_point", errors);
            if (id is not null && !ModIdPattern().IsMatch(id))
            {
                errors.Add("The mod ID is invalid.");
            }
            if (name is not null && name.Length > 128 || author is not null && author.Length > 128)
            {
                errors.Add("The mod name and author must not exceed 128 characters.");
            }
            if (version is not null && !SemanticVersionPattern().IsMatch(version))
            {
                errors.Add("The package version is not valid semantic versioning.");
            }
            if (entryPoint is not null &&
                (!IsSafePackagePath(entryPoint, false) || !packagePaths.Contains(entryPoint)))
            {
                errors.Add("The declared Lua entry point is missing or unsafe.");
            }
            if (!root.TryGetProperty("loader_api", out var apiElement) ||
                !apiElement.TryGetUInt32(out var api))
            {
                errors.Add("loader_api must be an unsigned integer.");
            }
            else
            {
                loaderApi = api;
                if (api != 1)
                {
                    errors.Add("The package requires an unsupported loader API.");
                }
            }
            ReadStringArray(root, "supported_game_builds", builds, errors);
            if (builds.Count == 0)
            {
                errors.Add("At least one supported game build is required.");
            }
            if (selectedBuildId is not null && !builds.Contains(selectedBuildId, StringComparer.Ordinal))
            {
                errors.Add($"The package does not support game build {selectedBuildId}.");
            }
            ReadStringArray(root, "capabilities", capabilities, errors);
            foreach (var capability in capabilities)
            {
                if (!AllowedCapabilities.Contains(capability))
                {
                    errors.Add("Unsupported capability: " + capability);
                }
            }
            ReadDependencies(root, dependencies, errors);
            if (id is not null && dependencies.Any(value => value.Id == id))
            {
                errors.Add("A mod cannot depend on itself.");
            }
            ReadLoadOrder(root, loadBefore, loadAfter, errors);
            ReadConfigurationDefaults(root, configurationDefaults, errors);
            if (id is not null && (loadBefore.Contains(id, StringComparer.Ordinal) ||
                loadAfter.Contains(id, StringComparer.Ordinal)))
            {
                errors.Add("A mod cannot order itself before or after itself.");
            }
        }
        catch (Exception exception) when (exception is JsonException or DecoderFallbackException)
        {
            errors.Add("mod.json is malformed: " + exception.Message);
        }
        return Result();

        ModPackageInfo Result() => new(
            packagePath,
            errors.Count == 0,
            id,
            name,
            author,
            version,
            loaderApi,
            builds,
            dependencies,
            loadBefore,
            loadAfter,
            capabilities,
            configurationDefaults,
            errors);
    }

    private static void ReadDependencies(
        JsonElement root,
        List<ModDependencyInfo> dependencies,
        List<string> errors)
    {
        if (!root.TryGetProperty("dependencies", out var element) ||
            element.ValueKind != JsonValueKind.Array)
        {
            errors.Add("dependencies must be an array.");
            return;
        }
        foreach (var dependency in element.EnumerateArray())
        {
            if (dependency.ValueKind != JsonValueKind.Object)
            {
                errors.Add("Every dependency must be an object.");
                continue;
            }
            var id = ReadString(dependency, "id", errors);
            var version = ReadString(dependency, "version", errors);
            if (id is not null && version is not null)
            {
                if (!ModIdPattern().IsMatch(id) || !VersionRequirementPattern().IsMatch(version))
                {
                    errors.Add("A dependency has an invalid ID or version requirement.");
                }
                dependencies.Add(new(id, version));
            }
        }
        if (dependencies.Select(value => value.Id).Distinct(StringComparer.Ordinal).Count() != dependencies.Count)
        {
            errors.Add("Dependency IDs must be unique.");
        }
    }

    private static void ReadLoadOrder(
        JsonElement root,
        List<string> loadBefore,
        List<string> loadAfter,
        List<string> errors)
    {
        if (!root.TryGetProperty("load_order", out var loadOrder) ||
            loadOrder.ValueKind != JsonValueKind.Object)
        {
            errors.Add("load_order must be an object.");
            return;
        }
        ReadStringArray(loadOrder, "before", loadBefore, errors);
        ReadStringArray(loadOrder, "after", loadAfter, errors);
        foreach (var id in loadBefore.Concat(loadAfter))
        {
            if (!ModIdPattern().IsMatch(id))
            {
                errors.Add("load_order contains an invalid mod ID.");
            }
        }
    }

    private static void ReadConfigurationDefaults(
        JsonElement root,
        Dictionary<string, JsonElement> destination,
        List<string> errors)
    {
        if (!root.TryGetProperty("configuration_defaults", out var element))
        {
            return;
        }
        if (element.ValueKind != JsonValueKind.Object)
        {
            errors.Add("configuration_defaults must be an object.");
            return;
        }
        foreach (var property in element.EnumerateObject())
        {
            if (string.IsNullOrWhiteSpace(property.Name) || property.Name.Length > 128)
            {
                errors.Add("configuration_defaults contains an invalid key.");
                continue;
            }
            destination.Add(property.Name, property.Value.Clone());
        }
    }

    private static string? ReadString(JsonElement root, string name, List<string> errors)
    {
        if (!root.TryGetProperty(name, out var element) || element.ValueKind != JsonValueKind.String)
        {
            errors.Add(name + " must be a string.");
            return null;
        }
        var value = element.GetString();
        if (string.IsNullOrWhiteSpace(value))
        {
            errors.Add(name + " must not be empty.");
            return null;
        }
        return value;
    }

    private static void ReadStringArray(
        JsonElement root,
        string name,
        List<string> destination,
        List<string> errors)
    {
        if (!root.TryGetProperty(name, out var element) || element.ValueKind != JsonValueKind.Array)
        {
            errors.Add(name + " must be an array.");
            return;
        }
        foreach (var item in element.EnumerateArray())
        {
            if (item.ValueKind != JsonValueKind.String || string.IsNullOrWhiteSpace(item.GetString()))
            {
                errors.Add(name + " must contain only non-empty strings.");
                continue;
            }
            destination.Add(item.GetString()!);
        }
        if (destination.Distinct(StringComparer.Ordinal).Count() != destination.Count)
        {
            errors.Add(name + " must not contain duplicate values.");
        }
    }

    private static bool IsSafePackagePath(string path, bool directory)
    {
        var value = directory ? path.TrimEnd('/') : path;
        if (string.IsNullOrEmpty(value) || value.Length > 240 || value[0] == '/' ||
            value.Contains('\\', StringComparison.Ordinal) || value.Contains(':', StringComparison.Ordinal))
        {
            return false;
        }
        return value.Split('/').All(component =>
            component.Length != 0 && component is not "." and not "..");
    }

    private static bool IsAllowedLocation(string path)
    {
        var separator = path.IndexOf('/', StringComparison.Ordinal);
        return separator < 0 ? AllowedRootFiles.Contains(path) : AllowedRoots.Contains(path[..separator]);
    }

    private static bool IsSymbolicLink(ZipArchiveEntry entry) =>
        ((entry.ExternalAttributes >> 16) & 0xf000) == 0xa000;

    private static async Task<byte[]> ReadEntryAsync(
        ZipArchiveEntry entry,
        CancellationToken cancellationToken)
    {
        using var source = await entry.OpenAsync(cancellationToken).ConfigureAwait(false);
        using var destination = new MemoryStream(entry.Length > int.MaxValue ? 0 : (int)entry.Length);
        await source.CopyToAsync(destination, cancellationToken).ConfigureAwait(false);
        return destination.ToArray();
    }

    private static ModPackageInfo EmptyResult(string packagePath, IReadOnlyList<string> errors) =>
        new(packagePath, false, null, null, null, null, null, [], [], [], [], [],
            new Dictionary<string, JsonElement>(), errors);

    private static void CopyAtomically(string source, string destination)
    {
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

    private sealed class PackageIdentityComparer : IEqualityComparer<(string Id, string Version)>
    {
        public static PackageIdentityComparer Instance { get; } = new();

        public bool Equals((string Id, string Version) left, (string Id, string Version) right) =>
            string.Equals(left.Id, right.Id, StringComparison.OrdinalIgnoreCase) &&
            string.Equals(left.Version, right.Version, StringComparison.OrdinalIgnoreCase);

        public int GetHashCode((string Id, string Version) value) => HashCode.Combine(
            StringComparer.OrdinalIgnoreCase.GetHashCode(value.Id),
            StringComparer.OrdinalIgnoreCase.GetHashCode(value.Version));
    }

    [GeneratedRegex("^[a-z][a-z0-9]*(\\.[a-z0-9][a-z0-9-]*)+$", RegexOptions.CultureInvariant)]
    private static partial Regex ModIdPattern();

    [GeneratedRegex(
        "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\\+[0-9A-Za-z.-]+)?$",
        RegexOptions.CultureInvariant)]
    private static partial Regex SemanticVersionPattern();

    [GeneratedRegex("^(?:\\*|>=|\\^)?[0-9]+\\.[0-9]+\\.[0-9]+$", RegexOptions.CultureInvariant)]
    private static partial Regex VersionRequirementPattern();
}
