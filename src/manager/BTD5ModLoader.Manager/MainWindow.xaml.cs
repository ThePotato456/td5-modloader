using System.IO;
using System.Windows;
using BTD5ModLoader.Manager.Core;
using Microsoft.Win32;

namespace BTD5ModLoader.Manager;

internal sealed partial class MainWindow : Window
{
    private const string SupportedBuildId = "steam-win32-4.8";
    private readonly string managerStateRoot = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "BTD5ModLoader");
    private string? selectedPackagePath;

    public MainWindow()
    {
        InitializeComponent();
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

    private async void InstallPackage_Click(object sender, RoutedEventArgs e)
    {
        if (selectedPackagePath is null)
        {
            return;
        }
        InstallPackageButton.IsEnabled = false;
        StatusText.Text = "Installing package…";
        try
        {
            var result = await ModPackageService.InstallAsync(
                selectedPackagePath, managerStateRoot, SupportedBuildId).ConfigureAwait(true);
            StatusText.Text = result.Message;
            InstallPackageButton.IsEnabled = result.Package.Valid;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            StatusText.Text = "Installation failed: " + exception.Message;
            InstallPackageButton.IsEnabled = true;
        }
    }

    private async Task SelectPackageAsync(string packagePath)
    {
        selectedPackagePath = null;
        InstallPackageButton.IsEnabled = false;
        StatusText.Text = "Validating package…";
        try
        {
            var package = await ModPackageService.InspectAsync(packagePath, SupportedBuildId)
                .ConfigureAwait(true);
            PackageNameText.Text = package.Name ?? Path.GetFileName(packagePath);
            AuthorVersionText.Text = package.Author is null
                ? "—"
                : $"{package.Author} / {package.Version ?? "unknown version"}";
            ModIdText.Text = package.Id ?? "—";
            CapabilitiesText.Text = package.Capabilities.Count == 0
                ? "None"
                : string.Join(", ", package.Capabilities);
            DependenciesText.Text = package.Dependencies.Count == 0
                ? "None"
                : string.Join(", ", package.Dependencies.Select(value => $"{value.Id} {value.Version}"));
            ValidationText.Text = package.Valid
                ? $"Valid for {SupportedBuildId}"
                : string.Join(Environment.NewLine, package.Errors.Select(error => "• " + error));
            StatusText.Text = package.Valid ? "Package is ready to install." : "Package cannot be installed.";
            if (package.Valid)
            {
                selectedPackagePath = packagePath;
                InstallPackageButton.IsEnabled = true;
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            ValidationText.Text = exception.Message;
            StatusText.Text = "Package could not be read.";
        }
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
}
