using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using BBCLauncher.Configuration;
using BBCLauncher.Models;

namespace BBCLauncher.Services
{
    public sealed class JavaRuntimeLauncher
    {
        private readonly LauncherConfig _config;

        public JavaRuntimeLauncher(LauncherConfig config)
        {
            _config = config;
        }

        public string Log4jConfigPath { get; set; }
        public string GameDirectoryOverride { get; set; }
        public string JdkPatchJarPath { get; set; }
        public string XboxGlfwDllPath { get; set; }
        public string OpenGlDllPath { get; set; }
        public string RealOpenGlDllPath { get; set; }
        public string OpenGlProvider { get; set; }
        public string XboxOpenGlDllPath { get; set; }
        public string XboxOpenAlDllPath { get; set; }

        public async Task<JavaLaunchPlan> BuildStartInfoAsync(
            string storageRoot,
            ResolutionOption resolution,
            MinecraftSession authSession,
            LaunchLog log = null)
        {
            await WriteBuildStepAsync(log, "Begin Java start info build");
            var javaPath = Path.Combine(storageRoot, _config.JavaExecutableRelativePath);
            if (!File.Exists(javaPath))
            {
                throw new FileNotFoundException("Java runtime not found. Bundle a Java 21 runtime under runtime/jre.", javaPath);
            }
            await WriteBuildStepAsync(log, "Java runtime found: " + javaPath);

            var defaultGameDir = Path.Combine(storageRoot, _config.GameDirectoryName);
            var gameDir = !string.IsNullOrWhiteSpace(GameDirectoryOverride)
                ? GameDirectoryOverride
                : defaultGameDir;
            var assetsDir = Path.Combine(storageRoot, _config.AssetsDirectoryName);
            var nativesDir = Path.Combine(storageRoot, _config.NativeLibrariesDirectoryName);
            var tempDir = Path.Combine(gameDir, _config.TempDirectoryName);
            var jnaTempDir = Path.Combine(tempDir, "jna");
            var lwjglTempDir = Path.Combine(tempDir, "lwjgl");
            var nettyTempDir = Path.Combine(tempDir, "netty");
            var logsDir = Path.Combine(gameDir, "logs");
            var downloadsDir = Path.Combine(gameDir, "downloads");
            var crashReportsDir = Path.Combine(gameDir, "crash-reports");
            var resourcePacksDir = Path.Combine(gameDir, "resourcepacks");
            var screenshotsDir = Path.Combine(gameDir, "screenshots");
            var savesDir = Path.Combine(gameDir, "saves");

            var writableDirectories = new[]
            {
                gameDir,
                logsDir,
                downloadsDir,
                crashReportsDir,
                resourcePacksDir,
                screenshotsDir,
                savesDir,
                tempDir,
                jnaTempDir,
                lwjglTempDir,
                nettyTempDir,
            };

            Directory.CreateDirectory(assetsDir);
            Directory.CreateDirectory(nativesDir);
            foreach (var directory in writableDirectories)
            {
                Directory.CreateDirectory(directory);
            }
            await WriteBuildStepAsync(log, "Writable directory layout created");

            string nativeLogPath = null;
            try
            {
                nativeLogPath = Path.Combine(logsDir, "native-glfw.log");
                File.WriteAllText(
                    nativeLogPath,
                    "[NativeSidecar] Native GLFW log started (UTC " + DateTime.UtcNow.ToString("O") + ")" + Environment.NewLine,
                    new UTF8Encoding(false));
                await WriteBuildStepAsync(log, "Native GLFW sidecar log prepared: " + nativeLogPath);
            }
            catch (Exception ex)
            {
                await WriteBuildStepAsync(log, "[WARN] Failed to prepare native GLFW sidecar log: " + ex.GetType().Name + ": " + ex.Message);
                nativeLogPath = null;
            }

            var classpathEntries = await BuildClasspathEntriesAsync(storageRoot);
            await WriteBuildStepAsync(log, "Classpath entries built: " + classpathEntries.Count);
            var classpath = string.Join(";", classpathEntries);
            var writableDiagnostics = ProbeWritableDirectories(writableDirectories);
            await WriteBuildStepAsync(log, "Writable directory probe complete");
            var lwjglOpenGlDllPath = !string.IsNullOrWhiteSpace(OpenGlDllPath)
                ? OpenGlDllPath
                : XboxOpenGlDllPath;
            var providerOpenGlDllPath = !string.IsNullOrWhiteSpace(RealOpenGlDllPath)
                ? RealOpenGlDllPath
                : lwjglOpenGlDllPath;
            await CleanMirroredOpenGlProviderBesideJavaAsync(javaPath, log);
            providerOpenGlDllPath = await MirrorOpenGlProviderBesideJavaAsync(
                javaPath,
                providerOpenGlDllPath,
                OpenGlProvider,
                log);
            if (!string.IsNullOrWhiteSpace(lwjglOpenGlDllPath) && File.Exists(lwjglOpenGlDllPath))
            {
                OpenGlDllPath = lwjglOpenGlDllPath;
            }
            if (!string.IsNullOrWhiteSpace(providerOpenGlDllPath) && File.Exists(providerOpenGlDllPath))
            {
                RealOpenGlDllPath = providerOpenGlDllPath;
            }
            if (_config.NativeCoreWindowHostMode
                && _config.NativeCoreWindowHostDirectMesaOpenGl
                && string.Equals(OpenGlProvider, "mesa-uwp", StringComparison.OrdinalIgnoreCase)
                && !string.IsNullOrWhiteSpace(providerOpenGlDllPath)
                && File.Exists(providerOpenGlDllPath))
            {
                lwjglOpenGlDllPath = providerOpenGlDllPath;
                OpenGlDllPath = providerOpenGlDllPath;
                await WriteBuildStepAsync(log, "Native CoreWindow host direct Mesa OpenGL selected for LWJGL: " + providerOpenGlDllPath);
            }
            var args = BuildArguments(
                gameDir,
                assetsDir,
                nativesDir,
                tempDir,
                jnaTempDir,
                lwjglTempDir,
                nettyTempDir,
                classpath,
                resolution,
                authSession);
            await WriteBuildStepAsync(log, "Java argument string built");
            var diagnostics = BuildDiagnostics(
                javaPath,
                gameDir,
                writableDiagnostics,
                classpathEntries,
                RedactSensitiveArgs(args),
                Log4jConfigPath,
                JdkPatchJarPath,
                XboxGlfwDllPath,
                lwjglOpenGlDllPath,
                providerOpenGlDllPath,
                OpenGlProvider,
                XboxOpenAlDllPath,
                nativeLogPath,
                ResolveGraphicsRuntimeMode(OpenGlProvider));
            await WriteBuildStepAsync(log, "Safe launch diagnostics built");

            var startInfo = new ProcessStartInfo
            {
                FileName = javaPath,
                Arguments = args,
                WorkingDirectory = gameDir,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8,
            };
            await WriteBuildStepAsync(log, "ProcessStartInfo created");
            startInfo.EnvironmentVariables["TEMP"] = tempDir;
            startInfo.EnvironmentVariables["TMP"] = tempDir;
            startInfo.EnvironmentVariables["USERPROFILE"] = gameDir;
            startInfo.EnvironmentVariables["APPDATA"] = gameDir;
            startInfo.EnvironmentVariables["LOCALAPPDATA"] = gameDir;
            startInfo.EnvironmentVariables["MINECRAFT_XBOX_LAUNCH_WIDTH"] = resolution.Width.ToString();
            startInfo.EnvironmentVariables["MINECRAFT_XBOX_LAUNCH_HEIGHT"] = resolution.Height.ToString();
            if (_config.EnableGameHostPresentation)
            {
                startInfo.EnvironmentVariables["MINECRAFT_XBOX_PRESENT_EVENT"] =
                    D3D12PresentationService.JavaPresentEventName;
            }
            if (!string.IsNullOrWhiteSpace(nativeLogPath))
            {
                startInfo.EnvironmentVariables["MINECRAFT_XBOX_NATIVE_LOG"] = nativeLogPath;
            }
            if (string.Equals(OpenGlProvider, "xbox-opengl", StringComparison.OrdinalIgnoreCase))
            {
                startInfo.EnvironmentVariables["MINECRAFT_XBOX_GLFW_FAKE_CONTEXT"] = "1";
            }
            else if (string.Equals(OpenGlProvider, "mesa-uwp", StringComparison.OrdinalIgnoreCase))
            {
                startInfo.EnvironmentVariables["MINECRAFT_XBOX_GLFW_MESA_EGL_CONTEXT"] = "1";
                if (_config.NativeCoreWindowHostMode && _config.NativeCoreWindowHostFatalSurfaceFailure)
                {
                    startInfo.EnvironmentVariables["MINECRAFT_XBOX_FATAL_ON_SURFACELESS"] = "1";
                }
            }
            ConfigureOpenGlProviderEnvironment(startInfo, providerOpenGlDllPath, OpenGlProvider);
            await WriteBuildStepAsync(log, "Process environment configured");

            return new JavaLaunchPlan
            {
                StartInfo = startInfo,
                JavaPath = javaPath,
                ClasspathEntries = classpathEntries,
                CommandPreview = "\"" + javaPath + "\" " + args,
                Diagnostics = diagnostics,
                NativeLogPath = nativeLogPath,
            };
        }

