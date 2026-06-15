using System;
using System.IO;
using System.Runtime.InteropServices;
using BBCLauncher.Configuration;
using Windows.ApplicationModel;
using Windows.Storage;

namespace BBCLauncher.Services
{
    public sealed class D3D12PresentationService : IDisposable
    {
        public const string JavaPresentEventName = @"Local\MinecraftXboxJavaPresent";

        private readonly LauncherConfig _config;
        private IntPtr _library = IntPtr.Zero;
        private bool _initialized;

        public D3D12PresentationService(LauncherConfig config)
        {
            _config = config;
        }

        public bool IsInitialized => _initialized;

        public static bool EnsureSwapEvent(LauncherConfig config, string storageRoot)
        {
            using (var service = new D3D12PresentationService(config))
            {
                return service.LoadBridge(storageRoot) && service.EnsureSwapEventInternal();
            }
        }

        public bool Initialize(IntPtr surfaceUnknown, int width, int height)
        {
            if (surfaceUnknown == IntPtr.Zero || width <= 0 || height <= 0)
            {
                LastError = "Invalid presentation surface or dimensions.";
                return false;
            }

            if (_library == IntPtr.Zero && !LoadBridge(ApplicationData.Current.LocalFolder.Path))
            {
                return false;
            }

            EnsureSwapEventInternal();
            var init = GetDelegate<PresentationInitDelegate>("mgb_presentation_init");
            var ok = init(surfaceUnknown, (uint)width, (uint)height) == 1;
            _initialized = ok;
            if (!ok)
            {
                LastError = ReadLastError();
            }

            return ok;
        }

        public bool Present(uint rgba)
        {
            if (!_initialized || _library == IntPtr.Zero)
            {
                return false;
            }

            var present = GetDelegate<PresentationPresentDelegate>("mgb_presentation_present");
            var ok = present(rgba) == 1;
            if (!ok)
            {
                LastError = ReadLastError();
            }

            return ok;
        }

        public bool ConsumeJavaSwapSignal()
        {
            if (_library == IntPtr.Zero)
            {
                return false;
            }

            var consume = GetDelegate<PresentationConsumeDelegate>("mgb_presentation_consume_swap_signal");
            return consume() == 1;
        }

        public ulong PresentCount => ReadCounter("mgb_presentation_get_present_count");

        public ulong JavaSwapCount => ReadCounter("mgb_presentation_get_java_swap_count");

        public ulong GlClearCount => ReadCounter("mgb_presentation_get_gl_clear_count");

        public ulong GlViewportCount => ReadCounter("mgb_presentation_get_gl_viewport_count");

        public ulong GlDrawCount => ReadCounter("mgb_presentation_get_gl_draw_count");

        public ulong GlTextureUploadCount => ReadCounter("mgb_presentation_get_gl_texture_upload_count");

        public ulong GlTextureUploadBytes => ReadCounter("mgb_presentation_get_gl_texture_upload_bytes");

        public ulong GlTextureSampleCount => ReadCounter("mgb_presentation_get_gl_texture_sample_count");

        public ulong GlTextureSampleWidth => ReadCounter("mgb_presentation_get_gl_texture_sample_width");

        public ulong GlTextureSampleHeight => ReadCounter("mgb_presentation_get_gl_texture_sample_height");

        public ulong GlTexturePresentCount => ReadCounter("mgb_presentation_get_gl_texture_present_count");

        public ulong GlGuiFrameCount => ReadCounter("mgb_presentation_get_gl_gui_frame_count");

        public ulong GlGuiFrameWidth => ReadCounter("mgb_presentation_get_gl_gui_frame_width");

        public ulong GlGuiFrameHeight => ReadCounter("mgb_presentation_get_gl_gui_frame_height");

        public ulong GlGuiFramePresentCount => ReadCounter("mgb_presentation_get_gl_gui_frame_present_count");

        public ulong GlGuiNonBlackPixels => ReadCounter("mgb_presentation_get_gl_gui_non_black_pixels");

        public ulong GlGuiNonTransparentPixels => ReadCounter("mgb_presentation_get_gl_gui_non_transparent_pixels");

        public ulong GlGuiAverageRed => ReadCounter("mgb_presentation_get_gl_gui_average_red");

        public ulong GlGuiAverageGreen => ReadCounter("mgb_presentation_get_gl_gui_average_green");

