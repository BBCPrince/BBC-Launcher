using System;
using System.Runtime.InteropServices;
using BBCLauncher.Models;
using BBCLauncher.Services;
using Windows.Foundation;
using Windows.UI;
using Windows.UI.Core;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Navigation;

namespace BBCLauncher
{
    public sealed partial class GameHostPage : Page
    {
        private GameHostLaunchContext _context;
        private D3D12PresentationService _presentation;
        private bool _renderingAttached;
        private bool _presentationInitAttempted;
        private uint _frameCounter;
        private bool _javaSwapFlash;
        private ulong _lastLoggedJavaSwapCount;
        private ulong _lastLoggedGlClearCount;
        private ulong _lastLoggedGlViewportCount;
        private ulong _lastLoggedGlDrawCount;
        private ulong _lastLoggedGlTextureUploadCount;
        private ulong _lastLoggedGlTextureSampleCount;
        private ulong _lastLoggedGlGuiFrameCount;
        private ulong _lastLoggedGlBufferUploadCount;
        private ulong _lastLoggedGlVertexAttribCount;
        private TypedEventHandler<CoreWindow, WindowActivatedEventArgs> _activatedHandler;
        private SizeChangedEventHandler _surfaceSizeChangedHandler;

        public GameHostPage()
        {
            InitializeComponent();
        }

        protected override void OnNavigatedTo(NavigationEventArgs e)
        {
            base.OnNavigatedTo(e);
            _context = e.Parameter as GameHostLaunchContext;
            _presentation = new D3D12PresentationService(App.Config);
            AppendPresentationLog("[Presentation] GameHostPage navigated; initializing surface.");
            Loaded += OnPageLoaded;
        }

        private void OnPageLoaded(object sender, RoutedEventArgs e)
        {
            Loaded -= OnPageLoaded;
            _surfaceSizeChangedHandler = OnRenderSurfaceSizeChanged;
            RenderSurface.SizeChanged += _surfaceSizeChangedHandler;
            AppendPresentationLog("[Presentation] GameHostPage loaded.");
            SchedulePresentationInit();
        }

        private void SchedulePresentationInit()
        {
            var coreWindow = Window.Current?.CoreWindow;
            if (coreWindow == null)
            {
                StatusText.Foreground = new SolidColorBrush(Colors.OrangeRed);
                StatusText.Text = "Presentation init failed: CoreWindow unavailable.";
                AppendPresentationLog("[Presentation][WARN] init failed: CoreWindow unavailable.");
                return;
            }

            _activatedHandler = OnCoreWindowActivated;
            coreWindow.Activated += _activatedHandler;

            // The XAML SwapChainPanel needs a completed layout pass before DXGI
            // accepts it as a composition target.
            _ = Dispatcher.RunAsync(CoreDispatcherPriority.Normal, TryInitializePresentation);
        }

        private void OnCoreWindowActivated(CoreWindow sender, WindowActivatedEventArgs args)
        {
            if (args.WindowActivationState == CoreWindowActivationState.Deactivated)
            {
                return;
            }

            TryInitializePresentation();
        }

        private void OnRenderSurfaceSizeChanged(object sender, SizeChangedEventArgs e)
        {
            TryInitializePresentation();
        }