        private static async Task CleanMirroredOpenGlProviderBesideJavaAsync(
            string javaPath,
            LaunchLog log)
        {
            var javaDirectory = Path.GetDirectoryName(javaPath);
            if (string.IsNullOrWhiteSpace(javaDirectory) || !Directory.Exists(javaDirectory))
            {
                return;
            }

            var staleProviderDlls = new[]
            {
                "opengl32.dll",
                "libgallium_wgl.dll",
                "libglapi.dll",
                "libEGL.dll",
                "libGLESv1_CM.dll",
                "libGLESv2.dll",
                "xbox_fmalloc.dll",
                "glu32.dll",
                "dxil.dll",
                "z-1.dll",
                "d3d12.dll",
                "dxcore.dll",
                "dxcompiler.dll",
            };

            var removed = 0;
            foreach (var name in staleProviderDlls)
            {
                var path = Path.Combine(javaDirectory, name);
                try
                {
                    if (File.Exists(path))
                    {
                        File.Delete(path);
                        removed++;
                    }
                }
                catch (Exception ex)
                {
                    await WriteBuildStepAsync(log, "[WARN] Failed to remove stale mirrored OpenGL provider DLL: " + path + " (" + ex.GetType().Name + ": " + ex.Message + ")");
                }
            }

            if (removed > 0)
            {
                await WriteBuildStepAsync(log, "Removed stale mirrored OpenGL provider DLLs beside java.exe: " + removed);
            }
            else
            {
                await WriteBuildStepAsync(log, "No stale mirrored OpenGL provider DLLs found beside java.exe");
            }
        }

