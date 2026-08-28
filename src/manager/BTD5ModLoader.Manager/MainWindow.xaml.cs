using System.IO;
using System.Windows;
using System.Windows.Controls;
using BTD5ModLoader.Manager.Core;
using Microsoft.Win32;

namespace BTD5ModLoader.Manager;

internal sealed partial class MainWindow : Window
{
    private const string SupportedBuildId = "steam-win32-4.8";
    private readonly string managerStateRoot = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "BTD5ModLoader");
    private IReadOnlyList<ModPackageInfo> installedPackages = [];
    private ModProfile? selectedProfile;
    private string? selectedPackagePath;

    public MainWindow()
    {
        InitializeComponent();
        ArtifactDirectoryText.Text = AppContext.BaseDirectory;
    }

    private async void Window_Loaded(object sender, RoutedEventArgs e)
    {
        TryDiscoverGame();
        await RefreshManagerStateAsync().ConfigureAwait(true);
    }

    private void ChooseGameDirectory_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = "Choose the BloonsTD5 game copy" };
        if (dialog.ShowDialog(this) == true)
        {
            GameDirectoryText.Text = dialog.FolderName;
            StatusText.Text = "Game copy selected. Use Setup to verify or install the loader.";
        }
    }

    private void ChooseArtifactDirectory_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = "Choose the loader artifact folder" };
        if (dialog.ShowDialog(this) == true)
        {
            ArtifactDirectoryText.Text = dialog.FolderName;
        }
    }

    private async void InstallLoader_Click(object sender, RoutedEventArgs e) =>
        await RunLoaderOperationAsync(service => service.InstallAsync(
            GameDirectoryText.Text, ArtifactDirectoryText.Text)).ConfigureAwait(true);

    private async void VerifyLoader_Click(object sender, RoutedEventArgs e) =>
        await RunLoaderOperationAsync(service => service.VerifyAsync(GameDirectoryText.Text))
            .ConfigureAwait(true);

    private async void RepairLoader_Click(object sender, RoutedEventArgs e) =>
        await RunLoaderOperationAsync(service => service.RepairAsync(
            GameDirectoryText.Text, ArtifactDirectoryText.Text)).ConfigureAwait(true);

    private async void UninstallLoader_Click(object sender, RoutedEventArgs e) =>
        await RunLoaderOperationAsync(service => service.UninstallAsync(GameDirectoryText.Text))
            .ConfigureAwait(true);

    private async Task RunLoaderOperationAsync(
        Func<LoaderInstallationService, Task<InstallationResult>> operation)
    {
        if (string.IsNullOrWhiteSpace(GameDirectoryText.Text))
        {
            StatusText.Text = "Choose a game copy first.";
            return;
        }
        try
        {
            StatusText.Text = "Working…";
            var result = await operation(new LoaderInstallationService(managerStateRoot))
                .ConfigureAwait(true);
            LoaderStatusText.Text = result.Message + FormatSuffix(result.Conflicts);
            StatusText.Text = LoaderStatusText.Text;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            StatusText.Text = "Loader operation failed: " + exception.Message;
        }
    }

    private async void ChoosePackage_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            CheckFileExists = true,
            DefaultExt = ".btd5mod",
            Filter = "BTD5 mod packages (*.btd5mod)|*.btd5mod",
            Multiselect = false,
            Title = "Choose a BTD5 mod package"
        };
        if (dialog.ShowDialog(this) == true)
        {
            await SelectPackageAsync(dialog.FileName).ConfigureAwait(true);
        }
    }

    private void PackageDropArea_DragOver(object sender, DragEventArgs e)
    {
        e.Effects = TryGetSinglePackage(e.Data, out _) ? DragDropEffects.Copy : DragDropEffects.None;
        e.Handled = true;
    }

    private async void PackageDropArea_Drop(object sender, DragEventArgs e)
    {
        if (TryGetSinglePackage(e.Data, out var packagePath))
        {
            await SelectPackageAsync(packagePath).ConfigureAwait(true);
        }
    }

    private async Task SelectPackageAsync(string packagePath)
    {
        selectedPackagePath = null;
        InstallPackageButton.IsEnabled = false;
        var package = await ModPackageService.InspectAsync(packagePath, SupportedBuildId)
            .ConfigureAwait(true);
        PackageDetailsText.Text = package.Valid
            ? $"{package.Name} {package.Version} by {package.Author}\nID: {package.Id}\n" +
              $"Dependencies: {FormatDependencies(package)}\nCapabilities: {FormatItems(package.Capabilities)}"
            : string.Join(Environment.NewLine, package.Errors.Select(error => "• " + error));
        if (package.Valid)
        {
            selectedPackagePath = packagePath;
            InstallPackageButton.IsEnabled = true;
        }
        StatusText.Text = package.Valid ? "Package is ready to install." : "Package validation failed.";
    }

    private async void InstallPackage_Click(object sender, RoutedEventArgs e)
    {
        if (selectedPackagePath is null)
        {
            return;
        }
        var result = await ModPackageService.InstallAsync(
            selectedPackagePath, managerStateRoot, SupportedBuildId).ConfigureAwait(true);
        StatusText.Text = result.Message;
        await RefreshManagerStateAsync().ConfigureAwait(true);
    }

    private async void UninstallPackage_Click(object sender, RoutedEventArgs e)
    {
        if (SelectedInstalledPackage() is not { Id: not null, Version: not null } package)
        {
            StatusText.Text = "Select an installed package first.";
            return;
        }
        var result = await new ProfileModService(managerStateRoot, SupportedBuildId)
            .UninstallPackageAsync(package.Id, package.Version).ConfigureAwait(true);
        StatusText.Text = result.Message + FormatSuffix(result.BlockingProfiles);
        await RefreshManagerStateAsync().ConfigureAwait(true);
    }

    private async void CreateProfile_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            selectedProfile = await new ProfileService(managerStateRoot)
                .CreateAsync(NewProfileNameText.Text).ConfigureAwait(true);
            NewProfileNameText.Clear();
            await RefreshManagerStateAsync(selectedProfile.Name).ConfigureAwait(true);
            StatusText.Text = "Profile created.";
        }
        catch (Exception exception) when (exception is ArgumentException or InvalidOperationException)
        {
            StatusText.Text = exception.Message;
        }
    }

    private async void ProfilesList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ProfilesList.SelectedItem is string name)
        {
            selectedProfile = await new ProfileService(managerStateRoot).LoadAsync(name).ConfigureAwait(true);
            RefreshProfileMods();
        }
    }

    private async void EnablePackage_Click(object sender, RoutedEventArgs e)
    {
        if (!TryGetOperationSelection(out var profile, out var package))
        {
            return;
        }
        await ApplyProfileChangeAsync(new ProfileModService(managerStateRoot, SupportedBuildId)
            .EnableAsync(profile.Name, package.Id!, package.Version!)).ConfigureAwait(true);
    }

    private async void DisableMod_Click(object sender, RoutedEventArgs e)
    {
        if (!TryGetSelectedProfileMod(out var profile, out var mod))
        {
            return;
        }
        await ApplyProfileChangeAsync(new ProfileModService(managerStateRoot, SupportedBuildId)
            .DisableAsync(profile.Name, mod.Id)).ConfigureAwait(true);
    }

    private async void MoveModUp_Click(object sender, RoutedEventArgs e) =>
        await MoveSelectedModAsync(-1).ConfigureAwait(true);

    private async void MoveModDown_Click(object sender, RoutedEventArgs e) =>
        await MoveSelectedModAsync(1).ConfigureAwait(true);

    private async Task MoveSelectedModAsync(int offset)
    {
        if (!TryGetSelectedProfileMod(out var profile, out var mod))
        {
            return;
        }
        var index = Math.Clamp(mod.Order + offset, 0, profile.Mods.Count - 1);
        await ApplyProfileChangeAsync(new ProfileModService(managerStateRoot, SupportedBuildId)
            .MoveAsync(profile.Name, mod.Id, index)).ConfigureAwait(true);
    }

    private async void RemoveMod_Click(object sender, RoutedEventArgs e)
    {
        if (!TryGetSelectedProfileMod(out var profile, out var mod))
        {
            return;
        }
        await ApplyProfileChangeAsync(new ProfileModService(managerStateRoot, SupportedBuildId)
            .RemoveAsync(profile.Name, mod.Id)).ConfigureAwait(true);
    }

    private async Task ApplyProfileChangeAsync(Task<ProfileChangeResult> operation)
    {
        var result = await operation.ConfigureAwait(true);
        selectedProfile = result.Profile;
        StatusText.Text = result.Success ? result.Message :
            result.Message + " " + string.Join(" ", result.Validation.Errors);
        RefreshProfileMods();
    }

    private async void RefreshStatus_Click(object sender, RoutedEventArgs e)
    {
        if (!TryGetLaunchInputs(out var gameDirectory, out var profileName))
        {
            return;
        }
        var status = await new GameLaunchService(managerStateRoot)
            .GetStatusAsync(gameDirectory, profileName).ConfigureAwait(true);
        ReadinessText.Text = status.Problems.Count == 0
            ? $"Ready. Build: {status.BuildId}; runtime: {status.RuntimeState}"
            : string.Join(Environment.NewLine, status.Problems.Select(problem => "• " + problem));
        StatusText.Text = status.Problems.Count == 0 ? "Launch validation passed." : "Launch is blocked.";
    }

    private async void LaunchModded_Click(object sender, RoutedEventArgs e)
    {
        if (!TryGetLaunchInputs(out var gameDirectory, out var profileName))
        {
            return;
        }
        var result = await new GameLaunchService(managerStateRoot).LaunchModdedAsync(
            gameDirectory, profileName, OfflineRiskCheck.IsChecked == true).ConfigureAwait(true);
        StatusText.Text = result.Message + FormatSuffix(result.Errors);
    }

    private async void LaunchVanilla_Click(object sender, RoutedEventArgs e)
    {
        var result = await new GameLaunchService(managerStateRoot)
            .LaunchVanillaAsync(selectedProfile?.Name).ConfigureAwait(true);
        StatusText.Text = result.Message + FormatSuffix(result.Errors);
    }

    private async void RefreshLogs_Click(object sender, RoutedEventArgs e)
    {
        var lines = await new GameLaunchService(managerStateRoot).ReadRuntimeLogAsync()
            .ConfigureAwait(true);
        RuntimeLogText.Text = string.Join(Environment.NewLine, lines);
        RuntimeLogText.ScrollToEnd();
    }

    private async void ExportDiagnostics_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrWhiteSpace(GameDirectoryText.Text))
        {
            StatusText.Text = "Choose a game copy first.";
            return;
        }
        var dialog = new SaveFileDialog
        {
            AddExtension = true,
            DefaultExt = ".zip",
            Filter = "ZIP archive (*.zip)|*.zip",
            FileName = "BTD5ModLoader-diagnostics.zip",
            Title = "Export loader diagnostics"
        };
        if (dialog.ShowDialog(this) != true)
        {
            return;
        }
        var result = await new DiagnosticsService(managerStateRoot).ExportAsync(
            dialog.FileName, GameDirectoryText.Text, selectedProfile?.Name).ConfigureAwait(true);
        StatusText.Text = result.Message;
    }

    private async Task RefreshManagerStateAsync(string? selectProfile = null)
    {
        installedPackages = await ModPackageService.ListInstalledAsync(
            managerStateRoot, SupportedBuildId).ConfigureAwait(true);
        var packageLabels = installedPackages.Select(FormatPackage).ToArray();
        InstalledPackagesList.ItemsSource = packageLabels;
        ProfilePackagesList.ItemsSource = packageLabels;
        var profiles = await new ProfileService(managerStateRoot).ListAsync().ConfigureAwait(true);
        ProfilesList.ItemsSource = profiles.Select(profile => profile.Name).ToArray();
        var desired = selectProfile ?? selectedProfile?.Name;
        if (desired is not null && ProfilesList.Items.Contains(desired))
        {
            ProfilesList.SelectedItem = desired;
        }
        else if (ProfilesList.Items.Count != 0)
        {
            ProfilesList.SelectedIndex = 0;
        }
        else
        {
            selectedProfile = null;
            RefreshProfileMods();
        }
    }

    private void RefreshProfileMods()
    {
        ProfileModsList.ItemsSource = selectedProfile?.Mods
            .OrderBy(mod => mod.Order)
            .Select(mod => $"{(mod.Enabled ? "Enabled" : "Disabled")}  {mod.Id} {mod.Version}")
            .ToArray() ?? [];
    }

    private void TryDiscoverGame()
    {
        var steamPath = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam")
            ?.GetValue("SteamPath") as string;
        if (string.IsNullOrWhiteSpace(steamPath))
        {
            return;
        }
        GameDirectoryText.Text = GameDiscovery.DiscoverGameDirectories(steamPath).FirstOrDefault() ?? string.Empty;
    }

    private ModPackageInfo? SelectedInstalledPackage()
    {
        var index = InstalledPackagesList.SelectedIndex >= 0
            ? InstalledPackagesList.SelectedIndex
            : ProfilePackagesList.SelectedIndex;
        return index >= 0 && index < installedPackages.Count ? installedPackages[index] : null;
    }

    private bool TryGetOperationSelection(out ModProfile profile, out ModPackageInfo package)
    {
        profile = selectedProfile!;
        package = SelectedInstalledPackage()!;
        if (profile is null || package is not { Valid: true, Id: not null, Version: not null })
        {
            StatusText.Text = "Select a profile and a valid installed package first.";
            return false;
        }
        return true;
    }

    private bool TryGetSelectedProfileMod(out ModProfile profile, out ProfileModEntry mod)
    {
        profile = selectedProfile!;
        mod = null!;
        if (profile is null || ProfileModsList.SelectedIndex < 0)
        {
            StatusText.Text = "Select a profile mod first.";
            return false;
        }
        mod = profile.Mods.OrderBy(value => value.Order).ElementAt(ProfileModsList.SelectedIndex);
        return true;
    }

    private bool TryGetLaunchInputs(out string gameDirectory, out string profileName)
    {
        gameDirectory = GameDirectoryText.Text;
        profileName = selectedProfile?.Name ?? string.Empty;
        if (string.IsNullOrWhiteSpace(gameDirectory) || string.IsNullOrWhiteSpace(profileName))
        {
            StatusText.Text = "Choose a game copy and profile first.";
            return false;
        }
        return true;
    }

    private static bool TryGetSinglePackage(IDataObject data, out string packagePath)
    {
        packagePath = string.Empty;
        if (!data.GetDataPresent(DataFormats.FileDrop) ||
            data.GetData(DataFormats.FileDrop) is not string[] { Length: 1 } files ||
            !string.Equals(Path.GetExtension(files[0]), ".btd5mod", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }
        packagePath = files[0];
        return true;
    }

    private static string FormatPackage(ModPackageInfo package) => package.Valid
        ? $"{package.Name} — {package.Id} {package.Version}"
        : $"Invalid package — {string.Join("; ", package.Errors)}";

    private static string FormatDependencies(ModPackageInfo package) => package.Dependencies.Count == 0
        ? "None"
        : string.Join(", ", package.Dependencies.Select(value => $"{value.Id} {value.Version}"));

    private static string FormatItems(IReadOnlyList<string> items) => items.Count == 0
        ? "None"
        : string.Join(", ", items);

    private static string FormatSuffix(IReadOnlyList<string> items) => items.Count == 0
        ? string.Empty
        : " (" + string.Join(", ", items) + ")";
}