        private void TryInitializePresentation()
        {
            if (_presentationInitAttempted || _presentation == null)
            {
                return;
            }

            _presentationInitAttempted = true;

            var resolution = _context?.Resolution ?? ResolutionOption.SeriesXDefaults[1];
            var coreWindow = Window.Current?.CoreWindow;
            var bounds = coreWindow?.Bounds ?? new Rect(0, 0, 0, 0);
            var width = resolution.Width;
            var height = resolution.Height;
            if (width <= 1 || height <= 1)
            {
                width = (int)Math.Max(1, RenderSurface.ActualWidth);
                height = (int)Math.Max(1, RenderSurface.ActualHeight);
            }
            if (width <= 1 || height <= 1)
            {
                width = (int)Math.Max(1, bounds.Width);
                height = (int)Math.Max(1, bounds.Height);
            }

            var unknown = Marshal.GetIUnknownForObject(RenderSurface);
            try
            {
                AppendPresentationLog("[Presentation] init attempt size=" + width + "x" + height);
                if (_presentation.Initialize(unknown, width, height))
                {
                    if (_activatedHandler != null && coreWindow != null)
                    {
                        coreWindow.Activated -= _activatedHandler;
                        _activatedHandler = null;
                    }
                    if (_surfaceSizeChangedHandler != null)
                    {
                        RenderSurface.SizeChanged -= _surfaceSizeChangedHandler;
                        _surfaceSizeChangedHandler = null;
                    }

                    StatusText.Text = "D3D12 XAML presentation active (" + width + "x" + height + "). Waiting for Minecraft render loop...";
                    TitleText.Text = "Minecraft is running";
                    AppendPresentationLog("[Presentation] D3D12 XAML presentation active size=" + width + "x" + height);
                }
                else
                {
                    _presentationInitAttempted = false;
                    StatusText.Foreground = new SolidColorBrush(Colors.OrangeRed);
                    StatusText.Text = "Presentation init failed: " + _presentation.LastError;
                    AppendPresentationLog("[Presentation][WARN] init failed: " + _presentation.LastError);
                }
            }
            finally
            {
                if (unknown != IntPtr.Zero)
                {
                    Marshal.Release(unknown);
                }
            }

            if (!_renderingAttached)
            {
                CompositionTarget.Rendering += OnRendering;
                _renderingAttached = true;
            }
        }

        protected override void OnNavigatedFrom(NavigationEventArgs e)
        {
            AppendPresentationLog("[Presentation] GameHostPage navigating away; disposing presentation.");
            if (_activatedHandler != null && Window.Current?.CoreWindow != null)
            {
                Window.Current.CoreWindow.Activated -= _activatedHandler;
                _activatedHandler = null;
            }
            if (_surfaceSizeChangedHandler != null)
            {
                RenderSurface.SizeChanged -= _surfaceSizeChangedHandler;
                _surfaceSizeChangedHandler = null;
            }

            if (_renderingAttached)
            {
                CompositionTarget.Rendering -= OnRendering;
                _renderingAttached = false;
            }

            _presentation?.Dispose();
            _presentation = null;
            base.OnNavigatedFrom(e);
        }

        private void OnRendering(object sender, object e)
        {
            try
            {
                OnRenderingCore(sender, e);
            }
            catch (Exception ex)
            {
                AppendPresentationLog("[Presentation][UnhandledRenderingException] " + ex);
                throw;
            }
        }