        private static async Task<string> MirrorOpenGlProviderBesideJavaAsync(
            string javaPath,
            string openGlDllPath,
            string openGlProvider,
            LaunchLog log)
        {
            if (string.IsNullOrWhiteSpace(javaPath)
                || string.IsNullOrWhiteSpace(openGlDllPath)
                || !File.Exists(openGlDllPath)
                || (!string.Equals(openGlProvider, "mesa-uwp", StringComparison.OrdinalIgnoreCase)
                    && !string.Equals(openGlProvider, "glon12", StringComparison.OrdinalIgnoreCase)))
            {
                return openGlDllPath;
            }

            var javaDirectory = Path.GetDirectoryName(javaPath);
            var providerDirectory = Path.GetDirectoryName(openGlDllPath);
            if (string.IsNullOrWhiteSpace(javaDirectory)
                || string.IsNullOrWhiteSpace(providerDirectory)
                || string.Equals(javaDirectory, providerDirectory, StringComparison.OrdinalIgnoreCase))
            {
                return openGlDllPath;
            }

            await WriteBuildStepAsync(log, "Mirroring OpenGL provider DLLs next to java.exe from " + providerDirectory);
            foreach (var dll in Directory.EnumerateFiles(providerDirectory, "*.dll", SearchOption.TopDirectoryOnly))
            {
                var destination = Path.Combine(javaDirectory, Path.GetFileName(dll));
                File.Copy(dll, destination, overwrite: true);
            }

            var mirroredOpenGlDll = Path.Combine(javaDirectory, Path.GetFileName(openGlDllPath));
            await WriteBuildStepAsync(log, "OpenGL provider load path mirrored to: " + mirroredOpenGlDll);
            return mirroredOpenGlDll;
        }

        private static Task WriteBuildStepAsync(LaunchLog log, string message)
        {
            return log == null
                ? Task.CompletedTask
                : log.WriteLineAsync("[LaunchPlan] " + message);
        }

        private async Task<List<string>> BuildClasspathEntriesAsync(string storageRoot)
        {
            var libRoot = Path.Combine(storageRoot, "libraries");
            var jars = new List<string>();
            var clientJar = Path.Combine(storageRoot, "client.jar");
            if (File.Exists(clientJar))
            {
                jars.Add(clientJar);
            }

            if (Directory.Exists(libRoot))
            {
                foreach (var file in Directory.EnumerateFiles(libRoot, "*.jar", SearchOption.AllDirectories))
                {
                    if (Path.GetFileName(file).IndexOf("-natives-", StringComparison.OrdinalIgnoreCase) >= 0)
                    {
                        continue;
                    }

                    jars.Add(file);
                }
            }

            await Task.CompletedTask;
            if (jars.Count == 0)
            {
                return new List<string> { clientJar };
            }

            return jars
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToList();
        }

