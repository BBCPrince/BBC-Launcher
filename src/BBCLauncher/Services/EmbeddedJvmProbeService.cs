using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using BBCLauncher.Configuration;
using Windows.ApplicationModel.Core;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.UI.Core;
using Windows.UI.Xaml;

namespace BBCLauncher.Services
{
    public sealed class EmbeddedJvmProbeService
    {
        private const int JniVersion18 = 0x00010008;
        private const uint LoadWithAlteredSearchPath = 0x00000008;
        private static readonly object NativeLibraryGate = new object();
        private static readonly List<IntPtr> PinnedNativeLibraries = new List<IntPtr>();

        private readonly LauncherConfig _config;

        public EmbeddedJvmProbeService(LauncherConfig config)
        {
            _config = config;
        }

        public EmbeddedJvmProbeResult Probe(string storageRoot, string stagedJdkPatchJarPath)
        {
            var result = new EmbeddedJvmProbeResult();
            try
            {
                var javaExe = Path.Combine(storageRoot, _config.JavaExecutableRelativePath);
                var javaBin = Path.GetDirectoryName(javaExe);
                if (string.IsNullOrWhiteSpace(javaBin))
                {
                    return result.Fail("[EmbeddedJVM][FAIL] Java executable path is invalid: " + javaExe);
                }

                var jreRoot = Path.GetFullPath(Path.Combine(javaBin, ".."));
                var serverDir = Path.Combine(javaBin, "server");
                var jvmPath = Path.Combine(serverDir, "jvm.dll");
                if (!File.Exists(jvmPath))
                {
                    return result.Fail("[EmbeddedJVM][FAIL] jvm.dll missing: " + jvmPath);
                }

                var nativeDir = Path.Combine(storageRoot, _config.NativeLibrariesDirectoryName);
                var gameDir = Path.Combine(storageRoot, _config.GameDirectoryName);
                var tempDir = Path.Combine(gameDir, _config.TempDirectoryName, "embedded-jvm");
                var classpathEntries = BuildClasspathEntries(storageRoot);
                var classpath = string.Join(";", classpathEntries);
                Directory.CreateDirectory(gameDir);
                Directory.CreateDirectory(tempDir);

                result.Lines.Add("[EmbeddedJVM] jvm.dll -> " + jvmPath);
                result.Lines.Add("[EmbeddedJVM] java.home -> " + jreRoot);
                result.Lines.Add("[EmbeddedJVM] native path -> " + nativeDir);
                result.Lines.Add("[EmbeddedJVM] classpath entries -> " + classpathEntries.Count);

                var oldPath = Environment.GetEnvironmentVariable("PATH") ?? string.Empty;
                var oldJavaHome = Environment.GetEnvironmentVariable("JAVA_HOME");
                try
                {
                    Environment.SetEnvironmentVariable("JAVA_HOME", jreRoot);
                    Environment.SetEnvironmentVariable(
                        "PATH",
                        serverDir + ";" + javaBin + ";" + nativeDir + ";" + oldPath);

                    TrySetDllDirectory(javaBin, result);
                    PreloadJvmDependencies(javaBin, result);

                    var jvmLibrary = LoadLibraryEx(jvmPath, IntPtr.Zero, LoadWithAlteredSearchPath);
                    if (jvmLibrary == IntPtr.Zero)
                    {
                        return result.Fail(
                            "[EmbeddedJVM][FAIL] LoadLibraryEx(jvm.dll) failed: " +
                            FormatLastWin32Error());
                    }
                    result.Lines.Add("[EmbeddedJVM][PASS] Loaded jvm.dll in launcher process");

                    var createExport = NativeLibrary.GetExport(jvmLibrary, "JNI_CreateJavaVM");
                    var create = Marshal.GetDelegateForFunctionPointer<JniCreateJavaVmDelegate>(createExport);
                    var createResult = CreateAndDestroyProbeJvm(create, jreRoot, nativeDir, tempDir, classpath, stagedJdkPatchJarPath, result);
                    return createResult == 0
                        ? result.Pass("[EmbeddedJVM][PASS] JNI_CreateJavaVM succeeded in launcher process")
                        : result.Fail("[EmbeddedJVM][FAIL] JNI_CreateJavaVM returned " + createResult + " (" + DescribeJniError(createResult) + ")");
                }
                finally
                {
                    TryClearDllDirectory(result);
                    Environment.SetEnvironmentVariable("PATH", oldPath);
                    Environment.SetEnvironmentVariable("JAVA_HOME", oldJavaHome);
                }
            }
            catch (Exception ex)
            {
                return result.Fail("[EmbeddedJVM][FAIL] " + ex.GetType().Name + ": " + ex.Message);
            }
        }

        public async Task<EmbeddedJvmLaunchResult> StartMinecraftAsync(
            string storageRoot,
            JavaLaunchPlan launchPlan,
            LaunchLog log,
            CancellationToken cancellationToken)
        {
            var result = new EmbeddedJvmLaunchResult();
            try
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (launchPlan == null)
                {
                    return result.Fail("[EmbeddedJVM][FAIL] Embedded launch missing Java launch plan");
                }

                var javaExe = Path.Combine(storageRoot, _config.JavaExecutableRelativePath);
                var javaBin = Path.GetDirectoryName(javaExe);
                if (string.IsNullOrWhiteSpace(javaBin))
                {
                    return result.Fail("[EmbeddedJVM][FAIL] Java executable path is invalid: " + javaExe);
                }

                var jreRoot = Path.GetFullPath(Path.Combine(javaBin, ".."));
                var serverDir = Path.Combine(javaBin, "server");
                var jvmPath = Path.Combine(serverDir, "jvm.dll");
                if (!File.Exists(jvmPath))
                {
                    return result.Fail("[EmbeddedJVM][FAIL] jvm.dll missing: " + jvmPath);
                }

                var nativeDir = Path.Combine(storageRoot, _config.NativeLibrariesDirectoryName);
                var command = BuildEmbeddedJavaCommand(launchPlan, jreRoot, result);
                if (command == null)
                {
                    return result;
                }
                foreach (var line in result.DrainUnwrittenLines())
                {
                    await log.WriteLineAsync(line);
                }

                if (!string.IsNullOrWhiteSpace(log?.LogPath))
                {
                    launchPlan.StartInfo.EnvironmentVariables["MINECRAFT_XBOX_NATIVE_LOG"] = log.LogPath;
                    Record(result, log, "[EmbeddedJVM] Native GLFW diagnostics redirected into launch log");
                }

                ApplyEmbeddedEnvironment(launchPlan, javaBin, serverDir, nativeDir, jreRoot, result, log);
                TrySetCurrentDirectory(launchPlan.StartInfo.WorkingDirectory, result, log);
                TrySetDllDirectory(javaBin, result);
                foreach (var line in result.DrainUnwrittenLines())
                {
                    await log.WriteLineAsync(line);
                }

                PreloadJvmDependencies(javaBin, result);
                foreach (var line in result.DrainUnwrittenLines())
                {
                    await log.WriteLineAsync(line);
                }

                PreloadOpenGlProviderDependencies(launchPlan, result, log);
                TryHandoffCoreWindowToGlfw(launchPlan, result, log);
                var warmupResult = await TryWarmupMesaEglOnCoreWindowThreadAsync(launchPlan, result, log);
                if (_config.NativeCoreWindowHostMode
                    && _config.NativeCoreWindowHostFatalSurfaceFailure
                    && warmupResult != 2)
                {
                    var failure = "[EmbeddedJVM][FAIL] CoreWindow EGL warmup did not create a real window surface; stopping before Minecraft JVM launch.";
                    Record(result, log, failure);
                    return result.Fail(failure);
                }

                var startup = new TaskCompletionSource<EmbeddedJvmLaunchResult>(
                    TaskCreationOptions.RunContinuationsAsynchronously);
                _ = Task.Run(() =>
                {
                    RunEmbeddedMinecraftThread(
                        jvmPath,
                        command,
                        result,
                        log,
                        startup);
                });

                var watchSeconds = Math.Max(3, Math.Min(10, _config.EarlyExitWatchSeconds <= 0 ? 5 : _config.EarlyExitWatchSeconds));
                var completed = await Task.WhenAny(startup.Task, Task.Delay(TimeSpan.FromSeconds(watchSeconds), cancellationToken));
                if (completed == startup.Task)
                {
                    return await startup.Task;
                }

                var runningMessage = "[EmbeddedJVM][PASS] Embedded Minecraft JVM thread is still running after startup watch";
                Record(result, log, runningMessage);
                return result.MarkRunning(runningMessage);
            }
            catch (OperationCanceledException)
            {
                return result.Fail("[EmbeddedJVM][FAIL] Embedded launch cancelled");
            }
            catch (Exception ex)
            {
                return result.Fail("[EmbeddedJVM][FAIL] " + ex.GetType().Name + ": " + ex.Message);
            }
        }

