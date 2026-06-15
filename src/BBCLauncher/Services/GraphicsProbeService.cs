using System;
using System.Runtime.InteropServices;
using Windows.Storage;
using System.Threading.Tasks;
using BBCLauncher.Configuration;
using Windows.UI.Xaml;

namespace BBCLauncher.Services
{
    /// <summary>
    /// Diagnostic build helper (spec section 10, test 10).
    /// Integrate ANGLE/Zink or your D3D12 OpenGL bridge here.
    /// </summary>
    public sealed class GraphicsProbeService
    {
        private readonly LauncherConfig _config;

        public GraphicsProbeService(LauncherConfig config)
        {
            _config = config;
        }

        public async Task<GraphicsProbeResult> RunProbeAsync(LaunchLog log)
        {
            await log.WriteLineAsync("[GraphicsProbe] Creating presentation surface");
            await Task.Delay(250);

            var backend = _config.GraphicsBackend;
            var bridge = new NativeGraphicsBridgeService(_config);
            var probe = bridge.ProbeBridge(ApplicationData.Current.LocalFolder.Path);
            var renderer = probe.Renderer;
            var version = probe.Version;
            var success = probe.IsSuccess;
            var message = probe.Message;

            await log.WriteGraphicsAsync(backend, renderer, version);
            await log.WriteLineAsync("[GraphicsProbe] " + message);

            if (string.Equals(_config.OpenGlProvider, "mesa-uwp", StringComparison.OrdinalIgnoreCase))
            {
                var useCoreWindowProbe = _config.EnableMesaCoreWindowProbe;
                var coreWindow = useCoreWindowProbe ? Window.Current?.CoreWindow : null;
                var bounds = coreWindow?.Bounds;
                var width = bounds.HasValue ? (int)Math.Max(1, bounds.Value.Width) : 64;
                var height = bounds.HasValue ? (int)Math.Max(1, bounds.Value.Height) : 64;
                var unknown = IntPtr.Zero;
                try
                {
                    await log.WriteLineAsync(
                        "[GraphicsProbe] Mesa-UWP EGL probe mode: " +
                        (useCoreWindowProbe ? "CoreWindow" : "surfaceless pbuffer"));

                    if (coreWindow != null)
                    {
                        unknown = Marshal.GetIUnknownForObject(coreWindow);
                    }

                    var mesaProbe = bridge.ProbeMesaEglCoreWindow(
                        ApplicationData.Current.LocalFolder.Path,
                        unknown,
                        width,
                        height);
                    await log.WriteLineAsync("[GraphicsProbe] " + mesaProbe.Message);
                    await log.WriteGraphicsAsync(backend, mesaProbe.Renderer, mesaProbe.Version);

                    success = success && mesaProbe.IsSuccess;
                    renderer = mesaProbe.Renderer;
                    version = mesaProbe.Version;
                    message = message + " " + mesaProbe.Message;
                }
                finally
                {
                    if (unknown != IntPtr.Zero)
                    {
                        Marshal.Release(unknown);
                    }
                }
            }

            return new GraphicsProbeResult
            {
                Success = success,
                Backend = backend,
                Renderer = renderer,
                Version = version,
                PresentationOk = success,
                Message = message,
            };
        }
    }

    public sealed class GraphicsProbeResult
    {
        public bool Success { get; set; }
        public bool PresentationOk { get; set; }
        public string Backend { get; set; }
        public string Renderer { get; set; }
        public string Version { get; set; }
        public string Message { get; set; }
    }
}
