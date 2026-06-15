using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using BBCLauncher.Configuration;
using Windows.Storage;

namespace BBCLauncher.Services
{
    public sealed class PathProbeService
    {
        private readonly LauncherConfig _config;

        public PathProbeService(LauncherConfig config)
        {
            _config = config;
        }

        public async Task<PathProbeReport> ProbeAsync(
            string javaExePath,
            string probeJarPath,
            string jdkPatchJarPath,
            IReadOnlyList<string> candidatePaths,
            CancellationToken cancellationToken)
        {
            var report = new PathProbeReport();
            if (string.IsNullOrWhiteSpace(javaExePath) || !File.Exists(javaExePath))
            {
                report.Lines.Add("[Probe][SKIP] java.exe not found at " + javaExePath);
                return report;
            }
            if (string.IsNullOrWhiteSpace(probeJarPath) || !File.Exists(probeJarPath))
            {
                report.Lines.Add("[Probe][SKIP] XboxPathProbe.jar not staged at " + probeJarPath);
                return report;
            }
            if (candidatePaths == null || candidatePaths.Count == 0)
            {
                report.Lines.Add("[Probe][SKIP] No candidate paths supplied.");
                return report;
            }

            var argBuilder = new StringBuilder();
            if (_config.ApplyJdkPatchToProbe && !string.IsNullOrWhiteSpace(jdkPatchJarPath) && File.Exists(jdkPatchJarPath))
            {
                argBuilder.Append("--patch-module \"java.base=").Append(jdkPatchJarPath).Append("\" ");
                report.Lines.Add("[Probe][Info] Patch applied to probe JVM: --patch-module java.base=" + jdkPatchJarPath);
            }
            argBuilder.Append("-jar \"").Append(probeJarPath).Append("\" ");
            foreach (var path in candidatePaths)
            {
                argBuilder.Append('"').Append(path).Append("\" ");
            }

            var startInfo = new ProcessStartInfo
            {
                FileName = javaExePath,
                Arguments = argBuilder.ToString().Trim(),
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                WorkingDirectory = Path.GetDirectoryName(probeJarPath) ?? string.Empty,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8,
            };

            var stdout = new StringBuilder();
            var stderr = new StringBuilder();
            using (var process = new Process { StartInfo = startInfo, EnableRaisingEvents = true })
            {
                process.OutputDataReceived += (_, e) =>
                {
                    if (!string.IsNullOrEmpty(e.Data)) { stdout.AppendLine(e.Data); }
                };
                process.ErrorDataReceived += (_, e) =>
                {
                    if (!string.IsNullOrEmpty(e.Data)) { stderr.AppendLine(e.Data); }
                };
                try
                {
                    process.Start();
                    process.BeginOutputReadLine();
                    process.BeginErrorReadLine();
                    var exitedInTime = await Task.Run(() => process.WaitForExit(_config.PathProbeTimeoutSeconds * 1000), cancellationToken);
                    if (!exitedInTime)
                    {
                        try { process.Kill(true); } catch { }
                        report.Lines.Add("[Probe][TIMEOUT] Probe did not finish within " + _config.PathProbeTimeoutSeconds + "s.");
                        return report;
                    }
                }
                catch (Exception ex)
                {
                    report.Lines.Add("[Probe][FAIL] Could not spawn java.exe: " + ex.GetType().Name + ": " + ex.Message);
                    return report;
                }
            }

            ParseProbeOutput(stdout.ToString(), stderr.ToString(), candidatePaths, report);
            return report;
        }