        private static List<string> BuildClasspathEntries(string storageRoot)
        {
            var entries = new List<string>();
            var clientJar = Path.Combine(storageRoot, "client.jar");
            if (File.Exists(clientJar))
            {
                entries.Add(clientJar);
            }

            var libRoot = Path.Combine(storageRoot, "libraries");
            if (Directory.Exists(libRoot))
            {
                foreach (var jar in Directory.EnumerateFiles(libRoot, "*.jar", SearchOption.AllDirectories))
                {
                    if (Path.GetFileName(jar).IndexOf("-natives-", StringComparison.OrdinalIgnoreCase) >= 0)
                    {
                        continue;
                    }

                    entries.Add(jar);
                }
            }

            return entries;
        }

        private static EmbeddedJavaCommand BuildEmbeddedJavaCommand(
            JavaLaunchPlan launchPlan,
            string jreRoot,
            EmbeddedJvmLaunchResult result)
        {
            var tokens = SplitCommandLine(launchPlan.StartInfo.Arguments ?? string.Empty);
            var mainIndex = tokens.FindIndex(t => string.Equals(t, "net.minecraft.client.main.Main", StringComparison.Ordinal));
            if (mainIndex < 0)
            {
                result.Fail("[EmbeddedJVM][FAIL] Could not find Minecraft main class in launch arguments");
                return null;
            }

            var options = new List<string>();
            for (var i = 0; i < mainIndex; i++)
            {
                var token = tokens[i];
                if (string.Equals(token, "-cp", StringComparison.Ordinal)
                    || string.Equals(token, "-classpath", StringComparison.Ordinal)
                    || string.Equals(token, "--class-path", StringComparison.Ordinal))
                {
                    if (i + 1 >= mainIndex)
                    {
                        result.Fail("[EmbeddedJVM][FAIL] Classpath option missing value");
                        return null;
                    }

                    options.Add("-Djava.class.path=" + tokens[++i]);
                    continue;
                }

                if (string.Equals(token, "--patch-module", StringComparison.Ordinal)
                    || string.Equals(token, "--add-opens", StringComparison.Ordinal))
                {
                    if (i + 1 >= mainIndex)
                    {
                        result.Fail("[EmbeddedJVM][FAIL] JVM option missing value: " + token);
                        return null;
                    }

                    options.Add(token + "=" + tokens[++i]);
                    continue;
                }

                options.Add(token);
            }

            if (!options.Exists(o => o.StartsWith("-Djava.home=", StringComparison.Ordinal)))
            {
                options.Insert(0, "-Djava.home=" + jreRoot);
            }

            if (!options.Exists(o => string.Equals(o, "-Xrs", StringComparison.Ordinal)))
            {
                options.Insert(0, "-Xrs");
            }

            var appArgs = new List<string>();
            for (var i = mainIndex + 1; i < tokens.Count; i++)
            {
                appArgs.Add(tokens[i]);
            }

            result.Lines.Add("[EmbeddedJVM] JVM options -> " + options.Count);
            result.Lines.Add("[EmbeddedJVM] Minecraft main args -> " + appArgs.Count);
            return new EmbeddedJavaCommand(options, appArgs);
        }

        private static List<string> SplitCommandLine(string commandLine)
        {
            var tokens = new List<string>();
            var current = new StringBuilder();
            var inQuotes = false;

            for (var i = 0; i < commandLine.Length; i++)
            {
                var c = commandLine[i];
                if (c == '\\' && i + 1 < commandLine.Length && commandLine[i + 1] == '"')
                {
                    current.Append('"');
                    i++;
                    continue;
                }

                if (c == '"')
                {
                    inQuotes = !inQuotes;
                    continue;
                }

                if (char.IsWhiteSpace(c) && !inQuotes)
                {
                    if (current.Length > 0)
                    {
                        tokens.Add(current.ToString());
                        current.Clear();
                    }
                    continue;
                }

                current.Append(c);
            }

            if (current.Length > 0)
            {
                tokens.Add(current.ToString());
            }

            return tokens;
        }

        private static void ApplyEmbeddedEnvironment(
            JavaLaunchPlan launchPlan,
            string javaBin,
            string serverDir,
            string nativeDir,
            string jreRoot,
            EmbeddedJvmLaunchResult result,
            LaunchLog log)
        {
            foreach (string key in launchPlan.StartInfo.EnvironmentVariables.Keys)
            {
                Environment.SetEnvironmentVariable(key, launchPlan.StartInfo.EnvironmentVariables[key]);
            }

            Environment.SetEnvironmentVariable("JAVA_HOME", jreRoot);

            var planPath = launchPlan.StartInfo.EnvironmentVariables["PATH"];
            if (string.IsNullOrWhiteSpace(planPath))
            {
                planPath = Environment.GetEnvironmentVariable("PATH") ?? string.Empty;
            }

            Environment.SetEnvironmentVariable(
                "PATH",
                serverDir + ";" + javaBin + ";" + nativeDir + ";" + planPath);

            if (Environment.GetEnvironmentVariable("MINECRAFT_XBOX_GLFW_MESA_EGL_CONTEXT") == "1")
            {
                Environment.SetEnvironmentVariable("EGL_PLATFORM", "windows");
            }

            Record(result, log, "[EmbeddedJVM] Applied embedded process environment");
        }