        private static List<string> BuildDiagnostics(
            string javaPath,
            string workingDirectory,
            List<string> writableDiagnostics,
            List<string> classpathEntries,
            string args,
            string log4jConfigPath,
            string jdkPatchJarPath,
            string xboxGlfwDllPath,
            string openGlDllPath,
            string realOpenGlDllPath,
            string openGlProvider,
            string xboxOpenAlDllPath,
            string nativeLogPath,
            string graphicsRuntimeMode)
        {
            var diagnostics = new List<string>
            {
                "[SafeLaunch] Java executable: " + javaPath,
                "[SafeLaunch] Working directory (gameDir): " + workingDirectory,
                "[SafeLaunch] Classpath entries: " + classpathEntries.Count,
            };

            if (!string.IsNullOrWhiteSpace(log4jConfigPath) && File.Exists(log4jConfigPath))
            {
                diagnostics.Add("[SafeLaunch] log4j2.configurationFile -> " + log4jConfigPath);
            }
            else
            {
                diagnostics.Add("[SafeLaunch][WARN] Xbox log4j config not staged; Minecraft will use default config which writes logs/latest.log");
            }

            if (!string.IsNullOrWhiteSpace(jdkPatchJarPath) && File.Exists(jdkPatchJarPath))
            {
                diagnostics.Add("[SafeLaunch] JDK patch -> --patch-module java.base=" + jdkPatchJarPath + " (replaces sun.nio.fs.WindowsLinkSupport for AccessDenied-free toRealPath on Xbox)");
            }
            else
            {
                diagnostics.Add("[SafeLaunch][WARN] xbox-jdk-patch.jar not staged; Minecraft's toRealPath() calls will fail with AccessDeniedException on Xbox.");
            }

            if (!string.IsNullOrWhiteSpace(xboxGlfwDllPath) && File.Exists(xboxGlfwDllPath))
            {
                diagnostics.Add("[SafeLaunch] Xbox GLFW stub -> -Dorg.lwjgl.glfw.libname=" + xboxGlfwDllPath + " (bypasses RegisterClassExW in LWJGL's bundled glfw.dll)");
            }
            else
            {
                diagnostics.Add("[SafeLaunch][WARN] xbox-glfw.dll not staged; LWJGL will load its bundled glfw.dll and crash on Xbox in glfwInit.");
            }

            if (!string.IsNullOrWhiteSpace(openGlDllPath) && File.Exists(openGlDllPath))
            {
                var provider = string.IsNullOrWhiteSpace(openGlProvider)
                    ? "xbox-opengl"
                    : openGlProvider;
                if (provider.Equals("glon12", StringComparison.OrdinalIgnoreCase))
                {
                    diagnostics.Add("[SafeLaunch] GLon12 OpenGL provider -> -Dorg.lwjgl.opengl.libname=" + openGlDllPath + " (Mesa D3D12-backed OpenGL)");
                }
                else if (provider.Equals("mesa-uwp", StringComparison.OrdinalIgnoreCase))
                {
                    if (!string.IsNullOrWhiteSpace(realOpenGlDllPath)
                        && string.Equals(openGlDllPath, realOpenGlDllPath, StringComparison.OrdinalIgnoreCase))
                    {
                        diagnostics.Add("[SafeLaunch] Mesa-UWP direct OpenGL provider -> -Dorg.lwjgl.opengl.libname=" + openGlDllPath);
                    }
                    else
                    {
                        diagnostics.Add("[SafeLaunch] Mesa-UWP LWJGL OpenGL load path -> -Dorg.lwjgl.opengl.libname=" + openGlDllPath + " (WGL compatibility shim)");
                    }
                    if (!string.IsNullOrWhiteSpace(realOpenGlDllPath))
                    {
                        diagnostics.Add("[SafeLaunch] Mesa-UWP real OpenGL provider -> MINECRAFT_XBOX_OPENGL_DLL=" + realOpenGlDllPath);
                    }
                    if (!string.IsNullOrWhiteSpace(graphicsRuntimeMode))
                    {
                        diagnostics.Add("[SafeLaunch] Graphics runtime mode -> MC_GRAPHICS_RUNTIME=" + graphicsRuntimeMode);
                    }
                    diagnostics.Add("[SafeLaunch] GLFW Mesa EGL context path enabled; embedded mode hands CoreWindow to xbox-glfw and preloads the exact OpenGL provider path.");
                }
                else
                {
                    diagnostics.Add("[SafeLaunch] Xbox OpenGL stub -> -Dorg.lwjgl.opengl.libname=" + openGlDllPath + " (replaces missing system opengl32.dll with 368-export stub)");
                    diagnostics.Add("[SafeLaunch] GLFW fake context mode enabled for xbox-opengl provider; Mesa/WGL context creation is bypassed.");
                }
            }
            else
            {
                diagnostics.Add("[SafeLaunch][WARN] No custom OpenGL provider staged; LWJGL.GL.create() will fail with UnsatisfiedLinkError for opengl32.dll on Xbox.");
            }

            if (!string.IsNullOrWhiteSpace(xboxOpenAlDllPath) && File.Exists(xboxOpenAlDllPath))
            {
                diagnostics.Add("[SafeLaunch] Xbox OpenAL stub -> -Dorg.lwjgl.openal.libname=" + xboxOpenAlDllPath + " (replaces bundled OpenAL.dll which imports dsound.dll, missing on Xbox UWP)");
            }
            else
            {
                diagnostics.Add("[SafeLaunch][WARN] xbox-openal.dll not staged; LWJGL.ALC.create() will fail with UnsatisfiedLinkError (error 127) loading OpenAL.dll on Xbox.");
            }

            if (!string.IsNullOrWhiteSpace(nativeLogPath))
            {
                diagnostics.Add("[SafeLaunch] Native GLFW sidecar log -> " + nativeLogPath);
            }
            else
            {
                diagnostics.Add("[SafeLaunch][WARN] Native GLFW sidecar log could not be prepared.");
            }
            diagnostics.Add("[SafeLaunch] Native GLFW diagnostics are written to Java stderr and MINECRAFT_XBOX_NATIVE_LOG.");

            diagnostics.AddRange(writableDiagnostics);

            var missing = classpathEntries
                .Where(path => !File.Exists(path))
                .Take(20)
                .ToList();

            if (missing.Count == 0)
            {
                diagnostics.Add("[SafeLaunch] Classpath validation: PASS");
            }
            else
            {
                diagnostics.Add("[SafeLaunch] Classpath validation: FAIL (" + missing.Count + " missing entries)");
                diagnostics.AddRange(missing.Select(m => "[SafeLaunch] Missing CP entry: " + m));
            }

            diagnostics.Add("[SafeLaunch] Command preview: \"" + javaPath + "\" " + args);
            return diagnostics;
        }

