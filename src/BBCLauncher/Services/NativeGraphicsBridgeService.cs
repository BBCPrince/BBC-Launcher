using System;
using System.IO;
using System.Runtime.InteropServices;
using BBCLauncher.Configuration;
using Windows.ApplicationModel;

namespace BBCLauncher.Services
{
    public sealed class NativeGraphicsBridgeService
    {
        private readonly LauncherConfig _config;

        public NativeGraphicsBridgeService(LauncherConfig config)
        {
            _config = config;
        }

        public NativeGraphicsProbeResult ProbeBridge(string storageRoot)
        {
            var dllPath = ResolveBridgePath(storageRoot);
            if (!File.Exists(dllPath))
            {
                return NativeGraphicsProbeResult.Fail("Bridge DLL missing: " + dllPath);
            }

            try
            {
                var library = NativeLibrary.Load(dllPath);
                try
                {
                    var createDeviceExport = NativeLibrary.GetExport(library, "mgb_create_d3d12_device_probe");
                    var getRendererExport = NativeLibrary.GetExport(library, "mgb_get_renderer_string");
                    var getVersionExport = NativeLibrary.GetExport(library, "mgb_get_version_string");
                    var getLastErrorExport = NativeLibrary.GetExport(library, "mgb_get_last_error_string");

                    var createDevice = Marshal.GetDelegateForFunctionPointer<CreateProbeDelegate>(createDeviceExport);
                    var getRenderer = Marshal.GetDelegateForFunctionPointer<GetStringDelegate>(getRendererExport);
                    var getVersion = Marshal.GetDelegateForFunctionPointer<GetStringDelegate>(getVersionExport);
                    var getLastError = Marshal.GetDelegateForFunctionPointer<GetStringDelegate>(getLastErrorExport);

                    var ok = createDevice() == 1;
                    var renderer = PtrToStringSafe(getRenderer());
                    var version = PtrToStringSafe(getVersion());
                    var error = PtrToStringSafe(getLastError());
                    if (!ok)
                    {
                        return NativeGraphicsProbeResult.Fail(
                            "Bridge probe failed: " + (string.IsNullOrWhiteSpace(error) ? "unknown error" : error));
                    }

                    return NativeGraphicsProbeResult.Success(renderer, version);
                }
                finally
                {
                    NativeLibrary.Free(library);
                }
            }
            catch (Exception ex)
            {
                return NativeGraphicsProbeResult.Fail("Bridge load/execute exception: " + ex.Message);
            }
        }

        public NativeGraphicsProbeResult ProbeMesaEglCoreWindow(
            string storageRoot,
            IntPtr coreWindowUnknown,
            int width,
            int height)
        {
            var dllPath = ResolveBridgePath(storageRoot);
            if (!File.Exists(dllPath))
            {
                return NativeGraphicsProbeResult.Fail("Bridge DLL missing: " + dllPath);
            }

            var mesaDirectory = ResolveMesaRuntimePath(storageRoot);
            if (!Directory.Exists(mesaDirectory))
            {
                return NativeGraphicsProbeResult.Fail("Mesa-UWP runtime missing: " + mesaDirectory);
            }

            try
            {
                var library = NativeLibrary.Load(dllPath);
                try
                {
                    var probeExport = NativeLibrary.GetExport(library, "mgb_probe_mesa_egl_corewindow");
                    var getRendererExport = NativeLibrary.GetExport(library, "mgb_get_renderer_string");
                    var getVersionExport = NativeLibrary.GetExport(library, "mgb_get_version_string");
                    var getLastErrorExport = NativeLibrary.GetExport(library, "mgb_get_last_error_string");

                    var probe = Marshal.GetDelegateForFunctionPointer<MesaEglCoreWindowProbeDelegate>(probeExport);
                    var getRenderer = Marshal.GetDelegateForFunctionPointer<GetStringDelegate>(getRendererExport);
                    var getVersion = Marshal.GetDelegateForFunctionPointer<GetStringDelegate>(getVersionExport);
                    var getLastError = Marshal.GetDelegateForFunctionPointer<GetStringDelegate>(getLastErrorExport);

                    var ok = probe(
                        mesaDirectory,
                        coreWindowUnknown,
                        (uint)Math.Max(1, width),
                        (uint)Math.Max(1, height)) == 1;
                    var renderer = PtrToStringSafe(getRenderer());
                    var version = PtrToStringSafe(getVersion());
                    var error = PtrToStringSafe(getLastError());
                    if (!ok)
                    {
                        return NativeGraphicsProbeResult.Fail(
                            "Mesa EGL probe failed: " +
                            (string.IsNullOrWhiteSpace(error) ? "unknown error" : error));
                    }

                    return NativeGraphicsProbeResult.Success(
                        renderer,
                        version,
                        "Mesa EGL probe succeeded.");
                }
                finally
                {
                    NativeLibrary.Free(library);
                }
            }
            catch (Exception ex)
            {
                return NativeGraphicsProbeResult.Fail("Mesa EGL/CoreWindow probe exception: " + ex.Message);
            }
        }

