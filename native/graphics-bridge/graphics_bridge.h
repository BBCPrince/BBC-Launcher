#pragma once

#ifdef _WIN32
#define MGB_EXPORT extern "C" __declspec(dllexport)
#else
#define MGB_EXPORT extern "C"
#endif

// Returns 1 when a D3D12 device probe succeeds, otherwise 0.
MGB_EXPORT int mgb_create_d3d12_device_probe();
MGB_EXPORT int mgb_probe_mesa_egl_corewindow(const wchar_t* mesaDirectory, void* coreWindowUnknown, unsigned int width, unsigned int height);

// Returns a static, null-terminated ANSI string.
MGB_EXPORT const char* mgb_get_renderer_string();
MGB_EXPORT const char* mgb_get_version_string();
MGB_EXPORT const char* mgb_get_last_error_string();

// Milestone 2: presentation owned by the UWP launcher process.
// surfaceUnknown is normally a XAML SwapChainPanel IUnknown; CoreWindow is
// still accepted as a fallback for non-XAML hosts.
MGB_EXPORT int mgb_presentation_ensure_swap_event();
MGB_EXPORT int mgb_presentation_consume_swap_signal();
MGB_EXPORT int mgb_presentation_init(void* surfaceUnknown, unsigned int width, unsigned int height);
MGB_EXPORT int mgb_presentation_present(unsigned int rgba);
MGB_EXPORT unsigned long long mgb_presentation_get_present_count();
MGB_EXPORT unsigned long long mgb_presentation_get_java_swap_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_clear_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_viewport_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_draw_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_upload_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_upload_bytes();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_sample_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_sample_width();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_sample_height();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_present_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_frame_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_frame_width();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_frame_height();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_frame_present_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_textured_triangle_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_color_triangle_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_skipped_vertex_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_last_texture_name();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_last_texture_width();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_last_texture_height();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_accepted_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_rejected_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_source_texture_name();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_source_width();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_source_height();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_width();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_height();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_pixels();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_non_black_pixels();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_non_transparent_pixels();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_average_red();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_average_green();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_average_blue();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_reject_reason();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_accepted_source_texture_name();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_accepted_source_width();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_accepted_source_height();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_buffer_upload_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_buffer_upload_bytes();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_vertex_attrib_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_program_use_count();
MGB_EXPORT unsigned long long mgb_presentation_get_gl_uniform_count();
MGB_EXPORT void mgb_presentation_shutdown();