        private static void PreloadOpenGlProviderDependencies(
            JavaLaunchPlan launchPlan,
            EmbeddedJvmLaunchResult result,
            LaunchLog log)
        {
            var openGlPath = launchPlan.StartInfo.EnvironmentVariables["MINECRAFT_XBOX_OPENGL_DLL"];
            if (string.IsNullOrWhiteSpace(openGlPath))
            {
                openGlPath = FindOptionValue(launchPlan.StartInfo.Arguments, "-Dorg.lwjgl.opengl.libname=");
            }

            if (string.IsNullOrWhiteSpace(openGlPath) || !File.Exists(openGlPath))
            {
                Record(result, log, "[EmbeddedJVM][WARN] OpenGL provider preload skipped; provider path missing");
                return;
            }

            var providerDirectory = Path.GetDirectoryName(openGlPath);
            if (string.IsNullOrWhiteSpace(providerDirectory))
            {
                Record(result, log, "[EmbeddedJVM][WARN] OpenGL provider preload skipped; provider directory missing");
                return;
            }

            var graphicsRuntimeMode = launchPlan.StartInfo.EnvironmentVariables["MC_GRAPHICS_RUNTIME"];
            var dependencyOrder = BuildOpenGlProviderDependencyOrder(
                graphicsRuntimeMode,
                Path.GetFileName(openGlPath));

            Record(
                result,
                log,
                "[EmbeddedJVM] Preloading OpenGL provider DLLs from " + providerDirectory +
                " runtime=" + (string.IsNullOrWhiteSpace(graphicsRuntimeMode) ? "<default>" : graphicsRuntimeMode));
            foreach (var dependencyName in dependencyOrder)
            {
                if (string.IsNullOrWhiteSpace(dependencyName))
                {
                    continue;
                }

                var dependencyPath = Path.Combine(providerDirectory, dependencyName);
                if (!File.Exists(dependencyPath))
                {
                    continue;
                }

                var handle = LoadLibraryEx(dependencyPath, IntPtr.Zero, LoadWithAlteredSearchPath);
                if (handle == IntPtr.Zero)
                {
                    Record(result, log, "[EmbeddedJVM][WARN] OpenGL preload failed for " + dependencyName + ": " + FormatLastWin32Error());
                }
                else
                {
                    Record(result, log, "[EmbeddedJVM][PASS] Preloaded OpenGL provider DLL " + dependencyName);
                }
            }
        }

        private static string[] BuildOpenGlProviderDependencyOrder(
            string graphicsRuntimeMode,
            string openGlFileName)
        {
            var common = new List<string>
            {
                "vcruntime140_app.dll",
                "vcruntime140_1_app.dll",
                "msvcp140_app.dll",
                "vccorlib140_app.dll",
                "vcruntime140.dll",
                "vcruntime140_1.dll",
                "msvcp140.dll",
            };

            if (string.Equals(graphicsRuntimeMode, "xboxone", StringComparison.OrdinalIgnoreCase))
            {
                common.Add("libEGL.dll");
                common.Add("libGLESv2.dll");
                common.Add(openGlFileName);
                return common.ToArray();
            }

            common.Add("libglapi.dll");
            common.Add("xbox_fmalloc.dll");
            common.Add("z-1.dll");
            common.Add("dxil.dll");
            common.Add("glu32.dll");
            common.Add("libgallium_wgl.dll");
            common.Add("libEGL.dll");
            common.Add("libGLESv2.dll");
            common.Add("libGLESv1_CM.dll");
            common.Add(openGlFileName);
            return common.ToArray();
        }

        private static void TrySetCurrentDirectory(
            string workingDirectory,
            EmbeddedJvmLaunchResult result,
            LaunchLog log)
        {
            if (string.IsNullOrWhiteSpace(workingDirectory))
            {
                return;
            }

            try
            {
                Directory.CreateDirectory(workingDirectory);
                Directory.SetCurrentDirectory(workingDirectory);
                Record(result, log, "[EmbeddedJVM] Current directory -> " + workingDirectory);
            }
            catch (Exception ex)
            {
                Record(result, log, "[EmbeddedJVM][WARN] Failed to set current directory: " + ex.GetType().Name + ": " + ex.Message);
            }
        }

        private async Task<int> TryWarmupMesaEglOnCoreWindowThreadAsync(
            JavaLaunchPlan launchPlan,
            EmbeddedJvmLaunchResult result,
            LaunchLog log)
        {
            if (Environment.GetEnvironmentVariable("MINECRAFT_XBOX_GLFW_MESA_EGL_CONTEXT") != "1")
            {
                return 2;
            }

            var dispatcher = Window.Current?.Dispatcher;
            if (dispatcher == null)
            {
                Record(result, log, "[EmbeddedJVM][WARN] Mesa EGL CoreWindow-thread warmup skipped; no CoreWindow dispatcher. " + FormatThreadInfo());
                return 0;
            }

            if (dispatcher.HasThreadAccess)
            {
                return TryWarmupMesaEglOnCurrentThread(launchPlan, result, log);
            }

            Exception warmupException = null;
            var warmupResult = 0;
            await dispatcher.RunAsync(CoreDispatcherPriority.High, () =>
            {
                try
                {
                    warmupResult = TryWarmupMesaEglOnCurrentThread(launchPlan, result, log);
                }
                catch (Exception ex)
                {
                    warmupException = ex;
                }
            });

            if (warmupException != null)
            {
                Record(result, log, "[EmbeddedJVM][WARN] Mesa EGL CoreWindow-thread warmup failed: " +
                    warmupException.GetType().Name + ": " + warmupException.Message);
                return 0;
            }

            return warmupResult;
        }

        private static int TryWarmupMesaEglOnCurrentThread(
            JavaLaunchPlan launchPlan,
            EmbeddedJvmLaunchResult result,
            LaunchLog log)
        {
            try
            {
                var glfwPath = FindOptionValue(launchPlan.StartInfo.Arguments, "-Dorg.lwjgl.glfw.libname=");
                if (string.IsNullOrWhiteSpace(glfwPath) || !File.Exists(glfwPath))
                {
                    Record(result, log, "[EmbeddedJVM][WARN] Mesa EGL CoreWindow-thread warmup skipped; xbox-glfw path missing");
                    return 0;
                }

                var coreWindow = Window.Current?.CoreWindow;
                if (coreWindow == null)
                {
                    Record(result, log, "[EmbeddedJVM][WARN] Mesa EGL CoreWindow-thread warmup skipped; Window.Current.CoreWindow is null. " + FormatThreadInfo());
                    return 0;
                }

                Record(result, log, "[EmbeddedJVM] Mesa EGL CoreWindow-thread warmup starting. " + FormatThreadInfo());
                var library = NativeLibrary.Load(glfwPath);
                lock (NativeLibraryGate)
                {
                    PinnedNativeLibraries.Add(library);
                }

                var export = NativeLibrary.GetExport(library, "minecraft_xbox_glfw_prepare_mesa_egl_on_core_window_thread");
                var prepare = Marshal.GetDelegateForFunctionPointer<PrepareMesaEglDelegate>(export);
                var prepared = prepare(1);
                if (prepared == 2)
                {
                    Record(result, log, "[EmbeddedJVM][PASS] Mesa EGL warmup created a real CoreWindow surface on the CoreWindow thread; context released for JVM thread. " + FormatThreadInfo());
                }
                else if (prepared == 1)
                {
                    Record(result, log, "[EmbeddedJVM][WARN] Mesa EGL warmup ran on the CoreWindow thread but still fell back before a real window surface. " + FormatThreadInfo());
                }
                else
                {
                    Record(result, log, "[EmbeddedJVM][WARN] Mesa EGL warmup did not create a window surface/context on CoreWindow thread. " + FormatThreadInfo());
                }

                return prepared;
            }
            catch (Exception ex)
            {
                Record(result, log, "[EmbeddedJVM][WARN] Mesa EGL CoreWindow-thread warmup failed: " + ex.GetType().Name + ": " + ex.Message);
                return 0;
            }
        }

