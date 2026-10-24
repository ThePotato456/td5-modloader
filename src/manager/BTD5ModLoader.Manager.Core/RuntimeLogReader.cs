using System.Text;

namespace BTD5ModLoader.Manager.Core;

/// <summary>Reads one bounded snapshot; subsequent refreshes reopen the path after rotation.</summary>
public static class RuntimeLogReader
{
    public const int MaximumBytes = 1024 * 1024;
    public const int MaximumLineBytes = 4096;
    public const string TruncatedPrefix = "[truncated] ";
    public const string PartialSuffix = " [partial record]";

    public static async Task<IReadOnlyList<string>> ReadAsync(
        string path, int maximumLines = 500, CancellationToken cancellationToken = default)
    {
        if (maximumLines is < 1 or > 10_000)
            throw new ArgumentOutOfRangeException(nameof(maximumLines));
        cancellationToken.ThrowIfCancellationRequested();
        try
        {
            var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete, 1, FileOptions.Asynchronous);
            await using (stream.ConfigureAwait(false))
            {
                return await ReadSnapshotAsync(stream, maximumLines, cancellationToken).ConfigureAwait(false);
            }
        }
        catch (FileNotFoundException) { return []; }
        catch (DirectoryNotFoundException) { return []; }
    }

    /// <summary>Reads at most MaximumBytes from a seekable stream, without owning it.</summary>
    public static async Task<IReadOnlyList<string>> ReadSnapshotAsync(
        Stream stream, int maximumLines = 500, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(stream);
        if (maximumLines is < 1 or > 10_000)
            throw new ArgumentOutOfRangeException(nameof(maximumLines));
        cancellationToken.ThrowIfCancellationRequested();
        var length = stream.Length;
        var count = (int)Math.Min(length, MaximumBytes);
        var bytes = new byte[count];
        var offset = length - count;
        stream.Seek(offset, SeekOrigin.Begin);
        var read = 0;
        while (read < count)
        {
            var received = await stream.ReadAsync(bytes.AsMemory(read), cancellationToken)
                .ConfigureAwait(false);
            if (received == 0) break;
            read += received;
        }
        // Never follow concurrent appends. A clear can shorten this snapshot; reopen next refresh.
        cancellationToken.ThrowIfCancellationRequested();
        var lines = new List<string>(Math.Min(maximumLines, 500));
        var chars = new char[MaximumLineBytes];
        var decoder = Encoding.UTF8.GetDecoder();
        var end = read;
        var partial = end > 0 && bytes[end - 1] != (byte)'\n';
        if (!partial && end > 0) end--;
        while (end > 0 && lines.Count < maximumLines)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var separator = Array.LastIndexOf(bytes, (byte)'\n', end - 1, end);
            var begin = separator + 1;
            var contentEnd = end;
            if (contentEnd > begin && bytes[contentEnd - 1] == (byte)'\r') contentEnd--;
            var clipped = contentEnd - begin > MaximumLineBytes || (begin == 0 && offset > 0);
            var textStart = Math.Max(begin, contentEnd - MaximumLineBytes);
            // The byte window or line cap may split a UTF-8 code point.
            if (clipped)
                while (textStart < contentEnd && (bytes[textStart] & 0xc0) == 0x80) textStart++;
            decoder.Reset();
            decoder.Convert(bytes.AsSpan(textStart, contentEnd - textStart), chars,
                flush: !partial, out _, out var charsUsed, out _);
            lines.Add((clipped ? TruncatedPrefix : string.Empty) + new string(chars, 0, charsUsed) +
                (partial ? PartialSuffix : string.Empty));
            partial = false;
            end = separator;
        }
        if (end == 0 && read > 0 && bytes[0] == (byte)'\n' && lines.Count < maximumLines)
            lines.Add(offset > 0 ? TruncatedPrefix : string.Empty);
        lines.Reverse();
        return lines;
    }
}
