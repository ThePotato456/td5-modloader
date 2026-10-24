using System.Diagnostics;
using System.Text.Json;

namespace BTD5ModLoader.Manager.Core;

public sealed record ScriptValidationResult(
    bool Ran,
    bool Valid,
    IReadOnlyList<string> Towers,
    IReadOnlyList<string> Errors);

// Runs a package's Lua entry script through btd5loader_validate.exe, the
// loader's own Lua host and tower registry built as a tool, so invalid custom
// tower definitions are reported before the package is installed or launched.
// When the tool is not next to the manager, nothing runs (Ran is false) and
// the loader still checks the script when the game loads it.
public static class ScriptValidationService
{
    public const string ValidatorFileName = "btd5loader_validate.exe";
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(30);

    public static string DefaultValidatorPath => Path.Combine(AppContext.BaseDirectory, ValidatorFileName);

    public static async Task<ScriptValidationResult> ValidateAsync(
        string packagePath,
        string? gameDirectory = null,
        string? validatorPath = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(packagePath);
        var tool = validatorPath ?? DefaultValidatorPath;
        if (!File.Exists(tool))
        {
            return new(false, true, [], []);
        }
        var startInfo = new ProcessStartInfo(tool)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };
        startInfo.ArgumentList.Add(Path.GetFullPath(packagePath));
        if (!string.IsNullOrWhiteSpace(gameDirectory) && Directory.Exists(gameDirectory))
        {
            startInfo.ArgumentList.Add("--game");
            startInfo.ArgumentList.Add(Path.GetFullPath(gameDirectory));
        }
        using var process = Process.Start(startInfo);
        if (process is null)
        {
            return new(true, false, [], ["The script validator could not be started."]);
        }
        var output = process.StandardOutput.ReadToEndAsync(cancellationToken);
        _ = process.StandardError.ReadToEndAsync(cancellationToken);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(Timeout);
        try
        {
            await process.WaitForExitAsync(timeout.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            process.Kill(entireProcessTree: true);
            return new(true, false, [], ["The package's Lua script did not finish loading within 30 seconds."]);
        }
        try
        {
            using var document = JsonDocument.Parse(await output.ConfigureAwait(false));
            var root = document.RootElement;
            var errors = root.TryGetProperty("errors", out var errorList)
                ? errorList.EnumerateArray().Select(error => error.GetString() ?? "").ToList()
                : [];
            var towers = root.TryGetProperty("towers", out var towerList)
                ? towerList.EnumerateArray().Select(tower => tower.GetString() ?? "").ToList()
                : [];
            var valid = root.TryGetProperty("valid", out var validValue) && validValue.GetBoolean();
            return new(true, valid && errors.Count == 0, towers, errors);
        }
        catch (JsonException)
        {
            return new(true, false, [], ["The script validator returned unreadable output."]);
        }
    }
}