        private static List<string> ProbeWritableDirectories(IEnumerable<string> directories)
        {
            var diagnostics = new List<string>();
            foreach (var directory in directories.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                try
                {
                    Directory.CreateDirectory(directory);
                    var probePath = Path.Combine(directory, ".minecraft-xbox-launcher-write-test");
                    File.WriteAllText(probePath, DateTime.UtcNow.ToString("O"));
                    File.Delete(probePath);
                    diagnostics.Add("[SafeLaunch][PASS] Writable directory: " + directory);
                }
                catch (Exception ex)
                {
                    diagnostics.Add("[SafeLaunch][WARN] Writable directory failed: " + directory + " (" + ex.GetType().Name + ": " + ex.Message + ")");
                }
            }

            return diagnostics;
        }

        private string BuildArguments(
            string gameDir,
            string assetsDir,
            string nativesDir,
            string tempDir,
            string jnaTempDir,
            string lwjglTempDir,
            string nettyTempDir,
            string classpath,
            ResolutionOption resolution,
            MinecraftSession authSession)
        {
            if (authSession == null)
            {
                throw new InvalidOperationException("Minecraft auth session is missing.");
            }

            var accessToken = authSession.MinecraftAccessToken;
            var username = string.IsNullOrWhiteSpace(authSession.Username)
                ? "XboxPlayer"
                : authSession.Username;
            var uuid = string.IsNullOrWhiteSpace(authSession.Uuid)
                ? "00000000000000000000000000000000"
                : NormalizeUuidForMinecraft(authSession.Uuid);
            var clientId = string.IsNullOrWhiteSpace(authSession.ClientId)
                ? (_config.MicrosoftClientId ?? "debug-client")
                : authSession.ClientId;
            var xuid = string.IsNullOrWhiteSpace(authSession.Xuid)
                ? "0"
                : authSession.Xuid;

            var builder = new StringBuilder();
            builder.Append($"-Xmx{_config.DefaultHeapMegabytes}M ");
            if (_config.UseJdkPatch && !string.IsNullOrWhiteSpace(JdkPatchJarPath) && File.Exists(JdkPatchJarPath))
            {
                builder.Append("--patch-module \"java.base=").Append(JdkPatchJarPath).Append("\" ");
            }
            builder.Append("--add-opens java.base/sun.nio.fs=ALL-UNNAMED ");
            builder.Append("--add-opens java.base/java.nio.file=ALL-UNNAMED ");
            builder.Append("--add-opens java.base/java.lang=ALL-UNNAMED ");
            builder.Append("-Djava.library.path=\"").Append(nativesDir).Append("\" ");
            builder.Append("-Djava.io.tmpdir=\"").Append(tempDir).Append("\" ");
            builder.Append("-Duser.home=\"").Append(gameDir).Append("\" ");
            builder.Append("-Duser.dir=\"").Append(gameDir).Append("\" ");
            builder.Append("-Djna.tmpdir=\"").Append(jnaTempDir).Append("\" ");
            builder.Append("-Djna.boot.library.path=\"").Append(nativesDir).Append("\" ");
            builder.Append("-Djna.library.path=\"").Append(nativesDir).Append("\" ");
            builder.Append("-Djna.nosys=true ");
            if (_config.UseXboxLog4jConfig && !string.IsNullOrWhiteSpace(Log4jConfigPath) && File.Exists(Log4jConfigPath))
            {
                builder.Append("-Dlog4j2.configurationFile=\"").Append(Log4jConfigPath).Append("\" ");
                builder.Append("-Dlog4j.configurationFile=\"").Append(Log4jConfigPath).Append("\" ");
            }
            builder.Append("-Dorg.lwjgl.system.SharedLibraryExtractPath=\"").Append(lwjglTempDir).Append("\" ");
            if (_config.UseXboxGlfwStub && !string.IsNullOrWhiteSpace(XboxGlfwDllPath) && File.Exists(XboxGlfwDllPath))
            {
                // LWJGL respects an absolute path here: when org.lwjgl.glfw.libname
                // is set, it skips extracting the bundled glfw.dll from its natives
                // jar and loads the path verbatim via LoadLibrary.
                builder.Append("-Dorg.lwjgl.glfw.libname=\"").Append(XboxGlfwDllPath).Append("\" ");
            }
            var selectedOpenGlDllPath = !string.IsNullOrWhiteSpace(OpenGlDllPath)
                ? OpenGlDllPath
                : XboxOpenGlDllPath;
            if (_config.UseCustomOpenGlProvider && !string.IsNullOrWhiteSpace(selectedOpenGlDllPath) && File.Exists(selectedOpenGlDllPath))
            {
                builder.Append("-Dorg.lwjgl.opengl.libname=\"").Append(selectedOpenGlDllPath).Append("\" ");
            }
            if (_config.UseXboxOpenAlStub && !string.IsNullOrWhiteSpace(XboxOpenAlDllPath) && File.Exists(XboxOpenAlDllPath))
            {
                // OpenAL Soft on Windows imports from dsound.dll which Xbox UWP
                // doesn't ship -> LoadLibrary fails with ERROR_PROC_NOT_FOUND.
                // Our stub has zero external deps and returns NULL from
                // alcOpenDevice so Mojang's SoundEngine cleanly disables audio.
                builder.Append("-Dorg.lwjgl.openal.libname=\"").Append(XboxOpenAlDllPath).Append("\" ");
            }
            builder.Append("-Dio.netty.native.workdir=\"").Append(nettyTempDir).Append("\" ");
            builder.Append("-Dminecraft.launcher.brand=xbox-uwp ");
            builder.Append("-Dminecraft.launcher.version=1.0 ");
            builder.Append($"-Dminecraft.graphics.backend={_config.GraphicsBackend} ");
            builder.Append($"-Dminecraft.audio.backend={_config.AudioBackend} ");
            builder.Append($"-Dminecraft.input.backend={_config.InputBackend} ");
            builder.Append($"-Dminecraft.window.width={resolution.Width} ");
            builder.Append($"-Dminecraft.window.height={resolution.Height} ");
            if (!string.IsNullOrWhiteSpace(accessToken))
            {
                builder.Append("-Dminecraft.session.token=redacted ");
            }

            builder.Append("-cp \"").Append(classpath).Append("\" ");
            builder.Append("net.minecraft.client.main.Main ");
            builder.Append("--version ").Append(_config.TargetMinecraftVersion).Append(' ');
            builder.Append("--gameDir \"").Append(gameDir).Append("\" ");
            builder.Append("--assetsDir \"").Append(assetsDir).Append("\" ");
            builder.Append("--assetIndex ").Append(_config.TargetMinecraftVersion).Append(' ');
            builder.Append("--username ").Append(QuoteArgument(username)).Append(' ');
            builder.Append("--uuid ").Append(uuid).Append(' ');
            builder.Append("--accessToken ").Append(QuoteArgument(string.IsNullOrWhiteSpace(accessToken) ? "0" : accessToken)).Append(' ');
            builder.Append("--clientId ").Append(QuoteArgument(clientId)).Append(' ');
            builder.Append("--xuid ").Append(QuoteArgument(xuid)).Append(' ');
            builder.Append("--userType msa ");
            builder.Append("--versionType release ");
            builder.Append("--width ").Append(resolution.Width).Append(' ');
            builder.Append("--height ").Append(resolution.Height).Append(' ');
            builder.Append("--fullscreen ");

            if (_config.SoftwareRenderingFallback)
            {
                builder.Append("--softwareRenderer ");
            }

            return builder.ToString();
        }

