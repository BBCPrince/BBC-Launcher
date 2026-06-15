using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.NetworkInformation;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using BBCLauncher.Configuration;
using BBCLauncher.Models;
using Windows.ApplicationModel.Core;
using Windows.Storage;
using Windows.UI;
using Windows.UI.Core;
using Windows.UI.ViewManagement;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Media;

namespace BBCLauncher.Services
{
    public sealed class LaunchOrchestrator
    {
        private readonly LauncherConfig _config;
        private readonly ManifestDownloader _downloader = new ManifestDownloader();
        private readonly FileVerifier _verifier = new FileVerifier();
        private readonly MicrosoftAuthService _auth;
        private readonly JavaRuntimeLauncher _javaLauncher;
        private readonly RuntimeAssetStager _assetStager;
        private readonly PathProbeService _pathProbe;

        public LaunchOrchestrator(LauncherConfig config)
        {
            _config = config;
            _auth = new MicrosoftAuthService(config);
            _javaLauncher = new JavaRuntimeLauncher(config);
            _assetStager = new RuntimeAssetStager(config);
            _pathProbe = new PathProbeService(config);
        }

        public event Action<LaunchStage, string> StageChanged;

        public async Task<LaunchResult> RunAsync(
            StorageFolder localFolder,
            ResolutionOption requestedResolution,
            CancellationToken cancellationToken)
        {
            LaunchLog log = null;
            var diagnostics = new List<string>();
            try
            {
                Report(LaunchStage.PreparingStorage, "Preparing local storage");
                var logsDir = _config.GetLogsRoot(localFolder);
                log = await LaunchLog.CreateAsync(logsDir);

                var storageRoot = localFolder.Path;
                var gameRoot = _config.GetGameRoot(localFolder);
                var assetsRoot = _config.GetAssetsRoot(localFolder);
                await log.WritePathsAsync(gameRoot, assetsRoot);

                await PackagedPayloadBootstrapper.CopyToLocalStateIfPresentAsync(log.WriteLineAsync);

                diagnostics.Add("[Info] SkipAuthenticationForDebug=" + _config.SkipAuthenticationForDebug);
                diagnostics.Add("[Info] SkipDownloadsForDebug=" + _config.SkipDownloadsForDebug);
                diagnostics.Add("[Info] RunGraphicsProbeBeforeLaunch=" + _config.RunGraphicsProbeBeforeLaunch);
                diagnostics.Add("[Info] RunEmbeddedJvmProbeBeforeLaunch=" + _config.RunEmbeddedJvmProbeBeforeLaunch);
                diagnostics.Add("[Info] EmbeddedJvmProbeMode=" + _config.EmbeddedJvmProbeMode);
                diagnostics.Add("[Info] EmbeddedMinecraftLaunchMode=" + _config.EmbeddedMinecraftLaunchMode);
                diagnostics.Add("[Info] NativeCoreWindowHostMode=" + _config.NativeCoreWindowHostMode);

                Report(LaunchStage.PreparingStorage, "Staging bundled native shims, log4j config, and probe");
                var staged = await _assetStager.StageAsync(localFolder);
                foreach (var line in staged.Lines)
                {
                    diagnostics.Add(line);
                    await log.WriteLineAsync(line);
                }
                if (!string.IsNullOrWhiteSpace(staged.Log4jConfigPath))
                {
                    _javaLauncher.Log4jConfigPath = staged.Log4jConfigPath;
                }
                if (!string.IsNullOrWhiteSpace(staged.JdkPatchJarPath))
                {
                    _javaLauncher.JdkPatchJarPath = staged.JdkPatchJarPath;
                }
                if (!string.IsNullOrWhiteSpace(staged.XboxGlfwDllPath))
                {
                    _javaLauncher.XboxGlfwDllPath = staged.XboxGlfwDllPath;
                }
                if (!string.IsNullOrWhiteSpace(staged.XboxOpenGlDllPath))
                {
                    _javaLauncher.XboxOpenGlDllPath = staged.XboxOpenGlDllPath;
                }
                if (!string.IsNullOrWhiteSpace(staged.OpenGlDllPath))
                {
                    _javaLauncher.OpenGlDllPath = staged.OpenGlDllPath;
                    _javaLauncher.OpenGlProvider = staged.OpenGlProvider;
                }
                if (!string.IsNullOrWhiteSpace(staged.RealOpenGlDllPath))
                {
                    _javaLauncher.RealOpenGlDllPath = staged.RealOpenGlDllPath;
                }
                if (!string.IsNullOrWhiteSpace(staged.XboxOpenAlDllPath))
                {
                    _javaLauncher.XboxOpenAlDllPath = staged.XboxOpenAlDllPath;
                }

                if (_config.RunPathProbeBeforeLaunch && !string.IsNullOrWhiteSpace(staged.PathProbeJarPath))
                {
                    Report(LaunchStage.PreparingStorage, "Probing child-process accessible directories");
                    var javaExe = Path.Combine(storageRoot, _config.JavaExecutableRelativePath);
                    var candidates = _pathProbe.BuildCandidatePaths(localFolder);
                    if (!string.IsNullOrWhiteSpace(_config.GameDirectoryOverride)
                        && !candidates.Contains(_config.GameDirectoryOverride, StringComparer.OrdinalIgnoreCase))
                    {
                        candidates.Insert(0, _config.GameDirectoryOverride);
                    }
                    foreach (var c in candidates)
                    {
                        try { Directory.CreateDirectory(c); } catch { }
                    }
                    var probeReport = await _pathProbe.ProbeAsync(
                        javaExe,
                        staged.PathProbeJarPath,
                        staged.JdkPatchJarPath,
                        candidates,
                        cancellationToken);
                    foreach (var line in probeReport.Lines)
                    {
                        diagnostics.Add(line);
                        await log.WriteLineAsync(line);
                    }
                    string chosen = null;
                    if (!string.IsNullOrWhiteSpace(_config.GameDirectoryOverride))
                    {
                        chosen = _config.GameDirectoryOverride;
                        diagnostics.Add("[Probe] GameDirectoryOverride from config: " + chosen);
                    }
                    else if (_config.AutoSelectGameDirectory && !string.IsNullOrWhiteSpace(probeReport.FirstFullyUsablePath))
                    {
                        chosen = probeReport.FirstFullyUsablePath;
                        diagnostics.Add("[Probe] Auto-selected fully-usable gameDir: " + chosen);
                    }
                    else
                    {
                        diagnostics.Add("[Probe][WARN] No directory passed all child-process checks. Falling back to default LocalState gameDir. Crashes are expected if Mojang calls toRealPath.");
                    }
                    if (!string.IsNullOrWhiteSpace(chosen))
                    {
                        _javaLauncher.GameDirectoryOverride = chosen;
                        try { Directory.CreateDirectory(chosen); } catch { }
                    }
                }

                if (!_config.SkipDownloadsForDebug && !NetworkInterface.GetIsNetworkAvailable())
                {
                    var hasCache = DirectoryExistsWithFiles(_config.GetGameRoot(localFolder));
                    if (!hasCache)
                    {
                        await log.WriteErrorAsync("No network and no cached game files.");
                        return LaunchResult.Fail(
                            "Download requires network on first launch.",
                            BuildDiagnosticText(diagnostics));
                    }
                }

                Report(LaunchStage.Authenticating, "Signing in with Microsoft");
                Action<string> authStatusHandler = msg =>
                {
                    diagnostics.Add("[Auth] " + msg);
                    Report(LaunchStage.Authenticating, msg);
                };

                AuthResult auth;
                _auth.AuthenticationStatusChanged += authStatusHandler;
                try
                {
                    auth = await _auth.SignInAsync();
                }
                finally
                {
                    _auth.AuthenticationStatusChanged -= authStatusHandler;
                }
                await log.WriteAuthSummaryAsync(auth.IsSuccess, auth.Message);
                diagnostics.Add("[Auth] " + auth.Message);
                if (auth.IsSuccess && !string.IsNullOrWhiteSpace(auth.Username))
                {
                    var authProfileLine = "[Auth] Minecraft profile=" + auth.Username +
                        " uuid=" + auth.Uuid +
                        " xuid=" + auth.Xuid;
                    diagnostics.Add(authProfileLine);
                    await log.WriteLineAsync(authProfileLine);
                }

                if (auth.IsCancelled)
                {
                    Report(LaunchStage.Cancelled, auth.Message);
                    return LaunchResult.Cancelled(auth.Message, BuildDiagnosticText(diagnostics));
                }

                if (!auth.IsSuccess)
                {
                    return LaunchResult.Fail(auth.Message, BuildDiagnosticText(diagnostics));
                }

                Report(LaunchStage.CheckingEntitlement, "Checking Minecraft Java ownership");
                await log.WriteLineAsync("[Entitlement] Starting ownership check");
                var entitlement = auth.Entitlement ??
                    await _auth.VerifyMinecraftJavaOwnershipAsync(auth.Session);
                diagnostics.Add("[Entitlement] " + entitlement.Message);
                await log.WriteLineAsync("[Entitlement] " + entitlement.Message);
                if (!entitlement.IsOwned)
                {
                    await log.WriteErrorAsync(entitlement.Message);
                    return LaunchResult.Fail(entitlement.Message, BuildDiagnosticText(diagnostics));
                }

                var manifestPath = Path.Combine(storageRoot, _config.LocalManifestFileName);

                DownloadManifest manifest = null;
                var verified = 0;
                var downloaded = 0;
                var failed = 0;
                if (_config.SkipDownloadsForDebug && !File.Exists(manifestPath))
                {
                    diagnostics.Add("[Preflight] SkipDownloadsForDebug enabled and no local manifest found; using runtime layout checks only.");
                    await log.WriteLineAsync("[Preflight] Download manifest verification skipped for debug local-file mode.");
                }
                else
                {
                    Report(LaunchStage.VerifyingCache, "Checking cached files");
                    manifest = await _downloader.LoadManifestAsync(_config.ManifestUrl, manifestPath);
                    diagnostics.Add("[Preflight] Manifest entries: " + manifest.Entries.Count);
                    var progress = new Progress<string>(msg => Report(LaunchStage.Downloading, msg));

                    foreach (var entry in manifest.Entries.Where(e => e.RequiredBeforeLaunch))
                    {
                        cancellationToken.ThrowIfCancellationRequested();
                        try
                        {
                            var existed = await _verifier.VerifyEntryAsync(storageRoot, entry);
                            if (existed)
                            {
                                verified++;
                                continue;
                            }

                            if (_config.SkipDownloadsForDebug)
                            {
                                failed++;
                                await log.WriteErrorAsync(
                                    "SkipDownloadsForDebug enabled and required file is missing: " + entry.LocalRelativePath);
                                continue;
                            }

                            if (!NetworkInterface.GetIsNetworkAvailable())
                            {
                                failed++;
                                await log.WriteErrorAsync("Network unavailable for missing file: " + entry.LocalRelativePath);
                                continue;
                            }

                            await _downloader.EnsureEntryAsync(storageRoot, entry, _verifier, progress, cancellationToken);
                            downloaded++;
                        }
                        catch (Exception ex)
                        {
                            failed++;
                            await log.WriteErrorAsync("Failed file: " + entry.LocalRelativePath, ex);
                        }
                    }

                    if (failed == 0)
                    {
                        Report(LaunchStage.Downloading, "Checking Minecraft assets");
                        try
                        {
                            var assetResult = await _downloader.EnsureAssetObjectsAsync(
                                storageRoot,
                                manifest,
                                progress,
                                cancellationToken);
                            diagnostics.Add("[Download] " + assetResult.Message);
                            await log.WriteLineAsync("[Download] " + assetResult.Message);
                            verified += assetResult.Verified;
                            downloaded += assetResult.Downloaded;
                        }
                        catch (Exception ex)
                        {
                            failed++;
                            await log.WriteErrorAsync("Failed Minecraft asset hydration", ex);
                        }
                    }
                }

                await log.WriteDownloadSummaryAsync(verified, downloaded, failed);
                diagnostics.Add("[Preflight] Verified files: " + verified);
                diagnostics.Add("[Preflight] Downloaded files: " + downloaded);
                diagnostics.Add("[Preflight] Failed files: " + failed);
                if (failed > 0)
                {
                    return LaunchResult.Fail(
                        "One or more required files could not be verified.",
                        BuildDiagnosticText(diagnostics));
                }

                var preflight = ValidateRuntimeLayout(storageRoot);
                diagnostics.AddRange(preflight.Lines);
                foreach (var line in preflight.Lines)
                {
                    await log.WriteLineAsync(line);
                }

                if (preflight.HasBlockingErrors)
                {
                    return LaunchResult.Fail(
                        "Runtime preflight failed. Review diagnostics.",
                        BuildDiagnosticText(diagnostics));
                }

                if (_config.EmbeddedJvmProbeMode || _config.RunEmbeddedJvmProbeBeforeLaunch)
                {
                    Report(LaunchStage.LaunchingJava, "Probing embedded JVM in launcher process");
                    var embeddedJvmProbe = new EmbeddedJvmProbeService(_config)
                        .Probe(storageRoot, staged.JdkPatchJarPath);
                    foreach (var line in embeddedJvmProbe.Lines)
                    {
                        diagnostics.Add(line);
                        await log.WriteLineAsync(line);
                    }

                    if (_config.EmbeddedJvmProbeMode)
                    {
                        return embeddedJvmProbe.IsSuccess
                            ? LaunchResult.ProbeSucceeded(embeddedJvmProbe.Message, BuildDiagnosticText(diagnostics))
                            : LaunchResult.Fail(embeddedJvmProbe.Message, BuildDiagnosticText(diagnostics));
                    }

                    if (!embeddedJvmProbe.IsSuccess)
                    {
                        var warning = "[EmbeddedJVM][WARN] Embedded JVM probe failed; continuing with external java.exe launch for comparison.";
                        diagnostics.Add(warning);
                        await log.WriteLineAsync(warning);
                    }
                }

                var effective = NormalizeResolution(requestedResolution);
                await ResolutionManager.SaveAsync(localFolder, effective);
                await log.WriteLineAsync("[Resolution] Using requested launch resolution without display probe");
                await log.WriteResolutionAsync(effective);
                await log.WriteLineAsync("[Resolution] Skipping resolution-settings save during Xbox launch test");
                diagnostics.Add("[Resolution] " + effective.Label + " " + effective.Width + "x" + effective.Height);

                if (_config.GraphicsProbeMode || _config.RunGraphicsProbeBeforeLaunch)
                {
                    Report(LaunchStage.StartingGraphics, "Running graphics probe");
                    var probe = new GraphicsProbeService(_config);
                    var probeResult = await probe.RunProbeAsync(log);
                    diagnostics.Add("[GraphicsProbe] " + probeResult.Message);
                    diagnostics.Add("[GraphicsProbe] Renderer=" + probeResult.Renderer + " Version=" + probeResult.Version);
                    if (!probeResult.Success || !probeResult.PresentationOk)
                    {
                        if (_config.GraphicsProbeMode)
                        {
                            return LaunchResult.Fail(
                                probeResult.Message ?? "Graphics probe failed.",
                                BuildDiagnosticText(diagnostics));
                        }

                        var warning = "[GraphicsProbe][WARN] Pre-launch graphics probe failed; continuing to Java launch.";
                        diagnostics.Add(warning);
                        await log.WriteLineAsync(warning);
                    }

                    if (_config.GraphicsProbeMode)
                    {
                        return LaunchResult.ProbeSucceeded(
                            probeResult.Message,
                            BuildDiagnosticText(diagnostics));
                    }
                }

                Report(LaunchStage.LaunchingJava, "Starting Minecraft Java " + _config.TargetMinecraftVersion);
                await log.WriteLineAsync("[LaunchPlan] Starting Java launch plan build");
                var launchPlan = await _javaLauncher.BuildStartInfoAsync(storageRoot, effective, auth.Session, log);
                await log.WriteLineAsync("[LaunchPlan] Java launch plan build complete");
                if (_config.SafeLaunchDiagnostics)
                {
                    diagnostics.AddRange(launchPlan.Diagnostics);
                    foreach (var line in launchPlan.Diagnostics)
                    {
                        await log.WriteLineAsync(line);
                    }

                    var classpathFail = launchPlan.Diagnostics.Any(l =>
                        l.IndexOf("Classpath validation: FAIL", StringComparison.OrdinalIgnoreCase) >= 0);
                    if (classpathFail)
                    {
                        return LaunchResult.Fail(
                            "Classpath validation failed. Review diagnostics.",
                            BuildDiagnosticText(diagnostics));
                    }
                }

                if (_config.EnableGameHostPresentation)
                {
                    await log.WriteLineAsync("[Presentation] Creating Java swap event before launch");
                    var eventReady = D3D12PresentationService.EnsureSwapEvent(_config, storageRoot);
                    var eventLine = eventReady
                        ? "[Presentation] Java swap event ready: " + D3D12PresentationService.JavaPresentEventName
                        : "[Presentation][WARN] Failed to create Java swap event before launch.";
                    await log.WriteLineAsync(eventLine);
                    diagnostics.Add(eventLine);
                }

                if (_config.EmbeddedMinecraftLaunchMode)
                {
                    Report(LaunchStage.LaunchingJava, "Starting embedded Minecraft Java " + _config.TargetMinecraftVersion);
                    var embeddedLaunch = await StartEmbeddedMinecraftAsync(
                        storageRoot,
                        launchPlan,
                        log,
                        cancellationToken);
                    diagnostics.AddRange(embeddedLaunch.Lines);
                    if (embeddedLaunch.IsRunning)
                    {
                        diagnostics.Add("[Launch] Embedded Minecraft JVM started in launcher process");
                        return LaunchResult.EmbeddedRunning(log.LogPath, BuildDiagnosticText(diagnostics));
                    }

                    return LaunchResult.Fail(
                        embeddedLaunch.Message ?? "Embedded Minecraft launch failed.",
                        BuildDiagnosticText(diagnostics));
                }

                await log.WriteLineAsync("[Java] Starting process");
                var process = _javaLauncher.Start(launchPlan, log);
                await log.WriteLineAsync("[Java] Process started pid=" + process.Id);
                diagnostics.Add("[Launch] Java process started pid=" + process.Id);

                var watchSeconds = Math.Max(0, _config.EarlyExitWatchSeconds);
                if (watchSeconds > 0)
                {
                    var exitedEarly = await Task.Run(() => process.WaitForExit(watchSeconds * 1000));
                    if (exitedEarly)
                    {
                        process.WaitForExit();
                        var exitCode = process.ExitCode;
                        var earlyExit = "[Java] Process exited during startup watch window with code=" + exitCode;
                        await log.WriteLineAsync(earlyExit);
                        await launchPlan.ForwardNativeLogAsync();
                        diagnostics.Add(earlyExit);
                        return LaunchResult.Fail(
                            "Minecraft exited during startup. Review the Java crash log.",
                            BuildDiagnosticText(diagnostics));
                    }
                }

                Report(LaunchStage.Running, "Minecraft is running");

                return LaunchResult.Running(process, log.LogPath, BuildDiagnosticText(diagnostics));
            }
            catch (OperationCanceledException)
            {
                Report(LaunchStage.Cancelled, "Launch cancelled");
                return LaunchResult.Cancelled("Launch cancelled.", BuildDiagnosticText(diagnostics));
            }
            catch (Exception ex)
            {
                if (log != null)
                {
                    await log.WriteErrorAsync(ex.Message, ex);
                }
                diagnostics.Add("[Exception] " + ex.Message);
                Report(LaunchStage.Failed, ex.Message);
                return LaunchResult.Fail(ex.Message, BuildDiagnosticText(diagnostics));
            }
        }

