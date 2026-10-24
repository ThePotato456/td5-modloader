using System.Reflection.PortableExecutable;
using System.Text;

namespace BTD5ModLoader.Manager.Core;

public static class RuntimeCapabilities
{
    // Inspect metadata without loading a 32-bit DLL into the manager process.
    public static bool HasLaunchIsolation(string runtimePath)
    {
        try
        {
            using var stream = File.OpenRead(runtimePath);
            if (stream.Length > 64 * 1024 * 1024) return false;
            using var pe = new PEReader(stream);
            var directory = pe.PEHeaders.PEHeader?.ExportTableDirectory;
            if (pe.PEHeaders.PEHeader?.Magic != PEMagic.PE32 || directory is null || directory.Value.Size < 40)
                return false;
            var exports = pe.GetSectionData(directory.Value.RelativeVirtualAddress).GetReader();
            exports.Offset = 24;
            var count = exports.ReadUInt32();
            if (count is 0 or > 4096) return false;
            exports.Offset = 32;
            var names = pe.GetSectionData(exports.ReadInt32()).GetReader();
            for (var index = 0; index < count; index++)
            {
                var block = pe.GetSectionData(names.ReadInt32());
                var name = block.GetContent(0, Math.Min(128, block.Length)).AsSpan();
                var end = name.IndexOf((byte)0);
                if (end >= 0 && Encoding.ASCII.GetString(name[..end]) == "BTD5Loader_LaunchProtocol1") return true;
            }
            return false;
        }
        catch (Exception exception) when (exception is IOException or BadImageFormatException or
            ArgumentOutOfRangeException or UnauthorizedAccessException)
        {
            return false;
        }
    }
}
