using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
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
    private IReadOnlyList<ModProfile> profilesSnapshot = [];
    private ModProfile? selectedProfile;
    private string? selectedPackagePath;
    private LoaderHealthResult? loaderHealth;
    private bool launchReady;
    private bool refreshingProfileSelection;
    private bool windowLoaded;
    private bool refreshingActivation;
    private bool refreshingLogs;
    private string readinessSummary = "Preparing manager state";
    private readonly DispatcherTimer runtimeLogTimer;
    private static readonly JsonSerializerOptions ProfileJsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };
    private static string ArtifactDirectory
    {
        get
        {
            var developerOverride = Environment.GetEnvironmentVariable("BTD5ML_ARTIFACT_DIRECTORY");
            return string.IsNullOrWhiteSpace(developerOverride)
                ? AppContext.BaseDirectory
                : Path.GetFullPath(developerOverride);
        }
    }

    public MainWindow()
    {
        InitializeComponent();
        runtimeLogTimer = new DispatcherTimer(DispatcherPriority.Background)
        {
            Interval = TimeSpan.FromSeconds(1)
        };
        runtimeLogTimer.Tick += RuntimeLogTimer_Tick;
    }

    private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ClickCount == 2)
        {
            WindowState = WindowState == WindowState.Maximized
                ? WindowState.Normal
                : WindowState.Maximized;
            return;
        }
        DragMove();
    }

    private void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void Maximize_Click(object sender, RoutedEventArgs e) => WindowState =
        WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    private void Close_Click(object sender, RoutedEventArgs e) => Close();

    private async void Options_Click(object sender, RoutedEventArgs e)
    {
        OptionsOverlay.Visibility = Visibility.Visible;
        runtimeLogTimer.Start();
        await RefreshLoaderHealthAsync().ConfigureAwait(true);
        await RefreshReadinessAsync().ConfigureAwait(true);
        await RefreshLogsAsync().ConfigureAwait(true);
    }

    private void CloseOptions_Click(object sender, RoutedEventArgs e) => CloseOptions();

    private void CloseOptions()
    {
        runtimeLogTimer.Stop();
        OptionsOverlay.Visibility = Visibility.Collapsed;
    }

    private void Window_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Escape || OptionsOverlay.Visibility != Visibility.Visible)
        {
            return;
        }
        CloseOptions();
        e.Handled = true;
    }

    private async void Window_Loaded(object sender, RoutedEventArgs e)
    {
        VersionText.Text = $"v{ProductInfo.Version}";
        var loaded = await new ManagerSettingsService(managerStateRoot).LoadAsync().ConfigureAwait(true);
        if (!string.IsNullOrWhiteSpace(loaded.Settings.GameDirectory) &&
            Directory.Exists(loaded.Settings.GameDirectory))
        {
            GameDirectoryText.Text = loaded.Settings.GameDirectory;
        }
        else if (loaded.Settings.GameDirectory is null)
        {
            TryDiscoverGame();
        }
        await RefreshManagerStateAsync(loaded.Settings.CurrentProfile).ConfigureAwait(true);
        await RefreshLoaderHealthAsync().ConfigureAwait(true);
        await RefreshReadinessAsync().ConfigureAwait(true);
        if (loaded.Recovered)
        {
            StatusText.Text = loaded.Warning;
        }
        windowLoaded = true;
    }

    private async void Window_Activated(object? sender, EventArgs e)
    {
        if (!windowLoaded || refreshingActivation)
        {
            return;
        }
        refreshingActivation = true;
        try
        {
            await RefreshManagerStateAsync(selectedProfile?.Name).ConfigureAwait(true);
            await RefreshReadinessAsync().ConfigureAwait(true);
        }
        catch (Exception exception) when (exception is IOException or InvalidDataException or
            InvalidOperationException or UnauthorizedAccessException or JsonException)
        {
            StatusText.Text = "Could not refresh the Mods folder: " + exception.Message;
        }
        finally
        {
            refreshingActivation = false;
        }
    }

    private async void ChooseGameDirectory_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = "Choose the BloonsTD5 game copy" };
        if (dialog.ShowDialog(this) == true)
        {
            GameDirectoryText.Text = dialog.FolderName;
            await new ManagerSettingsService(managerStateRoot)
                .SetGameDirectoryAsync(dialog.FolderName).ConfigureAwait(true);
            StatusText.Text = "Game copy saved.";
            await RefreshLoaderHealthAsync().ConfigureAwait(true);
            await RefreshReadinessAsync().ConfigureAwait(true);
        }
    }

    private async void LoaderPrimary_Click(object sender, RoutedEventArgs e)
    {
        if (loaderHealth is null)
        {
            await RefreshLoaderHealthAsync().ConfigureAwait(true);
            await RefreshReadinessAsync().ConfigureAwait(true);
            return;
        }
        if (loaderHealth.State == LoaderHealthState.NotInstalled)
        {
            await RunLoaderOperationAsync(service => service.InstallAsync(
                GameDirectoryText.Text, ArtifactDirectory)).ConfigureAwait(true);
        }
        else if (loaderHealth.State == LoaderHealthState.Repairable)
        {
            await RunLoaderOperationAsync(service => service.RepairAsync(
                GameDirectoryText.Text, ArtifactDirectory)).ConfigureAwait(true);
        }
        else
        {
            await RefreshLoaderHealthAsync().ConfigureAwait(true);
        }
    }

    private async void UninstallLoader_Click(object sender, RoutedEventArgs e)
    {
        if (MessageBox.Show(this,
                "Safely uninstall the loader? Only unchanged loader-owned files will be removed.",
                "Uninstall loader", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
        {
            return;
        }
        await RunLoaderOperationAsync(service => service.UninstallAsync(GameDirectoryText.Text))
            .ConfigureAwait(true);
    }

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
            await RefreshLoaderHealthAsync().ConfigureAwait(true);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            StatusText.Text = "Loader operation failed: " + exception.Message;
        }
    }

    private async Task RefreshLoaderHealthAsync()
    {
        if (string.IsNullOrWhiteSpace(GameDirectoryText.Text))
        {
            loaderHealth = null;
            LoaderStatusText.Text = "No game copy selected";
            LoaderPrimaryButton.Content = "Choose game";
            HealthIndicator.Background = Brushes.Goldenrod;
            UpdateActionState();
            return;
        }
        try
        {
            loaderHealth = await new LoaderInstallationService(managerStateRoot)
                .InspectAsync(GameDirectoryText.Text, ArtifactDirectory).ConfigureAwait(true);
            LoaderStatusText.Text = loaderHealth.State == LoaderHealthState.Healthy
                ? "Game Detected"
                : loaderHealth.Message;
            LoaderPrimaryButton.Content = loaderHealth.State switch
            {
                LoaderHealthState.NotInstalled => "Install loader",
                LoaderHealthState.Repairable => "Repair loader",
                _ => "Recheck"
            };
            HealthIndicator.Background = loaderHealth.State switch
            {
                LoaderHealthState.Healthy => Brushes.LimeGreen,
                LoaderHealthState.NotInstalled or LoaderHealthState.Repairable => Brushes.Goldenrod,
                _ => Brushes.IndianRed
            };
            UpdateActionState();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or ArgumentException)
        {
            loaderHealth = null;
            LoaderStatusText.Text = exception.Message;
            LoaderPrimaryButton.Content = "Recheck";
            HealthIndicator.Background = Brushes.IndianRed;
            UpdateActionState();
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
            if (selectedPackagePath is not null)
            {
                await InstallSelectedPackageAsync().ConfigureAwait(true);
            }
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
            if (selectedPackagePath is not null)
            {
                await InstallSelectedPackageAsync().ConfigureAwait(true);
            }
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
        await InstallSelectedPackageAsync().ConfigureAwait(true);
    }

    private async Task InstallSelectedPackageAsync()
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

    private void PackageList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (SelectedInstalledPackage() is not { } package)
        {
            SelectedModTitleText.Text = "Select a mod";
            SelectedModAuthorText.Text = "Choose a package from the Mods panel";
            SelectedModDescriptionText.Text = "Installed package details and profile status appear here.";
            SelectedModIcon.Source = AssetImage("monkey_modded.png");
            PackageDetailsText.Text = "Select a mod to see its package and profile state.";
            UpdateActionState();
            return;
        }
        var profileEntry = selectedProfile?.Mods.SingleOrDefault(mod =>
            string.Equals(mod.Id, package.Id, StringComparison.Ordinal));
        var profileState = profileEntry is null ? "Not in current profile" :
            profileEntry.Enabled ? "Enabled in current profile" : "Disabled in current profile";
        SelectedModTitleText.Text = package.Name ?? package.Id ?? "Invalid package";
        SelectedModAuthorText.Text = package.Valid ? $"by {package.Author}" : "Package validation failed";
        SelectedModDescriptionText.Text = profileState;
        SelectedModIcon.Source = AssetImage(IconNameForPackage(package));
        PackageDetailsText.Text = package.Valid
            ? $"Version                                      {package.Version}\n" +
              $"Author                                       {package.Author}\n" +
              $"Dependencies                           {FormatDependencies(package)}\n" +
              $"Settings                                    " +
              $"{(package.ConfigurationDefaults.Count == 0 ? "None" : package.ConfigurationDefaults.Count)}\n\n" +
              $"ID: {package.Id}\nCapabilities: {FormatItems(package.Capabilities)}"
            : string.Join(Environment.NewLine, package.Errors);
        UpdateActionState();
    }

    private async void ModToggle_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { DataContext: ModListItem item })
        {
            return;
        }
        ProfilePackagesList.SelectedItem = item;
        if (selectedProfile is null || item.Package is not { Id: not null, Version: not null })
        {
            StatusText.Text = "Select or create a profile before enabling mods.";
            return;
        }
        var entry = selectedProfile.Mods.SingleOrDefault(mod =>
            string.Equals(mod.Id, item.Package.Id, StringComparison.Ordinal));
        var service = new ProfileModService(managerStateRoot, SupportedBuildId);
        await ApplyProfileChangeAsync(entry is null or { Enabled: false }
            ? service.EnableAsync(selectedProfile.Name, item.Package.Id, item.Package.Version)
            : service.DisableAsync(selectedProfile.Name, item.Package.Id)).ConfigureAwait(true);
    }

    private void BrowseModsFolder_Click(object sender, RoutedEventArgs e)
    {
        var packagesRoot = Path.Combine(managerStateRoot, "packages");
        Directory.CreateDirectory(packagesRoot);
        StatusText.Text = "Copy .btd5mod files into this folder, then return to the manager.";
        Process.Start(new ProcessStartInfo(packagesRoot) { UseShellExecute = true });
    }

    private async void UninstallPackage_Click(object sender, RoutedEventArgs e)
    {
        if (SelectedInstalledPackage() is not { Id: not null, Version: not null } package)
        {
            StatusText.Text = "Select an installed package first.";
            return;
        }
        if (MessageBox.Show(this,
                $"Uninstall {package.Name} {package.Version}? Profiles that still reference it will block this operation.",
                "Uninstall mod package", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
        {
            return;
        }
        var result = await new ProfileModService(managerStateRoot, SupportedBuildId)
            .UninstallPackageAsync(package.Id, package.Version).ConfigureAwait(true);
        StatusText.Text = result.Message + FormatSuffix(result.BlockingProfiles);
        await RefreshManagerStateAsync().ConfigureAwait(true);
    }

    private async void CreateProfile_Click(object sender, RoutedEventArgs e)
    {
        var name = PromptWindow.Ask(this, "Create profile", "Profile name", "New Profile");
        if (name is null)
        {
            return;
        }
        await CreateProfileAsync(name).ConfigureAwait(true);
    }

    private async void CreateProfileQuick_Click(object sender, RoutedEventArgs e)
    {
        var name = PromptWindow.Ask(this, "Create profile", "Profile name", "New Profile");
        if (name is not null)
        {
            await CreateProfileAsync(name).ConfigureAwait(true);
        }
    }

    private async Task CreateProfileAsync(string name)
    {
        try
        {
            selectedProfile = await new ProfileService(managerStateRoot)
                .CreateAsync(name).ConfigureAwait(true);
            await new ManagerSettingsService(managerStateRoot)
                .SetCurrentProfileAsync(selectedProfile.Name).ConfigureAwait(true);
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
        if (refreshingProfileSelection)
        {
            return;
        }
        if (ProfilesList.SelectedItem is ProfileListItem item)
        {
            var name = item.Name;
            selectedProfile = await new ProfileService(managerStateRoot).LoadAsync(name).ConfigureAwait(true);
            await new ManagerSettingsService(managerStateRoot)
                .SetCurrentProfileAsync(name).ConfigureAwait(true);
            RefreshProfileMods();
            RefreshPackageLabels();
            RefreshProfileLabels(name);
            await RefreshReadinessAsync().ConfigureAwait(true);
        }
    }

    private async void ExportProfile_Click(object sender, RoutedEventArgs e)
    {
        if (selectedProfile is null)
        {
            StatusText.Text = "Select a profile to export.";
            return;
        }
        var dialog = new SaveFileDialog
        {
            AddExtension = true,
            DefaultExt = ".json",
            Filter = "BTD5 Mod Loader profile (*.json)|*.json",
            FileName = selectedProfile.Name + ".json",
            Title = "Export profile"
        };
        if (dialog.ShowDialog(this) != true)
        {
            return;
        }
        await File.WriteAllTextAsync(
            dialog.FileName,
            JsonSerializer.Serialize(selectedProfile, ProfileJsonOptions)).ConfigureAwait(true);
        StatusText.Text = "Profile exported.";
    }

    private async void ImportProfile_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            CheckFileExists = true,
            DefaultExt = ".json",
            Filter = "BTD5 Mod Loader profile (*.json)|*.json",
            Multiselect = false,
            Title = "Import profile"
        };
        if (dialog.ShowDialog(this) != true)
        {
            return;
        }
        try
        {
            using var stream = File.OpenRead(dialog.FileName);
            var imported = await JsonSerializer.DeserializeAsync<ModProfile>(
                stream, ProfileJsonOptions).ConfigureAwait(true) ??
                throw new InvalidDataException("The profile file is empty.");
            var name = PromptWindow.Ask(this, "Import profile", "Profile name", imported.Name);
            if (name is null)
            {
                return;
            }
            var profiles = new ProfileService(managerStateRoot);
            await profiles.CreateAsync(name).ConfigureAwait(true);
            selectedProfile = await profiles.SaveModsAsync(name, imported.Mods).ConfigureAwait(true);
            await new ManagerSettingsService(managerStateRoot)
                .SetCurrentProfileAsync(name).ConfigureAwait(true);
            await RefreshManagerStateAsync(name).ConfigureAwait(true);
            StatusText.Text = "Profile imported and selected.";
        }
        catch (Exception exception) when (exception is JsonException or IOException or
                                            InvalidDataException or ArgumentException or InvalidOperationException)
        {
            StatusText.Text = "Profile import failed: " + exception.Message;
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

    private async void ToggleMod_Click(object sender, RoutedEventArgs e)
    {
        if (selectedProfile is null || SelectedInstalledPackage() is not { Id: not null, Version: not null } package)
        {
            StatusText.Text = "Select a current profile and installed mod first.";
            return;
        }
        var entry = selectedProfile.Mods.SingleOrDefault(mod =>
            string.Equals(mod.Id, package.Id, StringComparison.Ordinal));
        var service = new ProfileModService(managerStateRoot, SupportedBuildId);
        await ApplyProfileChangeAsync(entry is null or { Enabled: false }
            ? service.EnableAsync(selectedProfile.Name, package.Id, package.Version)
            : service.DisableAsync(selectedProfile.Name, package.Id)).ConfigureAwait(true);
    }

    private async void ConfigureMod_Click(object sender, RoutedEventArgs e)
    {
        if (selectedProfile is null || SelectedInstalledPackage() is not { Id: not null } package)
        {
            StatusText.Text = "Select a current profile and mod first.";
            return;
        }
        var entry = selectedProfile.Mods.SingleOrDefault(mod =>
            string.Equals(mod.Id, package.Id, StringComparison.Ordinal));
        if (entry is null)
        {
            StatusText.Text = "Add the mod to the current profile before configuring it.";
            return;
        }
        if (package.ConfigurationDefaults.Count == 0)
        {
            StatusText.Text = "This mod does not define any configurable settings.";
            return;
        }
        var editor = new ModConfigurationWindow(
            package.Name ?? package.Id, entry.Configuration, package.ConfigurationDefaults)
        {
            Owner = this
        };
        if (editor.ShowDialog() == true)
        {
            await ApplyProfileChangeAsync(new ProfileModService(managerStateRoot, SupportedBuildId)
                .UpdateConfigurationAsync(selectedProfile.Name, entry.Id, editor.Configuration))
                .ConfigureAwait(true);
        }
    }

    private async void RenameProfile_Click(object sender, RoutedEventArgs e)
    {
        if (selectedProfile is null)
        {
            StatusText.Text = "Select a profile first.";
            return;
        }
        var newName = PromptWindow.Ask(this, "Rename profile", "New profile name", selectedProfile.Name);
        if (newName is null || string.Equals(newName, selectedProfile.Name, StringComparison.Ordinal))
        {
            return;
        }
        try
        {
            selectedProfile = await new ProfileService(managerStateRoot)
                .RenameAsync(selectedProfile.Name, newName).ConfigureAwait(true);
            await new ManagerSettingsService(managerStateRoot)
                .SetCurrentProfileAsync(newName).ConfigureAwait(true);
            await RefreshManagerStateAsync(newName).ConfigureAwait(true);
            StatusText.Text = "Profile renamed.";
        }
        catch (Exception exception) when (exception is ArgumentException or InvalidOperationException)
        {
            StatusText.Text = exception.Message;
        }
    }

    private async void DuplicateProfile_Click(object sender, RoutedEventArgs e)
    {
        if (selectedProfile is null)
        {
            StatusText.Text = "Select a profile first.";
            return;
        }
        var copyName = PromptWindow.Ask(
            this, "Duplicate profile", "Name for the copy", selectedProfile.Name + " Copy");
        if (copyName is null)
        {
            return;
        }
        try
        {
            selectedProfile = await new ProfileService(managerStateRoot)
                .DuplicateAsync(selectedProfile.Name, copyName).ConfigureAwait(true);
            await new ManagerSettingsService(managerStateRoot)
                .SetCurrentProfileAsync(copyName).ConfigureAwait(true);
            await RefreshManagerStateAsync(copyName).ConfigureAwait(true);
            StatusText.Text = "Profile duplicated and selected.";
        }
        catch (Exception exception) when (exception is ArgumentException or InvalidOperationException)
        {
            StatusText.Text = exception.Message;
        }
    }

    private async void DeleteProfile_Click(object sender, RoutedEventArgs e)
    {
        if (selectedProfile is null)
        {
            StatusText.Text = "Select a profile first.";
            return;
        }
        if (MessageBox.Show(this, $"Delete profile '{selectedProfile.Name}'? Installed mod packages are kept.",
                "Delete profile", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
        {
            return;
        }
        await new ProfileService(managerStateRoot).DeleteAsync(selectedProfile.Name).ConfigureAwait(true);
        selectedProfile = null;
        await new ManagerSettingsService(managerStateRoot).SetCurrentProfileAsync(null).ConfigureAwait(true);
        await RefreshManagerStateAsync().ConfigureAwait(true);
        await RefreshReadinessAsync().ConfigureAwait(true);
        StatusText.Text = "Profile deleted. Choose another profile explicitly.";
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

    private async void RemoveSelectedPackageFromProfile_Click(object sender, RoutedEventArgs e)
    {
        if (selectedProfile is null || SelectedInstalledPackage() is not { Id: not null } package)
        {
            StatusText.Text = "Select a profile and mod first.";
            return;
        }
        await ApplyProfileChangeAsync(new ProfileModService(managerStateRoot, SupportedBuildId)
            .RemoveAsync(selectedProfile.Name, package.Id)).ConfigureAwait(true);
    }

    private async Task ApplyProfileChangeAsync(Task<ProfileChangeResult> operation)
    {
        var result = await operation.ConfigureAwait(true);
        selectedProfile = result.Profile;
        StatusText.Text = result.Success ? result.Message :
            result.Message + " " + string.Join(" ", result.Validation.Errors);
        RefreshProfileMods();
        RefreshPackageLabels();
        if (selectedProfile is not null)
        {
            profilesSnapshot = profilesSnapshot.Select(profile =>
                string.Equals(profile.Name, selectedProfile.Name, StringComparison.OrdinalIgnoreCase)
                    ? selectedProfile
                    : profile).ToArray();
            RefreshProfileLabels(selectedProfile.Name);
        }
        await RefreshReadinessAsync().ConfigureAwait(true);
    }

    private async void RefreshStatus_Click(object sender, RoutedEventArgs e)
    {
        await RefreshLoaderHealthAsync().ConfigureAwait(true);
        await RefreshReadinessAsync(true).ConfigureAwait(true);
    }

    private void OfflineRiskCheck_Changed(object sender, RoutedEventArgs e)
    {
        if (IsInitialized)
        {
            StatusText.Text = OfflineRiskCheck.IsChecked == true
                ? "Steam Offline Mode acknowledged. Modded launch is available when all checks pass."
                : "Modded launch is blocked until Steam Offline Mode is acknowledged.";
            UpdateActionState();
        }
    }

    private void ProfileModsList_SelectionChanged(object sender, SelectionChangedEventArgs e) =>
        UpdateActionState();

    private async void LaunchModded_Click(object sender, RoutedEventArgs e)
    {
        if (!TryGetLaunchInputs(out var gameDirectory, out var profileName))
        {
            return;
        }
        if (OfflineRiskCheck.IsChecked != true)
        {
            OptionsOverlay.Visibility = Visibility.Visible;
            runtimeLogTimer.Start();
            OfflineRiskCheck.Focus();
            StatusText.Text = "Modded launch is blocked: acknowledge Steam Offline Mode above.";
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
        await RefreshLogsAsync().ConfigureAwait(true);
    }

    private async Task RefreshLogsAsync()
    {
        if (refreshingLogs)
        {
            return;
        }
        refreshingLogs = true;
        try
        {
            var lines = await new GameLaunchService(managerStateRoot).ReadRuntimeLogAsync()
                .ConfigureAwait(true);
            var updatedText = lines.Count == 0
                ? "Waiting for runtime output…"
                : string.Join(Environment.NewLine, lines.Select(FormatRuntimeLogLine));
            if (!string.Equals(RuntimeLogText.Text, updatedText, StringComparison.Ordinal))
            {
                var followTail = RuntimeLogText.Text.Length == 0 ||
                    RuntimeLogText.VerticalOffset >=
                    RuntimeLogText.ExtentHeight - RuntimeLogText.ViewportHeight - 8;
                RuntimeLogText.Text = updatedText;
                if (followTail)
                {
                    RuntimeLogText.ScrollToEnd();
                }
            }
            RuntimeLogLiveIndicator.Fill = new SolidColorBrush(Color.FromRgb(55, 201, 47));
            RuntimeLogLiveText.Foreground = new SolidColorBrush(Color.FromRgb(125, 222, 121));
            RuntimeLogLiveText.Text = "LIVE";
        }
        catch (IOException)
        {
            RuntimeLogLiveIndicator.Fill = Brushes.Goldenrod;
            RuntimeLogLiveText.Foreground = Brushes.Goldenrod;
            RuntimeLogLiveText.Text = "WAITING";
        }
        finally
        {
            refreshingLogs = false;
        }
    }

    private async void RuntimeLogTimer_Tick(object? sender, EventArgs e) =>
        await RefreshLogsAsync().ConfigureAwait(true);

    private static string FormatRuntimeLogLine(string line)
    {
        try
        {
            using var document = JsonDocument.Parse(line);
            var root = document.RootElement;
            var timestamp = root.TryGetProperty("timestamp", out var timestampElement) &&
                DateTimeOffset.TryParse(timestampElement.GetString(), out var parsedTimestamp)
                    ? parsedTimestamp.ToLocalTime().ToString("HH:mm:ss", CultureInfo.InvariantCulture)
                    : "--:--:--";
            var level = root.TryGetProperty("level", out var levelElement)
                ? levelElement.GetString()?.ToUpperInvariant() ?? "LOG"
                : "LOG";
            var component = root.TryGetProperty("component", out var componentElement)
                ? componentElement.GetString() ?? "runtime"
                : "runtime";
            var message = root.TryGetProperty("message", out var messageElement)
                ? messageElement.GetString() ?? string.Empty
                : line;
            return $"[{timestamp}] [{level}] {component}  {message}";
        }
        catch (JsonException)
        {
            return line;
        }
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
        profilesSnapshot = await new ProfileService(managerStateRoot).ListAsync().ConfigureAwait(true);
        var desired = selectProfile ?? selectedProfile?.Name;
        var desiredProfile = profilesSnapshot.SingleOrDefault(profile =>
            string.Equals(profile.Name, desired, StringComparison.OrdinalIgnoreCase));
        if (desiredProfile is not null)
        {
            selectedProfile = desiredProfile;
        }
        RefreshProfileLabels(desired);
        var desiredItem = ProfilesList.Items.Cast<ProfileListItem>().SingleOrDefault(item =>
            string.Equals(item.Name, desired, StringComparison.OrdinalIgnoreCase));
        if (desiredItem is not null)
        {
            ProfilesList.SelectedItem = desiredItem;
            RefreshProfileMods();
        }
        else
        {
            selectedProfile = null;
            ProfilesList.SelectedIndex = -1;
            if (desired is not null)
            {
                await new ManagerSettingsService(managerStateRoot)
                    .SetCurrentProfileAsync(null).ConfigureAwait(true);
                StatusText.Text = "The saved current profile no longer exists. Choose a profile explicitly.";
            }
            RefreshProfileMods();
        }
        RefreshPackageLabels();
        UpdateActionState();
    }

    private void RefreshProfileLabels(string? currentName)
    {
        var items = profilesSnapshot.Select(profile => new ProfileListItem(
            profile.Name,
            profile.Mods.Count(mod => mod.Enabled),
            profile.Mods.Count,
            string.Equals(profile.Name, currentName, StringComparison.OrdinalIgnoreCase))).ToArray();
        refreshingProfileSelection = true;
        try
        {
            ProfilesList.ItemsSource = items;
            ProfilesList.SelectedItem = items.SingleOrDefault(item => item.Current);
        }
        finally
        {
            refreshingProfileSelection = false;
        }
    }

    private void RefreshProfileMods()
    {
        CurrentProfileText.Text = selectedProfile is null
            ? "No current profile selected"
            : $"Current profile: {selectedProfile.Name}";
        ProfileSummaryText.Text = selectedProfile is null
            ? "Select a profile"
            : $"{selectedProfile.Mods.Count(mod => mod.Enabled)} enabled / {selectedProfile.Mods.Count} total";
        ProfileModsList.ItemsSource = selectedProfile?.Mods
            .OrderBy(mod => mod.Order)
            .Select(mod => $"{(mod.Enabled ? "● Enabled" : "○ Disabled")}  {mod.Id} {mod.Version}")
            .ToArray() ?? [];
        UpdateActionState();
    }

    private void RefreshPackageLabels()
    {
        var selected = SelectedInstalledPackage();
        var items = installedPackages.Select(package =>
        {
            var entry = package.Id is null ? null : selectedProfile?.Mods.SingleOrDefault(mod =>
                string.Equals(mod.Id, package.Id, StringComparison.Ordinal));
            return new ModListItem(
                package,
                package.Name ?? package.Id ?? "Invalid package",
                package.Version ?? "Invalid",
                AssetUri(IconNameForPackage(package)),
                AssetUri(entry is { Enabled: true } ? "switchon.png" : "switchoff.png"),
                entry is { Enabled: true },
                selectedProfile is null
                    ? "Select a profile first"
                    : entry is { Enabled: true }
                        ? "Disable in the current profile"
                        : "Enable in the current profile");
        }).ToArray();
        ProfilePackagesList.ItemsSource = items;
        ProfilePackagesList.SelectedItem = items.SingleOrDefault(item => selected is not null &&
            string.Equals(item.Package.Id, selected.Id, StringComparison.Ordinal) &&
            string.Equals(item.Package.Version, selected.Version, StringComparison.Ordinal)) ??
            items.FirstOrDefault(item => item.Package.Valid);
        var validPackages = installedPackages.Count(package => package.Valid);
        var enabledMods = selectedProfile?.Mods.Count(mod => mod.Enabled) ?? 0;
        ModsSummaryText.Text = validPackages == 0
            ? "No packages installed"
            : selectedProfile is null
                ? $"{validPackages} installed"
                : $"{enabledMods} enabled";
        UpdateActionState();
    }

    private async Task RefreshReadinessAsync(bool reportToStatusBar = false)
    {
        launchReady = false;
        if (string.IsNullOrWhiteSpace(GameDirectoryText.Text))
        {
            readinessSummary = "Choose a supported game copy.";
            UpdateActionState();
            return;
        }
        if (selectedProfile is null)
        {
            readinessSummary = "Choose a current profile to prepare a modded launch.";
            UpdateActionState();
            return;
        }
        try
        {
            var status = await new GameLaunchService(managerStateRoot)
                .GetStatusAsync(GameDirectoryText.Text, selectedProfile.Name).ConfigureAwait(true);
            launchReady = status.Problems.Count == 0;
            readinessSummary = launchReady
                ? $"Ready • {selectedProfile.Mods.Count(mod => mod.Enabled)} mods • {status.BuildId}"
                : status.Problems[0];
            if (reportToStatusBar)
            {
                StatusText.Text = launchReady
                    ? "Launch validation passed."
                    : "Launch blocked: " + string.Join(" ", status.Problems);
            }
        }
        catch (Exception exception) when (exception is IOException or InvalidDataException or InvalidOperationException)
        {
            readinessSummary = "Launch readiness could not be checked.";
            if (reportToStatusBar)
            {
                StatusText.Text = exception.Message;
            }
        }
        UpdateActionState();
    }

    private void UpdateActionState()
    {
        var package = SelectedInstalledPackage();
        var entry = package?.Id is null ? null : selectedProfile?.Mods.SingleOrDefault(mod =>
            string.Equals(mod.Id, package.Id, StringComparison.Ordinal));
        var hasPackage = package is { Valid: true, Id: not null, Version: not null };
        var hasProfile = selectedProfile is not null;
        ConfigureModButton.IsEnabled = entry is not null && package!.ConfigurationDefaults.Count != 0;
        ConfigureModButton.Visibility = entry is null ? Visibility.Collapsed : Visibility.Visible;
        UninstallPackageButton.IsEnabled = hasPackage;
        UninstallPackageButton.Visibility = hasPackage ? Visibility.Visible : Visibility.Collapsed;

        var selectedModIndex = ProfileModsList.SelectedIndex;
        MoveUpButton.IsEnabled = hasProfile && selectedModIndex > 0;
        MoveDownButton.IsEnabled = hasProfile && selectedModIndex >= 0 &&
            selectedModIndex < (selectedProfile?.Mods.Count ?? 0) - 1;
        RemoveFromProfileButton.IsEnabled = entry is not null;
        RemoveFromProfileButton.Visibility = entry is not null
            ? Visibility.Visible
            : Visibility.Collapsed;
        RenameProfileButton.IsEnabled = hasProfile;
        DuplicateProfileButton.IsEnabled = hasProfile;
        DeleteProfileButton.IsEnabled = hasProfile;

        ExportProfileButton.IsEnabled = hasProfile;
        var offlineAcknowledged = OfflineRiskCheck.IsChecked == true;
        var offlineBlocksLaunch = launchReady && !offlineAcknowledged;
        LaunchModdedButton.IsEnabled = launchReady;
        LaunchModdedText.Text = offlineBlocksLaunch ? "Offline Mode Required" : "Launch BTD5";
        LaunchModdedButton.Background = offlineBlocksLaunch
            ? new SolidColorBrush(Color.FromRgb(190, 116, 20))
            : new SolidColorBrush(Color.FromRgb(49, 194, 28));
        LaunchModdedButton.BorderBrush = offlineBlocksLaunch
            ? new SolidColorBrush(Color.FromRgb(229, 151, 44))
            : new SolidColorBrush(Color.FromRgb(67, 215, 46));
        ReadinessText.Text = offlineBlocksLaunch
            ? "Launch blocked • acknowledge Steam Offline Mode in Options"
            : readinessSummary;
        OfflineRequirementBorder.Background = !offlineAcknowledged
            ? new SolidColorBrush(Color.FromRgb(51, 37, 25))
            : new SolidColorBrush(Color.FromRgb(23, 42, 32));
        OfflineRequirementBorder.BorderBrush = !offlineAcknowledged
            ? new SolidColorBrush(Color.FromRgb(139, 89, 35))
            : new SolidColorBrush(Color.FromRgb(48, 105, 60));
        OfflineRequirementHint.Foreground = !offlineAcknowledged
            ? new SolidColorBrush(Color.FromRgb(255, 190, 112))
            : new SolidColorBrush(Color.FromRgb(111, 210, 123));
        OfflineRequirementHint.Text = !offlineAcknowledged
            ? "Required before launching with mods."
            : "Acknowledged — modded launch safety check passed.";
        LaunchModdedButton.ToolTip = !launchReady
            ? "Resolve the readiness message before launching."
            : offlineBlocksLaunch
                ? "Open Options and acknowledge Steam Offline Mode before launching."
                : "Launch the current profile.";
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
        return (ProfilePackagesList.SelectedItem as ModListItem)?.Package;
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

    private static string FormatDependencies(ModPackageInfo package) => package.Dependencies.Count == 0
        ? "None"
        : string.Join(", ", package.Dependencies.Select(value => $"{value.Id} {value.Version}"));

    private static string FormatItems(IReadOnlyList<string> items) => items.Count == 0
        ? "None"
        : string.Join(", ", items);

    private static string FormatSuffix(IReadOnlyList<string> items) => items.Count == 0
        ? string.Empty
        : " (" + string.Join(", ", items) + ")";

    private static BitmapImage AssetImage(string name) => new(new Uri(AssetUri(name), UriKind.Relative));

    private static string AssetUri(string name) => $"/BTD5ModLoader.Manager;component/Assets/{name}";

    private static string IconNameForPackage(ModPackageInfo package)
    {
        if (package.Capabilities.Contains("content.towers", StringComparer.Ordinal))
        {
            return "tower.png";
        }
        if (package.Id?.Contains("lives", StringComparison.OrdinalIgnoreCase) == true ||
            package.Id?.Contains("bloon", StringComparison.OrdinalIgnoreCase) == true)
        {
            return "baloon.png";
        }
        if (package.Id?.Contains("event", StringComparison.OrdinalIgnoreCase) == true ||
            package.Id?.Contains("ui", StringComparison.OrdinalIgnoreCase) == true)
        {
            return "computer.png";
        }
        return "monkey_modded.png";
    }

    private sealed record ModListItem(
        ModPackageInfo Package,
        string Name,
        string Version,
        string Icon,
        string ToggleIcon,
        bool Enabled,
        string ToggleHint);

    private sealed record ProfileListItem(string Name, int EnabledMods, int TotalMods, bool Current)
    {
        public string Summary => TotalMods == 1 ? "1 mod" : $"{TotalMods} mods";
    }
}