        private void Report(LaunchStage stage, string detail) => StageChanged?.Invoke(stage, detail);

        private Task<EmbeddedJvmLaunchResult> StartEmbeddedMinecraftAsync(
            string storageRoot,
            JavaLaunchPlan launchPlan,
            LaunchLog log,
            CancellationToken cancellationToken)
        {
            if (!_config.NativeCoreWindowHostMode)
            {
                return new EmbeddedJvmProbeService(_config)
                    .StartMinecraftAsync(storageRoot, launchPlan, log, cancellationToken);
            }

            return StartEmbeddedMinecraftInCoreWindowHostAsync(storageRoot, launchPlan, log, cancellationToken);
        }

        private async Task<EmbeddedJvmLaunchResult> StartEmbeddedMinecraftInCoreWindowHostAsync(
            string storageRoot,
            JavaLaunchPlan launchPlan,
            LaunchLog log,
            CancellationToken cancellationToken)
        {
            var viewReady = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
            var launchDone = new TaskCompletionSource<EmbeddedJvmLaunchResult>(TaskCreationOptions.RunContinuationsAsynchronously);

            try
            {
                await log.WriteLineAsync("[CoreWindowHost] Creating dedicated CoreWindow game view");
                var gameView = CoreApplication.CreateNewView();
                await gameView.Dispatcher.RunAsync(CoreDispatcherPriority.Normal, async () =>
                {
                    try
                    {
                        var appView = ApplicationView.GetForCurrentView();
                        var viewId = appView.Id;
                        try
                        {
                            appView.Title = "Minecraft CoreWindow Host";
                        }
                        catch
                        {
                        }

                        Window.Current.Content = new Grid
                        {
                            Background = new SolidColorBrush(Colors.Black),
                        };

                        var coreWindow = Window.Current.CoreWindow;
                        Window.Current.Activate();
                        await log.WriteLineAsync(
                            "[CoreWindowHost] SetWindow received CoreWindow viewId=" + viewId +
                            " state=" + FormatCoreWindowState(coreWindow) +
                            " managedThread=" + Environment.CurrentManagedThreadId +
                            " nativeThread=0x" + GetCurrentThreadId().ToString("X"));
                        viewReady.TrySetResult(viewId);
                    }
                    catch (Exception ex)
                    {
                        viewReady.TrySetException(ex);
                        launchDone.TrySetResult(new EmbeddedJvmLaunchResult()
                            .Fail("[CoreWindowHost][FAIL] " + ex.GetType().Name + ": " + ex.Message));
                    }
                });

                var gameViewId = await viewReady.Task;
                try
                {
                    var shown = await ApplicationViewSwitcher.TryShowAsStandaloneAsync(gameViewId);
                    await log.WriteLineAsync("[CoreWindowHost] TryShowAsStandaloneAsync(" + gameViewId + ") result=" + shown);
                }
                catch (Exception ex)
                {
                    await log.WriteLineAsync("[CoreWindowHost][WARN] TryShowAsStandaloneAsync failed: " + ex.GetType().Name + ": " + ex.Message);
                }

                await Task.Delay(250, cancellationToken);
                await gameView.Dispatcher.RunAsync(CoreDispatcherPriority.Normal, async () =>
                {
                    try
                    {
                        await log.WriteLineAsync(
                            "[CoreWindowHost] Launching embedded JVM from shown CoreWindow view state=" +
                            FormatCoreWindowState(Window.Current.CoreWindow) +
                            " managedThread=" + Environment.CurrentManagedThreadId +
                            " nativeThread=0x" + GetCurrentThreadId().ToString("X"));
                        var embeddedLaunch = await new EmbeddedJvmProbeService(_config)
                            .StartMinecraftAsync(storageRoot, launchPlan, log, cancellationToken);
                        launchDone.TrySetResult(embeddedLaunch);
                    }
                    catch (Exception ex)
                    {
                        launchDone.TrySetResult(new EmbeddedJvmLaunchResult()
                            .Fail("[CoreWindowHost][FAIL] " + ex.GetType().Name + ": " + ex.Message));
                    }
                });

                return await launchDone.Task;
            }
            catch (Exception ex)
            {
                return new EmbeddedJvmLaunchResult()
                    .Fail("[CoreWindowHost][FAIL] " + ex.GetType().Name + ": " + ex.Message);
            }
        }

