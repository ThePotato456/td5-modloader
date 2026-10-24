using BTD5ModLoader.Manager.Core;
using System.IO.Compression;
using System.Text.Json;

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
    Assert(!GameDiscovery.DiscoverRegisteredGameDirectories(steamRoot).Any(),
        "A copied folder without an app manifest was treated as registered.");
    var appManifest = Path.Combine(libraryRoot, "steamapps", "appmanifest_306020.acf");
    await File.WriteAllTextAsync(appManifest, "\"appid\" \"306020\"\n\"installdir\" \"BloonsTD5\"");
    Assert(GameDiscovery.DiscoverRegisteredGameDirectories(steamRoot).Single() == gameDirectory,
        "Registered Steam discovery did not follow the app manifest.");
    await File.WriteAllTextAsync(appManifest, "\"appid\" \"306020\"\n\"installdir\" \"..\"");
    Assert(!GameDiscovery.DiscoverRegisteredGameDirectories(steamRoot).Any(),
        "An invalid app manifest escaped the library directory.");
    File.Delete(appManifest);
    var validation = await GameDiscovery.ValidateAsync(gameDirectory, builds);
    Assert(validation.Supported && validation.BuildId == "fixture-build",
        "Known game build validation failed.");

    var service = new LoaderInstallationService(stateRoot, builds);
    var proxyTarget = Path.Combine(gameDirectory, "wininet.dll");
    var unsupportedDirectory = Path.Combine(testRoot, "unsupported-game");
    Directory.CreateDirectory(unsupportedDirectory);
    Assert((await service.InspectAsync(unsupportedDirectory, artifactDirectory)).State ==
        LoaderHealthState.UnsupportedGame, "An unsupported game was not reported explicitly.");
    Assert((await service.InspectAsync(gameDirectory, artifactDirectory)).State ==
        LoaderHealthState.NotInstalled, "A clean game was not reported as not installed.");
    await File.WriteAllTextAsync(proxyTarget, "pre-existing proxy");
    var foreignHealth = await service.InspectAsync(gameDirectory, artifactDirectory);
    Assert(foreignHealth.State == LoaderHealthState.Conflict &&
        foreignHealth.Files.Any(file => file is { RelativePath: "wininet.dll", State: LoaderFileState.Foreign }) &&
        foreignHealth.Message.Contains("not owned", StringComparison.OrdinalIgnoreCase),
        "A foreign proxy was not reported with an actionable install conflict.");
    var conflict = await service.InstallAsync(gameDirectory, artifactDirectory);
    Assert(!conflict.Success && conflict.Conflicts.SequenceEqual(new[] { "wininet.dll" }),
        "Install did not report the pre-existing proxy conflict.");
    Assert(await File.ReadAllTextAsync(proxyTarget) == "pre-existing proxy",
        "Install changed the pre-existing proxy.");
    File.Delete(proxyTarget);

    var proxyHash = await GameDiscovery.HashFileAsync(Path.Combine(artifactDirectory, "wininet.dll"));
    var runtimeHash = await GameDiscovery.HashFileAsync(
        Path.Combine(artifactDirectory, "btd5loader_runtime.dll"));
    var symbolHash = await GameDiscovery.HashFileAsync(
        Path.Combine(artifactDirectory, "symbols", "steam-win32-4.8.json"));
    File.Copy(Path.Combine(artifactDirectory, "wininet.dll"), proxyTarget);
    var interruptedInstallTemporary =
        Path.Combine(gameDirectory, "btd5loader_runtime.dll.btd5ml-interrupted.tmp");
    await File.WriteAllTextAsync(interruptedInstallTemporary, "partial install copy");
    await WriteOperationJournalAsync(
        service.GetOperationJournalPath(gameDirectory),
        new(
            1,
            gameDirectory,
            LoaderOperationKind.Install,
            [
                new("wininet.dll", proxyHash),
                new("btd5loader_runtime.dll", runtimeHash),
                new(Path.Combine("symbols", "steam-win32-4.8.json"), symbolHash)
            ]));
    var interruptedInstall = await service.InspectAsync(gameDirectory, artifactDirectory);
    Assert(interruptedInstall.State == LoaderHealthState.NotInstalled &&
        interruptedInstall.Message.Contains("rolled back", StringComparison.OrdinalIgnoreCase) &&
        !File.Exists(proxyTarget) && !File.Exists(interruptedInstallTemporary) &&
        !File.Exists(service.GetOperationJournalPath(gameDirectory)),
        "Health inspection did not roll back an interrupted installation journal.");

    var install = await service.InstallAsync(gameDirectory, artifactDirectory);
    Assert(install.Success && File.Exists(service.GetRecordPath(gameDirectory)),
        "Loader installation failed.");
    var healthyInstallation = await service.InspectAsync(gameDirectory, artifactDirectory);
    Assert(healthyInstallation.State == LoaderHealthState.Healthy &&
        healthyInstallation.InstalledVersion == ProductInfo.Version &&
        healthyInstallation.AvailableVersion == ProductInfo.Version,
        "A verified loader was not reported with its installed and available versions.");
    Assert((await service.VerifyAsync(gameDirectory)).Success, "Installed loader did not verify.");

    var installationRecordPath = service.GetRecordPath(gameDirectory);
    var currentInstallationRecord = await File.ReadAllTextAsync(installationRecordPath);
    var outdatedInstallationRecord = currentInstallationRecord.Replace(
        $"\"loaderVersion\": \"{ProductInfo.Version}\"",
        "\"loaderVersion\": \"0.0.1-test\"",
        StringComparison.Ordinal);
    Assert(!string.Equals(currentInstallationRecord, outdatedInstallationRecord, StringComparison.Ordinal),
        "The version-check fixture could not update the installation record.");
    await File.WriteAllTextAsync(installationRecordPath, outdatedInstallationRecord);
    var outdatedInstallation = await service.InspectAsync(gameDirectory, artifactDirectory);
    Assert(outdatedInstallation.State == LoaderHealthState.VersionMismatch &&
        outdatedInstallation.InstalledVersion == "0.0.1-test" &&
        outdatedInstallation.AvailableVersion == ProductInfo.Version,
        "An outdated installed loader version was not identified.");
    await File.WriteAllTextAsync(installationRecordPath, currentInstallationRecord);

    var runtimeTarget = Path.Combine(gameDirectory, "btd5loader_runtime.dll");
    File.Delete(proxyTarget);
    File.Delete(runtimeTarget);
    File.Copy(Path.Combine(artifactDirectory, "wininet.dll"), proxyTarget);
    var interruptedTemporary = runtimeTarget + ".btd5ml-interrupted.tmp";
    await File.WriteAllTextAsync(interruptedTemporary, "partial copy");
    await WriteOperationJournalAsync(
        service.GetOperationJournalPath(gameDirectory),
        new(
            1,
            gameDirectory,
            LoaderOperationKind.Repair,
            [new("wininet.dll", proxyHash), new("btd5loader_runtime.dll", runtimeHash)]));
    var interruptedRepair = await service.InspectAsync(gameDirectory, artifactDirectory);
    Assert(interruptedRepair.State == LoaderHealthState.Repairable &&
        interruptedRepair.Message.Contains("rolled back", StringComparison.OrdinalIgnoreCase) &&
        !File.Exists(proxyTarget) && !File.Exists(runtimeTarget) &&
        !File.Exists(interruptedTemporary) &&
        !File.Exists(service.GetOperationJournalPath(gameDirectory)),
        "Health inspection did not roll back an interrupted repair journal.");
    Assert((await service.RepairAsync(gameDirectory, artifactDirectory)).Success,
        "Repair did not recover after interrupted-operation reconciliation.");

    await WriteOperationJournalAsync(
        service.GetOperationJournalPath(gameDirectory),
        new(1, gameDirectory, LoaderOperationKind.Repair, [new("wininet.dll", proxyHash)]));
    var completedRepair = await service.InspectAsync(gameDirectory, artifactDirectory);
    Assert(completedRepair.State == LoaderHealthState.Healthy && File.Exists(proxyTarget) &&
        completedRepair.Message.Contains("confirmed", StringComparison.OrdinalIgnoreCase) &&
        !File.Exists(service.GetOperationJournalPath(gameDirectory)),
        "Health inspection did not safely clear a completed operation journal.");

    File.Delete(proxyTarget);
    await File.WriteAllTextAsync(runtimeTarget, "user-modified runtime");
    var transactionalRepairConflict = await service.RepairAsync(gameDirectory, artifactDirectory);
    Assert(!transactionalRepairConflict.Success && !File.Exists(proxyTarget),
        "A failed repair left an earlier restored file behind.");
    Assert(await File.ReadAllTextAsync(runtimeTarget) == "user-modified runtime",
        "A failed repair changed a modified loader file.");
    File.Copy(Path.Combine(artifactDirectory, "btd5loader_runtime.dll"), runtimeTarget, true);
    Assert((await service.RepairAsync(gameDirectory, artifactDirectory)).Success && File.Exists(proxyTarget),
        "Repair did not recover after a transaction preflight failure.");

    File.Delete(runtimeTarget);
    var missingVerification = await service.VerifyAsync(gameDirectory);
    Assert(!missingVerification.Success &&
        missingVerification.Conflicts.Contains("btd5loader_runtime.dll", StringComparer.OrdinalIgnoreCase),
        "Verification did not report a missing loader file.");
    Assert((await service.InspectAsync(gameDirectory, artifactDirectory)).State ==
        LoaderHealthState.Repairable, "A missing owned file was not reported as repairable.");
    Assert((await service.RepairAsync(gameDirectory, artifactDirectory)).Success && File.Exists(runtimeTarget),
        "Repair did not restore a missing loader-owned file.");

    await File.WriteAllTextAsync(proxyTarget, "user-modified proxy");
    var modifiedHealth = await service.InspectAsync(gameDirectory, artifactDirectory);
    Assert(modifiedHealth.State == LoaderHealthState.Conflict &&
        modifiedHealth.Files.Any(file => file is { RelativePath: "wininet.dll", State: LoaderFileState.Modified }) &&
        modifiedHealth.Message.Contains("will not be overwritten", StringComparison.OrdinalIgnoreCase),
        "A modified owned file was not reported with a manual recovery path.");
    var repairConflict = await service.RepairAsync(gameDirectory, artifactDirectory);
    Assert(!repairConflict.Success &&
        repairConflict.Conflicts.Contains("wininet.dll", StringComparer.OrdinalIgnoreCase),
        "Repair did not preserve and report a modified loader file.");
    Assert(await File.ReadAllTextAsync(proxyTarget) == "user-modified proxy",
        "Repair overwrote a modified loader file.");

    var partialUninstall = await service.UninstallAsync(gameDirectory);
    Assert(!partialUninstall.Success && File.Exists(proxyTarget) && !File.Exists(runtimeTarget) &&
        partialUninstall.Conflicts.Contains("wininet.dll", StringComparer.OrdinalIgnoreCase) &&
        partialUninstall.Message.Contains("preserved", StringComparison.OrdinalIgnoreCase),
        "Uninstall did not preserve only the modified loader file.");
    File.Copy(Path.Combine(artifactDirectory, "wininet.dll"), proxyTarget, true);
    var uninstall = await service.UninstallAsync(gameDirectory);
    Assert(uninstall.Success && !File.Exists(proxyTarget) && !File.Exists(service.GetRecordPath(gameDirectory)),
        "Uninstall did not finish after the conflict was recovered.");
    Directory.CreateDirectory(Path.GetDirectoryName(service.GetOperationJournalPath(gameDirectory))!);
    await File.WriteAllTextAsync(service.GetOperationJournalPath(gameDirectory), "{}");
    Assert((await service.InspectAsync(gameDirectory, artifactDirectory)).State ==
        LoaderHealthState.InvalidRecord && File.Exists(service.GetOperationJournalPath(gameDirectory)),
        "An invalid operation journal was not reported and preserved for recovery.");
    File.Delete(service.GetOperationJournalPath(gameDirectory));
    Assert((await service.InspectAsync(gameDirectory, Path.Combine(testRoot, "missing-artifacts"))).State ==
        LoaderHealthState.ArtifactUnavailable,
        "Missing release artifacts were not distinguished from a normal uninstalled state.");
    Directory.CreateDirectory(Path.GetDirectoryName(service.GetRecordPath(gameDirectory))!);
    await File.WriteAllTextAsync(service.GetRecordPath(gameDirectory), "{not json");
    Assert((await service.InspectAsync(gameDirectory, artifactDirectory)).State ==
        LoaderHealthState.InvalidRecord, "A damaged installation record was not reported safely.");
    File.Delete(service.GetRecordPath(gameDirectory));
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

    var looseStateRoot = Path.Combine(testRoot, "loose-package-state");
    var loosePackagesRoot = Path.Combine(looseStateRoot, "packages");
    Directory.CreateDirectory(loosePackagesRoot);
    File.Copy(packagePath, Path.Combine(loosePackagesRoot, "lifecycle-sample.btd5mod"));
    var loosePackages = await ModPackageService.ListInstalledAsync(looseStateRoot, "fixture-build");
    Assert(loosePackages is [{ Valid: true, Id: "sample.lifecycle", Version: "1.0.0" }],
        "A valid package copied directly into the Packages folder was not discovered.");

    var incompatible = await ModPackageService.InspectAsync(packagePath, "other-build");
    Assert(!incompatible.Valid && incompatible.Errors.Any(error => error.Contains(
        "does not support", StringComparison.Ordinal)), "An incompatible package was accepted.");
    var unsafePackage = Path.Combine(testRoot, "unsafe.btd5mod");
    CreatePackage(unsafePackage, manifest, ("lua/main.lua", "return {}"), ("../outside.txt", "unsafe"));
    var unsafeInfo = await ModPackageService.InspectAsync(unsafePackage, "fixture-build");
    Assert(!unsafeInfo.Valid && unsafeInfo.Errors.Any(error => error.Contains(
        "Unsafe", StringComparison.Ordinal)), "A package containing path traversal was accepted.");

    // Script validation runs the loader's Lua host (btd5loader_validate.exe) when it is available.
    var skippedScript = await ScriptValidationService.ValidateAsync(
        packagePath, validatorPath: Path.Combine(testRoot, "missing-validator.exe"));
    Assert(!skippedScript.Ran && skippedScript.Valid, "A missing script validator did not skip validation.");
    if (FindScriptValidator() is { } validator)
    {
        var towerPackage = Path.Combine(testRoot, "towers.btd5mod");
        CreatePackage(towerPackage, BuildManifest("sample.towers", "1.0.0"),
            ("lua/main.lua", "btd5.towers.register{ id = \"laser\", base = \"DartMonkey\", cost = 120 }"));
        var goodScript = await ScriptValidationService.ValidateAsync(towerPackage, validatorPath: validator);
        Assert(goodScript.Ran && goodScript.Valid && goodScript.Towers.SequenceEqual(new[] { "sample.towers:laser" }),
            "A valid custom tower script was rejected.");
        var brokenPackage = Path.Combine(testRoot, "broken-towers.btd5mod");
        CreatePackage(brokenPackage, BuildManifest("sample.broken", "1.0.0"),
            ("lua/main.lua", "btd5.towers.register{ id = \"laser\", upgrades = { { { name = \"A\", cost = 1 } } } }"));
        var brokenScript = await ScriptValidationService.ValidateAsync(brokenPackage, validatorPath: validator);
        Assert(brokenScript.Ran && !brokenScript.Valid &&
            brokenScript.Errors.Any(error => error.Contains("exactly 4 tiers", StringComparison.Ordinal)),
            "An invalid custom tower definition passed script validation.");
    }
    else
    {
        Console.WriteLine("Script validator is not built; its checks were skipped.");
    }

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
    var duplicated = await profileService.DuplicateAsync("Testing", "Testing Copy");
    Assert(duplicated.Mods.Count == profile.Mods.Count && duplicated.LaunchHistory.Count == 0,
        "Profile duplication did not preserve mods or clear launch history.");
    var renamed = await profileService.RenameAsync("Testing Copy", "Renamed");
    Assert(renamed.Name == "Renamed" && await profileService.LoadAsync("Testing Copy") is null,
        "Profile rename did not move the profile atomically.");
    await profileService.DeleteAsync("Renamed");
    Assert(await profileService.LoadAsync("Renamed") is null, "Profile deletion failed.");

    var migrationRoot = Path.Combine(testRoot, "migration-state");
    var migrationProfiles = new ProfileService(migrationRoot);
    await migrationProfiles.CreateAsync("Legacy");
    await migrationProfiles.SaveModsAsync("Legacy", configuredMods);
    await migrationProfiles.RecordLaunchAsync("Legacy", "modded", false, "legacy launch");
    var legacyProfilePath = Directory
        .EnumerateFiles(Path.Combine(migrationRoot, "profiles"), "*.json")
        .Single();
    var legacyProfileBytes = await File.ReadAllBytesAsync(legacyProfilePath);

    var migrationLoader = new LoaderInstallationService(migrationRoot, builds);
    var legacyInstallationPath = migrationLoader.GetRecordPath(gameDirectory);
    Directory.CreateDirectory(Path.GetDirectoryName(legacyInstallationPath)!);
    var legacyInstallation = new LoaderInstallationRecord(
        1,
        gameDirectory,
        "fixture-build",
        "0.0.1",
        [new InstalledFile("wininet.dll", new string('0', 64))]);
    await File.WriteAllTextAsync(
        legacyInstallationPath,
        JsonSerializer.Serialize(legacyInstallation, new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            WriteIndented = true
        }));
    var legacyInstallationBytes = await File.ReadAllBytesAsync(legacyInstallationPath);
    var legacyPackageDirectory = Path.Combine(migrationRoot, "packages", "legacy");
    Directory.CreateDirectory(legacyPackageDirectory);
    File.Copy(packagePath, Path.Combine(legacyPackageDirectory, "legacy.btd5mod"));

    var migration = await new ManagerStateMigrationService(migrationRoot).MigrateAsync();
    Assert(migration is
    {
        Migrated: true,
        ProfilesPreserved: 1,
        InstallationsPreserved: 1,
        PackagesPreserved: 1,
        InferredCurrentProfile: "Legacy"
    } && PathsEqual(migration.InferredGameDirectory!, gameDirectory),
        "Existing manager state was not migrated with unambiguous selections.");
    var migratedSettings = (await new ManagerSettingsService(migrationRoot).LoadAsync()).Settings;
    Assert(migratedSettings.CurrentProfile == "Legacy" &&
        PathsEqual(migratedSettings.GameDirectory!, gameDirectory),
        "Migration did not persist the inferred legacy selections.");
    var migratedProfile = await migrationProfiles.LoadAsync("Legacy");
    Assert(migratedProfile is { LaunchHistory: [{ Detail: "legacy launch" }] } &&
        migratedProfile.Mods[0].Configuration["greeting"].GetString() == "hello",
        "Migration did not preserve profile configuration and launch history.");
    var preservedProfileBytes = await File.ReadAllBytesAsync(legacyProfilePath);
    var preservedInstallationBytes = await File.ReadAllBytesAsync(legacyInstallationPath);
    Assert(legacyProfileBytes.SequenceEqual(preservedProfileBytes) &&
        legacyInstallationBytes.SequenceEqual(preservedInstallationBytes),
        "Migration rewrote legacy profile or installation state.");
    var profileBackupBytes = await File.ReadAllBytesAsync(Path.Combine(
        migrationRoot, "backups", "revamped-manager-v1", "profiles", Path.GetFileName(legacyProfilePath)));
    var installationBackupBytes = await File.ReadAllBytesAsync(Path.Combine(
        migrationRoot, "backups", "revamped-manager-v1", "installations",
        Path.GetFileName(legacyInstallationPath)));
    Assert(legacyProfileBytes.SequenceEqual(profileBackupBytes) &&
        legacyInstallationBytes.SequenceEqual(installationBackupBytes),
        "Migration backups were not byte-for-byte copies of legacy state.");
    Assert(!(await new ManagerStateMigrationService(migrationRoot).MigrateAsync()).Migrated,
        "Completed migration was not idempotent.");

    var ambiguousMigrationRoot = Path.Combine(testRoot, "ambiguous-migration-state");
    var ambiguousProfiles = new ProfileService(ambiguousMigrationRoot);
    await ambiguousProfiles.CreateAsync("First");
    await ambiguousProfiles.CreateAsync("Second");
    var ambiguousMigration = await new ManagerStateMigrationService(ambiguousMigrationRoot).MigrateAsync();
    Assert(ambiguousMigration.Migrated && ambiguousMigration.InferredCurrentProfile is null &&
        ambiguousMigration.Warning?.Contains("multiple existing profiles", StringComparison.Ordinal) == true &&
        (await new ManagerSettingsService(ambiguousMigrationRoot).LoadAsync())
            .Settings.CurrentProfile is null,
        "Migration silently selected one of multiple legacy profiles.");

    var settingsService = new ManagerSettingsService(stateRoot);
    await settingsService.SetGameDirectoryAsync(gameDirectory);
    var savedSettings = await settingsService.SetCurrentProfileAsync("Testing");
    Assert(PathsEqual(savedSettings.GameDirectory!, gameDirectory) &&
        savedSettings.CurrentProfile == "Testing" &&
        (await settingsService.LoadAsync()).Settings == savedSettings,
        "Manager game/profile selection did not persist.");
    var missingSavedGame = Path.Combine(testRoot, "removed-game-copy");
    await settingsService.SaveAsync(savedSettings with
    {
        GameDirectory = missingSavedGame,
        CurrentProfile = "Removed Profile"
    });
    var reconciledSettings = await settingsService.ReconcileSelectionsAsync();
    Assert(reconciledSettings is
    {
        Recovered: true,
        Settings.GameDirectory: null,
        Settings.CurrentProfile: null
    } && reconciledSettings.Warning?.Contains("explicitly", StringComparison.Ordinal) == true &&
        (await settingsService.LoadAsync()).Settings == ManagerSettingsService.Default(),
        "Invalid saved selections did not recover to an explicit safe state.");
    await File.WriteAllTextAsync(Path.Combine(stateRoot, "manager.json"), "{not json");
    var recoveredSettings = await settingsService.LoadAsync();
    Assert(recoveredSettings.Recovered && recoveredSettings.Settings == ManagerSettingsService.Default() &&
        File.Exists(Path.Combine(stateRoot, "manager.json")),
        "Invalid manager settings did not recover safely while preserving the source file.");

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
    var updatedConfiguration = new Dictionary<string, System.Text.Json.JsonElement>
    {
        ["enabled"] = System.Text.Json.JsonSerializer.SerializeToElement(false),
        ["label"] = System.Text.Json.JsonSerializer.SerializeToElement("custom"),
        ["weight"] = System.Text.Json.JsonSerializer.SerializeToElement(2.5)
    };
    Assert((await operations.UpdateConfigurationAsync(
        "Operations", "sample.application", updatedConfiguration)).Success,
        "Valid per-profile configuration could not be saved.");
    var invalidConfiguration = updatedConfiguration.ToDictionary(
        value => value.Key, value => value.Value, StringComparer.Ordinal);
    invalidConfiguration["enabled"] = System.Text.Json.JsonSerializer.SerializeToElement("no");
    var rejectedConfiguration = await operations.UpdateConfigurationAsync(
        "Operations", "sample.application", invalidConfiguration);
    Assert(!rejectedConfiguration.Success && rejectedConfiguration.Validation.Errors.Any(error =>
        error.Contains("must be a boolean", StringComparison.Ordinal)),
        "A configuration value with the wrong type was accepted.");
    Assert((await operations.ResetConfigurationAsync("Operations", "sample.application")).Success,
        "Resetting per-profile configuration to package defaults failed.");
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
    var toggledVersion = await operations.ToggleVersionAsync(
        "Operations", "sample.application", "1.1.0");
    Assert(toggledVersion.Success && toggledVersion.Profile?.Mods.Single(mod =>
            mod.Id == "sample.application") is { Version: "1.1.0", Enabled: true },
        "Toggling another installed version did not select it while keeping the mod enabled.");
    var toggledOff = await operations.ToggleVersionAsync(
        "Operations", "sample.application", "1.1.0");
    Assert(toggledOff.Success && toggledOff.Profile?.Mods.Single(mod =>
            mod.Id == "sample.application") is { Version: "1.1.0", Enabled: false },
        "Toggling the selected enabled version did not disable the mod.");
    var toggledOn = await operations.ToggleVersionAsync(
        "Operations", "sample.application", "1.0.0");
    Assert(toggledOn.Success && toggledOn.Profile?.Mods.Single(mod =>
            mod.Id == "sample.application") is { Version: "1.0.0", Enabled: true },
        "Toggling a version for a disabled mod did not enable the selected version.");
    var referencedUninstall = await operations.UninstallPackageAsync("sample.application", "1.0.0");
    Assert(!referencedUninstall.Success && referencedUninstall.BlockingProfiles.SequenceEqual(
        new[] { "Operations" }), "A profile-referenced package was uninstalled.");
    Assert((await operations.RemoveAsync("Operations", "sample.application")).Success,
        "A dependency-safe profile removal failed.");
    Assert((await operations.UninstallPackageAsync("sample.application", "1.0.0")).Success,
        "An unreferenced package could not be uninstalled.");

    Assert(!RuntimeCapabilities.HasLaunchIsolation(Path.Combine(artifactDirectory, "btd5loader_runtime.dll")),
        "A legacy runtime advertised launch isolation.");
    await File.WriteAllBytesAsync(Path.Combine(artifactDirectory, "btd5loader_runtime.dll"), ProtocolRuntimeFixture());
    Assert(RuntimeCapabilities.HasLaunchIsolation(Path.Combine(artifactDirectory, "btd5loader_runtime.dll")),
        "The protocol export was not detected in PE metadata.");
    Assert((await new LoaderInstallationService(operationsStateRoot, builds)
        .InstallAsync(gameDirectory, artifactDirectory)).Success,
        "Launch test loader installation failed.");
    Directory.CreateDirectory(Path.Combine(operationsStateRoot, "logs"));
    await File.WriteAllTextAsync(
        Path.Combine(operationsStateRoot, "logs", "runtime.jsonl"),
        "{\"component\":\"runtime\",\"message\":\"hooks_ready_no_hooks_registered\"}\n");
    var recordingLauncher = new RecordingGameProcessLauncher();
    var launches = new GameLaunchService(
        operationsStateRoot, builds, recordingLauncher, artifactDirectory,
        new SteamLaunchTarget(gameDirectory, "fixture-steam.exe"));
    var launchStatus = await launches.GetStatusAsync(gameDirectory, "Operations");
    Assert(launchStatus is
    {
        GameSupported: true,
        LoaderVerified: true,
        ProfileValid: true,
        RuntimeState: "hooks_ready_no_hooks_registered"
    }, "Launch readiness status was incorrect.");
    Assert(launchStatus.Issues.Count == 0, "A ready launch reported corrective actions.");

    var launchInstallationService = new LoaderInstallationService(operationsStateRoot, builds);
    var launchRecordPath = launchInstallationService.GetRecordPath(gameDirectory);
    var launchRecord = await File.ReadAllTextAsync(launchRecordPath);
    var outdatedLaunchRecord = launchRecord.Replace(
        $"\"loaderVersion\": \"{ProductInfo.Version}\"",
        "\"loaderVersion\": \"0.0.1-test\"",
        StringComparison.Ordinal);
    await File.WriteAllTextAsync(launchRecordPath, outdatedLaunchRecord);
    var outdatedReadiness = await launches.GetStatusAsync(gameDirectory, "Operations");
    Assert(!outdatedReadiness.LoaderVerified && outdatedReadiness.Issues.Any(issue =>
            issue is { Code: "loader.version_mismatch", Action: ReadinessAction.UpdateLoader }),
        "An outdated loader was not routed to the update action.");
    await File.WriteAllTextAsync(launchRecordPath, launchRecord);

    File.Delete(Path.Combine(gameDirectory, "btd5loader_runtime.dll"));
    var repairReadiness = await launches.GetStatusAsync(gameDirectory, "Operations");
    Assert(repairReadiness.Issues.Any(issue =>
            issue is { Code: "loader.repairable", Action: ReadinessAction.RepairLoader }),
        "A missing loader file was not routed to the repair action.");
    Assert((await new LoaderInstallationService(operationsStateRoot, builds)
            .RepairAsync(gameDirectory, artifactDirectory)).Success,
        "The loader could not be restored after the readiness routing test.");

    var missingProfileReadiness = await launches.GetStatusAsync(gameDirectory, "Missing Profile");
    Assert(missingProfileReadiness.Issues.Any(issue =>
            issue is { Code: "profile.missing", Action: ReadinessAction.SelectProfile }),
        "A vanished current profile was not routed to profile selection.");

    await operationsProfiles.CreateAsync("Missing Package");
    await operationsProfiles.SaveModsAsync(
        "Missing Package",
        [new("sample.not-installed", "1.0.0", true, 0,
            new Dictionary<string, JsonElement>())]);
    var missingPackageReadiness = await launches.GetStatusAsync(gameDirectory, "Missing Package");
    Assert(missingPackageReadiness.Issues.Any(issue => issue is
    {
        Code: "profile.package_missing",
        Action: ReadinessAction.InstallMod,
        ModId: "sample.not-installed"
    }), "A missing selected package was not routed to mod installation.");

    await operationsProfiles.CreateAsync("Invalid Configuration");
    var operationsProfile = await operationsProfiles.LoadAsync("Operations") ??
        throw new InvalidOperationException("The operation profile disappeared.");
    await operationsProfiles.SaveModsAsync(
        "Invalid Configuration",
        [
            operationsProfile.Mods.Single(mod => mod.Id == "sample.library") with { Order = 0 },
            new("sample.application", "1.1.0", true, 1, new Dictionary<string, JsonElement>())
        ]);
    var configurationReadiness = await launches.GetStatusAsync(gameDirectory, "Invalid Configuration");
    Assert(configurationReadiness.Issues.Any(issue => issue is
    {
        Code: "profile.configuration",
        Action: ReadinessAction.ConfigureMod,
        ModId: "sample.application"
    }), "Invalid per-profile settings were not routed to Configure.");

    var installedApplication = (await ModPackageService.ListInstalledAsync(
        operationsStateRoot, "fixture-build")).Single(package =>
            package.Id == "sample.application" && package.Version == "1.1.0");
    await operationsProfiles.CreateAsync("Broken Dependencies");
    await operationsProfiles.SaveModsAsync(
        "Broken Dependencies",
        [new("sample.application", "1.1.0", true, 0, installedApplication.ConfigurationDefaults)]);
    var dependencyReadiness = await launches.GetStatusAsync(gameDirectory, "Broken Dependencies");
    Assert(dependencyReadiness.Issues.Any(issue =>
            issue is
            {
                Code: "profile.dependencies",
                Action: ReadinessAction.ResolveDependencies,
                ModId: "sample.application"
            }),
        "A broken dependency did not identify the affected mod and corrective action.");

    Assert(!(await launches.LaunchModdedAsync(gameDirectory, "Operations", false)).Success &&
        recordingLauncher.Requests.Count == 0,
        "Modded launch did not require the offline-risk acknowledgement.");
    var moddedLaunch = await launches.LaunchModdedAsync(gameDirectory, "Operations", true);
    Assert(moddedLaunch.Success && recordingLauncher.Requests is
        [{ UseShellExecute: false }], "Validated modded launch was not handed to the process launcher.");
    var moddedRequest = recordingLauncher.Requests[0];
    Assert(moddedRequest.Arguments!.SequenceEqual(new[] { "-applaunch", "306020" }) &&
        recordingLauncher.LastTicket?.Contains("sample.library", StringComparison.Ordinal) == true &&
        !moddedRequest.Environment.ContainsKey("BTD5ML_ACTIVE_PROFILE") &&
        !File.Exists(recordingLauncher.LastTicketPath),
        "Modded launch did not use and clean up an explicit ticket.");
    Assert((await launches.LaunchVanillaAsync("Operations")).Success &&
        recordingLauncher.Requests is [_, { FileName: "fixture-steam.exe", UseShellExecute: false }] &&
        recordingLauncher.Requests[1].Arguments!.SequenceEqual(new[] { "-applaunch", "306020" }),
        "Vanilla launch carried modded arguments or was not handed to Steam.");
    var launchedProfile = await operationsProfiles.LoadAsync("Operations");
    Assert(launchedProfile?.LaunchHistory.Select(entry => entry.Mode)
        .SequenceEqual(new[] { "modded", "vanilla" }) == true,
        "Profile launch history did not record modded and vanilla launches.");
    recordingLauncher.Running = true;
    Assert(!(await launches.LaunchVanillaAsync()).Success &&
        !(await launches.LaunchModdedAsync(gameDirectory, "Operations", true)).Success,
        "An already-running game did not block mode changes.");
    recordingLauncher.Running = false;
    recordingLauncher.Pending = new TaskCompletionSource<int?>(TaskCreationOptions.RunContinuationsAsynchronously);
    var pendingLaunch = launches.LaunchModdedAsync(gameDirectory, "Operations", true);
    await recordingLauncher.Observed.Task;
    Assert(!(await launches.LaunchVanillaAsync()).Success,
        "A concurrent vanilla request changed an in-flight modded launch.");
    recordingLauncher.Pending.SetResult(1234);
    Assert((await pendingLaunch).Success, "The original coordinated launch did not finish.");
    recordingLauncher.Pending = null;
    recordingLauncher.FailStart = true;
    Assert(!(await launches.LaunchModdedAsync(gameDirectory, "Operations", true)).Success &&
        !Directory.EnumerateFiles(Path.Combine(gameDirectory, ".btd5ml-launch"), "*.json").Any(),
        "A failed process start left reusable intent.");
    recordingLauncher.FailStart = false;
    Assert((await launches.LaunchModdedAsync(gameDirectory, "Operations", true)).Success,
        "A launch did not recover after a failed process start.");
    using (var interrupted = new CancellationTokenSource())
    {
        recordingLauncher.Pending = new TaskCompletionSource<int?>(TaskCreationOptions.RunContinuationsAsynchronously);
        recordingLauncher.OnObserved = interrupted.Cancel;
        try
        {
            await launches.LaunchModdedAsync(gameDirectory, "Operations", true, interrupted.Token);
            Assert(false, "Interrupted launch did not propagate cancellation.");
        }
        catch (OperationCanceledException) { }
        Assert(!Directory.EnumerateFiles(Path.Combine(gameDirectory, ".btd5ml-launch"), "*.json").Any(),
            "Interrupted launch left a reusable ticket.");
        recordingLauncher.Pending = null;
        recordingLauncher.OnObserved = null;
    }
    var verifiedProxy = await File.ReadAllBytesAsync(Path.Combine(gameDirectory, "wininet.dll"));
    await File.WriteAllTextAsync(Path.Combine(gameDirectory, "wininet.dll"), "foreign proxy");
    Assert(!(await launches.LaunchVanillaAsync()).Success,
        "Vanilla launch accepted an unverified installed proxy.");
    Assert(await File.ReadAllTextAsync(Path.Combine(gameDirectory, "wininet.dll")) == "foreign proxy",
        "Vanilla validation overwrote a foreign proxy.");
    await File.WriteAllBytesAsync(Path.Combine(gameDirectory, "wininet.dll"), verifiedProxy);
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

    var runtimeLogPath = Path.Combine(operationsStateRoot, "logs", "runtime.jsonl");
    await File.WriteAllLinesAsync(runtimeLogPath, Enumerable.Range(1, 600).Select(index => $"line-{index}"));
    Assert((await launches.ReadRuntimeLogAsync(5)).SequenceEqual(
        new[] { "line-596", "line-597", "line-598", "line-599", "line-600" }),
        "Runtime log tailing did not return only the requested newest records.");
    await File.WriteAllTextAsync(runtimeLogPath, new string('x', RuntimeLogReader.MaximumBytes * 3));
    var oversizedTail = await launches.ReadRuntimeLogAsync();
    Assert(oversizedTail.Count == 1 && oversizedTail[0].StartsWith(RuntimeLogReader.TruncatedPrefix,
        StringComparison.Ordinal) && oversizedTail[0].EndsWith(RuntimeLogReader.PartialSuffix,
        StringComparison.Ordinal) && oversizedTail[0].Length <= RuntimeLogReader.MaximumLineBytes + 40,
        "Newline-free logs must cap returned bytes and identify truncation/partial records.");
    await File.WriteAllTextAsync(runtimeLogPath,
        "{\"message\":\"" + new string('x', 20000) + "\"}\nrecent\n");
    var jsonTail = await launches.ReadRuntimeLogAsync();
    Assert(jsonTail.Count == 2 && jsonTail[0].Length <= RuntimeLogReader.MaximumLineBytes + 40 &&
        jsonTail[1] == "recent", "Oversized JSON must retain recent complete records with bounded lines.");
    await File.WriteAllTextAsync(runtimeLogPath, string.Concat(Enumerable.Repeat("😀", 400000)) + "\nlast\n");
    var unicodeTail = await launches.ReadRuntimeLogAsync();
    Assert(unicodeTail.Count == 2 && !unicodeTail[0].Contains('\ufffd') && unicodeTail[1] == "last",
        "A UTF-8 window split must not introduce replacement characters.");
    await File.WriteAllBytesAsync(runtimeLogPath, [(byte)'o', (byte)'k', 0xf0, 0x9f]);
    Assert((await launches.ReadRuntimeLogAsync()).Single() == "ok" + RuntimeLogReader.PartialSuffix,
        "An incomplete trailing UTF-8 sequence must wait for the next snapshot.");
    using (var canceledRead = new CancellationTokenSource())
    {
        canceledRead.Cancel();
        try
        {
            await launches.ReadRuntimeLogAsync(cancellationToken: canceledRead.Token);
            throw new InvalidOperationException("Canceled log read completed successfully.");
        }
        catch (OperationCanceledException) { }
    }
    File.Move(runtimeLogPath, runtimeLogPath + ".old");
    await File.WriteAllTextAsync(runtimeLogPath, "replacement\n");
    Assert((await launches.ReadRuntimeLogAsync()).Single() == "replacement",
        "Refresh must reopen a replaced log file.");
    using (var changingLog = new ChangingLogStream())
    {
        var boundedTail = await RuntimeLogReader.ReadSnapshotAsync(changingLog, 7);
        Assert(changingLog.BytesRead == RuntimeLogReader.MaximumBytes && boundedTail.Count == 7 &&
            boundedTail.All(line => line.Length <= RuntimeLogReader.MaximumLineBytes + 40),
            "Growing logs must cap actual I/O bytes, line count, and individual output size.");
    }
    using (var clearedLog = new ChangingLogStream(clearDuringRead: true))
    {
        var clearedTail = await RuntimeLogReader.ReadSnapshotAsync(clearedLog);
        Assert(clearedLog.BytesRead <= 8192 && clearedTail.Count <= 500,
            "Concurrent truncation must stop at EOF without rescanning.");
    }
    using (var cancellation = new CancellationTokenSource())
    using (var canceledLog = new ChangingLogStream(onRead: cancellation.Cancel))
    {
        try
        {
            await RuntimeLogReader.ReadSnapshotAsync(canceledLog, cancellationToken: cancellation.Token);
            throw new InvalidOperationException("Mid-read cancellation was ignored.");
        }
        catch (OperationCanceledException) { }
        Assert(canceledLog.BytesRead == 8192, "Cancellation must prevent subsequent reads.");
    }
    await launches.ClearRuntimeLogAsync();
    Assert((await launches.ReadRuntimeLogAsync()).Count == 0,
        "Clearing the runtime log left old records behind.");
    await launches.ClearRuntimeLogAsync();

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

