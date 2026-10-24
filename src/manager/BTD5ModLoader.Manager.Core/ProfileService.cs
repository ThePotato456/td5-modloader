using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace BTD5ModLoader.Manager.Core;

public sealed record ProfileModEntry(
    string Id,
    string Version,
    bool Enabled,
    int Order,
    IReadOnlyDictionary<string, JsonElement> Configuration);

public sealed record ProfileLaunchEntry(
    DateTimeOffset StartedAtUtc,
    string Mode,
    bool Successful,
    string? Detail);

public sealed record ModProfile(
    int SchemaVersion,
    string Name,
    DateTimeOffset CreatedAtUtc,
    DateTimeOffset ModifiedAtUtc,
    IReadOnlyList<ProfileModEntry> Mods,
    IReadOnlyList<ProfileLaunchEntry> LaunchHistory);

public sealed class ProfileService
{
    private const int MaximumLaunchHistory = 100;
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };
    private readonly string profilesRoot;

    public ProfileService(string managerStateRoot)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(managerStateRoot);
        profilesRoot = Path.Combine(Path.GetFullPath(managerStateRoot), "profiles");
    }

    public async Task<ModProfile> CreateAsync(
        string name,
        CancellationToken cancellationToken = default)
    {
        ValidateName(name);
        var path = GetProfilePath(name);
        if (File.Exists(path))
        {
            throw new InvalidOperationException("A profile with this name already exists.");
        }
        var now = DateTimeOffset.UtcNow;
        var profile = new ModProfile(1, name, now, now, [], []);
        await SaveNewAsync(path, profile, cancellationToken).ConfigureAwait(false);
        return profile;
    }

    public async Task<ModProfile?> LoadAsync(
        string name,
        CancellationToken cancellationToken = default)
    {
        ValidateName(name);
        var path = GetProfilePath(name);
        if (!File.Exists(path))
        {
            return null;
        }
        var profile = await ReadAsync(path, cancellationToken).ConfigureAwait(false);
        if (!string.Equals(profile.Name, name, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("The profile name does not match its storage key.");
        }
        return profile;
    }

    public async Task<IReadOnlyList<ModProfile>> ListAsync(
        CancellationToken cancellationToken = default)
    {
        if (!Directory.Exists(profilesRoot))
        {
            return [];
        }
        var profiles = new List<ModProfile>();
        foreach (var path in Directory.EnumerateFiles(profilesRoot, "*.json")
                     .Order(StringComparer.OrdinalIgnoreCase))
        {
            profiles.Add(await ReadAsync(path, cancellationToken).ConfigureAwait(false));
        }
        return profiles.OrderBy(profile => profile.Name, StringComparer.OrdinalIgnoreCase).ToArray();
    }

    public async Task<ModProfile> SaveModsAsync(
        string name,
        IEnumerable<ProfileModEntry> mods,
        CancellationToken cancellationToken = default)
    {
        ValidateName(name);
        ArgumentNullException.ThrowIfNull(mods);
        var current = await LoadRequiredAsync(name, cancellationToken).ConfigureAwait(false);
        var normalized = mods
            .OrderBy(mod => mod.Order)
            .ThenBy(mod => mod.Id, StringComparer.Ordinal)
            .Select((mod, index) => mod with { Order = index })
            .ToArray();
        ValidateMods(normalized);
        var updated = current with
        {
            ModifiedAtUtc = DateTimeOffset.UtcNow,
            Mods = normalized
        };
        await ReplaceAsync(GetProfilePath(name), updated, cancellationToken).ConfigureAwait(false);
        return updated;
    }

    public async Task<ModProfile> RenameAsync(
        string name,
        string newName,
        CancellationToken cancellationToken = default)
    {
        ValidateName(name);
        ValidateName(newName);
        var current = await LoadRequiredAsync(name, cancellationToken).ConfigureAwait(false);
        var sourcePath = GetProfilePath(name);
        var destinationPath = GetProfilePath(newName);
        if (File.Exists(destinationPath) && !PathsEqual(sourcePath, destinationPath))
        {
            throw new InvalidOperationException("A profile with the new name already exists.");
        }
        var updated = current with { Name = newName, ModifiedAtUtc = DateTimeOffset.UtcNow };
        if (PathsEqual(sourcePath, destinationPath))
        {
            await ReplaceAsync(sourcePath, updated, cancellationToken).ConfigureAwait(false);
            return updated;
        }
        await SaveNewAsync(destinationPath, updated, cancellationToken).ConfigureAwait(false);
        try
        {
            File.Delete(sourcePath);
        }
        catch
        {
            File.Delete(destinationPath);
            throw;
        }
        return updated;
    }

    public async Task<ModProfile> DuplicateAsync(
        string name,
        string copyName,
        CancellationToken cancellationToken = default)
    {
        ValidateName(name);
        ValidateName(copyName);
        var source = await LoadRequiredAsync(name, cancellationToken).ConfigureAwait(false);
        var path = GetProfilePath(copyName);
        if (File.Exists(path))
        {
            throw new InvalidOperationException("A profile with this name already exists.");
        }
        var now = DateTimeOffset.UtcNow;
        var copy = source with
        {
            Name = copyName,
            CreatedAtUtc = now,
            ModifiedAtUtc = now,
            LaunchHistory = []
        };
        await SaveNewAsync(path, copy, cancellationToken).ConfigureAwait(false);
        return copy;
    }

    public async Task DeleteAsync(
        string name,
        CancellationToken cancellationToken = default)
    {
        ValidateName(name);
        var path = GetProfilePath(name);
        if (!File.Exists(path))
        {
            throw new InvalidOperationException("The profile does not exist.");
        }
        cancellationToken.ThrowIfCancellationRequested();
        File.Delete(path);
    }

    public async Task<ModProfile> RecordLaunchAsync(
        string name,
        string mode,
        bool successful,
        string? detail = null,
        CancellationToken cancellationToken = default)
    {
        ValidateName(name);
        if (mode is not ("modded" or "vanilla"))
        {
            throw new ArgumentException("Launch mode must be modded or vanilla.", nameof(mode));
        }
        var current = await LoadRequiredAsync(name, cancellationToken).ConfigureAwait(false);
        var history = current.LaunchHistory
            .Append(new ProfileLaunchEntry(DateTimeOffset.UtcNow, mode, successful, detail))
            .TakeLast(MaximumLaunchHistory)
            .ToArray();
        var updated = current with
        {
            ModifiedAtUtc = DateTimeOffset.UtcNow,
            LaunchHistory = history
        };
        await ReplaceAsync(GetProfilePath(name), updated, cancellationToken).ConfigureAwait(false);
        return updated;
    }

    public static IReadOnlyList<ProfileModEntry> EnabledInProfileOrder(ModProfile profile)
    {
        ArgumentNullException.ThrowIfNull(profile);
        return profile.Mods.Where(mod => mod.Enabled)
            .OrderBy(mod => mod.Order)
            .ThenBy(mod => mod.Id, StringComparer.Ordinal)
            .ToArray();
    }

    private async Task<ModProfile> LoadRequiredAsync(
        string name,
        CancellationToken cancellationToken)
    {
        return await LoadAsync(name, cancellationToken).ConfigureAwait(false) ??
               throw new InvalidOperationException("The profile does not exist.");
    }

    private static void ValidateName(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        if (name.Length > 64 || name.Any(char.IsControl))
        {
            throw new ArgumentException("Profile names must be 1-64 printable characters.", nameof(name));
        }
    }

    private static void ValidateMods(IReadOnlyList<ProfileModEntry> mods)
    {
        if (mods.Select(mod => mod.Id).Distinct(StringComparer.Ordinal).Count() != mods.Count)
        {
            throw new InvalidDataException("A profile cannot contain duplicate mod IDs.");
        }
        foreach (var mod in mods)
        {
            if (string.IsNullOrWhiteSpace(mod.Id) || string.IsNullOrWhiteSpace(mod.Version) ||
                mod.Order < 0)
            {
                throw new InvalidDataException("A profile contains an invalid mod entry.");
            }
        }
    }

    private string GetProfilePath(string name)
    {
        var normalized = name.Normalize(NormalizationForm.FormKC).ToUpperInvariant();
        var key = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(normalized)));
        return Path.Combine(profilesRoot, key + ".json");
    }

    private static bool PathsEqual(string left, string right) =>
        string.Equals(Path.GetFullPath(left), Path.GetFullPath(right), StringComparison.OrdinalIgnoreCase);

    private static async Task<ModProfile> ReadAsync(
        string path,
        CancellationToken cancellationToken)
    {
        using var stream = File.OpenRead(path);
        var profile = await JsonSerializer.DeserializeAsync<ModProfile>(
            stream, JsonOptions, cancellationToken).ConfigureAwait(false) ??
            throw new InvalidDataException("The profile file is empty.");
        ValidateName(profile.Name);
        ValidateMods(profile.Mods);
        if (profile.SchemaVersion != 1 || profile.LaunchHistory.Count > MaximumLaunchHistory)
        {
            throw new InvalidDataException("The profile file is invalid or unsupported.");
        }
        return profile;
    }

    private static async Task SaveNewAsync(
        string path,
        ModProfile profile,
        CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + ".btd5ml-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            await File.WriteAllTextAsync(
                temporary, JsonSerializer.Serialize(profile, JsonOptions), cancellationToken)
                .ConfigureAwait(false);
            File.Move(temporary, path, false);
        }
        finally
        {
            File.Delete(temporary);
        }
    }

    private static async Task ReplaceAsync(
        string path,
        ModProfile profile,
        CancellationToken cancellationToken)
    {
        var temporary = path + ".btd5ml-" + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            await File.WriteAllTextAsync(
                temporary, JsonSerializer.Serialize(profile, JsonOptions), cancellationToken)
                .ConfigureAwait(false);
            File.Move(temporary, path, true);
        }
        finally
        {
            File.Delete(temporary);
        }
    }
}