        private void OnRenderingCore(object sender, object e)
        {
            if (_presentation == null || !_presentation.IsInitialized)
            {
                return;
            }

            if (_presentation.ConsumeJavaSwapSignal())
            {
                _javaSwapFlash = true;
            }

            _frameCounter++;
            var rgba = BuildClearColor();
            var presented = _presentation.Present(rgba);
            var javaSwapCount = _presentation.JavaSwapCount;
            var glClearCount = _presentation.GlClearCount;
            var glViewportCount = _presentation.GlViewportCount;
            var glDrawCount = _presentation.GlDrawCount;
            var glTextureUploads = _presentation.GlTextureUploadCount;
            var glTextureBytes = _presentation.GlTextureUploadBytes;
            var glTextureSamples = _presentation.GlTextureSampleCount;
            var glTextureSampleWidth = _presentation.GlTextureSampleWidth;
            var glTextureSampleHeight = _presentation.GlTextureSampleHeight;
            var glTexturePresented = _presentation.GlTexturePresentCount;
            var glGuiFrames = _presentation.GlGuiFrameCount;
            var glGuiFrameWidth = _presentation.GlGuiFrameWidth;
            var glGuiFrameHeight = _presentation.GlGuiFrameHeight;
            var glGuiPresented = _presentation.GlGuiFramePresentCount;
            var glGuiNonBlackPixels = _presentation.GlGuiNonBlackPixels;
            var glGuiNonTransparentPixels = _presentation.GlGuiNonTransparentPixels;
            var glGuiAverageRed = _presentation.GlGuiAverageRed;
            var glGuiAverageGreen = _presentation.GlGuiAverageGreen;
            var glGuiAverageBlue = _presentation.GlGuiAverageBlue;
            var glGuiCenterRgb = _presentation.GlGuiCenterRgb;
            var glGuiTexturedTriangles = _presentation.GlGuiTexturedTriangleCount;
            var glGuiColorTriangles = _presentation.GlGuiColorTriangleCount;
            var glGuiSkippedVertices = _presentation.GlGuiSkippedVertexCount;
            var glGuiLastTextureName = _presentation.GlGuiLastTextureName;
            var glGuiLastTextureWidth = _presentation.GlGuiLastTextureWidth;
            var glGuiLastTextureHeight = _presentation.GlGuiLastTextureHeight;
            var glGuiCandidateAcceptedCount = _presentation.GlGuiCandidateAcceptedCount;
            var glGuiCandidateRejectedCount = _presentation.GlGuiCandidateRejectedCount;
            var glGuiCandidateSourceTextureName = _presentation.GlGuiCandidateSourceTextureName;
            var glGuiCandidateSourceWidth = _presentation.GlGuiCandidateSourceWidth;
            var glGuiCandidateSourceHeight = _presentation.GlGuiCandidateSourceHeight;
            var glGuiCandidateWidth = _presentation.GlGuiCandidateWidth;
            var glGuiCandidateHeight = _presentation.GlGuiCandidateHeight;
            var glGuiCandidatePixels = _presentation.GlGuiCandidatePixels;
            var glGuiCandidateNonBlackPixels = _presentation.GlGuiCandidateNonBlackPixels;
            var glGuiCandidateNonTransparentPixels = _presentation.GlGuiCandidateNonTransparentPixels;
            var glGuiCandidateAverageRed = _presentation.GlGuiCandidateAverageRed;
            var glGuiCandidateAverageGreen = _presentation.GlGuiCandidateAverageGreen;
            var glGuiCandidateAverageBlue = _presentation.GlGuiCandidateAverageBlue;
            var glGuiCandidateRejectReason = _presentation.GlGuiCandidateRejectReason;
            var glGuiCandidateAcceptedSourceTextureName = _presentation.GlGuiCandidateAcceptedSourceTextureName;
            var glGuiCandidateAcceptedSourceWidth = _presentation.GlGuiCandidateAcceptedSourceWidth;
            var glGuiCandidateAcceptedSourceHeight = _presentation.GlGuiCandidateAcceptedSourceHeight;
            var glTextureLastStoredName = _presentation.GlTextureLastStoredName;
            var glTextureLastStoredWidth = _presentation.GlTextureLastStoredWidth;
            var glTextureLastStoredHeight = _presentation.GlTextureLastStoredHeight;
            var glTextureLargestName = _presentation.GlTextureLargestName;
            var glTextureLargestWidth = _presentation.GlTextureLargestWidth;
            var glTextureLargestHeight = _presentation.GlTextureLargestHeight;
            var glTextureBestGuiName = _presentation.GlTextureBestGuiName;
            var glTextureBestGuiWidth = _presentation.GlTextureBestGuiWidth;
            var glTextureBestGuiHeight = _presentation.GlTextureBestGuiHeight;
            var glTextureRecordCount = _presentation.GlTextureRecordCount;
            var glTextureTableFullCount = _presentation.GlTextureTableFullCount;
            var glTextureExactGuiName = _presentation.GlTextureExactGuiName;
            var glTextureExactGuiWidth = _presentation.GlTextureExactGuiWidth;
            var glTextureExactGuiHeight = _presentation.GlTextureExactGuiHeight;
            var glTextureExactGuiHasPixels = _presentation.GlTextureExactGuiHasPixels;
            var glTextureBestAllocatedGuiName = _presentation.GlTextureBestAllocatedGuiName;
            var glTextureBestAllocatedGuiWidth = _presentation.GlTextureBestAllocatedGuiWidth;
            var glTextureBestAllocatedGuiHeight = _presentation.GlTextureBestAllocatedGuiHeight;
            var glTextureLastAllocationName = _presentation.GlTextureLastAllocationName;
            var glTextureLastAllocationWidth = _presentation.GlTextureLastAllocationWidth;
            var glTextureLastAllocationHeight = _presentation.GlTextureLastAllocationHeight;
            var glTextureShrinkPreservedCount = _presentation.GlTextureShrinkPreservedCount;
            var glTextureLastAttemptName = _presentation.GlTextureLastAttemptName;
            var glTextureLastAttemptLevel = _presentation.GlTextureLastAttemptLevel;
            var glTextureLastAttemptX = _presentation.GlTextureLastAttemptX;
            var glTextureLastAttemptY = _presentation.GlTextureLastAttemptY;
            var glTextureLastAttemptWidth = _presentation.GlTextureLastAttemptWidth;
            var glTextureLastAttemptHeight = _presentation.GlTextureLastAttemptHeight;
            var glTextureLastAttemptFormat = _presentation.GlTextureLastAttemptFormat;
            var glTextureLastAttemptType = _presentation.GlTextureLastAttemptType;
            var glTextureLastAttemptReason = _presentation.GlTextureLastAttemptReason;
            var glTextureLastAttemptPbo = _presentation.GlTextureLastAttemptPbo;
            var glTextureLastAttemptUnit = _presentation.GlTextureLastAttemptUnit;
            var glTextureExactGuiUploadAttemptCount = _presentation.GlTextureExactGuiUploadAttemptCount;
            var glTextureExactGuiUploadAcceptedCount = _presentation.GlTextureExactGuiUploadAcceptedCount;
            var glTextureExactGuiUploadRejectedCount = _presentation.GlTextureExactGuiUploadRejectedCount;
            var glTextureExactGuiLastRejectReason = _presentation.GlTextureExactGuiLastRejectReason;
            var glFramebufferBinds = _presentation.GlFramebufferBindCount;
            var glFramebufferAttaches = _presentation.GlFramebufferAttachCount;
            var glFramebufferBlits = _presentation.GlFramebufferBlitCount;
            var glFramebufferDrawName = _presentation.GlFramebufferDrawName;
            var glFramebufferReadName = _presentation.GlFramebufferReadName;
            var glFramebufferColorTextureName = _presentation.GlFramebufferColorTextureName;
            var glFramebufferColorTextureWidth = _presentation.GlFramebufferColorTextureWidth;
            var glFramebufferColorTextureHeight = _presentation.GlFramebufferColorTextureHeight;
            var glFramebufferLastBlitSourceTextureName = _presentation.GlFramebufferLastBlitSourceTextureName;
            var glFramebufferLastBlitDestTextureName = _presentation.GlFramebufferLastBlitDestTextureName;
            var glFramebufferLastBlitWidth = _presentation.GlFramebufferLastBlitWidth;
            var glFramebufferLastBlitHeight = _presentation.GlFramebufferLastBlitHeight;
            var glBufferUploads = _presentation.GlBufferUploadCount;
            var glBufferBytes = _presentation.GlBufferUploadBytes;
            var glVertexAttribs = _presentation.GlVertexAttribCount;
            var glProgramUses = _presentation.GlProgramUseCount;
            var glUniforms = _presentation.GlUniformCount;

            if (_frameCounter % 30 == 0)
            {
                StatsText.Text =
                    "Launcher frames: " + _presentation.PresentCount +
                    " | Java swap signals: " + javaSwapCount +
                    " | GL clears: " + glClearCount +
                    " | GL draws: " + glDrawCount +
                    " | Tex: " + glTextureUploads +
                    " | Tex sample: " + glTextureSampleWidth + "x" + glTextureSampleHeight +
                    " -> " + glTexturePresented +
                    " | GUI: " + glGuiFrameWidth + "x" + glGuiFrameHeight +
                    " -> " + glGuiPresented +
                    " | Pix: " + glGuiNonBlackPixels +
                    " | Tri: " + glGuiTexturedTriangles + "/" + glGuiColorTriangles +
                    " | GUI tex: " + glGuiLastTextureName + " " + glGuiLastTextureWidth + "x" + glGuiLastTextureHeight +
                    " | Cand R: " + glGuiCandidateRejectReason +
                    " | Buf: " + glBufferUploads +
                    " | PID: " + (_context?.LaunchResult?.Process?.Id.ToString() ?? "?");
            }

            var firstJavaSwap = javaSwapCount > 0 && _lastLoggedJavaSwapCount == 0;
            var firstGlClear = glClearCount > 0 && _lastLoggedGlClearCount == 0;
            var firstGlDraw = glDrawCount > 0 && _lastLoggedGlDrawCount == 0;
            var firstTextureUpload = glTextureUploads > 0 && _lastLoggedGlTextureUploadCount == 0;
            var firstTextureSample = glTextureSamples > 0 && _lastLoggedGlTextureSampleCount == 0;
            var firstGuiFrame = glGuiFrames > 0 && _lastLoggedGlGuiFrameCount == 0;
            var firstBufferUpload = glBufferUploads > 0 && _lastLoggedGlBufferUploadCount == 0;
            var firstVertexAttrib = glVertexAttribs > 0 && _lastLoggedGlVertexAttribCount == 0;
            var diagnosticBurstLog =
                (_frameCounter >= 590 && _frameCounter <= 660) ||
                (_frameCounter > 660 && _frameCounter <= 900 && _frameCounter % 10 == 0) ||
                (_frameCounter > 900 && _frameCounter % 60 == 0);
            var periodicLog = _frameCounter % 120 == 0 || diagnosticBurstLog;
            if (periodicLog || firstJavaSwap || firstGlClear || firstGlDraw || firstTextureUpload || firstTextureSample || firstGuiFrame || firstBufferUpload || firstVertexAttrib)
            {
                _lastLoggedJavaSwapCount = javaSwapCount;
                _lastLoggedGlClearCount = glClearCount;
                _lastLoggedGlViewportCount = glViewportCount;
                _lastLoggedGlDrawCount = glDrawCount;
                _lastLoggedGlTextureUploadCount = glTextureUploads;
                _lastLoggedGlTextureSampleCount = glTextureSamples;
                _lastLoggedGlGuiFrameCount = glGuiFrames;
                _lastLoggedGlBufferUploadCount = glBufferUploads;
                _lastLoggedGlVertexAttribCount = glVertexAttribs;
                AppendPresentationLog(
                    "[Presentation] frames=" + _presentation.PresentCount +
                    " javaSwapSignals=" + javaSwapCount +
                    " glClears=" + glClearCount +
                    " glViewports=" + glViewportCount +
                    " glDraws=" + glDrawCount +
                    " glTexUploads=" + glTextureUploads +
                    " glTexMB=" + FormatMegabytes(glTextureBytes) +
                    " glTexSamples=" + glTextureSamples +
                    " glTexSample=" + glTextureSampleWidth + "x" + glTextureSampleHeight +
                    " glTexPresented=" + glTexturePresented +
                    " glGuiFrames=" + glGuiFrames +
                    " glGui=" + glGuiFrameWidth + "x" + glGuiFrameHeight +
                    " glGuiPresented=" + glGuiPresented +
                    " glGuiPixels=" + glGuiNonBlackPixels + "/" + glGuiNonTransparentPixels +
                    " glGuiAvg=" + glGuiAverageRed + "," + glGuiAverageGreen + "," + glGuiAverageBlue +
                    " glGuiCenter=0x" + glGuiCenterRgb.ToString("X6") +
                    " glGuiTexTri=" + glGuiTexturedTriangles +
                    " glGuiColorTri=" + glGuiColorTriangles +
                    " glGuiSkipVert=" + glGuiSkippedVertices +
                    " glGuiTex=" + glGuiLastTextureName + ":" + glGuiLastTextureWidth + "x" + glGuiLastTextureHeight +
                    " glGuiCand=" + glGuiCandidateSourceTextureName + ":" + glGuiCandidateSourceWidth + "x" + glGuiCandidateSourceHeight + ">" + glGuiCandidateWidth + "x" + glGuiCandidateHeight +
                    " pix=" + glGuiCandidateNonBlackPixels + "/" + glGuiCandidateNonTransparentPixels + "/" + glGuiCandidatePixels +
                    " avg=" + glGuiCandidateAverageRed + "," + glGuiCandidateAverageGreen + "," + glGuiCandidateAverageBlue +
                    " accrej=" + glGuiCandidateAcceptedCount + "/" + glGuiCandidateRejectedCount +
                    " R" + glGuiCandidateRejectReason +
                    " lastGood=" + glGuiCandidateAcceptedSourceTextureName + ":" + glGuiCandidateAcceptedSourceWidth + "x" + glGuiCandidateAcceptedSourceHeight +
                    " glTexLast=" + glTextureLastStoredName + ":" + glTextureLastStoredWidth + "x" + glTextureLastStoredHeight +
                    " glTexLargest=" + glTextureLargestName + ":" + glTextureLargestWidth + "x" + glTextureLargestHeight +
                    " glTexBestGui=" + glTextureBestGuiName + ":" + glTextureBestGuiWidth + "x" + glTextureBestGuiHeight +
                    " glTexRecords=" + glTextureRecordCount +
                    " glTexFull=" + glTextureTableFullCount +
                    " glTexExactGui=" + glTextureExactGuiName + ":" + glTextureExactGuiWidth + "x" + glTextureExactGuiHeight + ":" + glTextureExactGuiHasPixels +
                    " glTexBestAllocGui=" + glTextureBestAllocatedGuiName + ":" + glTextureBestAllocatedGuiWidth + "x" + glTextureBestAllocatedGuiHeight +
                    " glTexAlloc=" + glTextureLastAllocationName + ":" + glTextureLastAllocationWidth + "x" + glTextureLastAllocationHeight +
                    " glTexShrinkKeep=" + glTextureShrinkPreservedCount +
                    " glTexAttempt=" + glTextureLastAttemptName + ":L" + glTextureLastAttemptLevel + "@" + glTextureLastAttemptX + "," + glTextureLastAttemptY + " " + glTextureLastAttemptWidth + "x" + glTextureLastAttemptHeight + " F" + glTextureLastAttemptFormat + " T" + glTextureLastAttemptType + " R" + glTextureLastAttemptReason + " PBO" + glTextureLastAttemptPbo + " U" + glTextureLastAttemptUnit +
                    " glTexExactUpload=" + glTextureExactGuiUploadAttemptCount + "/" + glTextureExactGuiUploadAcceptedCount + "/" + glTextureExactGuiUploadRejectedCount + ":" + glTextureExactGuiLastRejectReason +
                    " glFbo=" + glFramebufferBinds + "/" + glFramebufferAttaches + "/" + glFramebufferBlits +
                    " glFboDrawRead=" + glFramebufferDrawName + "/" + glFramebufferReadName +
                    " glFboTex=" + glFramebufferColorTextureName + ":" + glFramebufferColorTextureWidth + "x" + glFramebufferColorTextureHeight +
                    " glFboBlit=" + glFramebufferLastBlitSourceTextureName + ">" + glFramebufferLastBlitDestTextureName + ":" + glFramebufferLastBlitWidth + "x" + glFramebufferLastBlitHeight +
                    " glBufUploads=" + glBufferUploads +
                    " glBufMB=" + FormatMegabytes(glBufferBytes) +
                    " glAttribs=" + glVertexAttribs +
                    " glPrograms=" + glProgramUses +
                    " glUniforms=" + glUniforms +
                    " pid=" + (_context?.LaunchResult?.Process?.Id.ToString() ?? "?") +
                    (presented ? "" : " presentError=" + _presentation.LastError));
            }

            if (_javaSwapFlash && _frameCounter % 15 == 0)
            {
                _javaSwapFlash = false;
            }
        }

        private void AppendPresentationLog(string line)
        {
            try
            {
                LaunchLog.AppendSharedLine(_context?.LaunchResult?.LogPath, line);
            }
            catch
            {
                // The overlay is still the source of truth if the log file is locked.
            }
        }

        private static string FormatMegabytes(ulong bytes)
        {
            return (bytes / 1048576.0).ToString("0.00", System.Globalization.CultureInfo.InvariantCulture);
        }

        private uint BuildClearColor()
        {
            if (_javaSwapFlash)
            {
                return 0xFF2D6A4Fu;
            }

            var pulse = (Math.Sin(_frameCounter * 0.04) + 1.0) * 0.5;
            var blue = (byte)(16 + (int)(pulse * 40));
            return 0xFF000000u | blue;
        }
    }
}