        private void ConfigureOpenGlProviderEnvironment(
            ProcessStartInfo startInfo,
            string openGlDllPath,
            string openGlProvider)
        {
            if (string.IsNullOrWhiteSpace(openGlDllPath)
                || !File.Exists(openGlDllPath)
                || (!string.Equals(openGlProvider, "glon12", StringComparison.OrdinalIgnoreCase)
                    && !string.Equals(openGlProvider, "mesa-uwp", StringComparison.OrdinalIgnoreCase)))
            {
                return;
            }

            startInfo.EnvironmentVariables["MINECRAFT_XBOX_OPENGL_DLL"] = openGlDllPath;
            startInfo.EnvironmentVariables["MINECRAFT_XBOX_REAL_OPENGL_DLL"] = openGlDllPath;
            var graphicsRuntimeMode = ResolveGraphicsRuntimeMode(openGlProvider);
            if (!string.IsNullOrWhiteSpace(graphicsRuntimeMode))
            {
                startInfo.EnvironmentVariables["MC_GRAPHICS_RUNTIME"] = graphicsRuntimeMode;
            }

            var providerDirectory = Path.GetDirectoryName(openGlDllPath);
            if (!string.IsNullOrWhiteSpace(providerDirectory))
            {
                PrefixProcessPath(startInfo, providerDirectory);
            }

            if (string.Equals(openGlProvider, "mesa-uwp", StringComparison.OrdinalIgnoreCase))
            {
                startInfo.EnvironmentVariables["GALLIUM_DRIVER"] = "d3d12";
                startInfo.EnvironmentVariables["MESA_LOADER_DRIVER_OVERRIDE"] = "d3d12";
                startInfo.EnvironmentVariables["MINECRAFT_XBOX_OPENGL_FORWARD_MESA"] = "0";
                if (_config.NativeCoreWindowHostMode)
                {
                    startInfo.EnvironmentVariables["MINECRAFT_XBOX_COREWINDOW_HOST"] = "1";
                    startInfo.EnvironmentVariables["MINECRAFT_XBOX_GLFW_PREFER_NATIVE_COREWINDOW"] = "1";
                    startInfo.EnvironmentVariables["MINECRAFT_XBOX_GLFW_PREFER_NATIVE_DESCRIPTOR"] = "1";
                }
                if (!string.IsNullOrWhiteSpace(providerDirectory))
                {
                    startInfo.EnvironmentVariables["MESA_FMALLOC_CACHE_FILE"] =
                        BuildMesaFmallocCachePath(providerDirectory, "mesa-fmalloc-java.swap");
                    startInfo.EnvironmentVariables["MESA_FMALLOC_CACHE_MB"] = "512";
                }

                if (!string.IsNullOrWhiteSpace(_config.Glon12D3D12AdapterName))
                {
                    startInfo.EnvironmentVariables["MESA_D3D12_DEFAULT_ADAPTER_NAME"] =
                        _config.Glon12D3D12AdapterName;
                }
                return;
            }

            if (_config.EnableGlon12D3D12Environment)
            {
                startInfo.EnvironmentVariables["GALLIUM_DRIVER"] = "d3d12";
                startInfo.EnvironmentVariables["MESA_LOADER_DRIVER_OVERRIDE"] = "d3d12";
                if (!string.IsNullOrWhiteSpace(_config.Glon12D3D12AdapterName))
                {
                    startInfo.EnvironmentVariables["MESA_D3D12_DEFAULT_ADAPTER_NAME"] =
                        _config.Glon12D3D12AdapterName;
                }
            }
        }