        private static void TryHandoffCoreWindowToGlfw(
            JavaLaunchPlan launchPlan,
            EmbeddedJvmLaunchResult result,
            LaunchLog log)
        {
            IntPtr coreWindowUnknown = IntPtr.Zero;
            IntPtr eglDescriptorUnknown = IntPtr.Zero;
            try
            {
                var glfwPath = FindOptionValue(launchPlan.StartInfo.Arguments, "-Dorg.lwjgl.glfw.libname=");
                if (string.IsNullOrWhiteSpace(glfwPath) || !File.Exists(glfwPath))
                {
                    Record(result, log, "[EmbeddedJVM][WARN] CoreWindow handoff skipped; xbox-glfw path missing");
                    return;
                }

                var coreWindow = Window.Current?.CoreWindow;
                if (coreWindow == null)
                {
                    Record(result, log, "[EmbeddedJVM][WARN] CoreWindow handoff skipped; Window.Current.CoreWindow is null");
                    return;
                }

                PublishCoreWindowForEgl(coreWindow, launchPlan, result, log);

                coreWindowUnknown = Marshal.GetIUnknownForObject(coreWindow);
                var library = NativeLibrary.Load(glfwPath);
                lock (NativeLibraryGate)
                {
                    PinnedNativeLibraries.Add(library);
                }

                var export = NativeLibrary.GetExport(library, "minecraft_xbox_glfw_set_core_window");
                var setCoreWindow = Marshal.GetDelegateForFunctionPointer<SetCoreWindowDelegate>(export);
                setCoreWindow(coreWindowUnknown);
                Record(result, log, "[EmbeddedJVM][PASS] CoreWindow handed to xbox-glfw.dll. " + FormatThreadInfo());

                var descriptor = BuildEglWindowDescriptor(coreWindow, launchPlan);
                eglDescriptorUnknown = Marshal.GetIUnknownForObject(descriptor);
                var descriptorExport = NativeLibrary.GetExport(library, "minecraft_xbox_glfw_set_egl_window_descriptor");
                var setDescriptor = Marshal.GetDelegateForFunctionPointer<SetCoreWindowDelegate>(descriptorExport);
                setDescriptor(eglDescriptorUnknown);
                Record(
                    result,
                    log,
                    "[EmbeddedJVM][PASS] EGL CoreWindow PropertySet descriptor handed to xbox-glfw.dll " +
                    descriptor["EGLRenderSurfaceSizeProperty"] + ". " + FormatThreadInfo());
            }
            catch (Exception ex)
            {
                Record(result, log, "[EmbeddedJVM][WARN] CoreWindow handoff failed: " + ex.GetType().Name + ": " + ex.Message);
            }
            finally
            {
                if (coreWindowUnknown != IntPtr.Zero)
                {
                    Marshal.Release(coreWindowUnknown);
                }
                if (eglDescriptorUnknown != IntPtr.Zero)
                {
                    Marshal.Release(eglDescriptorUnknown);
                }
            }
        }

        private static PropertySet BuildEglWindowDescriptor(CoreWindow coreWindow, JavaLaunchPlan launchPlan)
        {
            var bounds = coreWindow.Bounds;
            var width = FindPositiveIntOption(
                launchPlan.StartInfo.Arguments,
                "--width",
                "-Dminecraft.window.width=");
            var height = FindPositiveIntOption(
                launchPlan.StartInfo.Arguments,
                "--height",
                "-Dminecraft.window.height=");

            if (width <= 1)
            {
                width = (int)Math.Max(1, bounds.Width);
            }

            if (height <= 1)
            {
                height = (int)Math.Max(1, bounds.Height);
            }

            if (width <= 1)
            {
                width = 1920;
            }
            if (height <= 1)
            {
                height = 1080;
            }

            var descriptor = new PropertySet();
            descriptor["EGLNativeWindowTypeProperty"] = coreWindow;
            descriptor["EGLRenderSurfaceSizeProperty"] = PropertyValue.CreateSize(new Size(width, height));
            return descriptor;
        }

        private static void PublishCoreWindowForEgl(
            CoreWindow coreWindow,
            JavaLaunchPlan launchPlan,
            EmbeddedJvmLaunchResult result,
            LaunchLog log)
        {
            try
            {
                var descriptor = BuildEglWindowDescriptor(coreWindow, launchPlan);
                CoreApplication.Properties["EGLNativeWindowTypeProperty"] = coreWindow;
                CoreApplication.Properties["EGLRenderSurfaceSizeProperty"] = descriptor["EGLRenderSurfaceSizeProperty"];
                Record(
                    result,
                    log,
                    "[EmbeddedJVM][PASS] CoreWindow published for EGL via CoreApplication.Properties " +
                    descriptor["EGLRenderSurfaceSizeProperty"] + ". " + FormatThreadInfo());
            }
            catch (Exception ex)
            {
                Record(result, log, "[EmbeddedJVM][WARN] CoreWindow CoreApplication.Properties publish failed: " +
                    ex.GetType().Name + ": " + ex.Message);
            }
        }

        private static string FindOptionValue(string arguments, string prefix)
        {
            foreach (var token in SplitCommandLine(arguments ?? string.Empty))
            {
                if (token.StartsWith(prefix, StringComparison.Ordinal))
                {
                    return token.Substring(prefix.Length);
                }
            }

            return null;
        }

        private static int FindPositiveIntOption(string arguments, string optionName, string propertyPrefix)
        {
            var tokens = SplitCommandLine(arguments ?? string.Empty);
            for (var i = 0; i < tokens.Count; i++)
            {
                var token = tokens[i];
                if (string.Equals(token, optionName, StringComparison.Ordinal) &&
                    i + 1 < tokens.Count &&
                    int.TryParse(tokens[i + 1], out var optionValue) &&
                    optionValue > 0)
                {
                    return optionValue;
                }

                if (token.StartsWith(propertyPrefix, StringComparison.Ordinal) &&
                    int.TryParse(token.Substring(propertyPrefix.Length), out var propertyValue) &&
                    propertyValue > 0)
                {
                    return propertyValue;
                }
            }

            return 0;
        }

