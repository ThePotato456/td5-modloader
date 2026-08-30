namespace BTD5ModLoader.Manager.Core;

public sealed record ProfileValidationResult(
    bool Valid,
    IReadOnlyList<string> Errors,
    IReadOnlyList<ModPackageInfo> OrderedPackages);

public sealed record ProfileChangeResult(
    bool Success,
    string Message,
    ModProfile? Profile,
    ProfileValidationResult Validation);

public sealed record PackageRemovalResult(
    bool Success,
    string Message,
    IReadOnlyList<string> BlockingProfiles);

public sealed class ProfileModService
{
    private readonly string managerStateRoot;
    private readonly string buildId;
    private readonly ProfileService profiles;

    public ProfileModService(string managerStateRoot, string buildId)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(managerStateRoot);
        ArgumentException.ThrowIfNullOrWhiteSpace(buildId);
        this.managerStateRoot = Path.GetFullPath(managerStateRoot);
        this.buildId = buildId;
        profiles = new ProfileService(this.managerStateRoot);
    }

    public async Task<ProfileValidationResult> ValidateAsync(
        string profileName,
        CancellationToken cancellationToken = default)
    {
        var profile = await profiles.LoadAsync(profileName, cancellationToken).ConfigureAwait(false) ??
            throw new InvalidOperationException("The profile does not exist.");
        return await ValidateAsync(profile, cancellationToken).ConfigureAwait(false);
    }

    public async Task<ProfileChangeResult> EnableAsync(
        string profileName,
        string modId,
        string version,
        CancellationToken cancellationToken = default)
    {
        var profile = await RequireProfileAsync(profileName, cancellationToken).ConfigureAwait(false);
        var mods = profile.Mods.ToList();
        var index = mods.FindIndex(mod => string.Equals(mod.Id, modId, StringComparison.Ordinal));
        if (index < 0)
        {
            var package = (await ModPackageService.ListInstalledAsync(
                    managerStateRoot, buildId, cancellationToken).ConfigureAwait(false))
                .SingleOrDefault(value => value.Valid &&
                    string.Equals(value.Id, modId, StringComparison.Ordinal) &&
                    string.Equals(value.Version, version, StringComparison.Ordinal));
            mods.Add(new(modId, version, true, mods.Count,
                package?.ConfigurationDefaults ??
                new Dictionary<string, System.Text.Json.JsonElement>()));
        }
        else
        {
            mods[index] = mods[index] with { Version = version, Enabled = true };
        }
        return await TrySaveAsync(profile, mods, "Mod enabled.", cancellationToken).ConfigureAwait(false);
    }

    public async Task<ProfileChangeResult> DisableAsync(
        string profileName,
        string modId,
        CancellationToken cancellationToken = default)
    {
        var profile = await RequireProfileAsync(profileName, cancellationToken).ConfigureAwait(false);
        var mods = profile.Mods.ToList();
        var index = mods.FindIndex(mod => string.Equals(mod.Id, modId, StringComparison.Ordinal));
        if (index < 0)
        {
            return MissingMod(profile, modId);
        }
        mods[index] = mods[index] with { Enabled = false };
        return await TrySaveAsync(profile, mods, "Mod disabled.", cancellationToken).ConfigureAwait(false);
    }

    public async Task<ProfileChangeResult> ToggleVersionAsync(
        string profileName,
        string modId,
        string version,
        CancellationToken cancellationToken = default)
    {
        var profile = await RequireProfileAsync(profileName, cancellationToken).ConfigureAwait(false);
        var entry = profile.Mods.SingleOrDefault(mod => string.Equals(mod.Id, modId, StringComparison.Ordinal));
        if (entry is null or { Enabled: false })
        {
            return await EnableAsync(profileName, modId, version, cancellationToken).ConfigureAwait(false);
        }
        if (!string.Equals(entry.Version, version, StringComparison.Ordinal))
        {
            return await ChangeVersionAsync(profileName, modId, version, cancellationToken).ConfigureAwait(false);
        }
        return await DisableAsync(profileName, modId, cancellationToken).ConfigureAwait(false);
    }

    public async Task<ProfileChangeResult> ChangeVersionAsync(
        string profileName,
        string modId,
        string version,
        CancellationToken cancellationToken = default)
    {
        var profile = await RequireProfileAsync(profileName, cancellationToken).ConfigureAwait(false);
        var mods = profile.Mods.ToList();
        var index = mods.FindIndex(mod => string.Equals(mod.Id, modId, StringComparison.Ordinal));
        if (index < 0)
        {
            return MissingMod(profile, modId);
        }
        mods[index] = mods[index] with { Version = version };
        return await TrySaveAsync(
            profile, mods, "Mod version changed.", cancellationToken).ConfigureAwait(false);
    }

    public async Task<ProfileChangeResult> UpdateConfigurationAsync(
        string profileName,
        string modId,
        IReadOnlyDictionary<string, System.Text.Json.JsonElement> configuration,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(configuration);
        var profile = await RequireProfileAsync(profileName, cancellationToken).ConfigureAwait(false);
        var mods = profile.Mods.ToList();
        var index = mods.FindIndex(mod => string.Equals(mod.Id, modId, StringComparison.Ordinal));
        if (index < 0)
        {
            return MissingMod(profile, modId);
        }
        var package = await FindInstalledPackageAsync(
            mods[index].Id, mods[index].Version, cancellationToken).ConfigureAwait(false);
        if (package is null)
        {
            return new(false, "The selected mod version is not installed.", profile,
                new(false, [$"{mods[index].Id} {mods[index].Version} is not installed."], []));
        }
        var configurationErrors = ValidateConfiguration(package, configuration);
        if (configurationErrors.Count != 0)
        {
            return new(false, "The configuration contains invalid values.", profile,
                new(false, configurationErrors, []));
        }
        mods[index] = mods[index] with
        {
            Configuration = configuration.ToDictionary(
                value => value.Key, value => value.Value.Clone(), StringComparer.Ordinal)
        };
        return await TrySaveAsync(
            profile, mods, "Mod configuration saved.", cancellationToken).ConfigureAwait(false);
    }

    public async Task<ProfileChangeResult> ResetConfigurationAsync(
        string profileName,
        string modId,
        CancellationToken cancellationToken = default)
    {
        var profile = await RequireProfileAsync(profileName, cancellationToken).ConfigureAwait(false);
        var entry = profile.Mods.SingleOrDefault(mod => string.Equals(mod.Id, modId, StringComparison.Ordinal));
        if (entry is null)
        {
            return MissingMod(profile, modId);
        }
        var package = await FindInstalledPackageAsync(entry.Id, entry.Version, cancellationToken)
            .ConfigureAwait(false);
        if (package is null)
        {
            return new(false, "The selected mod version is not installed.", profile,
                new(false, [$"{entry.Id} {entry.Version} is not installed."], []));
        }
        return await UpdateConfigurationAsync(
            profileName, modId, package.ConfigurationDefaults, cancellationToken).ConfigureAwait(false);
    }

    public async Task<ProfileChangeResult> MoveAsync(
        string profileName,
        string modId,
        int newIndex,
        CancellationToken cancellationToken = default)
    {
        var profile = await RequireProfileAsync(profileName, cancellationToken).ConfigureAwait(false);
        var mods = profile.Mods.OrderBy(mod => mod.Order).ThenBy(mod => mod.Id, StringComparer.Ordinal).ToList();
        var oldIndex = mods.FindIndex(mod => string.Equals(mod.Id, modId, StringComparison.Ordinal));
        if (oldIndex < 0)
        {
            return MissingMod(profile, modId);
        }
        if (newIndex < 0 || newIndex >= mods.Count)
        {
            throw new ArgumentOutOfRangeException(nameof(newIndex));
        }
        var selected = mods[oldIndex];
        mods.RemoveAt(oldIndex);
        mods.Insert(newIndex, selected);
        var ordered = mods.Select((mod, index) => mod with { Order = index }).ToArray();
        return await TrySaveAsync(
            profile, ordered, "Mod order changed.", cancellationToken).ConfigureAwait(false);
    }

    public async Task<ProfileChangeResult> RemoveAsync(
        string profileName,
        string modId,
        CancellationToken cancellationToken = default)
    {
        var profile = await RequireProfileAsync(profileName, cancellationToken).ConfigureAwait(false);
        var mods = profile.Mods.Where(mod => !string.Equals(mod.Id, modId, StringComparison.Ordinal)).ToArray();
        if (mods.Length == profile.Mods.Count)
        {
            return MissingMod(profile, modId);
        }
        return await TrySaveAsync(
            profile, mods, "Mod removed from profile.", cancellationToken).ConfigureAwait(false);
    }

    public async Task<PackageRemovalResult> UninstallPackageAsync(
        string modId,
        string version,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(modId);
        ArgumentException.ThrowIfNullOrWhiteSpace(version);
        var blockingProfiles = (await profiles.ListAsync(cancellationToken).ConfigureAwait(false))
            .Where(profile => profile.Mods.Any(mod =>
                string.Equals(mod.Id, modId, StringComparison.Ordinal) &&
                string.Equals(mod.Version, version, StringComparison.Ordinal)))
            .Select(profile => profile.Name)
            .Order(StringComparer.OrdinalIgnoreCase)
            .ToArray();
        if (blockingProfiles.Length != 0)
        {
            return new(false, "The package is still referenced by one or more profiles.", blockingProfiles);
        }

        var package = (await ModPackageService.ListInstalledAsync(
                managerStateRoot, buildId, cancellationToken).ConfigureAwait(false))
            .SingleOrDefault(value => string.Equals(value.Id, modId, StringComparison.Ordinal) &&
                string.Equals(value.Version, version, StringComparison.Ordinal));
        if (package is null)
        {
            return new(false, "The package is not installed.", []);
        }
        if (!package.Valid)
        {
            return new(false, "The installed package is invalid and was preserved for manual recovery.", []);
        }

        var packagesRoot = Path.Combine(managerStateRoot, "packages") + Path.DirectorySeparatorChar;
        var packagePath = Path.GetFullPath(package.PackagePath);
        if (!packagePath.StartsWith(packagesRoot, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("The installed package path is outside manager storage.");
        }
        File.Delete(packagePath);
        DeleteIfEmpty(Path.GetDirectoryName(packagePath), packagesRoot);
        DeleteIfEmpty(Path.GetDirectoryName(Path.GetDirectoryName(packagePath)), packagesRoot);
        return new(true, "Package uninstalled.", []);
    }

    private async Task<ProfileChangeResult> TrySaveAsync(
        ModProfile original,
        IReadOnlyList<ProfileModEntry> candidateMods,
        string successMessage,
        CancellationToken cancellationToken)
    {
        var candidate = original with { Mods = candidateMods };
        var validation = await ValidateAsync(candidate, cancellationToken).ConfigureAwait(false);
        if (!validation.Valid)
        {
            return new(false, "The requested change would make the profile invalid.", original, validation);
        }
        var saved = await profiles.SaveModsAsync(original.Name, candidateMods, cancellationToken)
            .ConfigureAwait(false);
        return new(true, successMessage, saved, validation);
    }

    private async Task<ProfileValidationResult> ValidateAsync(
        ModProfile profile,
        CancellationToken cancellationToken)
    {
        var errors = new List<string>();
        var installed = await ModPackageService.ListInstalledAsync(
            managerStateRoot, buildId, cancellationToken).ConfigureAwait(false);
        foreach (var invalid in installed.Where(package => !package.Valid))
        {
            errors.Add($"Installed package {Path.GetFileName(Path.GetDirectoryName(invalid.PackagePath))} is invalid: " +
                string.Join("; ", invalid.Errors));
        }
        var byVersion = installed.Where(package => package.Valid && package.Id is not null && package.Version is not null)
            .ToDictionary(package => (package.Id!, package.Version!), PackageIdentityComparer.Instance);
        foreach (var entry in profile.Mods)
        {
            if (byVersion.TryGetValue((entry.Id, entry.Version), out var configuredPackage))
            {
                errors.AddRange(ValidateConfiguration(configuredPackage, entry.Configuration));
            }
        }
        var enabledEntries = ProfileService.EnabledInProfileOrder(profile);
        var enabledById = enabledEntries.ToDictionary(mod => mod.Id, StringComparer.Ordinal);
        var enabledPackages = new Dictionary<string, ModPackageInfo>(StringComparer.Ordinal);
        foreach (var entry in enabledEntries)
        {
            if (!byVersion.TryGetValue((entry.Id, entry.Version), out var package))
            {
                errors.Add($"{entry.Id} {entry.Version} is enabled but not installed or compatible.");
                continue;
            }
            enabledPackages.Add(entry.Id, package);
        }

        var outgoing = enabledEntries.ToDictionary(
            entry => entry.Id, _ => new HashSet<string>(StringComparer.Ordinal), StringComparer.Ordinal);
        var indegree = enabledEntries.ToDictionary(entry => entry.Id, _ => 0, StringComparer.Ordinal);
        foreach (var (id, package) in enabledPackages)
        {
            foreach (var dependency in package.Dependencies)
            {
                if (!enabledById.TryGetValue(dependency.Id, out var dependencyEntry))
                {
                    errors.Add($"{id} requires enabled dependency {dependency.Id} {dependency.Version}.");
                    continue;
                }
                if (!VersionSatisfies(dependencyEntry.Version, dependency.Version))
                {
                    errors.Add($"{id} requires {dependency.Id} {dependency.Version}, but " +
                        $"{dependencyEntry.Version} is selected.");
                }
                AddEdge(dependency.Id, id, outgoing, indegree);
            }
            foreach (var before in package.LoadBefore.Where(enabledById.ContainsKey))
            {
                AddEdge(id, before, outgoing, indegree);
            }
            foreach (var after in package.LoadAfter.Where(enabledById.ContainsKey))
            {
                AddEdge(after, id, outgoing, indegree);
            }
        }

        var profileOrder = enabledEntries.Select((entry, index) => (entry.Id, index))
            .ToDictionary(value => value.Id, value => value.index, StringComparer.Ordinal);
        var ready = indegree.Where(value => value.Value == 0).Select(value => value.Key).ToList();
        var orderedIds = new List<string>();
        while (ready.Count != 0)
        {
            ready.Sort((left, right) =>
            {
                var comparison = profileOrder[left].CompareTo(profileOrder[right]);
                return comparison != 0 ? comparison : string.CompareOrdinal(left, right);
            });
            var id = ready[0];
            ready.RemoveAt(0);
            orderedIds.Add(id);
            foreach (var dependent in outgoing[id].Order(StringComparer.Ordinal))
            {
                if (--indegree[dependent] == 0)
                {
                    ready.Add(dependent);
                }
            }
        }
        if (orderedIds.Count != enabledEntries.Count)
        {
            errors.Add("The enabled mods contain a dependency or load-order cycle.");
        }
        var orderedPackages = orderedIds.Where(enabledPackages.ContainsKey)
            .Select(id => enabledPackages[id]).ToArray();
        return new(errors.Count == 0, errors, errors.Count == 0 ? orderedPackages : []);
    }

    private async Task<ModPackageInfo?> FindInstalledPackageAsync(
        string modId,
        string version,
        CancellationToken cancellationToken)
    {
        return (await ModPackageService.ListInstalledAsync(
                managerStateRoot, buildId, cancellationToken).ConfigureAwait(false))
            .SingleOrDefault(package => package.Valid &&
                string.Equals(package.Id, modId, StringComparison.Ordinal) &&
                string.Equals(package.Version, version, StringComparison.Ordinal));
    }

    private static List<string> ValidateConfiguration(
        ModPackageInfo package,
        IReadOnlyDictionary<string, System.Text.Json.JsonElement> configuration)
    {
        var errors = new List<string>();
        foreach (var key in configuration.Keys.Except(package.ConfigurationDefaults.Keys, StringComparer.Ordinal))
        {
            errors.Add($"{package.Id} configuration contains unknown setting '{key}'.");
        }
        foreach (var (key, defaultValue) in package.ConfigurationDefaults)
        {
            if (!configuration.TryGetValue(key, out var value))
            {
                errors.Add($"{package.Id} configuration is missing setting '{key}'.");
                continue;
            }
            if (!KindsMatch(value.ValueKind, defaultValue.ValueKind))
            {
                errors.Add($"{package.Id} setting '{key}' must be {DescribeKind(defaultValue.ValueKind)}.");
            }
        }
        return errors;
    }

    private static bool KindsMatch(
        System.Text.Json.JsonValueKind left,
        System.Text.Json.JsonValueKind right) =>
        left == right ||
        left is System.Text.Json.JsonValueKind.True or System.Text.Json.JsonValueKind.False &&
        right is System.Text.Json.JsonValueKind.True or System.Text.Json.JsonValueKind.False;

    private static string DescribeKind(System.Text.Json.JsonValueKind kind) => kind switch
    {
        System.Text.Json.JsonValueKind.True or System.Text.Json.JsonValueKind.False => "a boolean",
        System.Text.Json.JsonValueKind.Number => "a number",
        System.Text.Json.JsonValueKind.String => "text",
        System.Text.Json.JsonValueKind.Object => "an object",
        System.Text.Json.JsonValueKind.Array => "a list",
        System.Text.Json.JsonValueKind.Null => "null",
        _ => "a defined value"
    };

    private async Task<ModProfile> RequireProfileAsync(
        string profileName,
        CancellationToken cancellationToken)
    {
        return await profiles.LoadAsync(profileName, cancellationToken).ConfigureAwait(false) ??
            throw new InvalidOperationException("The profile does not exist.");
    }

    private static ProfileChangeResult MissingMod(ModProfile profile, string modId)
    {
        var validation = new ProfileValidationResult(false, [$"{modId} is not in the profile."], []);
        return new(false, "The mod is not in the profile.", profile, validation);
    }

    private static void AddEdge(
        string from,
        string to,
        Dictionary<string, HashSet<string>> outgoing,
        Dictionary<string, int> indegree)
    {
        if (from != to && outgoing.TryGetValue(from, out var edges) &&
            indegree.TryGetValue(to, out var degree) && edges.Add(to))
        {
            indegree[to] = degree + 1;
        }
    }

    private static bool VersionSatisfies(string versionText, string requirement)
    {
        if (requirement == "*")
        {
            return true;
        }
        var operation = requirement.StartsWith(">=", StringComparison.Ordinal) ? ">=" :
            requirement.StartsWith('^') ? "^" : "=";
        var expectedText = operation == "=" ? requirement : requirement[operation.Length..];
        if (!ParsedVersion.TryParse(versionText, out var version) ||
            !ParsedVersion.TryParse(expectedText, out var expected))
        {
            return false;
        }
        if (operation == "=")
        {
            return version == expected;
        }
        if (version.NumericCompareTo(expected) < 0)
        {
            return false;
        }
        if (operation == ">=")
        {
            return true;
        }
        return expected.Major > 0 ? version.Major == expected.Major :
            expected.Minor > 0 ? version.Major == 0 && version.Minor == expected.Minor :
            version.Major == 0 && version.Minor == 0 && version.Patch == expected.Patch;
    }

    private static void DeleteIfEmpty(string? directory, string packagesRoot)
    {
        if (directory is not null && directory.StartsWith(packagesRoot, StringComparison.OrdinalIgnoreCase) &&
            Directory.Exists(directory) && !Directory.EnumerateFileSystemEntries(directory).Any())
        {
            Directory.Delete(directory);
        }
    }

    private readonly record struct ParsedVersion(int Major, int Minor, int Patch, string Prerelease)
    {
        public static bool TryParse(string text, out ParsedVersion version)
        {
            version = default;
            var withoutBuild = text.Split('+', 2)[0];
            var components = withoutBuild.Split('-', 2);
            var numbers = components[0].Split('.');
            if (numbers.Length != 3 || !int.TryParse(numbers[0], out var major) ||
                !int.TryParse(numbers[1], out var minor) || !int.TryParse(numbers[2], out var patch))
            {
                return false;
            }
            version = new(major, minor, patch, components.Length == 2 ? components[1] : string.Empty);
            return true;
        }

        public int NumericCompareTo(ParsedVersion other) =>
            (Major, Minor, Patch).CompareTo((other.Major, other.Minor, other.Patch));
    }

    private sealed class PackageIdentityComparer : IEqualityComparer<(string Id, string Version)>
    {
        public static PackageIdentityComparer Instance { get; } = new();

        public bool Equals((string Id, string Version) left, (string Id, string Version) right) =>
            string.Equals(left.Id, right.Id, StringComparison.Ordinal) &&
            string.Equals(left.Version, right.Version, StringComparison.Ordinal);

        public int GetHashCode((string Id, string Version) value) => HashCode.Combine(
            StringComparer.Ordinal.GetHashCode(value.Id),
            StringComparer.Ordinal.GetHashCode(value.Version));
    }
}
