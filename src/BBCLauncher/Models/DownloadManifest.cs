using System.Collections.Generic;

namespace BBCLauncher.Models
{
    public sealed class DownloadManifest
    {
        public string MinecraftVersion { get; set; } = "1.21.1";
        public List<ManifestEntry> Entries { get; set; } = new List<ManifestEntry>();
    }

    public sealed class ManifestEntry
    {
        public string RemoteUrl { get; set; }
        public string LocalRelativePath { get; set; }
        public long ExpectedSizeBytes { get; set; }
        public string Sha256 { get; set; }
        public bool RequiredBeforeLaunch { get; set; } = true;
    }
}