        private string ResolveGraphicsRuntimeMode(string openGlProvider)
        {
            if (!string.IsNullOrWhiteSpace(_config.GraphicsRuntimeMode))
            {
                return _config.GraphicsRuntimeMode.Trim();
            }

            if (string.Equals(openGlProvider, "glon12", StringComparison.OrdinalIgnoreCase))
            {
                return "glon12";
            }

            if (string.Equals(openGlProvider, "mesa-uwp", StringComparison.OrdinalIgnoreCase))
            {
                return "mesa-uwp";
            }

            return string.Empty;
        }

        private static string BuildMesaFmallocCachePath(string providerDirectory, string fileName)
        {
            var parent = Directory.GetParent(providerDirectory)?.FullName;
            return Path.Combine(
                string.IsNullOrWhiteSpace(parent) ? providerDirectory : parent,
                fileName);
        }

        private static void PrefixProcessPath(ProcessStartInfo startInfo, string directory)
        {
            var currentPath = startInfo.EnvironmentVariables["PATH"] ?? string.Empty;
            startInfo.EnvironmentVariables["PATH"] = string.IsNullOrWhiteSpace(currentPath)
                ? directory
                : directory + ";" + currentPath;
        }

        private static string QuoteArgument(string value)
        {
            return "\"" + (value ?? string.Empty).Replace("\"", "\\\"") + "\"";
        }