        private static void RunEmbeddedMinecraftThread(
            string jvmPath,
            EmbeddedJavaCommand command,
            EmbeddedJvmLaunchResult result,
            LaunchLog log,
            TaskCompletionSource<EmbeddedJvmLaunchResult> startup)
        {
            try
            {
                Record(result, log, "[EmbeddedJVM] JVM worker thread starting. " + FormatThreadInfo());
                Record(result, log, "[EmbeddedJVM] Loading JVM for embedded Minecraft: " + jvmPath);
                var jvmLibrary = LoadLibraryEx(jvmPath, IntPtr.Zero, LoadWithAlteredSearchPath);
                if (jvmLibrary == IntPtr.Zero)
                {
                    startup.TrySetResult(result.Fail(
                        "[EmbeddedJVM][FAIL] LoadLibraryEx(jvm.dll) failed: " +
                        FormatLastWin32Error()));
                    return;
                }

                Record(result, log, "[EmbeddedJVM][PASS] Loaded jvm.dll for embedded Minecraft");
                var createExport = NativeLibrary.GetExport(jvmLibrary, "JNI_CreateJavaVM");
                var create = Marshal.GetDelegateForFunctionPointer<JniCreateJavaVmDelegate>(createExport);
                var runResult = CreateAndRunMinecraftJvm(create, command, result, log, startup);
                if (runResult != 0)
                {
                    startup.TrySetResult(result.Fail("[EmbeddedJVM][FAIL] Embedded JVM returned " + runResult + " (" + DescribeJniError(runResult) + ")"));
                }
            }
            catch (Exception ex)
            {
                startup.TrySetResult(result.Fail("[EmbeddedJVM][FAIL] Embedded thread exception: " + ex.GetType().Name + ": " + ex.Message));
            }
        }

        private static int CreateAndRunMinecraftJvm(
            JniCreateJavaVmDelegate create,
            EmbeddedJavaCommand command,
            EmbeddedJvmLaunchResult result,
            LaunchLog log,
            TaskCompletionSource<EmbeddedJvmLaunchResult> startup)
        {
            var optionStrings = new List<IntPtr>();
            var optionsPtr = IntPtr.Zero;
            var argsPtr = IntPtr.Zero;
            IntPtr javaVm = IntPtr.Zero;
            IntPtr jniEnv = IntPtr.Zero;

            try
            {
                var optionSize = Marshal.SizeOf<JavaVmOption>();
                optionsPtr = Marshal.AllocHGlobal(optionSize * command.JvmOptions.Count);
                for (var i = 0; i < command.JvmOptions.Count; i++)
                {
                    var optionStringPtr = Marshal.StringToHGlobalAnsi(command.JvmOptions[i]);
                    optionStrings.Add(optionStringPtr);
                    Marshal.StructureToPtr(
                        new JavaVmOption
                        {
                            OptionString = optionStringPtr,
                            ExtraInfo = IntPtr.Zero,
                        },
                        IntPtr.Add(optionsPtr, optionSize * i),
                        false);
                }

                var args = new JavaVmInitArgs
                {
                    Version = JniVersion18,
                    OptionCount = command.JvmOptions.Count,
                    Options = optionsPtr,
                    IgnoreUnrecognized = 1,
                };
                argsPtr = Marshal.AllocHGlobal(Marshal.SizeOf<JavaVmInitArgs>());
                Marshal.StructureToPtr(args, argsPtr, false);

                Record(result, log, "[EmbeddedJVM] Creating embedded Minecraft JVM with " + command.JvmOptions.Count + " options");
                var createResult = create(out javaVm, out jniEnv, argsPtr);
                Record(result, log, "[EmbeddedJVM] JNI_CreateJavaVM result=" + createResult + " vm=0x" + javaVm.ToInt64().ToString("X") + " env=0x" + jniEnv.ToInt64().ToString("X"));
                if (createResult != 0 || javaVm == IntPtr.Zero || jniEnv == IntPtr.Zero)
                {
                    return createResult;
                }

                var jni = JniApi.FromEnv(jniEnv);
                var prepared = PrepareMainCall(jniEnv, jni, command.MinecraftArgs, result, log);
                if (prepared == null)
                {
                    return -1;
                }

                var runningMessage = "[EmbeddedJVM][PASS] Invoking net.minecraft.client.main.Main.main inside launcher process";
                Record(result, log, runningMessage);
                startup.TrySetResult(result.MarkRunning(runningMessage));
                jni.CallStaticVoidMethodA(jniEnv, prepared.MainClass, prepared.MainMethod, prepared.ArgumentsPtr);
                if (ClearPendingException(jniEnv, jni, result, log, "[EmbeddedJVM][FAIL] Minecraft Main.main threw a Java exception"))
                {
                    return -1;
                }

                Record(result, log, "[EmbeddedJVM][WARN] Minecraft Main.main returned; game loop is no longer running");
                var destroyResult = DestroyJavaVm(javaVm);
                Record(result, log, "[EmbeddedJVM] DestroyJavaVM result=" + destroyResult + " (" + DescribeJniError(destroyResult) + ")");
                return destroyResult;
            }
            finally
            {
                foreach (var ptr in optionStrings)
                {
                    Marshal.FreeHGlobal(ptr);
                }

                if (optionsPtr != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(optionsPtr);
                }

                if (argsPtr != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(argsPtr);
                }
            }
        }

        private static PreparedMainCall PrepareMainCall(
            IntPtr jniEnv,
            JniApi jni,
            IReadOnlyList<string> minecraftArgs,
            EmbeddedJvmLaunchResult result,
            LaunchLog log)
        {
            var mainClass = jni.FindClass(jniEnv, "net/minecraft/client/main/Main");
            if (mainClass == IntPtr.Zero)
            {
                ClearPendingException(jniEnv, jni, result, log, "[EmbeddedJVM][FAIL] FindClass(net/minecraft/client/main/Main) failed");
                return null;
            }

            var mainMethod = jni.GetStaticMethodId(jniEnv, mainClass, "main", "([Ljava/lang/String;)V");
            if (mainMethod == IntPtr.Zero)
            {
                ClearPendingException(jniEnv, jni, result, log, "[EmbeddedJVM][FAIL] GetStaticMethodID(Main.main) failed");
                jni.DeleteLocalRef(jniEnv, mainClass);
                return null;
            }

            var stringClass = jni.FindClass(jniEnv, "java/lang/String");
            if (stringClass == IntPtr.Zero)
            {
                ClearPendingException(jniEnv, jni, result, log, "[EmbeddedJVM][FAIL] FindClass(java/lang/String) failed");
                jni.DeleteLocalRef(jniEnv, mainClass);
                return null;
            }

            var argArray = jni.NewObjectArray(jniEnv, minecraftArgs.Count, stringClass, IntPtr.Zero);
            if (argArray == IntPtr.Zero)
            {
                ClearPendingException(jniEnv, jni, result, log, "[EmbeddedJVM][FAIL] NewObjectArray(String[]) failed");
                jni.DeleteLocalRef(jniEnv, stringClass);
                jni.DeleteLocalRef(jniEnv, mainClass);
                return null;
            }

            for (var i = 0; i < minecraftArgs.Count; i++)
            {
                var valuePtr = StringToHGlobalUtf8(minecraftArgs[i] ?? string.Empty);
                var javaString = IntPtr.Zero;
                try
                {
                    javaString = jni.NewStringUtf(jniEnv, valuePtr);
                }
                finally
                {
                    Marshal.FreeHGlobal(valuePtr);
                }

                if (javaString == IntPtr.Zero)
                {
                    ClearPendingException(jniEnv, jni, result, log, "[EmbeddedJVM][FAIL] NewStringUTF failed for arg index " + i);
                    jni.DeleteLocalRef(jniEnv, stringClass);
                    jni.DeleteLocalRef(jniEnv, argArray);
                    jni.DeleteLocalRef(jniEnv, mainClass);
                    return null;
                }

                jni.SetObjectArrayElement(jniEnv, argArray, i, javaString);
                jni.DeleteLocalRef(jniEnv, javaString);
                if (ClearPendingException(jniEnv, jni, result, log, "[EmbeddedJVM][FAIL] SetObjectArrayElement failed for arg index " + i))
                {
                    jni.DeleteLocalRef(jniEnv, stringClass);
                    jni.DeleteLocalRef(jniEnv, argArray);
                    jni.DeleteLocalRef(jniEnv, mainClass);
                    return null;
                }
            }

            jni.DeleteLocalRef(jniEnv, stringClass);

            var jniArgsPtr = Marshal.AllocHGlobal(Marshal.SizeOf<JniValue>());
            Marshal.StructureToPtr(new JniValue { Object = argArray }, jniArgsPtr, false);
            Record(result, log, "[EmbeddedJVM][PASS] Prepared Minecraft Main.main String[] args");

            return new PreparedMainCall
            {
                MainClass = mainClass,
                MainMethod = mainMethod,
                ArgumentArray = argArray,
                ArgumentsPtr = jniArgsPtr,
            };
        }

