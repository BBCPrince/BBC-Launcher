using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using BBCLauncher.Configuration;
using Windows.ApplicationModel;
using Windows.Storage;

namespace BBCLauncher.Services
{
    public sealed class RuntimeAssetStager
    {
        private readonly LauncherConfig _config;

        public RuntimeAssetStager(LauncherConfig config)
        {
            _config = config;
        }

        public async Task<RuntimeAssetStageResult> StageAsync(StorageFolder localFolder)
        {
            var result = new RuntimeAssetStageResult();
            var nativeDir = _config.GetNativeRoot(localFolder);
            Directory.CreateDirectory(nativeDir);
            var packageFolder = Package.Current.InstalledLocation;

            if (_config.StageBundledNativeShims && _config.BundledNativeShims != null)
            {
                foreach (var dllName in _config.BundledNativeShims)
                {
                    if (string.IsNullOrWhiteSpace(dllName))
                    {
                        continue;
                    }

                    var destination = Path.Combine(nativeDir, dllName);
                    try
                    {
                        var copied = await CopyFromPackageAsync(
                            packageFolder,
                            "native/" + dllName,
                            destination);
                        result.Lines.Add(copied
                            ? "[Stage][PASS] Native asset staged: " + dllName
                            : "[Stage][WARN] Native asset not in app package: " + dllName);
                }
                catch (Exception ex)
                {
                    result.Lines.Add("[Stage][WARN] Failed to stage " + dllName + ": " + DescribeException(ex));
                }
                }
            }

            if (_config.UseXboxLog4jConfig && !string.IsNullOrWhiteSpace(_config.XboxLog4jConfigResource))
            {
                var fileName = Path.GetFileName(_config.XboxLog4jConfigResource);
                var destination = Path.Combine(nativeDir, fileName);
                try
                {
                    var copied = await CopyFromPackageAsync(
                        packageFolder,
                        _config.XboxLog4jConfigResource,
                        destination);
                    if (copied)
                    {
                        result.Log4jConfigPath = destination;
                        result.Lines.Add("[Stage][PASS] Xbox log4j config staged: " + destination);
                    }
                    else
                    {
                        result.Lines.Add("[Stage][WARN] Xbox log4j config not bundled in package.");
                    }
                }
                catch (Exception ex)
                {
                    result.Lines.Add("[Stage][WARN] Failed to stage log4j config: " + DescribeException(ex));
                }
            }

            if (_config.RunPathProbeBeforeLaunch && !string.IsNullOrWhiteSpace(_config.PathProbeJarPackagePath))
            {
                var fileName = Path.GetFileName(_config.PathProbeJarPackagePath);
                var destination = Path.Combine(nativeDir, fileName);
                try
                {
                    var copied = await CopyFromPackageAsync(
                        packageFolder,
                        _config.PathProbeJarPackagePath,
                        destination);
                    if (copied)
                    {
                        result.PathProbeJarPath = destination;
                        result.Lines.Add("[Stage][PASS] XboxPathProbe.jar staged: " + destination);
                    }
                    else
                    {
                        result.Lines.Add("[Stage][WARN] XboxPathProbe.jar not bundled in package.");
                    }
                }
                catch (Exception ex)
                {
                    result.Lines.Add("[Stage][WARN] Failed to stage XboxPathProbe.jar: " + DescribeException(ex));
                }
            }

            if (_config.UseJdkPatch && !string.IsNullOrWhiteSpace(_config.JdkPatchJarPackagePath))
            {
                var fileName = Path.GetFileName(_config.JdkPatchJarPackagePath);
                var destination = Path.Combine(nativeDir, fileName);
                try
                {
                    var copied = await CopyFromPackageAsync(
                        packageFolder,
                        _config.JdkPatchJarPackagePath,
                        destination);
                    if (copied)
                    {
                        result.JdkPatchJarPath = destination;
                        result.Lines.Add("[Stage][PASS] JDK patch staged (java.base WindowsLinkSupport replacement): " + destination);
                    }
                    else
                    {
                        result.Lines.Add("[Stage][WARN] xbox-jdk-patch.jar not bundled in package. toRealPath() will fail with AccessDenied on Xbox.");
                    }
                }
                catch (Exception ex)
                {
                    result.Lines.Add("[Stage][WARN] Failed to stage xbox-jdk-patch.jar: " + DescribeException(ex));
                }
            }

            if (_config.UseXboxGlfwStub && !string.IsNullOrWhiteSpace(_config.XboxGlfwDllPackagePath))
            {
                var fileName = Path.GetFileName(_config.XboxGlfwDllPackagePath);
                var destination = Path.Combine(nativeDir, fileName);
                try
                {
                    var copied = await CopyFromPackageAsync(
                        packageFolder,
                        _config.XboxGlfwDllPackagePath,
                        destination);
                    if (copied)
                    {
                        result.XboxGlfwDllPath = destination;
                        result.Lines.Add("[Stage][PASS] Xbox GLFW stub staged (LWJGL will load this instead of bundled glfw.dll): " + destination);
                    }
                    else
                    {
                        if (File.Exists(destination))
                        {
                            result.XboxGlfwDllPath = destination;
                            result.Lines.Add("[Stage][WARN] xbox-glfw.dll not bundled in package; using existing LocalState copy: " + destination);
                        }
                        else
                        {
                            result.Lines.Add("[Stage][WARN] xbox-glfw.dll not bundled in package. LWJGL will fall back to its bundled glfw.dll which calls RegisterClassExW and crashes on Xbox.");
                        }
                    }
                }
                catch (Exception ex)
                {
                    result.Lines.Add("[Stage][WARN] Failed to stage xbox-glfw.dll: " + DescribeException(ex));
                    if (File.Exists(destination))
                    {
                        result.XboxGlfwDllPath = destination;
                        result.Lines.Add("[Stage][WARN] Using existing LocalState xbox-glfw.dll after stage failure: " + destination);
                    }
                }
            }

            var selectedOpenGlProvider = false;
            var openGlProvider = string.IsNullOrWhiteSpace(_config.OpenGlProvider)
                ? "auto"
                : _config.OpenGlProvider.Trim();
            var allowMesaUwp = openGlProvider.Equals("auto", StringComparison.OrdinalIgnoreCase)
                || openGlProvider.Equals("mesa-uwp", StringComparison.OrdinalIgnoreCase);
            var requireMesaUwp = openGlProvider.Equals("mesa-uwp", StringComparison.OrdinalIgnoreCase);
            var allowGlon12 = openGlProvider.Equals("glon12", StringComparison.OrdinalIgnoreCase);
            var requireGlon12 = openGlProvider.Equals("glon12", StringComparison.OrdinalIgnoreCase);

            if (_config.UseCustomOpenGlProvider && allowMesaUwp)
            {
                if (!string.IsNullOrWhiteSpace(_config.MesaUwpRuntimePackagePath))
                {
                    var destination = Path.Combine(
                        localFolder.Path,
                        _config.MesaUwpRuntimePackagePath.Replace('/', Path.DirectorySeparatorChar));
                    try
                    {
                        var copied = await CopyDirectoryFromPackageAsync(
                            packageFolder,
                            _config.MesaUwpRuntimePackagePath,
                            destination);
                        if (copied)
                        {
                            result.Lines.Add("[Stage][PASS] Mesa-UWP runtime staged: " + destination);
                        }
                    }
                    catch (Exception ex)
                    {
                        result.Lines.Add("[Stage][WARN] Failed to stage Mesa-UWP runtime: " + DescribeException(ex));
                    }
                }

                var mesaUwpDllPath = Path.Combine(
                    localFolder.Path,
                    (_config.MesaUwpOpenGlDllPackagePath ?? "native/mesa-uwp/opengl32.dll").Replace('/', Path.DirectorySeparatorChar));
                if (File.Exists(mesaUwpDllPath))
                {
                    var mesaCompatShimPath = string.Empty;
                    if (_config.UseXboxOpenGlStub && !string.IsNullOrWhiteSpace(_config.XboxOpenGlDllPackagePath))
                    {
                        var fileName = Path.GetFileName(_config.XboxOpenGlDllPackagePath);
                        var destination = Path.Combine(nativeDir, fileName);
                        try
                        {
                            var copied = await CopyFromPackageAsync(
                                packageFolder,
                                _config.XboxOpenGlDllPackagePath,
                                destination);
                            if (copied)
                            {
                                mesaCompatShimPath = destination;
                                result.Lines.Add("[Stage][PASS] Mesa-UWP LWJGL compatibility shim staged: " + destination);
                            }
                            else
                            {
                                result.Lines.Add("[Stage][WARN] Mesa-UWP compatibility shim not bundled; LWJGL will load Mesa opengl32.dll directly.");
                            }
                        }
                        catch (Exception ex)
                        {
                            result.Lines.Add("[Stage][WARN] Failed to stage Mesa-UWP compatibility shim: " + DescribeException(ex));
                        }
                    }

                    result.OpenGlDllPath = string.IsNullOrWhiteSpace(mesaCompatShimPath)
                        ? mesaUwpDllPath
                        : mesaCompatShimPath;
                    result.OpenGlProvider = "mesa-uwp";
                    result.RealOpenGlDllPath = mesaUwpDllPath;
                    result.XboxOpenGlDllPath = result.OpenGlDllPath;
                    selectedOpenGlProvider = true;
                    result.Lines.Add("[Stage][PASS] Mesa-UWP real OpenGL provider selected: " + mesaUwpDllPath);
                    if (!string.Equals(result.OpenGlDllPath, mesaUwpDllPath, StringComparison.OrdinalIgnoreCase))
                    {
                        result.Lines.Add("[Stage][PASS] LWJGL OpenGL load path uses compatibility shim: " + result.OpenGlDllPath);
                    }
                    else
                    {
                        result.Lines.Add("[Stage][PASS] LWJGL OpenGL load path uses Mesa opengl32.dll directly");
                    }
                }
                else if (requireMesaUwp)
                {
                    result.Lines.Add("[Stage][WARN] Mesa-UWP requested but opengl32.dll was not found at: " + mesaUwpDllPath);
                }
            }

            if (_config.UseCustomOpenGlProvider && !selectedOpenGlProvider && !requireMesaUwp && allowGlon12)
            {
                if (!string.IsNullOrWhiteSpace(_config.Glon12RuntimePackagePath))
                {
                    var destination = Path.Combine(
                        localFolder.Path,
                        _config.Glon12RuntimePackagePath.Replace('/', Path.DirectorySeparatorChar));
                    try
                    {
                        var copied = await CopyDirectoryFromPackageAsync(
                            packageFolder,
                            _config.Glon12RuntimePackagePath,
                            destination);
                        if (copied)
                        {
                            result.Lines.Add("[Stage][PASS] GLon12 runtime staged: " + destination);
                        }
                    }
                    catch (Exception ex)
                    {
                        result.Lines.Add("[Stage][WARN] Failed to stage GLon12 runtime: " + DescribeException(ex));
                    }
                }

                var glon12DllPath = Path.Combine(
                    localFolder.Path,
                    (_config.Glon12OpenGlDllPackagePath ?? "native/glon12/opengl32.dll").Replace('/', Path.DirectorySeparatorChar));
                if (File.Exists(glon12DllPath))
                {
                    result.OpenGlDllPath = glon12DllPath;
                    result.OpenGlProvider = "glon12";
                    result.XboxOpenGlDllPath = glon12DllPath;
                    selectedOpenGlProvider = true;
                    result.Lines.Add("[Stage][PASS] GLon12 OpenGL provider selected: " + glon12DllPath);
                }
                else if (requireGlon12)
                {
                    result.Lines.Add("[Stage][WARN] GLon12 requested but opengl32.dll was not found at: " + glon12DllPath);
                }
            }

            if (_config.UseCustomOpenGlProvider && !selectedOpenGlProvider && !requireMesaUwp && !requireGlon12 && _config.UseXboxOpenGlStub && !string.IsNullOrWhiteSpace(_config.XboxOpenGlDllPackagePath))
            {
                var fileName = Path.GetFileName(_config.XboxOpenGlDllPackagePath);
                var destination = Path.Combine(nativeDir, fileName);
                try
                {
                    var copied = await CopyFromPackageAsync(
                        packageFolder,
                        _config.XboxOpenGlDllPackagePath,
                        destination);
                    if (copied)
                    {
                        result.XboxOpenGlDllPath = destination;
                        result.OpenGlDllPath = destination;
                        result.OpenGlProvider = "xbox-opengl";
                        result.Lines.Add("[Stage][PASS] Xbox OpenGL stub staged (LWJGL will load this instead of system opengl32.dll): " + destination);
                    }
                    else
                    {
                        result.Lines.Add("[Stage][WARN] xbox-opengl.dll not bundled in package. LWJGL will fail to locate opengl32.dll on Xbox.");
                    }
                }
                catch (Exception ex)
                {
                    result.Lines.Add("[Stage][WARN] Failed to stage xbox-opengl.dll: " + DescribeException(ex));
                }
            }

            if (_config.UseXboxOpenAlStub && !string.IsNullOrWhiteSpace(_config.XboxOpenAlDllPackagePath))
            {
                var fileName = Path.GetFileName(_config.XboxOpenAlDllPackagePath);
                var destination = Path.Combine(nativeDir, fileName);
                try
                {
                    var copied = await CopyFromPackageAsync(
                        packageFolder,
                        _config.XboxOpenAlDllPackagePath,
                        destination);
                    if (copied)
                    {
                        result.XboxOpenAlDllPath = destination;
                        result.Lines.Add("[Stage][PASS] Xbox OpenAL stub staged (LWJGL will load this instead of bundled OpenAL.dll which imports dsound.dll): " + destination);
                    }
                    else
                    {
                        result.Lines.Add("[Stage][WARN] xbox-openal.dll not bundled in package. LWJGL.ALC.create() will fail loading OpenAL.dll on Xbox (error 127).");
                    }
                }
                catch (Exception ex)
                {
                    result.Lines.Add("[Stage][WARN] Failed to stage xbox-openal.dll: " + DescribeException(ex));
                }
            }

            return result;
        }