        private static string NormalizeUuidForMinecraft(string uuid)
        {
            return string.IsNullOrWhiteSpace(uuid)
                ? "00000000000000000000000000000000"
                : uuid.Replace("-", string.Empty);
        }

        private static string RedactSensitiveArgs(string args)
        {
            return System.Text.RegularExpressions.Regex.Replace(
                args,
                "--accessToken\\s+\"[^\"]*\"",
                "--accessToken \"redacted\"");
        }

        public Process Start(JavaLaunchPlan launchPlan, LaunchLog log)
        {
            if (launchPlan == null)
            {
                throw new ArgumentNullException(nameof(launchPlan));
            }

            return Start(launchPlan.StartInfo, log, launchPlan);
        }

        public Process Start(ProcessStartInfo startInfo, LaunchLog log)
        {
            return Start(startInfo, log, null);
        }

        private Process Start(ProcessStartInfo startInfo, LaunchLog log, JavaLaunchPlan launchPlan)
        {
            var process = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
            if (launchPlan != null)
            {
                var nativeLogForwarder = new NativeSidecarLogForwarder(launchPlan.NativeLogPath, log);
                launchPlan.ForwardNativeLogAsync = nativeLogForwarder.ForwardAsync;
                process.Exited += (_, __) =>
                {
                    Task.Run(async () =>
                    {
                        await Task.Delay(100);
                        await nativeLogForwarder.ForwardAsync();
                    });
                };
            }

            process.OutputDataReceived += (_, e) =>
            {
                if (!string.IsNullOrEmpty(e.Data))
                {
                    log.WriteLineAsync("[Java stdout] " + e.Data);
                }
            };
            process.ErrorDataReceived += (_, e) =>
            {
                if (!string.IsNullOrEmpty(e.Data))
                {
                    log.WriteLineAsync("[Java stderr] " + e.Data);
                }
            };
            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            return process;
        }

        private static async Task AppendNativeSidecarLogAsync(string nativeLogPath, LaunchLog log)
        {
            if (log == null || string.IsNullOrWhiteSpace(nativeLogPath))
            {
                return;
            }

            try
            {
                if (!File.Exists(nativeLogPath))
                {
                    await log.WriteLineAsync("[NativeSidecar][WARN] Native GLFW log file was not created: " + nativeLogPath);
                    return;
                }

                await log.WriteLineAsync("[NativeSidecar] Begin " + nativeLogPath);
                var forwarded = 0;
                foreach (var line in File.ReadLines(nativeLogPath).Take(500))
                {
                    await log.WriteLineAsync("[NativeSidecar] " + line);
                    forwarded++;
                }

                if (forwarded >= 500)
                {
                    await log.WriteLineAsync("[NativeSidecar][WARN] Sidecar log truncated after 500 lines.");
                }
                await log.WriteLineAsync("[NativeSidecar] End");
            }
            catch (Exception ex)
            {
                await log.WriteLineAsync("[NativeSidecar][WARN] Failed to read native GLFW sidecar log: " + ex.GetType().Name + ": " + ex.Message);
            }
        }

        private sealed class NativeSidecarLogForwarder
        {
            private readonly string _nativeLogPath;
            private readonly LaunchLog _log;
            private int _forwarded;

            public NativeSidecarLogForwarder(string nativeLogPath, LaunchLog log)
            {
                _nativeLogPath = nativeLogPath;
                _log = log;
            }

            public Task ForwardAsync()
            {
                if (Interlocked.Exchange(ref _forwarded, 1) != 0)
                {
                    return Task.CompletedTask;
                }

                return AppendNativeSidecarLogAsync(_nativeLogPath, _log);
            }
        }
    }

    public sealed class JavaLaunchPlan
    {
        public ProcessStartInfo StartInfo { get; set; }
        public string JavaPath { get; set; }
        public List<string> ClasspathEntries { get; set; }
        public string CommandPreview { get; set; }
        public List<string> Diagnostics { get; set; }
        public string NativeLogPath { get; set; }
        public Func<Task> ForwardNativeLogAsync { get; set; } = () => Task.CompletedTask;
    }
}