        private static IntPtr StringToHGlobalUtf8(string value)
        {
            var bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
            var ptr = Marshal.AllocHGlobal(bytes.Length + 1);
            Marshal.Copy(bytes, 0, ptr, bytes.Length);
            Marshal.WriteByte(ptr, bytes.Length, 0);
            return ptr;
        }

        private static bool ClearPendingException(
            IntPtr jniEnv,
            JniApi jni,
            EmbeddedJvmLaunchResult result,
            LaunchLog log,
            string message)
        {
            var exception = jni.ExceptionOccurred(jniEnv);
            if (exception == IntPtr.Zero)
            {
                return false;
            }

            Record(result, log, message);
            try
            {
                jni.ExceptionDescribe(jniEnv);
            }
            catch
            {
            }
            jni.ExceptionClear(jniEnv);
            jni.DeleteLocalRef(jniEnv, exception);
            return true;
        }

        private static void Record(EmbeddedJvmLaunchResult result, LaunchLog log, string line)
        {
            result.AddLoggedLine(line);
            if (log != null)
            {
                log.WriteLineAsync(line).GetAwaiter().GetResult();
            }
        }

        private static string FormatThreadInfo()
        {
            try
            {
                return "managedThread=" + Environment.CurrentManagedThreadId +
                    " nativeThread=0x" + GetCurrentThreadId().ToString("X");
            }
            catch
            {
                return "managedThread=" + Environment.CurrentManagedThreadId;
            }
        }

        private static void PreloadJvmDependencies(string javaBin, EmbeddedJvmProbeResult result) =>
            PreloadJvmDependencies(javaBin, line => result.Lines.Add(line));

        private static void PreloadJvmDependencies(string javaBin, EmbeddedJvmLaunchResult result) =>
            PreloadJvmDependencies(javaBin, line => result.Lines.Add(line));

        private static void PreloadJvmDependencies(string javaBin, Action<string> addLine)
        {
            var dependencyNames = new[]
            {
                "VCRUNTIME140.dll",
                "VCRUNTIME140_1.dll",
                "api-ms-win-crt-stdio-l1-1-0.dll",
                "api-ms-win-crt-string-l1-1-0.dll",
                "api-ms-win-crt-math-l1-1-0.dll",
                "api-ms-win-crt-utility-l1-1-0.dll",
                "api-ms-win-crt-runtime-l1-1-0.dll",
                "api-ms-win-crt-convert-l1-1-0.dll",
                "api-ms-win-crt-environment-l1-1-0.dll",
                "api-ms-win-crt-filesystem-l1-1-0.dll",
                "api-ms-win-crt-time-l1-1-0.dll",
                "api-ms-win-crt-heap-l1-1-0.dll",
                "ucrtbase.dll",
            };

            foreach (var dependencyName in dependencyNames)
            {
                var dependencyPath = Path.Combine(javaBin, dependencyName);
                if (!File.Exists(dependencyPath))
                {
                    addLine("[EmbeddedJVM][WARN] Dependency not staged: " + dependencyPath);
                    continue;
                }

                var handle = LoadLibraryEx(dependencyPath, IntPtr.Zero, LoadWithAlteredSearchPath);
                if (handle == IntPtr.Zero)
                {
                    addLine(
                        "[EmbeddedJVM][WARN] Preload failed for " +
                        dependencyName +
                        ": " +
                        FormatLastWin32Error());
                }
                else
                {
                    addLine("[EmbeddedJVM][PASS] Preloaded " + dependencyName);
                }
            }
        }

        private static void TrySetDllDirectory(string javaBin, EmbeddedJvmProbeResult result) =>
            TrySetDllDirectory(javaBin, line => result.Lines.Add(line));

        private static void TrySetDllDirectory(string javaBin, EmbeddedJvmLaunchResult result) =>
            TrySetDllDirectory(javaBin, line => result.Lines.Add(line));

        private static void TrySetDllDirectory(string javaBin, Action<string> addLine)
        {
            try
            {
                if (SetDllDirectory(javaBin))
                {
                    addLine("[EmbeddedJVM][PASS] DLL search directory set to java bin");
                }
                else
                {
                    addLine("[EmbeddedJVM][WARN] SetDllDirectory(java bin) failed: " + FormatLastWin32Error());
                }
            }
            catch (Exception ex)
            {
                addLine("[EmbeddedJVM][WARN] SetDllDirectory unavailable: " + ex.GetType().Name + ": " + ex.Message);
            }
        }

        private static void TryClearDllDirectory(EmbeddedJvmProbeResult result)
        {
            try
            {
                SetDllDirectory(null);
            }
            catch (Exception ex)
            {
                result.Lines.Add("[EmbeddedJVM][WARN] SetDllDirectory reset unavailable: " + ex.GetType().Name + ": " + ex.Message);
            }
        }

        private static string FormatLastWin32Error()
        {
            var error = Marshal.GetLastWin32Error();
            return "GetLastError=" + error + " (0x" + error.ToString("X8") + ")";
        }