        private static async Task<bool> CopyDirectoryFromPackageAsync(
            StorageFolder packageRoot,
            string relativeAppPath,
            string destinationFullPath)
        {
            var normalized = relativeAppPath.Replace('\\', '/').Trim('/');
            var sourcePath = Path.Combine(
                packageRoot.Path,
                normalized.Replace('/', Path.DirectorySeparatorChar));
            if (!Directory.Exists(sourcePath))
            {
                return false;
            }

            await Task.Run(() => CopyDirectory(sourcePath, destinationFullPath));
            return true;
        }

        private static async Task<bool> CopyFromPackageAsync(
            StorageFolder packageRoot,
            string relativeAppPath,
            string destinationFullPath)
        {
            var normalized = relativeAppPath.Replace('\\', '/').Trim('/');
            var sourcePath = Path.Combine(
                packageRoot.Path,
                normalized.Replace('/', Path.DirectorySeparatorChar));
            if (!File.Exists(sourcePath))
            {
                return false;
            }

            await Task.Run(() => CopyFileReplacing(sourcePath, destinationFullPath));
            return true;
        }

        private static void CopyFileReplacing(string sourceFullPath, string destinationFullPath)
        {
            var destinationDir = Path.GetDirectoryName(destinationFullPath);
            if (!string.IsNullOrWhiteSpace(destinationDir))
            {
                Directory.CreateDirectory(destinationDir);
            }

            if (File.Exists(destinationFullPath))
            {
                File.SetAttributes(destinationFullPath, System.IO.FileAttributes.Normal);
            }
            File.Copy(sourceFullPath, destinationFullPath, true);
        }