        public ulong GlGuiAverageBlue => ReadCounter("mgb_presentation_get_gl_gui_average_blue");

        public ulong GlGuiCenterRgb => ReadCounter("mgb_presentation_get_gl_gui_center_rgb");

        public ulong GlGuiTexturedTriangleCount => ReadCounter("mgb_presentation_get_gl_gui_textured_triangle_count");

        public ulong GlGuiColorTriangleCount => ReadCounter("mgb_presentation_get_gl_gui_color_triangle_count");

        public ulong GlGuiSkippedVertexCount => ReadCounter("mgb_presentation_get_gl_gui_skipped_vertex_count");

        public ulong GlGuiLastTextureName => ReadCounter("mgb_presentation_get_gl_gui_last_texture_name");

        public ulong GlGuiLastTextureWidth => ReadCounter("mgb_presentation_get_gl_gui_last_texture_width");

        public ulong GlGuiLastTextureHeight => ReadCounter("mgb_presentation_get_gl_gui_last_texture_height");

        public ulong GlGuiCandidateAcceptedCount => ReadCounter("mgb_presentation_get_gl_gui_candidate_accepted_count");

        public ulong GlGuiCandidateRejectedCount => ReadCounter("mgb_presentation_get_gl_gui_candidate_rejected_count");

        public ulong GlGuiCandidateSourceTextureName => ReadCounter("mgb_presentation_get_gl_gui_candidate_source_texture_name");

        public ulong GlGuiCandidateSourceWidth => ReadCounter("mgb_presentation_get_gl_gui_candidate_source_width");

        public ulong GlGuiCandidateSourceHeight => ReadCounter("mgb_presentation_get_gl_gui_candidate_source_height");

        public ulong GlGuiCandidateWidth => ReadCounter("mgb_presentation_get_gl_gui_candidate_width");

        public ulong GlGuiCandidateHeight => ReadCounter("mgb_presentation_get_gl_gui_candidate_height");

        public ulong GlGuiCandidatePixels => ReadCounter("mgb_presentation_get_gl_gui_candidate_pixels");

        public ulong GlGuiCandidateNonBlackPixels => ReadCounter("mgb_presentation_get_gl_gui_candidate_non_black_pixels");

        public ulong GlGuiCandidateNonTransparentPixels => ReadCounter("mgb_presentation_get_gl_gui_candidate_non_transparent_pixels");

        public ulong GlGuiCandidateAverageRed => ReadCounter("mgb_presentation_get_gl_gui_candidate_average_red");

        public ulong GlGuiCandidateAverageGreen => ReadCounter("mgb_presentation_get_gl_gui_candidate_average_green");

        public ulong GlGuiCandidateAverageBlue => ReadCounter("mgb_presentation_get_gl_gui_candidate_average_blue");

        public ulong GlGuiCandidateRejectReason => ReadCounter("mgb_presentation_get_gl_gui_candidate_reject_reason");

        public ulong GlGuiCandidateAcceptedSourceTextureName => ReadCounter("mgb_presentation_get_gl_gui_candidate_accepted_source_texture_name");

        public ulong GlGuiCandidateAcceptedSourceWidth => ReadCounter("mgb_presentation_get_gl_gui_candidate_accepted_source_width");

        public ulong GlGuiCandidateAcceptedSourceHeight => ReadCounter("mgb_presentation_get_gl_gui_candidate_accepted_source_height");

        public ulong GlTextureLastStoredName => ReadCounter("mgb_presentation_get_gl_texture_last_stored_name");

        public ulong GlTextureLastStoredWidth => ReadCounter("mgb_presentation_get_gl_texture_last_stored_width");

        public ulong GlTextureLastStoredHeight => ReadCounter("mgb_presentation_get_gl_texture_last_stored_height");

        public ulong GlTextureLargestName => ReadCounter("mgb_presentation_get_gl_texture_largest_name");

        public ulong GlTextureLargestWidth => ReadCounter("mgb_presentation_get_gl_texture_largest_width");

        public ulong GlTextureLargestHeight => ReadCounter("mgb_presentation_get_gl_texture_largest_height");

        public ulong GlTextureBestGuiName => ReadCounter("mgb_presentation_get_gl_texture_best_gui_name");

        public ulong GlTextureBestGuiWidth => ReadCounter("mgb_presentation_get_gl_texture_best_gui_width");