        private string ResolveBridgePath(string storageRoot)
        {
            var dllName = _config.GraphicsBridgeDllName ?? "graphics_bridge.dll";
            var nativeDirectoryName = _config.NativeLibrariesDirectoryName ?? "native";
            var localPath = Path.Combine(storageRoot, nativeDirectoryName, dllName);
            if (File.Exists(localPath))
            {
                return localPath;
            }

            try
            {
                var packagePath = Path.Combine(
                    Package.Current.InstalledLocation.Path,
                    nativeDirectoryName,
                    dllName);
                if (File.Exists(packagePath))
                {
                    return packagePath;
                }
            }
            catch
            {
                // Package.Current is unavailable in some non-app test hosts.
            }

            return localPath;
        }

        private string ResolveMesaRuntimePath(string storageRoot)
        {
            var packageRelative = _config.MesaUwpRuntimePackagePath ?? "native/mesa-uwp";
            var normalized = packageRelative
                .Replace('/', Path.DirectorySeparatorChar)
                .Replace('\\', Path.DirectorySeparatorChar);
            var localPath = Path.Combine(storageRoot, normalized);
            if (Directory.Exists(localPath))
            {
                return localPath;
            }

            try
            {
                var packagePath = Path.Combine(Package.Current.InstalledLocation.Path, normalized);
                if (Directory.Exists(packagePath))
                {
                    return packagePath;
                }
            }
            catch
            {
                // Package.Current is unavailable in some non-app test hosts.
            }

            return localPath;
        }

        private static string PtrToStringSafe(IntPtr ptr)
        {
            return ptr == IntPtr.Zero ? string.Empty : Marshal.PtrToStringAnsi(ptr) ?? string.Empty;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int CreateProbeDelegate();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private delegate int MesaEglCoreWindowProbeDelegate(
            [MarshalAs(UnmanagedType.LPWStr)] string mesaDirectory,
            IntPtr coreWindowUnknown,
            uint width,
            uint height);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr GetStringDelegate();
    }

    public sealed class NativeGraphicsProbeResult
    {
        public bool IsSuccess { get; private set; }
        public string Renderer { get; private set; }
        public string Version { get; private set; }
        public string Message { get; private set; }

        public static NativeGraphicsProbeResult Success(string renderer, string version)
        {
            return Success(renderer, version, "Native D3D12 bridge probe succeeded.");
        }

        public static NativeGraphicsProbeResult Success(string renderer, string version, string message)
        {
            return new NativeGraphicsProbeResult
            {
                IsSuccess = true,
                Renderer = string.IsNullOrWhiteSpace(renderer) ? "d3d12-bridge" : renderer,
                Version = string.IsNullOrWhiteSpace(version) ? "bridge-version-unknown" : version,
                Message = message,
            };
        }

        public static NativeGraphicsProbeResult Fail(string message)
        {
            return new NativeGraphicsProbeResult
            {
                IsSuccess = false,
                Renderer = "bridge-unavailable",
                Version = "bridge-unavailable",
                Message = message,
            };
        }
    }
}