        private static void CopyDirectory(string sourceRoot, string destinationRoot)
        {
            Directory.CreateDirectory(destinationRoot);

            foreach (var directory in Directory.EnumerateDirectories(sourceRoot, "*", SearchOption.AllDirectories))
            {
                var relative = GetRelativePath(sourceRoot, directory);
                Directory.CreateDirectory(Path.Combine(destinationRoot, relative));
            }

            foreach (var file in Directory.EnumerateFiles(sourceRoot, "*", SearchOption.AllDirectories))
            {
                var relative = GetRelativePath(sourceRoot, file);
                var destination = Path.Combine(destinationRoot, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(destination) ?? destinationRoot);
                File.Copy(file, destination, true);
            }
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

        private static string DescribeException(Exception ex)
        {
            var message = ex.Message;
            if (string.IsNullOrWhiteSpace(message))
            {
                message = "HRESULT 0x" + ex.HResult.ToString("X8");
            }

            return ex.GetType().Name + ": " + message;
        }
    }

    public sealed class RuntimeAssetStageResult
    {
        public List<string> Lines { get; } = new List<string>();
        public string Log4jConfigPath { get; set; }
        public string PathProbeJarPath { get; set; }
        public string JdkPatchJarPath { get; set; }
        public string XboxGlfwDllPath { get; set; }
        public string OpenGlDllPath { get; set; }
        public string RealOpenGlDllPath { get; set; }
        public string OpenGlProvider { get; set; }
        public string XboxOpenGlDllPath { get; set; }
        public string XboxOpenAlDllPath { get; set; }
    }
}
