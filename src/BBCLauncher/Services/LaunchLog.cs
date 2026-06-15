using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using BBCLauncher.Models;

namespace BBCLauncher.Services
{
    public sealed class LaunchLog : IDisposable
    {
        private static readonly object FileGate = new object();
        private readonly string _logPath;
        private readonly StreamWriter _writer;

        public static string CurrentSharedLogPath { get; private set; }

        private LaunchLog(string logPath, StreamWriter writer)
        {
            _logPath = logPath;
            _writer = writer;
        }

        public static async Task<LaunchLog> CreateAsync(string logsDirectory)
        {
            Directory.CreateDirectory(logsDirectory);
            var fileName = $"launch-{DateTime.UtcNow:yyyyMMdd-HHmmss}.log";
            var path = Path.Combine(logsDirectory, fileName);
            var stream = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.ReadWrite);
            var writer = new StreamWriter(stream, Encoding.UTF8) { AutoFlush = true };
            var log = new LaunchLog(path, writer);
            CurrentSharedLogPath = path;
            await log.WriteLineAsync("Launch log started (UTC " + DateTime.UtcNow.ToString("O") + ")");
            return log;
        }

        public string LogPath => _logPath;

        public Task WriteStageAsync(LaunchStage stage, string detail = null) =>
            WriteLineAsync($"[{stage}] {detail ?? string.Empty}".Trim());

        public Task WriteResolutionAsync(ResolutionOption resolution) =>
            WriteLineAsync($"[Resolution] {resolution.Label} {resolution.Width}x{resolution.Height}");

        public Task WritePathsAsync(string gameRoot, string assetsRoot) =>
            WriteLineAsync($"[Paths] game={gameRoot} assets={assetsRoot}");

        public Task WriteAuthSummaryAsync(bool success, string summary) =>
            WriteLineAsync($"[Authentication] success={success} {summary}");

        public Task WriteDownloadSummaryAsync(int verified, int downloaded, int failed) =>
            WriteLineAsync($"[Download] verified={verified} downloaded={downloaded} failed={failed}");

        public Task WriteGraphicsAsync(string backend, string renderer, string version) =>
            WriteLineAsync($"[Graphics] backend={backend} renderer={renderer} version={version}");

        public Task WriteErrorAsync(string message, Exception ex = null)
        {
            var line = "[Error] " + message;
            if (ex != null)
            {
                line += Environment.NewLine + ex;
            }
            return WriteLineAsync(line);
        }

        public Task WriteLineAsync(string line)
        {
            lock (FileGate)
            {
                _writer.WriteLine(DateTime.UtcNow.ToString("O") + " " + line);
            }
            return Task.CompletedTask;
        }

        public static void AppendSharedLine(string logPath, string line)
        {
            if (string.IsNullOrWhiteSpace(logPath))
            {
                return;
            }

            lock (FileGate)
            {
                using (var stream = new FileStream(logPath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
                using (var writer = new StreamWriter(stream, new UTF8Encoding(false)))
                {
                    writer.WriteLine(DateTime.UtcNow.ToString("O") + " " + line);
                }
            }
        }

        public void Dispose()
        {
            _writer?.Dispose();
        }
    }
}