        public ulong GlTextureBestGuiHeight => ReadCounter("mgb_presentation_get_gl_texture_best_gui_height");

        public ulong GlTextureRecordCount => ReadCounter("mgb_presentation_get_gl_texture_record_count");

        public ulong GlTextureTableFullCount => ReadCounter("mgb_presentation_get_gl_texture_table_full_count");

        public ulong GlTextureExactGuiName => ReadCounter("mgb_presentation_get_gl_texture_exact_gui_name");

        public ulong GlTextureExactGuiWidth => ReadCounter("mgb_presentation_get_gl_texture_exact_gui_width");

        public ulong GlTextureExactGuiHeight => ReadCounter("mgb_presentation_get_gl_texture_exact_gui_height");

        public ulong GlTextureExactGuiHasPixels => ReadCounter("mgb_presentation_get_gl_texture_exact_gui_has_pixels");

        public ulong GlTextureBestAllocatedGuiName => ReadCounter("mgb_presentation_get_gl_texture_best_allocated_gui_name");

        public ulong GlTextureBestAllocatedGuiWidth => ReadCounter("mgb_presentation_get_gl_texture_best_allocated_gui_width");

        public ulong GlTextureBestAllocatedGuiHeight => ReadCounter("mgb_presentation_get_gl_texture_best_allocated_gui_height");

        public ulong GlTextureLastAllocationName => ReadCounter("mgb_presentation_get_gl_texture_last_allocation_name");

        public ulong GlTextureLastAllocationWidth => ReadCounter("mgb_presentation_get_gl_texture_last_allocation_width");

        public ulong GlTextureLastAllocationHeight => ReadCounter("mgb_presentation_get_gl_texture_last_allocation_height");

        public ulong GlTextureShrinkPreservedCount => ReadCounter("mgb_presentation_get_gl_texture_shrink_preserved_count");

        public ulong GlTextureLastAttemptName => ReadCounter("mgb_presentation_get_gl_texture_last_attempt_name");

        public ulong GlTextureLastAttemptLevel => ReadCounter("mgb_presentation_get_gl_texture_last_attempt_level");

        public ulong GlTextureLastAttemptX => ReadCounter("mgb_presentation_get_gl_texture_last_attempt_x");

        public ulong GlTextureLastAttemptY => ReadCounter("mgb_presentation_get_gl_texture_last_attempt_y");

        public ulong GlTextureLastAttemptWidth => ReadCounter("mgb_presentation_get_gl_texture_last_attempt_width");

        public ulong GlTextureLastAttemptHeight => ReadCounter("mgb_presentation_get_gl_texture_last_attempt_height");

        public ulong GlTextureLastAttemptFormat => ReadCounter("mgb_presentation_get_gl_texture_last_attempt_format");

        public ulong GlTextureLastAttemptType => ReadCounter("mgb_presentation_get_gl_texture_last_attempt_type");

        public ulong GlTextureLastAttemptReason => ReadCounter("mgb_presentation_get_gl_texture_last_attempt_reason");

        public ulong GlTextureLastAttemptPbo => ReadCounter("mgb_presentation_get_gl_texture_last_attempt_pbo");

        public ulong GlTextureLastAttemptUnit => ReadCounter("mgb_presentation_get_gl_texture_last_attempt_unit");

        public ulong GlTextureExactGuiUploadAttemptCount => ReadCounter("mgb_presentation_get_gl_texture_exact_gui_upload_attempt_count");

        public ulong GlTextureExactGuiUploadAcceptedCount => ReadCounter("mgb_presentation_get_gl_texture_exact_gui_upload_accepted_count");

        public ulong GlTextureExactGuiUploadRejectedCount => ReadCounter("mgb_presentation_get_gl_texture_exact_gui_upload_rejected_count");

        public ulong GlTextureExactGuiLastRejectReason => ReadCounter("mgb_presentation_get_gl_texture_exact_gui_last_reject_reason");

        public ulong GlFramebufferBindCount => ReadCounter("mgb_presentation_get_gl_framebuffer_bind_count");

        public ulong GlFramebufferAttachCount => ReadCounter("mgb_presentation_get_gl_framebuffer_attach_count");

        public ulong GlFramebufferBlitCount => ReadCounter("mgb_presentation_get_gl_framebuffer_blit_count");

        public ulong GlFramebufferDrawName => ReadCounter("mgb_presentation_get_gl_framebuffer_draw_name");

