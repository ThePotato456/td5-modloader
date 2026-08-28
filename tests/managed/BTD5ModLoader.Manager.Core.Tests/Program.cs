using BTD5ModLoader.Manager.Core;
using System.IO.Compression;

if (string.IsNullOrWhiteSpace(ProductInfo.Name) ||
    string.IsNullOrWhiteSpace(ProductInfo.Version))
{
    return Fail("Product metadata smoke test failed.");
}

var testRoot = Path.Combine(Path.GetTempPath(), "btd5ml-manager-tests-" + Guid.NewGuid().ToString("N"));
try
{
    var steamRoot = Path.Combine(testRoot, "Steam");
    var libraryRoot = Path.Combine(testRoot, "SteamLibrary");
    var gameDirectory = Path.Combine(libraryRoot, "steamapps", "common", "BloonsTD5");
    var artifactDirectory = Path.Combine(testRoot, "artifacts");
    var stateRoot = Path.Combine(testRoot, "state");
    Directory.CreateDirectory(Path.Combine(steamRoot, "steamapps"));
    Directory.CreateDirectory(Path.Combine(gameDirectory, "Assets"));
    Directory.CreateDirectory(Path.Combine(artifactDirectory, "symbols"));

    var escapedLibrary = libraryRoot.Replace("\\", "\\\\", StringComparison.Ordinal);
    await File.WriteAllTextAsync(
        Path.Combine(steamRoot, "steamapps", "libraryfolders.vdf"),
        $"\"libraryfolders\"\n{{\n  \"1\" {{ \"path\" \"{escapedLibrary}\" }}\n}}");
    var fixtureExecutable = new byte[512];
    fixtureExecutable[0] = (byte)'M';
    fixtureExecutable[1] = (byte)'Z';
    fixtureExecutable[0x3c] = 0x80;
    fixtureExecutable[0x80] = (byte)'P';
    fixtureExecutable[0x81] = (byte)'E';
    fixtureExecutable[0x84] = 0x4c;
    fixtureExecutable[0x85] = 0x01;
    await File.WriteAllBytesAsync(Path.Combine(gameDirectory, "BTD5-Win.exe"), fixtureExecutable);
    await File.WriteAllTextAsync(Path.Combine(gameDirectory, "Assets", "BTD5.jet"), "fixture assets");
    await File.WriteAllTextAsync(Path.Combine(artifactDirectory, "wininet.dll"), "proxy fixture");
    await File.WriteAllTextAsync(
        Path.Combine(artifactDirectory, "btd5loader_runtime.dll"), "runtime fixture");
    await File.WriteAllTextAsync(
        Path.Combine(artifactDirectory, "symbols", "steam-win32-4.8.json"), "{}");

    var executableHash = await GameDiscovery.HashFileAsync(Path.Combine(gameDirectory, "BTD5-Win.exe"));
    var assetsHash = await GameDiscovery.HashFileAsync(Path.Combine(gameDirectory, "Assets", "BTD5.jet"));
    var builds = new[] { new KnownGameBuild("fixture-build", executableHash, assetsHash) };
    var discovered = GameDiscovery.DiscoverGameDirectories(steamRoot).ToArray();
    Assert(discovered.Length == 1 && PathsEqual(discovered[0], gameDirectory),
        "Steam library discovery did not find the fixture game.");
    var validation = await GameDiscovery.ValidateAsync(gameDirectory, builds);
    Assert(validation.Supported && validation.BuildId == "fixture-build",
        "Known game build validation failed.");

    var service = new LoaderInstallationService(stateRoot, builds);
    var proxyTarget = Path.Combine(gameDirectory, "wininet.dll");
    await File.WriteAllTextAsync(proxyTarget, "pre-existing proxy");
    var conflict = await service.InstallAsync(gameDirectory, artifactDirectory);
    Assert(!conflict.Success && conflict.Conflicts.SequenceEqual(new[] { "wininet.dll" }),
        "Install did not report the pre-existing proxy conflict.");
    Assert(await File.ReadAllTextAsync(proxyTarget) == "pre-existing proxy",
        "Install changed the pre-existing proxy.");
    File.Delete(proxyTarget);

    var install = await service.InstallAsync(gameDirectory, artifactDirectory);
    Assert(install.Success && File.Exists(service.GetRecordPath(gameDirectory)),
        "Loader installation failed.");
    Assert((await service.VerifyAsync(gameDirectory)).Success, "Installed loader did not verify.");

    var runtimeTarget = Path.Combine(gameDirectory, "btd5loader_runtime.dll");
    File.Delete(runtimeTarget);
    var missingVerification = await service.VerifyAsync(gameDirectory);
    Assert(!missingVerification.Success &&
        missingVerification.Conflicts.Contains("btd5loader_runtime.dll", StringComparer.OrdinalIgnoreCase),
        "Verification did not report a missing loader file.");
    Assert((await service.RepairAsync(gameDirectory, artifactDirectory)).Success && File.Exists(runtimeTarget),
        "Repair did not restore a missing loader-owned file.");

    await File.WriteAllTextAsync(proxyTarget, "user-modified proxy");
    var repairConflict = await service.RepairAsync(gameDirectory, artifactDirectory);
    Assert(!repairConflict.Success &&
        repairConflict.Conflicts.Contains("wininet.dll", StringComparer.OrdinalIgnoreCase),
        "Repair did not preserve and report a modified loader file.");
    Assert(await File.ReadAllTextAsync(proxyTarget) == "user-modified proxy",
        "Repair overwrote a modified loader file.");

    var partialUninstall = await service.UninstallAsync(gameDirectory);
    Assert(!partialUninstall.Success && File.Exists(proxyTarget) && !File.Exists(runtimeTarget),
        "Uninstall did not preserve only the modified loader file.");
    File.Copy(Path.Combine(artifactDirectory, "wininet.dll"), proxyTarget, true);
    var uninstall = await service.UninstallAsync(gameDirectory);
    Assert(uninstall.Success && !File.Exists(proxyTarget) && !File.Exists(service.GetRecordPath(gameDirectory)),
        "Uninstall did not finish after the conflict was recovered.");
    Assert(await GameDiscovery.HashFileAsync(Path.Combine(gameDirectory, "BTD5-Win.exe")) == executableHash &&
        await GameDiscovery.HashFileAsync(Path.Combine(gameDirectory, "Assets", "BTD5.jet")) == assetsHash,
        "The install workflow modified proprietary game files.");

    var packagePath = Path.Combine(testRoot, "lifecycle.btd5mod");
    const string manifest = """
        {
          "id": "sample.lifecycle",
          "name": "Lifecycle Sample",
          "author": "Test Author",
          "version": "1.0.0",
          "entry_point": "lua/main.lua",
          "loader_api": 1,
          "supported_game_builds": ["fixture-build"],
          "dependencies": [{"id":"sample.library","version":"^1.0.0"}],
          "load_order": {"before":[],"after":[]},
          "capabilities": ["storage"]
        }
        """;
    CreatePackage(packagePath, manifest, ("lua/main.lua", "return {}"));
    var packageInfo = await ModPackageService.InspectAsync(packagePath, "fixture-build");
    Assert(packageInfo.Valid && packageInfo.Id == "sample.lifecycle" &&
        packageInfo.Dependencies.Count == 1 && packageInfo.Capabilities.SequenceEqual(new[] { "storage" }),
        "Valid package identity and permissions were not inspected correctly.");
    var packageInstall = await ModPackageService.InstallAsync(packagePath, stateRoot, "fixture-build");
    Assert(packageInstall.Success && packageInstall.InstalledPath is not null &&
        File.Exists(packageInstall.InstalledPath), "Valid package installation failed.");
    Assert((await ModPackageService.InstallAsync(packagePath, stateRoot, "fixture-build")).Success,
        "Reinstalling an identical package was not idempotent.");

    var incompatible = await ModPackageService.InspectAsync(packagePath, "other-build");
    Assert(!incompatible.Valid && incompatible.Errors.Any(error => error.Contains(
        "does not support", StringComparison.Ordinal)), "An incompatible package was accepted.");
    var unsafePackage = Path.Combine(testRoot, "unsafe.btd5mod");
    CreatePackage(unsafePackage, manifest, ("lua/main.lua", "return {}"), ("../outside.txt", "unsafe"));
    var unsafeInfo = await ModPackageService.InspectAsync(unsafePackage, "fixture-build");
    Assert(!unsafeInfo.Valid && unsafeInfo.Errors.Any(error => error.Contains(
        "Unsafe", StringComparison.Ordinal)), "A package containing path traversal was accepted.");

    var profileService = new ProfileService(stateRoot);
    var profile = await profileService.CreateAsync("Testing");
    Assert(profile.Name == "Testing" && (await profileService.ListAsync()).Count == 1,
        "Named profile creation or listing failed.");
    var configuredMods = new[]
    {
        new ProfileModEntry(
            "sample.second", "2.0.0", true, 20,
            new Dictionary<string, System.Text.Json.JsonElement>
            {
                ["difficulty"] = System.Text.Json.JsonSerializer.SerializeToElement("hard")
            }),
        new ProfileModEntry(
            "sample.lifecycle", "1.0.0", true, 10,
            new Dictionary<string, System.Text.Json.JsonElement>
            {
                ["greeting"] = System.Text.Json.JsonSerializer.SerializeToElement("hello")
            })
    };
    profile = await profileService.SaveModsAsync("Testing", configuredMods);
    var ordered = ProfileService.EnabledInProfileOrder(profile);
    Assert(ordered.Select(mod => mod.Id).SequenceEqual(new[] { "sample.lifecycle", "sample.second" }) &&
        ordered.Select(mod => mod.Order).SequenceEqual(new[] { 0, 1 }),
        "Profile mod order was not normalized deterministically.");
    profile = await profileService.RecordLaunchAsync("Testing", "modded", false, "fixture crash");
    Assert(profile.LaunchHistory is [{ Mode: "modded", Successful: false }] &&
        profile.Mods[0].Configuration["greeting"].GetString() == "hello",
        "Profile configuration or launch history did not round-trip.");
    await AssertThrowsAsync<InvalidOperationException>(
        () => profileService.CreateAsync("testing"),
        "Case-insensitive duplicate profile name was accepted.");

    Console.WriteLine("Manager core integration tests passed.");
    return 0;
}
catch (Exception exception)
{
    return Fail(exception.ToString());
}
finally
{
    if (Directory.Exists(testRoot))
    {
        Directory.Delete(testRoot, true);
    }
}

static void Assert(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

static bool PathsEqual(string left, string right) =>
    string.Equals(Path.GetFullPath(left), Path.GetFullPath(right), StringComparison.OrdinalIgnoreCase);

static int Fail(string message)
{
    Console.Error.WriteLine(message);
    return 1;
}

static void CreatePackage(
    string path,
    string manifest,
    params (string Path, string Contents)[] entries)
{
    using var archive = ZipFile.Open(path, ZipArchiveMode.Create);
    WriteEntry("mod.json", manifest);
    foreach (var entry in entries)
    {
        WriteEntry(entry.Path, entry.Contents);
    }

    void WriteEntry(string entryPath, string contents)
    {
        var entry = archive.CreateEntry(entryPath, CompressionLevel.Optimal);
        using var writer = new StreamWriter(entry.Open());
        writer.Write(contents);
    }
}

static async Task AssertThrowsAsync<TException>(Func<Task> action, string message)
    where TException : Exception
{
    try
    {
        await action();
    }
    catch (TException)
    {
        return;
    }
    throw new InvalidOperationException(message);
}