        private static string FormatCoreWindowState(CoreWindow coreWindow)
        {
            if (coreWindow == null)
            {
                return "<null>";
            }

            var bounds = coreWindow.Bounds;
            return ((int)bounds.Width) + "x" + ((int)bounds.Height) +
                " visible=" + coreWindow.Visible;
        }

        private static ResolutionOption NormalizeResolution(ResolutionOption requested)
        {
            if (requested == null || requested.Width <= 0 || requested.Height <= 0)
            {
                return ResolutionOption.SeriesXDefaults[1];
            }

            return requested;
        }

        private static bool DirectoryExistsWithFiles(string path)
        {
            if (!Directory.Exists(path))
            {
                return false;
            }
            return Directory.EnumerateFiles(path, "*", SearchOption.AllDirectories).Any();
        }

        private PreflightReport ValidateRuntimeLayout(string storageRoot)
        {
            var report = new PreflightReport();

            var javaPath = Path.Combine(storageRoot, _config.JavaExecutableRelativePath);
            AddCheck(report, File.Exists(javaPath), true, "Java runtime present", javaPath);

            var clientJarPath = Path.Combine(storageRoot, "client.jar");
            AddCheck(report, File.Exists(clientJarPath), true, "client.jar present", clientJarPath);

            var librariesPath = Path.Combine(storageRoot, "libraries");
            var libraryCount = Directory.Exists(librariesPath)
                ? Directory.EnumerateFiles(librariesPath, "*.jar", SearchOption.AllDirectories).Count()
                : 0;
            AddCheck(report, libraryCount > 0, true, "Minecraft libraries found", "count=" + libraryCount);

            var nativeRoot = Path.Combine(storageRoot, _config.NativeLibrariesDirectoryName);
            var nativeExists = Directory.Exists(nativeRoot);
            AddCheck(report, nativeExists, false, "Native directory present", nativeRoot);

            if (!nativeExists)
            {
                return report;
            }

            var requested = (_config.NativeLibrariesToProbe ?? Array.Empty<string>())
                .Where(x => !string.IsNullOrWhiteSpace(x))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToList();

            var bundledShims = (_config.BundledNativeShims ?? Array.Empty<string>())
                .Where(x => !string.IsNullOrWhiteSpace(x))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .Where(x => !requested.Contains(x, StringComparer.OrdinalIgnoreCase))
                .ToList();

            ProbeNativeDll(report, nativeRoot, requested, isBlockingOnFailure: true);
            ProbeNativeDll(report, nativeRoot, bundledShims, isBlockingOnFailure: false);

            return report;
        }

