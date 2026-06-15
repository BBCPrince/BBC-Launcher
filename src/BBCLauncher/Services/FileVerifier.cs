using System;
using System.IO;
using System.Security.Cryptography;
using System.Threading.Tasks;
using BBCLauncher.Models;

namespace BBCLauncher.Services
{
    public sealed class FileVerifier
    {
        public async Task<bool> VerifyEntryAsync(string rootPath, ManifestEntry entry)
        {
            var fullPath = Path.Combine(rootPath, entry.LocalRelativePath);
            if (!File.Exists(fullPath))
            {
                return false;
            }

            var info = new FileInfo(fullPath);
            if (entry.ExpectedSizeBytes > 0 && info.Length != entry.ExpectedSizeBytes)
            {
                return false;
            }

            if (string.IsNullOrWhiteSpace(entry.Sha256))
            {
                return true;
            }

            var hash = await ComputeSha256Async(fullPath);
            return string.Equals(hash, entry.Sha256, StringComparison.OrdinalIgnoreCase);
        }

        public static async Task<string> ComputeSha256Async(string path)
        {
            using (var sha = SHA256.Create())
            using (var stream = File.OpenRead(path))
            {
                var bytes = await Task.Run(() => sha.ComputeHash(stream));
                return BitConverter.ToString(bytes).Replace("-", string.Empty);
            }
        }
    }
}