        private static int CreateAndDestroyProbeJvm(
            JniCreateJavaVmDelegate create,
            string jreRoot,
            string nativeDir,
            string tempDir,
            string classpath,
            string stagedJdkPatchJarPath,
            EmbeddedJvmProbeResult result)
        {
            var options = new List<string>
            {
                "-Xrs",
                "-Djava.home=" + jreRoot,
                "-Djava.io.tmpdir=" + tempDir,
                "-Duser.home=" + tempDir,
                "-Duser.dir=" + tempDir,
                "-Djava.library.path=" + nativeDir,
                "-Dorg.lwjgl.system.SharedLibraryExtractPath=" + tempDir,
                "-Djava.class.path=" + classpath,
            };

            if (!string.IsNullOrWhiteSpace(stagedJdkPatchJarPath) && File.Exists(stagedJdkPatchJarPath))
            {
                options.Add("--patch-module=java.base=" + stagedJdkPatchJarPath);
                options.Add("--add-opens=java.base/sun.nio.fs=ALL-UNNAMED");
                options.Add("--add-opens=java.base/java.nio.file=ALL-UNNAMED");
                options.Add("--add-opens=java.base/java.lang=ALL-UNNAMED");
            }

            var optionStrings = new List<IntPtr>();
            var optionsPtr = IntPtr.Zero;
            var argsPtr = IntPtr.Zero;
            IntPtr javaVm = IntPtr.Zero;
            IntPtr jniEnv = IntPtr.Zero;

            try
            {
                var optionSize = Marshal.SizeOf<JavaVmOption>();
                optionsPtr = Marshal.AllocHGlobal(optionSize * options.Count);
                for (var i = 0; i < options.Count; i++)
                {
                    var optionStringPtr = Marshal.StringToHGlobalAnsi(options[i]);
                    optionStrings.Add(optionStringPtr);
                    Marshal.StructureToPtr(
                        new JavaVmOption
                        {
                            OptionString = optionStringPtr,
                            ExtraInfo = IntPtr.Zero,
                        },
                        IntPtr.Add(optionsPtr, optionSize * i),
                        false);
                }

                var args = new JavaVmInitArgs
                {
                    Version = JniVersion18,
                    OptionCount = options.Count,
                    Options = optionsPtr,
                    IgnoreUnrecognized = 1,
                };
                argsPtr = Marshal.AllocHGlobal(Marshal.SizeOf<JavaVmInitArgs>());
                Marshal.StructureToPtr(args, argsPtr, false);

                result.Lines.Add("[EmbeddedJVM] Creating probe JVM with " + options.Count + " options");
                var createResult = create(out javaVm, out jniEnv, argsPtr);
                result.Lines.Add("[EmbeddedJVM] JNI_CreateJavaVM result=" + createResult + " vm=0x" + javaVm.ToInt64().ToString("X") + " env=0x" + jniEnv.ToInt64().ToString("X"));
                if (createResult == 0 && javaVm != IntPtr.Zero)
                {
                    VerifyClassVisible(jniEnv, "java/lang/System", result);
                    VerifyClassVisible(jniEnv, "net/minecraft/client/main/Main", result);
                    VerifyStaticMethodVisible(
                        jniEnv,
                        "net/minecraft/client/main/Main",
                        "main",
                        "([Ljava/lang/String;)V",
                        result);
                    var destroyResult = DestroyJavaVm(javaVm);
                    result.Lines.Add("[EmbeddedJVM] DestroyJavaVM result=" + destroyResult + " (" + DescribeJniError(destroyResult) + ")");
                }

                return createResult;
            }
            finally
            {
                foreach (var ptr in optionStrings)
                {
                    Marshal.FreeHGlobal(ptr);
                }

                if (optionsPtr != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(optionsPtr);
                }

                if (argsPtr != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(argsPtr);
                }
            }
        }

        private static void VerifyClassVisible(IntPtr jniEnv, string className, EmbeddedJvmProbeResult result)
        {
            var functionTable = Marshal.ReadIntPtr(jniEnv);
            var findClassPtr = Marshal.ReadIntPtr(functionTable, IntPtr.Size * 6);
            var exceptionOccurredPtr = Marshal.ReadIntPtr(functionTable, IntPtr.Size * 15);
            var exceptionClearPtr = Marshal.ReadIntPtr(functionTable, IntPtr.Size * 17);
            var deleteLocalRefPtr = Marshal.ReadIntPtr(functionTable, IntPtr.Size * 23);

            var findClass = Marshal.GetDelegateForFunctionPointer<FindClassDelegate>(findClassPtr);
            var exceptionOccurred = Marshal.GetDelegateForFunctionPointer<ExceptionOccurredDelegate>(exceptionOccurredPtr);
            var exceptionClear = Marshal.GetDelegateForFunctionPointer<ExceptionClearDelegate>(exceptionClearPtr);
            var deleteLocalRef = Marshal.GetDelegateForFunctionPointer<DeleteLocalRefDelegate>(deleteLocalRefPtr);

            var clazz = findClass(jniEnv, className);
            if (clazz == IntPtr.Zero)
            {
                var exception = exceptionOccurred(jniEnv);
                if (exception != IntPtr.Zero)
                {
                    exceptionClear(jniEnv);
                    deleteLocalRef(jniEnv, exception);
                }

                result.Lines.Add("[EmbeddedJVM][FAIL] FindClass(" + className + ") failed");
                return;
            }

            result.Lines.Add("[EmbeddedJVM][PASS] FindClass(" + className + ")");
            deleteLocalRef(jniEnv, clazz);
        }

        private static void VerifyStaticMethodVisible(
            IntPtr jniEnv,
            string className,
            string methodName,
            string signature,
            EmbeddedJvmProbeResult result)
        {
            var functionTable = Marshal.ReadIntPtr(jniEnv);
            var findClassPtr = Marshal.ReadIntPtr(functionTable, IntPtr.Size * 6);
            var exceptionOccurredPtr = Marshal.ReadIntPtr(functionTable, IntPtr.Size * 15);
            var exceptionClearPtr = Marshal.ReadIntPtr(functionTable, IntPtr.Size * 17);
            var deleteLocalRefPtr = Marshal.ReadIntPtr(functionTable, IntPtr.Size * 23);
            var getStaticMethodIdPtr = Marshal.ReadIntPtr(functionTable, IntPtr.Size * 113);

            var findClass = Marshal.GetDelegateForFunctionPointer<FindClassDelegate>(findClassPtr);
            var exceptionOccurred = Marshal.GetDelegateForFunctionPointer<ExceptionOccurredDelegate>(exceptionOccurredPtr);
            var exceptionClear = Marshal.GetDelegateForFunctionPointer<ExceptionClearDelegate>(exceptionClearPtr);
            var deleteLocalRef = Marshal.GetDelegateForFunctionPointer<DeleteLocalRefDelegate>(deleteLocalRefPtr);
            var getStaticMethodId = Marshal.GetDelegateForFunctionPointer<GetStaticMethodIdDelegate>(getStaticMethodIdPtr);

            var clazz = findClass(jniEnv, className);
            if (clazz == IntPtr.Zero)
            {
                ClearPendingException(jniEnv, exceptionOccurred, exceptionClear, deleteLocalRef);
                result.Lines.Add("[EmbeddedJVM][FAIL] GetStaticMethodID(" + className + "." + methodName + signature + ") class lookup failed");
                return;
            }

            var method = getStaticMethodId(jniEnv, clazz, methodName, signature);
            if (method == IntPtr.Zero)
            {
                ClearPendingException(jniEnv, exceptionOccurred, exceptionClear, deleteLocalRef);
                deleteLocalRef(jniEnv, clazz);
                result.Lines.Add("[EmbeddedJVM][FAIL] GetStaticMethodID(" + className + "." + methodName + signature + ") failed");
                return;
            }

            result.Lines.Add("[EmbeddedJVM][PASS] GetStaticMethodID(" + className + "." + methodName + signature + ")");
            deleteLocalRef(jniEnv, clazz);
        }

        private static void ClearPendingException(
            IntPtr jniEnv,
            ExceptionOccurredDelegate exceptionOccurred,
            ExceptionClearDelegate exceptionClear,
            DeleteLocalRefDelegate deleteLocalRef)
        {
            var exception = exceptionOccurred(jniEnv);
            if (exception == IntPtr.Zero)
            {
                return;
            }

            exceptionClear(jniEnv);
            deleteLocalRef(jniEnv, exception);
        }

