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
          "capabilities": ["storage"],
          "configuration_defaults": {"greeting":"hello"}
        }
        """;
    CreatePackage(packagePath, manifest, ("lua/main.lua", "return {}"));
    var packageInfo = await ModPackageService.InspectAsync(packagePath, "fixture-build");
    Assert(packageInfo.Valid && packageInfo.Id == "sample.lifecycle" &&
        packageInfo.Dependencies.Count == 1 && packageInfo.Capabilities.SequenceEqual(new[] { "storage" }) &&
        packageInfo.ConfigurationDefaults["greeting"].GetString() == "hello",
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

    var operationsStateRoot = Path.Combine(testRoot, "operations-state");
    var libraryPackage = Path.Combine(testRoot, "library.btd5mod");
    var applicationV1Package = Path.Combine(testRoot, "application-v1.btd5mod");
    var applicationV11Package = Path.Combine(testRoot, "application-v11.btd5mod");
    var applicationV2Package = Path.Combine(testRoot, "application-v2.btd5mod");
    CreatePackage(libraryPackage, BuildManifest("sample.library", "1.0.0"),
        ("lua/main.lua", "return {}"));
    CreatePackage(applicationV1Package,
        BuildManifest("sample.application", "1.0.0", "sample.library", "^1.0.0"),
        ("lua/main.lua", "return {}"));
    CreatePackage(applicationV11Package,
        BuildManifest("sample.application", "1.1.0", "sample.library", "^1.0.0"),
        ("lua/main.lua", "return {}"));
    CreatePackage(applicationV2Package,
        BuildManifest("sample.application", "2.0.0", "sample.library", "^2.0.0"),
        ("lua/main.lua", "return {}"));
    foreach (var operationPackage in new[]
             {
                 libraryPackage, applicationV1Package, applicationV11Package, applicationV2Package
             })
    {
        Assert((await ModPackageService.InstallAsync(
            operationPackage, operationsStateRoot, "fixture-build")).Success,
            "Operation test package installation failed.");
    }

    var operationsProfiles = new ProfileService(operationsStateRoot);
    await operationsProfiles.CreateAsync("Operations");
    var operations = new ProfileModService(operationsStateRoot, "fixture-build");
    var missingDependency = await operations.EnableAsync(
        "Operations", "sample.application", "1.0.0");
    Assert(!missingDependency.Success && missingDependency.Validation.Errors.Any(error =>
        error.Contains("requires enabled dependency", StringComparison.Ordinal)),
        "Enabling a mod with a missing dependency was accepted.");
    Assert((await operations.EnableAsync("Operations", "sample.library", "1.0.0")).Success,
        "Dependency package could not be enabled.");
    Assert((await operations.EnableAsync("Operations", "sample.application", "1.0.0")).Success,
        "Mod could not be enabled after its dependency.");
    var dependencyOrder = await operations.ValidateAsync("Operations");
    Assert(dependencyOrder.Valid && dependencyOrder.OrderedPackages.Select(package => package.Id)
        .SequenceEqual(new[] { "sample.library", "sample.application" }),
        "Dependency-safe load order was not resolved.");
    Assert((await operations.MoveAsync("Operations", "sample.application", 0)).Success &&
        (await operations.ValidateAsync("Operations")).OrderedPackages.Select(package => package.Id)
        .SequenceEqual(new[] { "sample.library", "sample.application" }),
        "Profile reordering overrode a mandatory dependency edge.");
    var blockedDisable = await operations.DisableAsync("Operations", "sample.library");
    Assert(!blockedDisable.Success && blockedDisable.Validation.Errors.Any(error =>
        error.Contains("requires enabled dependency", StringComparison.Ordinal)),
        "Disabling a required dependency was accepted.");
    Assert((await operations.ChangeVersionAsync(
        "Operations", "sample.application", "1.1.0")).Success,
        "Compatible mod upgrade failed.");
    var incompatibleUpgrade = await operations.ChangeVersionAsync(
        "Operations", "sample.application", "2.0.0");
    Assert(!incompatibleUpgrade.Success && incompatibleUpgrade.Validation.Errors.Any(error =>
        error.Contains("but 1.0.0 is selected", StringComparison.Ordinal)),
        "An upgrade with an incompatible dependency was accepted.");
    Assert((await operations.ChangeVersionAsync(
        "Operations", "sample.application", "1.0.0")).Success,
        "Compatible mod downgrade failed.");
    var referencedUninstall = await operations.UninstallPackageAsync("sample.application", "1.0.0");
    Assert(!referencedUninstall.Success && referencedUninstall.BlockingProfiles.SequenceEqual(
        new[] { "Operations" }), "A profile-referenced package was uninstalled.");
    Assert((await operations.RemoveAsync("Operations", "sample.application")).Success,
        "A dependency-safe profile removal failed.");
    Assert((await operations.UninstallPackageAsync("sample.application", "1.0.0")).Success,
        "An unreferenced package could not be uninstalled.");

    Assert((await new LoaderInstallationService(operationsStateRoot, builds)
        .InstallAsync(gameDirectory, artifactDirectory)).Success,
        "Launch test loader installation failed.");
    Directory.CreateDirectory(Path.Combine(operationsStateRoot, "logs"));
    await File.WriteAllTextAsync(
        Path.Combine(operationsStateRoot, "logs", "runtime.jsonl"),
        "{\"component\":\"runtime\",\"message\":\"hooks_ready_no_hooks_registered\"}\n");
    var recordingLauncher = new RecordingGameProcessLauncher();
    var launches = new GameLaunchService(operationsStateRoot, builds, recordingLauncher);
    var launchStatus = await launches.GetStatusAsync(gameDirectory, "Operations");
    Assert(launchStatus is
    {
        GameSupported: true,
        LoaderVerified: true,
        ProfileValid: true,
        RuntimeState: "hooks_ready_no_hooks_registered"
    }, "Launch readiness status was incorrect.");
    Assert(!(await launches.LaunchModdedAsync(gameDirectory, "Operations", false)).Success &&
        recordingLauncher.Requests.Count == 0,
        "Modded launch did not require the offline-risk acknowledgement.");
    var moddedLaunch = await launches.LaunchModdedAsync(gameDirectory, "Operations", true);
    Assert(moddedLaunch.Success && recordingLauncher.Requests is
        [{ UseShellExecute: false }], "Validated modded launch was not handed to the process launcher.");
    var moddedRequest = recordingLauncher.Requests[0];
    Assert(moddedRequest.Environment.TryGetValue("BTD5ML_ACTIVE_PROFILE", out var handoffPath) &&
        File.Exists(handoffPath) &&
        (await File.ReadAllTextAsync(handoffPath)).Contains("sample.library", StringComparison.Ordinal),
        "Modded launch did not write and pass the active-profile handoff.");
    Assert((await launches.LaunchVanillaAsync("Operations")).Success &&
        recordingLauncher.Requests is [_, { FileName: "steam://run/306020", UseShellExecute: true }],
        "Vanilla launch was not handed to Steam.");
    var launchedProfile = await operationsProfiles.LoadAsync("Operations");
    Assert(launchedProfile?.LaunchHistory.Select(entry => entry.Mode)
        .SequenceEqual(new[] { "modded", "vanilla" }) == true,
        "Profile launch history did not record modded and vanilla launches.");
    await File.AppendAllTextAsync(
        Path.Combine(operationsStateRoot, "logs", "runtime.jsonl"),
        $"{{\"component\":\"test\",\"message\":\"{gameDirectory} {operationsStateRoot}\"}}\n");
    var diagnosticsPath = Path.Combine(testRoot, "diagnostics.zip");
    var diagnostics = await new DiagnosticsService(operationsStateRoot, builds)
        .ExportAsync(diagnosticsPath, gameDirectory, "Operations");
    Assert(diagnostics.Success && File.Exists(diagnosticsPath), "Diagnostics export failed.");
    using (var diagnosticsArchive = await ZipFile.OpenReadAsync(diagnosticsPath))
    {
        Assert(diagnosticsArchive.GetEntry("summary.json") is not null &&
            diagnosticsArchive.GetEntry("runtime.jsonl") is not null &&
            diagnosticsArchive.GetEntry("README.txt") is not null,
            "Diagnostics export omitted required reports.");
        using var reader = new StreamReader(
            await diagnosticsArchive.GetEntry("runtime.jsonl")!.OpenAsync());
        var exportedLog = await reader.ReadToEndAsync();
        Assert(!exportedLog.Contains(gameDirectory, StringComparison.OrdinalIgnoreCase) &&
            !exportedLog.Contains(operationsStateRoot, StringComparison.OrdinalIgnoreCase) &&
            exportedLog.Contains("[GAME_DIRECTORY]", StringComparison.Ordinal),
            "Diagnostics export did not redact machine-specific paths.");
    }

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

static string BuildManifest(
    string id,
    string version,
    string? dependencyId = null,
    string? dependencyVersion = null)
{
    var dependencies = dependencyId is null
        ? "[]"
        : $"[{{\"id\":\"{dependencyId}\",\"version\":\"{dependencyVersion}\"}}]";
    return $$"""
        {
          "id": "{{id}}",
          "name": "{{id}}",
          "author": "Test Author",
          "version": "{{version}}",
          "entry_point": "lua/main.lua",
          "loader_api": 1,
          "supported_game_builds": ["fixture-build"],
          "dependencies": {{dependencies}},
          "load_order": {"before":[],"after":[]},
          "capabilities": []
        }
        """;
}

sealed class RecordingGameProcessLauncher : IGameProcessLauncher
{
    public List<GameProcessRequest> Requests { get; } = [];

    public int Start(GameProcessRequest request)
    {
        Requests.Add(request);
        return 1000 + Requests.Count;
    }
}
