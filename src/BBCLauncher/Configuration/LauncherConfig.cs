using System;
using System.IO;
using System.Threading.Tasks;
using Newtonsoft.Json;
using Windows.Storage;

namespace BBCLauncher.Configuration
{
    public sealed class LauncherConfig
    {
        public string MicrosoftClientId { get; set; } = "REPLACE_WITH_AZURE_APP_CLIENT_ID";
        public string TargetMinecraftVersion { get; set; } = "1.21.1";
        public string JavaRuntimeVersion { get; set; } = "21";
        public string ManifestUrl { get; set; } = "https://example.invalid/manifest.json";
        public string LocalManifestFileName { get; set; } = "download-manifest.json";
        public bool EnableResolutionPicker { get; set; } = false;
        public bool IncludeStandardControllerMod { get; set; } = true;
        public bool IncludeXboxUiMod { get; set; } = false;
        public bool GraphicsProbeMode { get; set; } = false;
        public bool SoftwareRenderingFallback { get; set; } = false;
        public bool SkipAuthenticationForDebug { get; set; } = false;
        public bool SkipDownloadsForDebug { get; set; } = false;
        public bool RunGraphicsProbeBeforeLaunch { get; set; } = false;
        public bool EmbeddedJvmProbeMode { get; set; } = false;
        public bool RunEmbeddedJvmProbeBeforeLaunch { get; set; } = false;
        public bool EmbeddedMinecraftLaunchMode { get; set; } = false;
        public bool SafeLaunchDiagnostics { get; set; } = true;
        public string[] NativeLibrariesToProbe { get; set; } = new string[0];
        public string GraphicsBridgeDllName { get; set; } = "graphics_bridge.dll";
        public bool UseXboxLog4jConfig { get; set; } = true;
        public bool StageBundledNativeShims { get; set; } = true;
        public string XboxLog4jConfigResource { get; set; } = "Resources/log4j2-xbox.xml";
        public string[] BundledNativeShims { get; set; } = new[] { "Ole32.dll", "Pdh.dll", "graphics_bridge.dll" };
        public bool RunPathProbeBeforeLaunch { get; set; } = true;
        public string PathProbeJarPackagePath { get; set; } = "native/XboxPathProbe.jar";
        public int PathProbeTimeoutSeconds { get; set; } = 25;
        public string[] PathProbeExtraCandidates { get; set; } = new string[0];
        public bool AutoSelectGameDirectory { get; set; } = true;
        public string GameDirectoryOverride { get; set; } = "";
        public bool UseJdkPatch { get; set; } = true;
        public string JdkPatchJarPackagePath { get; set; } = "native/xbox-jdk-patch.jar";
        public bool ApplyJdkPatchToProbe { get; set; } = true;

        // Replace LWJGL's bundled glfw.dll (which calls RegisterClassExW, not
        // supported on Xbox UWP) with our stub. The stub returns safe defaults
        // so we can clear early GLFW init and see what Mojang touches next.
        public bool UseXboxGlfwStub { get; set; } = true;
        public string XboxGlfwDllPackagePath { get; set; } = "native/xbox-glfw.dll";

        // Replace LWJGL's bundled opengl32 lookup (Xbox UWP doesn't ship
        // opengl32.dll) with our stub.  Returns enough fake GL state for
        // GLCapabilities to believe we have OpenGL 4.6 core profile.
        public bool UseCustomOpenGlProvider { get; set; } = true;
        public string OpenGlProvider { get; set; } = "auto";
        public string GraphicsRuntimeMode { get; set; } = "";
        public bool UseXboxOpenGlStub { get; set; } = true;
        public string XboxOpenGlDllPackagePath { get; set; } = "native/xbox-opengl.dll";
        public string MesaUwpRuntimePackagePath { get; set; } = "native/mesa-uwp";
        public string MesaUwpOpenGlDllPackagePath { get; set; } = "native/mesa-uwp/opengl32.dll";
        public bool EnableMesaUwpD3D12Environment { get; set; } = true;
        public bool EnableMesaCoreWindowProbe { get; set; } = false;
        public bool NativeCoreWindowHostMode { get; set; } = false;
        public bool NativeCoreWindowHostDirectMesaOpenGl { get; set; } = true;
        public bool NativeCoreWindowHostFatalSurfaceFailure { get; set; } = true;
        public string Glon12RuntimePackagePath { get; set; } = "native/glon12";
        public string Glon12OpenGlDllPackagePath { get; set; } = "native/glon12/opengl32.dll";
        public bool EnableGlon12D3D12Environment { get; set; } = true;
        public string Glon12D3D12AdapterName { get; set; } = "";

        // Replace LWJGL's bundled OpenAL.dll (OpenAL Soft, which imports from
        // dsound.dll / mmdevapi - neither available on Xbox UWP) with our
        // stub. alcOpenDevice returns NULL so Mojang's SoundEngine cleanly
        // disables audio and continues startup.
        public bool UseXboxOpenAlStub { get; set; } = true;
        public string XboxOpenAlDllPackagePath { get; set; } = "native/xbox-openal.dll";

        // After Java launch succeeds, navigate to GameHostPage which owns the
        // CoreWindow D3D12 swapchain (Milestone 2 presentation host).
        public bool EnableGameHostPresentation { get; set; } = true;

        public string GraphicsBackend { get; set; } = "d3d12-opengl";
        public string AudioBackend { get; set; } = "xbox";
        public string InputBackend { get; set; } = "gamepad-bridge";
        public int DefaultHeapMegabytes { get; set; } = 4096;
        public string JavaExecutableRelativePath { get; set; } = "runtime/jre/bin/java.exe";
        public string GameDirectoryName { get; set; } = "game";
        public string AssetsDirectoryName { get; set; } = "assets";
        public string LogsDirectoryName { get; set; } = "logs";
        public string NativeLibrariesDirectoryName { get; set; } = "native";
        public string TempDirectoryName { get; set; } = "tmp";
        public int EarlyExitWatchSeconds { get; set; } = 30;

        public static async Task<LauncherConfig> LoadAsync()
        {
            try
            {
                var installed = await StorageFile.GetFileFromApplicationUriAsync(
                    new Uri("ms-appx:///launcher.config.json"));
                var json = await FileIO.ReadTextAsync(installed);
                return JsonConvert.DeserializeObject<LauncherConfig>(json) ?? new LauncherConfig();
            }
            catch
            {
                return new LauncherConfig();
            }
        }

        public string GetGameRoot(StorageFolder localFolder) =>
            Path.Combine(localFolder.Path, GameDirectoryName);

        public string GetAssetsRoot(StorageFolder localFolder) =>
            Path.Combine(localFolder.Path, AssetsDirectoryName);

        public string GetLogsRoot(StorageFolder localFolder) =>
            Path.Combine(localFolder.Path, LogsDirectoryName);

        public string GetNativeRoot(StorageFolder localFolder) =>
            Path.Combine(localFolder.Path, NativeLibrariesDirectoryName);
    }
}
