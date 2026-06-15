using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Threading.Tasks;
using Windows.ApplicationModel;
using Windows.Storage;

namespace BBCLauncher.Services
{
    public static class PackagedPayloadBootstrapper
    {
        private const string PackagePayloadDirectoryName = "LocalStatePayload";
        private const string PackagePayloadZipName = "minecraft-localstate-payload.zip";
        private const string MarkerFileName = ".packaged-payload-version";
        private const string SummaryFileName = "staging-summary.json";

        public static async Task CopyToLocalStateIfPresentAsync(Func<string, Task> logAsync = null)
        {
            try
            {
                var packageRoot = Package.Current.InstalledLocation.Path;
                var payloadRoot = Path.Combine(packageRoot, PackagePayloadDirectoryName);
                var payloadZipPath = Path.Combine(payloadRoot, PackagePayloadZipName);
                if (!Directory.Exists(payloadRoot) && !File.Exists(payloadZipPath))
                {
                    await LogAsync(logAsync, "[Payload][SKIP] No packaged LocalState payload found.");
                    return;
                }

                var localRoot = ApplicationData.Current.LocalFolder.Path;
                var markerPath = Path.Combine(localRoot, MarkerFileName);
                var payloadMarker = await BuildPayloadMarkerAsync(payloadRoot, payloadZipPath);
                if (File.Exists(markerPath))
                {
                    var currentMarker = await Task.Run(() => File.ReadAllText(markerPath));
                    if (string.Equals(currentMarker, payloadMarker, StringComparison.Ordinal))
                    {
                        await LogAsync(logAsync, "[Payload][PASS] Packaged LocalState payload already current.");
                        return;
                    }
                }

                PayloadCopyResult copyResult;
                if (File.Exists(payloadZipPath))
                {
                    copyResult = await Task.Run(() => ExtractZipToDirectory(payloadZipPath, localRoot));
                }
                else
                {
                    copyResult = await Task.Run(() => CopyDirectory(payloadRoot, localRoot));
                }

                await LogAsync(
                    logAsync,
                    "[Payload][INFO] Packaged LocalState payload copy: files=" + copyResult.FilesCopied +
                    " dirs=" + copyResult.DirectoriesCreated +
                    " failed=" + copyResult.Failures.Count);

                foreach (var failure in copyResult.Failures)
                {
                    await LogAsync(logAsync, "[Payload][WARN] " + failure);
                }

                if (copyResult.Failures.Count == 0)
                {
                    await Task.Run(() => File.WriteAllText(markerPath, payloadMarker));
                    await LogAsync(logAsync, "[Payload][PASS] Packaged LocalState payload copied and marker updated.");
                }
                else
                {
                    await LogAsync(logAsync, "[Payload][WARN] Packaged LocalState payload had copy failures; marker not updated so the next launch will retry.");
                }
            }
            catch (Exception ex)
            {
                await LogAsync(logAsync, "[Payload][FAIL] Packaged LocalState payload bootstrap failed: " + DescribeException(ex));
            }
        }

        private static async Task<string> BuildPayloadMarkerAsync(string payloadRoot, string payloadZipPath)
        {
            if (File.Exists(payloadZipPath))
            {
                var zipInfo = new FileInfo(payloadZipPath);
                return PackagePayloadZipName + "|" + zipInfo.Length + "|" + zipInfo.LastWriteTimeUtc.ToString("O");
            }

            var summaryPath = Path.Combine(payloadRoot, SummaryFileName);
            if (File.Exists(summaryPath))
            {
                return await Task.Run(() => File.ReadAllText(summaryPath));
            }

            return Directory.GetLastWriteTimeUtc(payloadRoot).ToString("O");
        }

