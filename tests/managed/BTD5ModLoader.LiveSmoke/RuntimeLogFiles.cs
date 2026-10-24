// SPDX-License-Identifier: GPL-3.0-only
// The runtime rotates runtime.jsonl to runtime.1.jsonl at its size cap, so
// acceptance checks read both files, oldest first.
internal static class RuntimeLogFiles
{
    public static string RotatedPath(string logPath) =>
        Path.Combine(Path.GetDirectoryName(logPath) ?? string.Empty, "runtime.1.jsonl");

    public static void Delete(string logPath)
    {
        File.Delete(RotatedPath(logPath));
        File.Delete(logPath);
    }

    public static async Task<string> ReadAsync(string logPath)
    {
        var builder = new System.Text.StringBuilder();
        foreach (var path in new[] { RotatedPath(logPath), logPath })
        {
            if (!File.Exists(path))
            {
                continue;
            }
            try
            {
                await using var stream = new FileStream(
                    path,
                    FileMode.Open,
                    FileAccess.Read,
                    FileShare.ReadWrite | FileShare.Delete,
                    4096,
                    FileOptions.Asynchronous | FileOptions.SequentialScan);
                using var reader = new StreamReader(stream);
                builder.Append(await reader.ReadToEndAsync());
            }
            catch (FileNotFoundException)
            {
                // Rotation may move a file between the existence check and open.
            }
        }
        return builder.ToString();
    }
}