        private static readonly HashSet<string> KnownSystemDllNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "Ole32.dll", "Pdh.dll", "Kernel32.dll", "User32.dll", "Advapi32.dll", "Shell32.dll", "Combase.dll",
        };

        private static void ProbeNativeDll(
            PreflightReport report,
            string nativeRoot,
            List<string> dlls,
            bool isBlockingOnFailure)
        {
            foreach (var name in dlls)
            {
                var dll = Path.Combine(nativeRoot, name);
                if (!File.Exists(dll))
                {
                    AddCheck(report, false, isBlockingOnFailure, "Native DLL missing", dll);
                    continue;
                }

                if (KnownSystemDllNames.Contains(Path.GetFileName(dll)))
                {
                    report.Lines.Add(
                        "[Preflight][INFO] " + Path.GetFileName(dll) +
                        " is a Windows KnownDLL; LoadLibrary intercepts to System32 regardless of absolute path. JNA in the child process uses its own search path so the shim is still active there. (file present at " + dll + ")");
                    continue;
                }

                if (IsMesaUwpRuntimeDll(dll))
                {
                    report.Lines.Add("[Preflight][PASS] Mesa-UWP runtime DLL present; launcher load probe skipped: " + dll);
                    continue;
                }

                if (NativeLibrary.TryLoad(dll, out var handle))
                {
                    NativeLibrary.Free(handle);
                    AddCheck(report, true, false, "Native DLL load probe", dll);
                }
                else
                {
                    AddCheck(report, false, isBlockingOnFailure, "Native DLL failed to load probe", dll);
                }
            }
        }