        public ulong GlFramebufferReadName => ReadCounter("mgb_presentation_get_gl_framebuffer_read_name");

        public ulong GlFramebufferColorTextureName => ReadCounter("mgb_presentation_get_gl_framebuffer_color_texture_name");

        public ulong GlFramebufferColorTextureWidth => ReadCounter("mgb_presentation_get_gl_framebuffer_color_texture_width");

        public ulong GlFramebufferColorTextureHeight => ReadCounter("mgb_presentation_get_gl_framebuffer_color_texture_height");

        public ulong GlFramebufferLastBlitSourceTextureName => ReadCounter("mgb_presentation_get_gl_framebuffer_last_blit_source_texture_name");

        public ulong GlFramebufferLastBlitDestTextureName => ReadCounter("mgb_presentation_get_gl_framebuffer_last_blit_dest_texture_name");

        public ulong GlFramebufferLastBlitWidth => ReadCounter("mgb_presentation_get_gl_framebuffer_last_blit_width");

        public ulong GlFramebufferLastBlitHeight => ReadCounter("mgb_presentation_get_gl_framebuffer_last_blit_height");

        public ulong GlBufferUploadCount => ReadCounter("mgb_presentation_get_gl_buffer_upload_count");

        public ulong GlBufferUploadBytes => ReadCounter("mgb_presentation_get_gl_buffer_upload_bytes");

        public ulong GlVertexAttribCount => ReadCounter("mgb_presentation_get_gl_vertex_attrib_count");

        public ulong GlProgramUseCount => ReadCounter("mgb_presentation_get_gl_program_use_count");

        public ulong GlUniformCount => ReadCounter("mgb_presentation_get_gl_uniform_count");

        public string LastError { get; private set; }

        public void Dispose()
        {
            if (_library != IntPtr.Zero)
            {
                try
                {
                    if (_initialized)
                    {
                        var shutdown = GetDelegate<PresentationShutdownDelegate>("mgb_presentation_shutdown");
                        shutdown();
                    }
                }
                catch
                {
                    // Best effort during page teardown.
                }

                NativeLibrary.Free(_library);
                _library = IntPtr.Zero;
            }

            _initialized = false;
        }

        private bool LoadBridge(string storageRoot)
        {
            if (_library != IntPtr.Zero)
            {
                return true;
            }

            var dllPath = ResolveBridgePath(storageRoot);
            if (!File.Exists(dllPath))
            {
                LastError = "Bridge DLL missing: " + dllPath;
                return false;
            }

            try
            {
                _library = NativeLibrary.Load(dllPath);
                return true;
            }
            catch (Exception ex)
            {
                LastError = "Bridge load failed: " + ex.Message;
                return false;
            }
        }

        private bool EnsureSwapEventInternal()
        {
            var ensure = GetDelegate<PresentationEnsureEventDelegate>("mgb_presentation_ensure_swap_event");
            var ok = ensure() == 1;
            if (!ok)
            {
                LastError = ReadLastError();
            }

            return ok;
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
                // Package.Current unavailable in some hosts.
            }

            return localPath;
        }

        private string ReadLastError()
        {
            var getLastError = GetDelegate<GetStringDelegate>("mgb_get_last_error_string");
            return PtrToStringSafe(getLastError());
        }

        private ulong ReadCounter(string exportName)
        {
            if (_library == IntPtr.Zero)
            {
                return 0;
            }

            var getCounter = GetDelegate<PresentationCounterDelegate>(exportName);
            return getCounter();
        }

        private TDelegate GetDelegate<TDelegate>(string exportName)
            where TDelegate : Delegate
        {
            var export = NativeLibrary.GetExport(_library, exportName);
            return Marshal.GetDelegateForFunctionPointer<TDelegate>(export);
        }

        private static string PtrToStringSafe(IntPtr ptr)
        {
            return ptr == IntPtr.Zero ? string.Empty : Marshal.PtrToStringAnsi(ptr) ?? string.Empty;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int PresentationEnsureEventDelegate();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int PresentationConsumeDelegate();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int PresentationInitDelegate(IntPtr surfaceUnknown, uint width, uint height);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int PresentationPresentDelegate(uint rgba);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate ulong PresentationCounterDelegate();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void PresentationShutdownDelegate();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr GetStringDelegate();
    }
}
