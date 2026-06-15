using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net.Http;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;
using BBCLauncher.Models;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace BBCLauncher.Services
{
    public sealed class ManifestDownloader
    {
        private static readonly HttpClient Http = new HttpClient();

        public async Task<DownloadManifest> LoadManifestAsync(string manifestUrl, string localManifestPath)
        {
            if (File.Exists(localManifestPath))
            {
                var cached = await Task.Run(() => File.ReadAllText(localManifestPath));
                return NormalizeManifest(JsonConvert.DeserializeObject<DownloadManifest>(cached));
            }

            if (string.IsNullOrWhiteSpace(manifestUrl))
            {
                throw new InvalidOperationException("ManifestUrl is empty and local cached manifest was not found.");
            }

            string json;
            if (Uri.TryCreate(manifestUrl, UriKind.Absolute, out var manifestUri) &&
                manifestUri.Scheme.Equals("file", StringComparison.OrdinalIgnoreCase))
            {
                json = await Task.Run(() => File.ReadAllText(manifestUri.LocalPath));
            }
            else if (File.Exists(manifestUrl))
            {
                json = await Task.Run(() => File.ReadAllText(manifestUrl));
            }
            else
            {
                json = await Http.GetStringAsync(manifestUrl);
            }

            Directory.CreateDirectory(Path.GetDirectoryName(localManifestPath) ?? ".");
            await Task.Run(() => File.WriteAllText(localManifestPath, json));
            return NormalizeManifest(JsonConvert.DeserializeObject<DownloadManifest>(json));
        }

        public async Task<bool> EnsureEntryAsync(
            string rootPath,
            ManifestEntry entry,
            FileVerifier verifier,
            IProgress<string> progress,
            CancellationToken cancellationToken)
        {
            if (await verifier.VerifyEntryAsync(rootPath, entry))
            {
                progress?.Report($"Verified {entry.LocalRelativePath}");
                ExpandArchiveIfNeeded(rootPath, entry, progress);
                return true;
            }

            progress?.Report($"Downloading {entry.LocalRelativePath}");
            var destination = Path.Combine(rootPath, entry.LocalRelativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(destination) ?? rootPath);

            using (var response = await Http.GetAsync(entry.RemoteUrl, HttpCompletionOption.ResponseHeadersRead, cancellationToken))
            {
                response.EnsureSuccessStatusCode();
                using (var input = await response.Content.ReadAsStreamAsync())
                using (var output = File.Create(destination))
                {
                    await input.CopyToAsync(output, 81920, cancellationToken);
                }
            }

            if (!await verifier.VerifyEntryAsync(rootPath, entry))
            {
                throw new InvalidOperationException("Downloaded file failed verification: " + entry.LocalRelativePath);
            }

            ExpandArchiveIfNeeded(rootPath, entry, progress);
            return true;
        }

        public async Task<AssetHydrationResult> EnsureAssetObjectsAsync(
            string rootPath,
            DownloadManifest manifest,
            IProgress<string> progress,
            CancellationToken cancellationToken)
        {
            var indexPath = FindDownloadedAssetIndex(rootPath, manifest);
            if (string.IsNullOrWhiteSpace(indexPath) || !File.Exists(indexPath))
            {
                return AssetHydrationResult.Skipped("No downloaded Minecraft asset index was found.");
            }

            var json = await Task.Run(() => File.ReadAllText(indexPath));
            var root = JObject.Parse(json);
            var objects = root["objects"] as JObject;
            if (objects == null || !objects.Properties().Any())
            {
                return AssetHydrationResult.Skipped("Asset index has no objects: " + indexPath);
            }

            var entries = new List<AssetObjectEntry>();
            foreach (var property in objects.Properties())
            {
                var hash = property.Value == null ? null : property.Value.Value<string>("hash");
                var size = property.Value == null ? 0L : property.Value.Value<long?>("size") ?? 0L;
                if (string.IsNullOrWhiteSpace(hash) || hash.Length < 2)
                {
                    continue;
                }

                var prefix = hash.Substring(0, 2);
                entries.Add(new AssetObjectEntry
                {
                    Hash = hash,
                    ExpectedSizeBytes = size,
                    RemoteUrl = "https://resources.download.minecraft.net/" + prefix + "/" + hash,
                    LocalPath = Path.Combine(rootPath, "assets", "objects", prefix, hash),
                });
            }

            if (entries.Count == 0)
            {
                return AssetHydrationResult.Skipped("Asset index had no usable object entries: " + indexPath);
            }

            var result = new AssetHydrationResult { Total = entries.Count };
            var missing = entries
                .Where(entry => !AssetObjectIsPresent(entry))
                .ToList();

            result.Verified = entries.Count - missing.Count;
            if (missing.Count == 0)
            {
                progress?.Report("Minecraft assets already verified: " + result.Verified + "/" + result.Total);
                result.Message = "Minecraft assets already verified: " + result.Total;
                return result;
            }

            progress?.Report("Downloading Minecraft assets: 0/" + missing.Count);
            var completed = 0;
            foreach (var entry in missing)
            {
                cancellationToken.ThrowIfCancellationRequested();
                await DownloadAssetObjectAsync(entry, cancellationToken);
                completed++;
                result.Downloaded++;
                if (completed == missing.Count || completed % 25 == 0)
                {
                    progress?.Report("Downloading Minecraft assets: " + completed + "/" + missing.Count);
                }
            }

            result.Message = "Minecraft assets hydrated: downloaded=" + result.Downloaded +
                " verified=" + result.Verified +
                " total=" + result.Total;
            return result;
        }

        private static void ExpandArchiveIfNeeded(
            string rootPath,
            ManifestEntry entry,
            IProgress<string> progress)
        {
            if (!entry.LocalRelativePath.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
            {
                return;
            }

            var archivePath = Path.Combine(rootPath, entry.LocalRelativePath);
            if (!File.Exists(archivePath))
            {
                return;
            }

            var destination = Path.GetDirectoryName(archivePath) ?? rootPath;
            var hasAnyJars = Directory.Exists(destination) &&
                Directory.EnumerateFiles(destination, "*.jar", SearchOption.AllDirectories).Any();
            if (hasAnyJars)
            {
                return;
            }

            ZipFile.ExtractToDirectory(archivePath, destination, true);
            progress?.Report($"Extracted {entry.LocalRelativePath}");
        }

        private static string FindDownloadedAssetIndex(string rootPath, DownloadManifest manifest)
        {
            var entries = manifest == null ? null : manifest.Entries;
            if (entries != null)
            {
                foreach (var entry in entries)
                {
                    if (entry == null || string.IsNullOrWhiteSpace(entry.LocalRelativePath))
                    {
                        continue;
                    }

                    var relative = entry.LocalRelativePath.Replace('/', Path.DirectorySeparatorChar);
                    if (relative.IndexOf(
                            "assets" + Path.DirectorySeparatorChar + "indexes" + Path.DirectorySeparatorChar,
                            StringComparison.OrdinalIgnoreCase) < 0 ||
                        !relative.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    var candidate = Path.Combine(rootPath, relative);
                    if (File.Exists(candidate))
                    {
                        return candidate;
                    }
                }
            }

            var indexesRoot = Path.Combine(rootPath, "assets", "indexes");
            if (!Directory.Exists(indexesRoot))
            {
                return null;
            }

            return Directory
                .EnumerateFiles(indexesRoot, "*.json", SearchOption.TopDirectoryOnly)
                .OrderByDescending(File.GetLastWriteTimeUtc)
                .FirstOrDefault();
        }

        private static bool AssetObjectIsPresent(AssetObjectEntry entry)
        {
            if (!File.Exists(entry.LocalPath))
            {
                return false;
            }

            var info = new FileInfo(entry.LocalPath);
            if (entry.ExpectedSizeBytes > 0 && info.Length != entry.ExpectedSizeBytes)
            {
                return false;
            }

            var sha1 = ComputeSha1(entry.LocalPath);
            return string.Equals(sha1, entry.Hash, StringComparison.OrdinalIgnoreCase);
        }

        private static async Task DownloadAssetObjectAsync(
            AssetObjectEntry entry,
            CancellationToken cancellationToken)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(entry.LocalPath) ?? ".");
            var tempPath = entry.LocalPath + ".download";
            if (File.Exists(tempPath))
            {
                File.Delete(tempPath);
            }

            using (var response = await Http.GetAsync(entry.RemoteUrl, HttpCompletionOption.ResponseHeadersRead, cancellationToken))
            {
                response.EnsureSuccessStatusCode();
                using (var input = await response.Content.ReadAsStreamAsync())
                using (var output = File.Create(tempPath))
                {
                    await input.CopyToAsync(output, 81920, cancellationToken);
                }
            }

            var tempInfo = new FileInfo(tempPath);
            if (entry.ExpectedSizeBytes > 0 && tempInfo.Length != entry.ExpectedSizeBytes)
            {
                File.Delete(tempPath);
                throw new InvalidOperationException("Downloaded asset size mismatch: " + entry.Hash);
            }

            var sha1 = ComputeSha1(tempPath);
            if (!string.Equals(sha1, entry.Hash, StringComparison.OrdinalIgnoreCase))
            {
                File.Delete(tempPath);
                throw new InvalidOperationException("Downloaded asset hash mismatch: " + entry.Hash);
            }

            if (File.Exists(entry.LocalPath))
            {
                File.SetAttributes(entry.LocalPath, System.IO.FileAttributes.Normal);
                File.Delete(entry.LocalPath);
            }
            File.Move(tempPath, entry.LocalPath);
        }

        private static string ComputeSha1(string path)
        {
            using (var sha1 = SHA1.Create())
            using (var stream = File.OpenRead(path))
            {
                var bytes = sha1.ComputeHash(stream);
                return BitConverter.ToString(bytes).Replace("-", string.Empty).ToLowerInvariant();
            }
        }

        private static DownloadManifest NormalizeManifest(DownloadManifest manifest)
        {
            manifest = manifest ?? new DownloadManifest();
            manifest.Entries = manifest.Entries ?? new System.Collections.Generic.List<ManifestEntry>();
            return manifest;
        }

        private sealed class AssetObjectEntry
        {
            public string Hash { get; set; }
            public long ExpectedSizeBytes { get; set; }
            public string RemoteUrl { get; set; }
            public string LocalPath { get; set; }
        }
    }

    public sealed class AssetHydrationResult
    {
        public int Total { get; set; }
        public int Verified { get; set; }
        public int Downloaded { get; set; }
        public string Message { get; set; }

        public static AssetHydrationResult Skipped(string message)
        {
            return new AssetHydrationResult { Message = message };
        }
    }
}