static async Task WriteOperationJournalAsync(string path, LoaderOperationJournal journal)
{
    Directory.CreateDirectory(Path.GetDirectoryName(path)!);
    await File.WriteAllTextAsync(
        path,
        JsonSerializer.Serialize(journal, new JsonSerializerOptions
        {
            WriteIndented = true,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase
        }));
}

static bool PathsEqual(string left, string right) =>
    string.Equals(Path.GetFullPath(left), Path.GetFullPath(right), StringComparison.OrdinalIgnoreCase);

static int Fail(string message)
{
    Console.Error.WriteLine(message);
    return 1;
}

// The native validator from this repository's build output, if built.
static string? FindScriptValidator()
{
    for (var directory = new DirectoryInfo(AppContext.BaseDirectory); directory is not null; directory = directory.Parent)
    {
        foreach (var configuration in new[] { "Release", "Debug" })
        {
            var candidate = Path.Combine(directory.FullName, "out", "build",
                "windows-x86-" + configuration.ToLowerInvariant(), "src", "native", "tools", configuration,
                ScriptValidationService.ValidatorFileName);
            if (File.Exists(candidate)) return candidate;
        }
    }
    return null;
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
          "capabilities": [],
          "configuration_defaults": {"enabled":true,"label":"default","weight":1.0}
        }
        """;
}

static byte[] ProtocolRuntimeFixture()
{
    var bytes = new byte[1024];
    void U16(int offset, ushort value) => System.Buffers.Binary.BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(offset), value);
    void U32(int offset, uint value) => System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(offset), value);
    U16(0, 0x5a4d);
    U32(0x3c, 0x80);
    U32(0x80, 0x4550);
    U16(0x84, 0x14c);
    U16(0x86, 1);
    U16(0x94, 224);
    U16(0x98, 0x10b);
    U32(0xb8, 4096);
    U32(0xbc, 512);
    U32(0xd0, 8192);
    U32(0xd4, 512);
    U32(0xf4, 16);
    U32(0xf8, 0x1000);
    U32(0xfc, 0x100);
    U32(0x180, 512);
    U32(0x184, 0x1000);
    U32(0x188, 512);
    U32(0x18c, 512);
    U32(0x218, 1);
    U32(0x220, 0x1040);
    U32(0x240, 0x1060);
    System.Text.Encoding.ASCII.GetBytes("BTD5Loader_LaunchProtocol1").CopyTo(bytes, 0x260);
    return bytes;
}

sealed class RecordingGameProcessLauncher : IGameProcessLauncher
{
    public List<GameProcessRequest> Requests { get; } = [];
    public string? LastTicket { get; private set; }
    public string? LastTicketPath { get; private set; }
    public bool Running { get; set; }
    public bool FailStart { get; set; }
    public TaskCompletionSource<int?>? Pending { get; set; }
    public Action? OnObserved { get; set; }
    public TaskCompletionSource Observed { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    public bool IsGameRunning() => Running;
    public long GetProcessStartTime(int processId) => 123456789;
    public async Task<int?> AwaitRuntimeAsync(string ticketPath, CancellationToken cancellationToken)
    {
        LastTicketPath = ticketPath;
        LastTicket = await File.ReadAllTextAsync(ticketPath, cancellationToken);
        if (Pending is null) return 1234;
        Observed.TrySetResult();
        OnObserved?.Invoke();
        return await Pending.Task.WaitAsync(cancellationToken);
    }
    public Task<int?> AwaitGameProcessAsync(string gameDirectory, DateTime startedAfter, CancellationToken cancellationToken)
        => Task.FromResult<int?>(1234);

    public int Start(GameProcessRequest request)
    {
        if (FailStart) throw new System.ComponentModel.Win32Exception("fixture start failure");
        Requests.Add(request);
        return 1000 + Requests.Count;
    }
}

// A huge virtual file that appends on each short read, or clears after the first read.
sealed class ChangingLogStream(bool clearDuringRead = false, Action? onRead = null) : Stream
{
    private long length = 8L * 1024 * 1024 * 1024;
    public int BytesRead { get; private set; }
    public override bool CanRead => true;
    public override bool CanSeek => true;
    public override bool CanWrite => false;
    public override long Length => length;
    public override long Position { get; set; }
    public override long Seek(long offset, SeekOrigin origin) => Position = origin switch
    {
        SeekOrigin.Begin => offset,
        SeekOrigin.Current => Position + offset,
        _ => length + offset
    };
    public override ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (Position >= length) return ValueTask.FromResult(0);
        var count = (int)Math.Min(Math.Min(buffer.Length, 8192), length - Position);
        buffer.Span[..count].Fill((byte)'x');
        for (var index = 79; index < count; index += 80) buffer.Span[index] = (byte)'\n';
        BytesRead += count;
        Position += count;
        length = clearDuringRead ? 0 : length + count;
        onRead?.Invoke();
        return ValueTask.FromResult(count);
    }
    public override int Read(byte[] buffer, int offset, int count) => throw new NotSupportedException();
    public override void Flush() => throw new NotSupportedException();
    public override void SetLength(long value) => throw new NotSupportedException();
    public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
}