        private static int DestroyJavaVm(IntPtr javaVm)
        {
            var invokeTable = Marshal.ReadIntPtr(javaVm);
            var destroyPtr = Marshal.ReadIntPtr(invokeTable, IntPtr.Size * 3);
            var destroy = Marshal.GetDelegateForFunctionPointer<DestroyJavaVmDelegate>(destroyPtr);
            return destroy(javaVm);
        }

        private static string DescribeJniError(int code)
        {
            switch (code)
            {
                case 0:
                    return "JNI_OK";
                case -1:
                    return "JNI_ERR";
                case -2:
                    return "JNI_EDETACHED";
                case -3:
                    return "JNI_EVERSION";
                case -4:
                    return "JNI_ENOMEM";
                case -5:
                    return "JNI_EEXIST";
                case -6:
                    return "JNI_EINVAL";
                default:
                    return "JNI_UNKNOWN";
            }
        }

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int JniCreateJavaVmDelegate(out IntPtr javaVm, out IntPtr jniEnv, IntPtr args);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int DestroyJavaVmDelegate(IntPtr javaVm);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void SetCoreWindowDelegate(IntPtr coreWindowUnknown);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int PrepareMesaEglDelegate(int releaseCurrentAfterWarmup);

        [UnmanagedFunctionPointer(CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        private delegate IntPtr FindClassDelegate(IntPtr env, string name);

        [UnmanagedFunctionPointer(CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        private delegate IntPtr GetStaticMethodIdDelegate(IntPtr env, IntPtr clazz, string name, string signature);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void CallStaticVoidMethodADelegate(IntPtr env, IntPtr clazz, IntPtr method, IntPtr args);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate IntPtr NewStringUtfDelegate(IntPtr env, IntPtr utf);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate IntPtr NewObjectArrayDelegate(IntPtr env, int length, IntPtr elementClass, IntPtr initialElement);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void SetObjectArrayElementDelegate(IntPtr env, IntPtr array, int index, IntPtr value);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate IntPtr ExceptionOccurredDelegate(IntPtr env);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void ExceptionDescribeDelegate(IntPtr env);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void ExceptionClearDelegate(IntPtr env);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate void DeleteLocalRefDelegate(IntPtr env, IntPtr localRef);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern bool SetDllDirectory(string pathName);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr LoadLibraryEx(string fileName, IntPtr file, uint flags);

        [DllImport("kernel32.dll")]
        private static extern uint GetCurrentThreadId();

        [StructLayout(LayoutKind.Sequential)]
        private struct JavaVmOption
        {
            public IntPtr OptionString;
            public IntPtr ExtraInfo;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct JavaVmInitArgs
        {
            public int Version;
            public int OptionCount;
            public IntPtr Options;
            public int IgnoreUnrecognized;
        }

        [StructLayout(LayoutKind.Explicit)]
        private struct JniValue
        {
            [FieldOffset(0)]
            public IntPtr Object;
        }

        private sealed class EmbeddedJavaCommand
        {
            public EmbeddedJavaCommand(List<string> jvmOptions, List<string> minecraftArgs)
            {
                JvmOptions = jvmOptions;
                MinecraftArgs = minecraftArgs;
            }

            public List<string> JvmOptions { get; }
            public List<string> MinecraftArgs { get; }
        }

        private sealed class PreparedMainCall
        {
            public IntPtr MainClass { get; set; }
            public IntPtr MainMethod { get; set; }
            public IntPtr ArgumentArray { get; set; }
            public IntPtr ArgumentsPtr { get; set; }
        }

        private sealed class JniApi
        {
            public FindClassDelegate FindClass { get; private set; }
            public GetStaticMethodIdDelegate GetStaticMethodId { get; private set; }
            public CallStaticVoidMethodADelegate CallStaticVoidMethodA { get; private set; }
            public ExceptionOccurredDelegate ExceptionOccurred { get; private set; }
            public ExceptionDescribeDelegate ExceptionDescribe { get; private set; }
            public ExceptionClearDelegate ExceptionClear { get; private set; }
            public DeleteLocalRefDelegate DeleteLocalRef { get; private set; }
            public NewStringUtfDelegate NewStringUtf { get; private set; }
            public NewObjectArrayDelegate NewObjectArray { get; private set; }
            public SetObjectArrayElementDelegate SetObjectArrayElement { get; private set; }

            public static JniApi FromEnv(IntPtr env)
            {
                var table = Marshal.ReadIntPtr(env);
                return new JniApi
                {
                    FindClass = Get<FindClassDelegate>(table, 6),
                    ExceptionOccurred = Get<ExceptionOccurredDelegate>(table, 15),
                    ExceptionDescribe = Get<ExceptionDescribeDelegate>(table, 16),
                    ExceptionClear = Get<ExceptionClearDelegate>(table, 17),
                    DeleteLocalRef = Get<DeleteLocalRefDelegate>(table, 23),
                    GetStaticMethodId = Get<GetStaticMethodIdDelegate>(table, 113),
                    CallStaticVoidMethodA = Get<CallStaticVoidMethodADelegate>(table, 143),
                    NewStringUtf = Get<NewStringUtfDelegate>(table, 167),
                    NewObjectArray = Get<NewObjectArrayDelegate>(table, 172),
                    SetObjectArrayElement = Get<SetObjectArrayElementDelegate>(table, 174),
                };
            }

            private static TDelegate Get<TDelegate>(IntPtr table, int index) where TDelegate : Delegate
            {
                return Marshal.GetDelegateForFunctionPointer<TDelegate>(
                    Marshal.ReadIntPtr(table, IntPtr.Size * index));
            }
        }
    }

    public sealed class EmbeddedJvmProbeResult
    {
        public bool IsSuccess { get; private set; }
        public string Message { get; private set; }
        public List<string> Lines { get; } = new List<string>();

        public EmbeddedJvmProbeResult Pass(string message)
        {
            IsSuccess = true;
            Message = message;
            Lines.Add(message);
            return this;
        }

        public EmbeddedJvmProbeResult Fail(string message)
        {
            IsSuccess = false;
            Message = message;
            Lines.Add(message);
            return this;
        }
    }

    public sealed class EmbeddedJvmLaunchResult
    {
        private int _writtenCount;

        public bool IsSuccess { get; private set; }
        public bool IsRunning { get; private set; }
        public string Message { get; private set; }
        public List<string> Lines { get; } = new List<string>();

        public void AddLoggedLine(string line)
        {
            Lines.Add(line);
            _writtenCount = Lines.Count;
        }

        public EmbeddedJvmLaunchResult Running(string message)
        {
            IsSuccess = true;
            IsRunning = true;
            Message = message;
            Lines.Add(message);
            return this;
        }

        public EmbeddedJvmLaunchResult MarkRunning(string message)
        {
            IsSuccess = true;
            IsRunning = true;
            Message = message;
            return this;
        }

        public EmbeddedJvmLaunchResult Fail(string message)
        {
            IsSuccess = false;
            IsRunning = false;
            Message = message;
            Lines.Add(message);
            return this;
        }

        public List<string> DrainUnwrittenLines()
        {
            var lines = new List<string>();
            while (_writtenCount < Lines.Count)
            {
                lines.Add(Lines[_writtenCount]);
                _writtenCount++;
            }

            return lines;
        }
    }
}