        private static void ParseProbeOutput(
            string stdout,
            string stderr,
            IReadOnlyList<string> candidatePaths,
            PathProbeReport report)
        {
            foreach (var line in stdout.Split('\n'))
            {
                var trimmed = line.Trim();
                if (string.IsNullOrEmpty(trimmed)) continue;
                if (trimmed.StartsWith("PROBE_BEGIN|", StringComparison.Ordinal))
                {
                    report.Lines.Add("[Probe][Env] " + trimmed.Substring("PROBE_BEGIN|".Length));
                    continue;
                }
                if (trimmed.StartsWith("PROBE_END", StringComparison.Ordinal)) continue;
                if (trimmed.StartsWith("PROBE_ERROR|", StringComparison.Ordinal))
                {
                    report.Lines.Add("[Probe][ERROR] " + trimmed.Substring("PROBE_ERROR|".Length));
                    continue;
                }
                if (!trimmed.StartsWith("PROBE|", StringComparison.Ordinal)) continue;

                var parts = trimmed.Substring("PROBE|".Length).Split('|');
                if (parts.Length < 2) continue;
                var result = new PathProbeResult { Path = parts[0] };
                for (var i = 1; i < parts.Length; i++)
                {
                    var kv = parts[i].Split(new[] { '=' }, 2);
                    if (kv.Length != 2) continue;
                    result.Steps[kv[0]] = kv[1];
                    if (!string.Equals(kv[1], "OK", StringComparison.OrdinalIgnoreCase))
                    {
                        result.FailedSteps.Add(kv[0]);
                    }
                }
                report.Results.Add(result);
            }

            foreach (var path in candidatePaths)
            {
                var exists = report.Results.Any(r => string.Equals(r.Path, path, StringComparison.OrdinalIgnoreCase));
                if (!exists)
                {
                    var missing = new PathProbeResult { Path = path };
                    missing.Steps["status"] = "no output";
                    missing.FailedSteps.Add("no output");
                    report.Results.Add(missing);
                }
            }

            foreach (var r in report.Results)
            {
                var status = r.IsFullyUsable ? "USABLE" : "blocked";
                var detail = string.Join(" ", r.Steps.Select(kvp => kvp.Key + "=" + kvp.Value));
                report.Lines.Add("[Probe][" + status + "] " + r.Path + "  " + detail);
            }

            if (!string.IsNullOrWhiteSpace(stderr))
            {
                foreach (var line in stderr.Split('\n'))
                {
                    var trimmed = line.Trim();
                    if (!string.IsNullOrEmpty(trimmed))
                    {
                        report.Lines.Add("[Probe][stderr] " + trimmed);
                    }
                }
            }

            report.FirstFullyUsablePath = report.Results
                .Where(r => r.IsFullyUsable)
                .Select(r => r.Path)
                .FirstOrDefault();
        }

        public List<string> BuildCandidatePaths(StorageFolder localFolder)
        {
            var candidates = new List<string>();
            void Add(string path)
            {
                if (!string.IsNullOrWhiteSpace(path) && !candidates.Contains(path, StringComparer.OrdinalIgnoreCase))
                {
                    candidates.Add(path);
                }
            }

            Add(Path.Combine(localFolder.Path, _config.GameDirectoryName));
            try { Add(Path.Combine(ApplicationData.Current.LocalCacheFolder.Path, _config.GameDirectoryName)); } catch { }
            try { Add(Path.Combine(ApplicationData.Current.TemporaryFolder.Path, _config.GameDirectoryName)); } catch { }
            Add(Path.Combine(Path.GetTempPath(), "MinecraftXbox", _config.GameDirectoryName));
            Add(@"Q:\Users\Public\MinecraftXbox\" + _config.GameDirectoryName);
            Add(@"T:\MinecraftXbox\" + _config.GameDirectoryName);

            if (_config.PathProbeExtraCandidates != null)
            {
                foreach (var extra in _config.PathProbeExtraCandidates)
                {
                    Add(extra);
                }
            }

            return candidates;
        }
    }

    public sealed class PathProbeReport
    {
        public List<string> Lines { get; } = new List<string>();
        public List<PathProbeResult> Results { get; } = new List<PathProbeResult>();
        public string FirstFullyUsablePath { get; set; }
    }

    public sealed class PathProbeResult
    {
        public string Path { get; set; }
        public Dictionary<string, string> Steps { get; } = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public List<string> FailedSteps { get; } = new List<string>();

        public bool IsFullyUsable
        {
            get
            {
                if (!Steps.TryGetValue("createDir", out var c) || !string.Equals(c, "OK", StringComparison.OrdinalIgnoreCase)) return false;
                if (!Steps.TryGetValue("toRealPath", out var t) || !string.Equals(t, "OK", StringComparison.OrdinalIgnoreCase)) return false;
                if (!Steps.TryGetValue("write", out var w) || !string.Equals(w, "OK", StringComparison.OrdinalIgnoreCase)) return false;
                if (!Steps.TryGetValue("getFileStore", out var g) || !string.Equals(g, "OK", StringComparison.OrdinalIgnoreCase)) return false;
                return true;
            }
        }
    }
}