        private static bool IsMesaUwpRuntimeDll(string dll)
        {
            return dll.IndexOf(Path.DirectorySeparatorChar + "mesa-uwp" + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) >= 0
                || dll.IndexOf(Path.AltDirectorySeparatorChar + "mesa-uwp" + Path.AltDirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) >= 0;
        }

        private static void AddCheck(
            PreflightReport report,
            bool isSuccess,
            bool isBlockingOnFailure,
            string title,
            string detail)
        {
            if (isSuccess)
            {
                report.Lines.Add("[Preflight][PASS] " + title + ": " + detail);
                return;
            }

            var level = isBlockingOnFailure ? "FAIL" : "WARN";
            report.Lines.Add("[Preflight][" + level + "] " + title + ": " + detail);
            if (isBlockingOnFailure)
            {
                report.HasBlockingErrors = true;
            }
        }

        private static string BuildDiagnosticText(List<string> diagnostics)
        {
            return diagnostics.Count == 0
                ? "No diagnostics captured."
                : string.Join(Environment.NewLine, diagnostics);
        }

        [DllImport("kernel32.dll")]
        private static extern uint GetCurrentThreadId();
    }

    public sealed class LaunchResult
    {
        public bool IsSuccess { get; private set; }
        public bool IsRunning { get; private set; }
        public bool IsCancelled { get; private set; }
        public bool IsProbe { get; private set; }
        public string Message { get; private set; }
        public string LogPath { get; private set; }
        public string Diagnostics { get; private set; }
        public System.Diagnostics.Process Process { get; private set; }

        public static LaunchResult Running(System.Diagnostics.Process process, string logPath, string diagnostics) =>
            new LaunchResult
            {
                IsSuccess = true,
                IsRunning = true,
                Process = process,
                LogPath = logPath,
                Message = "Running",
                Diagnostics = diagnostics,
            };

        public static LaunchResult ProbeSucceeded(string message, string diagnostics) =>
            new LaunchResult { IsSuccess = true, IsProbe = true, Message = message, Diagnostics = diagnostics };

        public static LaunchResult EmbeddedRunning(string logPath, string diagnostics) =>
            new LaunchResult
            {
                IsSuccess = true,
                IsRunning = true,
                LogPath = logPath,
                Message = "Embedded Minecraft JVM running",
                Diagnostics = diagnostics,
            };

        public static LaunchResult Fail(string message, string diagnostics) =>
            new LaunchResult { Message = message, Diagnostics = diagnostics };

        public static LaunchResult Cancelled(string message, string diagnostics) =>
            new LaunchResult { IsCancelled = true, Message = message, Diagnostics = diagnostics };
    }

    public sealed class PreflightReport
    {
        public List<string> Lines { get; } = new List<string>();
        public bool HasBlockingErrors { get; set; }
    }
}