        private static PayloadCopyResult ExtractZipToDirectory(string zipPath, string destinationRoot)
        {
            var result = new PayloadCopyResult();
            using (var archive = ZipFile.OpenRead(zipPath))
            {
                foreach (var entry in archive.Entries)
                {
                    var normalized = entry.FullName.Replace('\\', '/');
                    if (string.IsNullOrWhiteSpace(normalized))
                    {
                        continue;
                    }

                    var destination = GetSafeDestinationPath(destinationRoot, normalized);
                    if (destination == null)
                    {
                        result.AddFailure(normalized + ": refused path outside LocalState");
                        continue;
                    }

                    try
                    {
                        if (string.IsNullOrEmpty(entry.Name))
                        {
                            Directory.CreateDirectory(destination);
                            result.DirectoriesCreated++;
                            continue;
                        }

                        Directory.CreateDirectory(Path.GetDirectoryName(destination) ?? destinationRoot);
                        if (File.Exists(destination))
                        {
                            File.SetAttributes(destination, System.IO.FileAttributes.Normal);
                        }
                        entry.ExtractToFile(destination, true);
                        result.FilesCopied++;
                    }
                    catch (Exception ex)
                    {
                        result.AddFailure(normalized + ": " + DescribeException(ex));
                    }
                }
            }

            return result;
        }

        private static PayloadCopyResult CopyDirectory(string sourceRoot, string destinationRoot)
        {
            var result = new PayloadCopyResult();

            foreach (var directory in Directory.EnumerateDirectories(sourceRoot, "*", SearchOption.AllDirectories))
            {
                var relative = GetRelativePath(sourceRoot, directory);
                var destination = GetSafeDestinationPath(destinationRoot, relative);
                if (destination == null)
                {
                    result.AddFailure(relative + ": refused path outside LocalState");
                    continue;
                }

                try
                {
                    Directory.CreateDirectory(destination);
                    result.DirectoriesCreated++;
                }
                catch (Exception ex)
                {
                    result.AddFailure(relative + ": " + DescribeException(ex));
                }
            }

            foreach (var file in Directory.EnumerateFiles(sourceRoot, "*", SearchOption.AllDirectories))
            {
                var relative = GetRelativePath(sourceRoot, file);
                var destination = GetSafeDestinationPath(destinationRoot, relative);
                if (destination == null)
                {
                    result.AddFailure(relative + ": refused path outside LocalState");
                    continue;
                }

                try
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(destination) ?? destinationRoot);
                    if (File.Exists(destination))
                    {
                        File.SetAttributes(destination, System.IO.FileAttributes.Normal);
                    }
                    File.Copy(file, destination, true);
                    result.FilesCopied++;
                }
                catch (Exception ex)
                {
                    result.AddFailure(relative + ": " + DescribeException(ex));
                }
            }

            return result;
        }

        private static string GetRelativePath(string root, string path)
        {
            var rootUri = new Uri(AppendDirectorySeparator(root));
            var pathUri = new Uri(path);
            return Uri.UnescapeDataString(rootUri.MakeRelativeUri(pathUri).ToString())
                .Replace('/', Path.DirectorySeparatorChar);
        }

        private static string AppendDirectorySeparator(string path)
        {
            return path.EndsWith(Path.DirectorySeparatorChar.ToString(), StringComparison.Ordinal)
                ? path
                : path + Path.DirectorySeparatorChar;
        }

        private static string GetSafeDestinationPath(string root, string relativePath)
        {
            var rootFullPath = Path.GetFullPath(AppendDirectorySeparator(root));
            var destination = Path.GetFullPath(
                Path.Combine(rootFullPath, relativePath.Replace('/', Path.DirectorySeparatorChar)));
            return destination.StartsWith(rootFullPath, StringComparison.OrdinalIgnoreCase)
                ? destination
                : null;
        }

        private static Task LogAsync(Func<string, Task> logAsync, string line)
        {
            return logAsync == null ? Task.CompletedTask : logAsync(line);
        }

        private static string DescribeException(Exception ex)
        {
            var message = ex.Message;
            if (string.IsNullOrWhiteSpace(message))
            {
                message = "HRESULT 0x" + ex.HResult.ToString("X8");
            }

            return ex.GetType().Name + ": " + message;
        }

        private sealed class PayloadCopyResult
        {
            private const int MaxFailureSamples = 16;

            public int FilesCopied { get; set; }
            public int DirectoriesCreated { get; set; }
            public List<string> Failures { get; } = new List<string>();

            public void AddFailure(string failure)
            {
                if (Failures.Count < MaxFailureSamples)
                {
                    Failures.Add(failure);
                }
                else if (Failures.Count == MaxFailureSamples)
                {
                    Failures.Add("additional payload copy failures suppressed");
                }
            }
        }
    }
}
