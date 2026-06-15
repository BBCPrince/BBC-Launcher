// xbox_glfw.cpp
//
// Stub implementation of GLFW 3.3.x for Xbox UWP.
//
// Why this exists:
//   GLFW's bundled Win32 build (shipped inside lwjgl-glfw-natives-windows.jar)
//   calls RegisterClassExW during glfwInit() to register a helper window class.
//   That API is not supported in the Xbox UWP runtime (user32 there is a
//   OneCore forwarder shim that returns ERROR_CALL_NOT_IMPLEMENTED).  LWJGL
//   surfaces this as:
//
//     IllegalStateException: Failed to initialize GLFW, errors:
//       [0x10008]Win32: Failed to register helper window class:
//       This function is not supported on this system.
//
// Replacing glfw.dll with this DLL lets LWJGL load successfully and lets us
// see exactly which GLFW symbols Minecraft actually depends on before we
// invest in a real CoreWindow-backed implementation.
//
// IMPORTANT - this is intentionally a stub:
//   * No window is created.
//   * No D3D12 / OpenGL context is created.
//   * No real input is delivered (callbacks are stored but never fired).
//   * Rendering will fail downstream once Mojang tries to call OpenGL via the
//     null pointer returned by glfwGetProcAddress.  That's the expected
//     "next crash" - it tells us where milestone 2 has to focus.
//
// Goals for the stub:
//   * Every one of the 140 symbols exported by the real glfw.dll is exported
//     here, so LWJGL's GetProcAddress lookups never return null.
//   * glfwInit returns success and glfwCreateWindow returns a non-null
//     opaque handle so Mojang clears the early-init phase.
//   * Reasonable default state is reported (1920x1080, 60 Hz, single Xbox
//     monitor, 1.0 content scale) so any Mojang code path that *reads* state
//     during init keeps marching.

#include "../xbox-gl-command-state.h"

#include <windows.h>
#include <Unknwn.h>
#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.applicationmodel.core.h>
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <windows.gaming.input.h>
#include <windows.ui.core.h>
#include <windows.ui.input.h>
#include <windows.system.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <atomic>
#include <mutex>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::Gaming::Input;
using namespace ABI::Windows::System;
using namespace ABI::Windows::UI::Core;

// Templates cannot have C linkage (MSVC C2894), so this helper lives at file
// scope and is invoked from the extern "C" exports via the macro below.
namespace xglfw {
    template <typename TFn>
    TFn SwapCallback(TFn& slot, TFn newCb) {
        TFn previous = slot;
        slot = newCb;
        return previous;
    }
}
#define XSWAP(slot, cb) xglfw::SwapCallback((slot), (cb))

extern "C" {

#define XGLFW_API extern "C" __declspec(dllexport)

typedef struct GLFWmonitor GLFWmonitor;
typedef struct GLFWwindow  GLFWwindow;
typedef struct GLFWcursor  GLFWcursor;

typedef void (*GLFWglproc)(void);
typedef void (*GLFWvkproc)(void);

typedef void (*GLFWerrorfun)(int error_code, const char* description);
typedef void (*GLFWwindowposfun)(GLFWwindow* window, int xpos, int ypos);
typedef void (*GLFWwindowsizefun)(GLFWwindow* window, int width, int height);
typedef void (*GLFWwindowclosefun)(GLFWwindow* window);
typedef void (*GLFWwindowrefreshfun)(GLFWwindow* window);
typedef void (*GLFWwindowfocusfun)(GLFWwindow* window, int focused);
typedef void (*GLFWwindowiconifyfun)(GLFWwindow* window, int iconified);
typedef void (*GLFWwindowmaximizefun)(GLFWwindow* window, int maximized);
typedef void (*GLFWframebuffersizefun)(GLFWwindow* window, int width, int height);
typedef void (*GLFWwindowcontentscalefun)(GLFWwindow* window, float xscale, float yscale);
typedef void (*GLFWmousebuttonfun)(GLFWwindow* window, int button, int action, int mods);
typedef void (*GLFWcursorposfun)(GLFWwindow* window, double xpos, double ypos);
typedef void (*GLFWcursorenterfun)(GLFWwindow* window, int entered);
typedef void (*GLFWscrollfun)(GLFWwindow* window, double xoffset, double yoffset);
typedef void (*GLFWkeyfun)(GLFWwindow* window, int key, int scancode, int action, int mods);
typedef void (*GLFWcharfun)(GLFWwindow* window, unsigned int codepoint);
typedef void (*GLFWcharmodsfun)(GLFWwindow* window, unsigned int codepoint, int mods);
typedef void (*GLFWdropfun)(GLFWwindow* window, int path_count, const char* paths[]);
typedef void (*GLFWmonitorfun)(GLFWmonitor* monitor, int event);
typedef void (*GLFWjoystickfun)(int jid, int event);

typedef struct GLFWvidmode {
    int width;
    int height;
    int redBits;
    int greenBits;
    int blueBits;
    int refreshRate;
} GLFWvidmode;

typedef struct GLFWgammaramp {
    unsigned short* red;
    unsigned short* green;
    unsigned short* blue;
    unsigned int size;
} GLFWgammaramp;

typedef struct GLFWimage {
    int width;
    int height;
    unsigned char* pixels;
} GLFWimage;

typedef struct GLFWgamepadstate {
    unsigned char buttons[15];
    float axes[6];
} GLFWgamepadstate;

typedef void* (*GLFWallocatefun)(size_t size, void* user);
typedef void* (*GLFWreallocatefun)(void* block, size_t size, void* user);
typedef void  (*GLFWdeallocatefun)(void* block, void* user);

typedef struct GLFWallocator {
    GLFWallocatefun  allocate;
    GLFWreallocatefun reallocate;
    GLFWdeallocatefun deallocate;
    void* user;
} GLFWallocator;

// Vulkan types treated as opaque so we don't pull in vulkan headers.
typedef void* VkInstance;
typedef void* VkPhysicalDevice;
typedef uint64_t VkSurfaceKHR;
typedef int VkResult;
typedef void* PFN_vkGetInstanceProcAddr;
struct VkAllocationCallbacks;

#include "glfw_constants_generated.h"

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

static std::atomic<int> g_initialized{ 0 };
static std::atomic<int> g_last_error_code{ GLFW_NO_ERROR };

// Sentinel handles. We never dereference them; LWJGL/Mojang only need
// non-null comparisons to keep going.
static GLFWwindow*  kPrimaryWindow  = reinterpret_cast<GLFWwindow*>(static_cast<uintptr_t>(0x7878787800000001ull));
static GLFWmonitor* kPrimaryMonitor = reinterpret_cast<GLFWmonitor*>(static_cast<uintptr_t>(0x7878787800000002ull));
static GLFWcursor*  kSharedCursor   = reinterpret_cast<GLFWcursor*>(static_cast<uintptr_t>(0x7878787800000003ull));

static const int kDefaultWidth = 1280;
static const int kDefaultHeight = 720;

static int QueryLaunchDimension(const wchar_t* name, int fallback) {
    wchar_t buffer[32] = {};
    DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return fallback;
    }

    int value = _wtoi(buffer);
    return value > 0 ? value : fallback;
}

static bool IsTruthyEnvironment(const wchar_t* name) {
    wchar_t buffer[16] = {};
    DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    return length > 0 &&
        length < std::size(buffer) &&
        buffer[0] != L'\0' &&
        buffer[0] != L'0' &&
        _wcsicmp(buffer, L"false") != 0 &&
        _wcsicmp(buffer, L"no") != 0;
}

static bool QueryEnvironmentInt(const wchar_t* name, int& value) {
    wchar_t buffer[32] = {};
    DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return false;
    }

    int parsed = _wtoi(buffer);
    if (parsed < 0) {
        return false;
    }

    value = parsed;
    return true;
}

static const int kDefaultRefresh = 60;
static const char* kMonitorName = "Xbox Display";
static const char* kVersionString = "3.3.8 Xbox-gles-runtime-1.0.123";

// WinRT ABI interface IDs Mesa expects as native window objects.  Managed
// Marshal.GetIUnknownForObject gives us COM identity pointers, not necessarily
// these exact interface vtables, so query before handing them to EGL.
static const IID kIidCoreWindow = {
    0x79b9d5f2, 0x879e, 0x4b89, { 0xb7, 0x98, 0x79, 0xe4, 0x75, 0x98, 0x03, 0x0c }
};
static const IID kIidPropertySetMap = {
    0x1b0d3570, 0x0877, 0x5ec2, { 0x8a, 0x2c, 0x3b, 0x95, 0x39, 0x50, 0x6a, 0xca }
};
static const IID kIidInspectable = {
    0xaf86e2e0, 0xb12d, 0x4c6a, { 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90 }
};
static constexpr wchar_t kEglNativeWindowTypeProperty[] = L"EGLNativeWindowTypeProperty";
static constexpr wchar_t kEglRenderSurfaceSizeProperty[] = L"EGLRenderSurfaceSizeProperty";

using EGLDisplay = void*;
using EGLConfig = void*;
using EGLSurface = void*;
using EGLContext = void*;
using EGLBoolean = unsigned int;
using EGLenum = unsigned int;
using EGLint = int;

static constexpr EGLDisplay EGL_NO_DISPLAY = nullptr;
static constexpr EGLSurface EGL_NO_SURFACE = nullptr;
static constexpr EGLContext EGL_NO_CONTEXT = nullptr;
static constexpr EGLint EGL_FALSE_VALUE = 0;
static constexpr EGLint EGL_TRUE_VALUE = 1;
static constexpr EGLint EGL_NONE_VALUE = 0x3038;
static constexpr EGLint EGL_PBUFFER_BIT_VALUE = 0x0001;
static constexpr EGLint EGL_WINDOW_BIT_VALUE = 0x0004;
static constexpr EGLint EGL_OPENGL_BIT_VALUE = 0x0008;
static constexpr EGLint EGL_OPENGL_ES3_BIT_VALUE = 0x00000040;
static constexpr EGLint EGL_SURFACE_TYPE_VALUE = 0x3033;
static constexpr EGLint EGL_RENDERABLE_TYPE_VALUE = 0x3040;
static constexpr EGLint EGL_RED_SIZE_VALUE = 0x3024;
static constexpr EGLint EGL_GREEN_SIZE_VALUE = 0x3023;
static constexpr EGLint EGL_BLUE_SIZE_VALUE = 0x3022;
static constexpr EGLint EGL_ALPHA_SIZE_VALUE = 0x3021;
static constexpr EGLint EGL_DEPTH_SIZE_VALUE = 0x3025;
static constexpr EGLint EGL_STENCIL_SIZE_VALUE = 0x3026;
static constexpr EGLint EGL_WIDTH_VALUE = 0x3057;
static constexpr EGLint EGL_HEIGHT_VALUE = 0x3056;
static constexpr EGLint EGL_CONTEXT_MAJOR_VERSION_VALUE = 0x3098;
static constexpr EGLint EGL_CONTEXT_MINOR_VERSION_VALUE = 0x30FB;
static constexpr EGLint EGL_CONTEXT_OPENGL_PROFILE_MASK_VALUE = 0x30FD;
static constexpr EGLint EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_VALUE = 0x00000001;
static constexpr EGLint EGL_CONTEXT_CLIENT_VERSION_VALUE = 0x3098;
static constexpr EGLint EGL_VENDOR_VALUE = 0x3053;
static constexpr EGLint EGL_VERSION_VALUE = 0x3054;
static constexpr EGLint EGL_EXTENSIONS_VALUE = 0x3055;
static constexpr EGLint EGL_CLIENT_APIS_VALUE = 0x308D;
static constexpr EGLenum EGL_OPENGL_API_VALUE = 0x30A2;
static constexpr EGLenum EGL_OPENGL_ES_API_VALUE = 0x30A0;
static constexpr EGLenum EGL_PLATFORM_SURFACELESS_MESA_VALUE = 0x31DD;
static constexpr unsigned int GL_RGBA_VALUE = 0x1908;
static constexpr unsigned int GL_UNSIGNED_BYTE_VALUE = 0x1401;

typedef EGLDisplay(__stdcall* EglGetDisplayProc)(void*);
typedef EGLDisplay(__stdcall* EglGetPlatformDisplayProc)(EGLenum, void*, const intptr_t*);
typedef EGLDisplay(__stdcall* EglGetPlatformDisplayExtProc)(EGLenum, void*, const EGLint*);
typedef EGLBoolean(__stdcall* EglInitializeProc)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean(__stdcall* EglGetConfigsProc)(EGLDisplay, EGLConfig*, EGLint, EGLint*);
typedef EGLBoolean(__stdcall* EglChooseConfigProc)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
typedef EGLBoolean(__stdcall* EglBindApiProc)(EGLenum);
typedef EGLSurface(__stdcall* EglCreatePbufferSurfaceProc)(EGLDisplay, EGLConfig, const EGLint*);
typedef EGLSurface(__stdcall* EglCreateWindowSurfaceProc)(EGLDisplay, EGLConfig, void*, const EGLint*);
typedef EGLSurface(__stdcall* EglCreatePlatformWindowSurfaceProc)(EGLDisplay, EGLConfig, void*, const intptr_t*);
typedef EGLSurface(__stdcall* EglCreatePlatformWindowSurfaceExtProc)(EGLDisplay, EGLConfig, void*, const EGLint*);
typedef EGLContext(__stdcall* EglCreateContextProc)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
typedef EGLBoolean(__stdcall* EglMakeCurrentProc)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLContext(__stdcall* EglGetCurrentContextProc)(void);
typedef EGLBoolean(__stdcall* EglSwapBuffersProc)(EGLDisplay, EGLSurface);
typedef EGLBoolean(__stdcall* EglSwapIntervalProc)(EGLDisplay, EGLint);
typedef EGLBoolean(__stdcall* EglDestroyContextProc)(EGLDisplay, EGLContext);
typedef EGLBoolean(__stdcall* EglDestroySurfaceProc)(EGLDisplay, EGLSurface);
typedef EGLBoolean(__stdcall* EglTerminateProc)(EGLDisplay);
typedef EGLint(__stdcall* EglGetErrorProc)(void);
typedef void* (__stdcall* EglGetProcAddressProc)(const char*);
typedef const char* (__stdcall* EglQueryStringProc)(EGLDisplay, EGLint);
typedef void(__stdcall* GlReadPixelsProc)(int, int, int, int, unsigned int, unsigned int, void*);
typedef void(__cdecl* OpenGlProcInitProc)(void);
typedef int(__stdcall* MesaUwpEnsureWglInitializedProc)(void);
typedef const char* (__cdecl* MesaUwpGetWglInitStageProc)(void);
typedef const char* (__cdecl* MesaUwpGetWglInitHistoryProc)(void);
typedef const char* (__cdecl* MesaUwpGetWglInitDetailProc)(void);
typedef void* (__cdecl* MesaStwGetDeviceProc)(void);
typedef bool(__cdecl* MesaStwInitScreenProc)(void*);

struct WindowState {
    int shouldClose = 0;
    void* userPointer = nullptr;
    GLFWwindowposfun cbPos = nullptr;
    GLFWwindowsizefun cbSize = nullptr;
    GLFWwindowclosefun cbClose = nullptr;
    GLFWwindowrefreshfun cbRefresh = nullptr;
    GLFWwindowfocusfun cbFocus = nullptr;
    GLFWwindowiconifyfun cbIconify = nullptr;
    GLFWwindowmaximizefun cbMaximize = nullptr;
    GLFWframebuffersizefun cbFramebuffer = nullptr;
    GLFWwindowcontentscalefun cbContentScale = nullptr;
    GLFWmousebuttonfun cbMouseButton = nullptr;
    GLFWcursorposfun cbCursorPos = nullptr;
    GLFWcursorenterfun cbCursorEnter = nullptr;
    GLFWscrollfun cbScroll = nullptr;
    GLFWkeyfun cbKey = nullptr;
    GLFWcharfun cbChar = nullptr;
    GLFWcharmodsfun cbCharMods = nullptr;
    GLFWdropfun cbDrop = nullptr;
};
static WindowState g_window;

static GLFWerrorfun    g_cbError = nullptr;
static GLFWmonitorfun  g_cbMonitor = nullptr;
static GLFWjoystickfun g_cbJoystick = nullptr;
static void* g_gamepadUserPointer = nullptr;
static std::mutex g_gamepadStaticsMutex;
static ComPtr<IGamepadStatics> g_gamepadStatics;
static float g_gamepadAxes[GLFW_GAMEPAD_AXIS_LAST + 1] = {};
static unsigned char g_gamepadButtons[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
static unsigned char g_gamepadHats[1] = { GLFW_HAT_CENTERED };
static std::atomic<int> g_lastGamepadPresent{ -1 };
static std::atomic<int> g_gamepadMouseMode{ 0 };
static std::atomic<int> g_gamepadMouseToggleLatch{ 0 };
static std::atomic<unsigned long long> g_gamepadMouseLastTickMs{ 0 };
static std::atomic<unsigned long long> g_gamepadMouseLastWheelMs{ 0 };
static std::atomic<int> g_gamepadMouseEventLogCount{ 0 };

static std::atomic<int> g_window_created{ 0 };
static std::atomic<int> g_window_hint_client_api{ 0x00030001 }; // GLFW_OPENGL_API
static std::atomic<int> g_window_hint_context_major{ 3 };
static std::atomic<int> g_window_hint_context_minor{ 2 };
static std::atomic<unsigned char> g_keyStates[GLFW_KEY_LAST + 1] = {};
static std::atomic<unsigned char> g_mouseButtonStates[GLFW_MOUSE_BUTTON_LAST + 1] = {};

static LARGE_INTEGER g_qpcFreq;
static LARGE_INTEGER g_qpcStart;
static double g_timeOffset = 0.0;

namespace {
    static HMODULE g_openGlModule = nullptr;
    static HDC g_wglDc = nullptr;
    static HBITMAP g_wglBitmap = nullptr;
    static HGDIOBJ g_wglOldBitmap = nullptr;
    static bool g_wglSyntheticDc = false;
    static HGLRC g_wglContext = nullptr;
    static std::atomic<int> g_contextCurrent{ 0 };
    static std::atomic<int> g_wglDisabled{ 0 };
    static std::atomic<int> g_eglDisabled{ 0 };
    static std::atomic<int> g_eglCoreWindowSurfaceDisabled{ 0 };

    using WglChoosePixelFormatProc = int (WINAPI *)(HDC, const PIXELFORMATDESCRIPTOR*);
    using WglSetPixelFormatProc = BOOL (WINAPI *)(HDC, int, const PIXELFORMATDESCRIPTOR*);
    using WglCreateContextProc = HGLRC (WINAPI *)(HDC);
    using WglDeleteContextProc = BOOL (WINAPI *)(HGLRC);
    using WglMakeCurrentProc = BOOL (WINAPI *)(HDC, HGLRC);
    using WglGetCurrentContextProc = HGLRC (WINAPI *)();
    using WglGetProcAddressProc = PROC (WINAPI *)(LPCSTR);
    using WglSwapBuffersProc = BOOL (WINAPI *)(HDC);

    static WglChoosePixelFormatProc g_wglChoosePixelFormat = nullptr;
    static WglSetPixelFormatProc g_wglSetPixelFormat = nullptr;
    static WglCreateContextProc g_wglCreateContext = nullptr;
    static WglDeleteContextProc g_wglDeleteContext = nullptr;
    static WglMakeCurrentProc g_wglMakeCurrent = nullptr;
    static WglGetCurrentContextProc g_wglGetCurrentContext = nullptr;
    static WglGetProcAddressProc g_wglGetProcAddress = nullptr;
    static WglSwapBuffersProc g_wglSwapBuffers = nullptr;
    static HMODULE g_eglModule = nullptr;
    static HMODULE g_glesModule = nullptr;
    static EGLDisplay g_eglDisplay = EGL_NO_DISPLAY;
    static EGLConfig g_eglConfig = nullptr;
    static EGLSurface g_eglSurface = EGL_NO_SURFACE;
    static EGLContext g_eglContext = EGL_NO_CONTEXT;
    static std::atomic<int> g_eglSurfacelessContext{ 0 };
    static const char* g_eglSurfacePath = "none";
    static EglGetDisplayProc g_eglGetDisplay = nullptr;
    static EglGetPlatformDisplayProc g_eglGetPlatformDisplay = nullptr;
    static EglGetPlatformDisplayExtProc g_eglGetPlatformDisplayEXT = nullptr;
    static EglInitializeProc g_eglInitialize = nullptr;
    static EglGetConfigsProc g_eglGetConfigs = nullptr;
    static EglChooseConfigProc g_eglChooseConfig = nullptr;
    static EglBindApiProc g_eglBindApi = nullptr;
    static EglCreatePbufferSurfaceProc g_eglCreatePbufferSurface = nullptr;
    static EglCreateWindowSurfaceProc g_eglCreateWindowSurface = nullptr;
    static EglCreatePlatformWindowSurfaceProc g_eglCreatePlatformWindowSurface = nullptr;
    static EglCreatePlatformWindowSurfaceExtProc g_eglCreatePlatformWindowSurfaceEXT = nullptr;
    static EglCreateContextProc g_eglCreateContext = nullptr;
    static EglMakeCurrentProc g_eglMakeCurrent = nullptr;
    static EglGetCurrentContextProc g_eglGetCurrentContext = nullptr;
    static EglSwapBuffersProc g_eglSwapBuffers = nullptr;
    static EglSwapIntervalProc g_eglSwapInterval = nullptr;
    static EglDestroyContextProc g_eglDestroyContext = nullptr;
    static EglDestroySurfaceProc g_eglDestroySurface = nullptr;
    static EglTerminateProc g_eglTerminate = nullptr;
    static EglGetErrorProc g_eglGetError = nullptr;
    static EglGetProcAddressProc g_eglGetProcAddress = nullptr;
    static EglQueryStringProc g_eglQueryString = nullptr;
    static GlReadPixelsProc g_glReadPixels = nullptr;
    static HANDLE g_glCommandStateMapping = nullptr;
    static MinecraftXboxGlCommandState* g_glCommandState = nullptr;
    static unsigned char* g_eglReadback = nullptr;
    static size_t g_eglReadbackBytes = 0;
    static int g_eglWidth = 0;
    static int g_eglHeight = 0;
    static std::atomic<int> g_requestedSwapInterval{ 1 };
    static IUnknown* g_coreWindowUnknown = nullptr;
    static IUnknown* g_eglWindowDescriptorUnknown = nullptr;
    static std::mutex g_coreWindowMutex;
    static std::atomic<int> g_mesaWglScreenInitResult{ -1 };

    static int EglErrorCode();
    static int LogEglRecoverableException(const char* call, unsigned long code);

    static void DebugLine(const char* text) {
        OutputDebugStringA("[xbox-glfw] ");
        OutputDebugStringA(text);
        OutputDebugStringA("\n");

        char stderrLine[2400] = {};
        int stderrWritten = std::snprintf(stderrLine, sizeof(stderrLine), "[Native][xbox-glfw] %s\r\n", text);
        if (stderrWritten > 0) {
            HANDLE stderrHandle = GetStdHandle(STD_ERROR_HANDLE);
            if (stderrHandle != INVALID_HANDLE_VALUE && stderrHandle != nullptr) {
                DWORD bytes = 0;
                WriteFile(stderrHandle, stderrLine, static_cast<DWORD>(stderrWritten), &bytes, nullptr);
            }
        }

        wchar_t path[1024] = {};
        DWORD len = GetEnvironmentVariableW(
            L"MINECRAFT_XBOX_NATIVE_LOG",
            path,
            static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
        if (len == 0 || len >= (sizeof(path) / sizeof(path[0]))) {
            return;
        }

        HANDLE file = CreateFileW(
            path,
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }

        char line[2400] = {};
        int written = std::snprintf(line, sizeof(line), "[Native][xbox-glfw] %s\r\n", text);
        if (written > 0) {
            DWORD bytes = 0;
            WriteFile(file, line, static_cast<DWORD>(written), &bytes, nullptr);
            FlushFileBuffers(file);
        }
        CloseHandle(file);
    }

    static int QueryMonitorRefreshRate() {
        int refresh = kDefaultRefresh;
        QueryEnvironmentInt(L"MINECRAFT_XBOX_REFRESH_RATE", refresh);
        if (refresh < 24) {
            refresh = kDefaultRefresh;
        }
        if (refresh > 240) {
            refresh = 240;
        }
        return refresh;
    }

    static int QueryForcedSwapInterval(int fallback) {
        int forced = fallback;
        if (!QueryEnvironmentInt(L"MINECRAFT_XBOX_FORCE_SWAP_INTERVAL", forced)) {
            return fallback;
        }
        return forced > 0 ? 1 : 0;
    }

    static bool ApplyMesaEglSwapInterval(const char* reason) {
        if (!g_eglSwapInterval || g_eglDisplay == EGL_NO_DISPLAY) {
            return false;
        }

        int requested = g_requestedSwapInterval.load(std::memory_order_acquire);
        int interval = QueryForcedSwapInterval(requested);
        EGLBoolean ok = EGL_FALSE_VALUE;
        __try {
            ok = g_eglSwapInterval(g_eglDisplay, static_cast<EGLint>(interval));
        }
        __except (LogEglRecoverableException("eglSwapInterval", GetExceptionCode())) {
            ok = EGL_FALSE_VALUE;
        }

        char line[192] = {};
        std::snprintf(
            line,
            sizeof(line),
            "eglSwapInterval(%d) reason=%s result=%d err=0x%04x requested=%d",
            interval,
            reason ? reason : "<unknown>",
            ok ? 1 : 0,
            EglErrorCode(),
            requested);
        DebugLine(line);
        return ok != EGL_FALSE_VALUE;
    }

    static MinecraftXboxGlCommandState* EnsureGlCommandState();

    static LONG64 ReadVolatile64(volatile LONG64 const* value) {
        return InterlockedCompareExchange64(const_cast<volatile LONG64*>(value), 0, 0);
    }

    static LONG64 DeltaNonNegative(LONG64 current, LONG64 previous) {
        return current >= previous ? current - previous : 0;
    }

    struct GlFrameCounterSnapshot {
        bool ready = false;
        LONG64 drawSerial = 0;
        LONG64 textureUploadSerial = 0;
        LONG64 textureSubUploadSerial = 0;
        LONG64 textureUploadBytes = 0;
        LONG64 bufferUploadSerial = 0;
        LONG64 bufferSubUploadSerial = 0;
        LONG64 bufferUploadBytes = 0;
        LONG64 framebufferBindSerial = 0;
        LONG64 framebufferAttachSerial = 0;
        LONG64 framebufferBlitSerial = 0;
        LONG64 programUseSerial = 0;
        LONG64 uniformSerial = 0;
        LONG64 flushSerial = 0;
        LONG64 textureBindSerial = 0;
        LONG64 drawCallTimeUs = 0;
        LONG64 textureUploadCallTimeUs = 0;
        LONG64 bufferUploadCallTimeUs = 0;
        LONG64 programUseCallTimeUs = 0;
        LONG64 uniformCallTimeUs = 0;
        LONG64 textureBindCallTimeUs = 0;
        LONG64 framebufferCallTimeUs = 0;
        LONG64 syncCallTimeUs = 0;
        LONG64 fenceSyncCallTimeUs = 0;
        LONG64 clientWaitSyncCallTimeUs = 0;
        LONG64 waitSyncCallTimeUs = 0;
        LONG64 deleteSyncCallTimeUs = 0;
        LONG64 isSyncCallTimeUs = 0;
        LONG64 flushCallTimeUs = 0;
        LONG64 lastTextureUploadBytes = 0;
        LONG64 lastBufferSize = 0;
        int lastTextureWidth = 0;
        int lastTextureHeight = 0;
        unsigned int lastBufferTarget = 0;
    };

    struct GlFrameCounterDelta {
        bool ready = false;
        LONG64 draws = 0;
        LONG64 textureUploads = 0;
        LONG64 textureSubUploads = 0;
        LONG64 textureUploadBytes = 0;
        LONG64 bufferUploads = 0;
        LONG64 bufferSubUploads = 0;
        LONG64 bufferUploadBytes = 0;
        LONG64 framebufferBinds = 0;
        LONG64 framebufferAttaches = 0;
        LONG64 framebufferBlits = 0;
        LONG64 programUses = 0;
        LONG64 uniforms = 0;
        LONG64 flushes = 0;
        LONG64 textureBinds = 0;
        LONG64 drawCallTimeUs = 0;
        LONG64 textureUploadCallTimeUs = 0;
        LONG64 bufferUploadCallTimeUs = 0;
        LONG64 programUseCallTimeUs = 0;
        LONG64 uniformCallTimeUs = 0;
        LONG64 textureBindCallTimeUs = 0;
        LONG64 framebufferCallTimeUs = 0;
        LONG64 syncCallTimeUs = 0;
        LONG64 fenceSyncCallTimeUs = 0;
        LONG64 clientWaitSyncCallTimeUs = 0;
        LONG64 waitSyncCallTimeUs = 0;
        LONG64 deleteSyncCallTimeUs = 0;
        LONG64 isSyncCallTimeUs = 0;
        LONG64 flushCallTimeUs = 0;
    };

    static GlFrameCounterSnapshot CaptureGlFrameCounterSnapshot() {
        GlFrameCounterSnapshot snapshot{};
        auto* state = EnsureGlCommandState();
        snapshot.ready = MinecraftXboxIsGlCommandStateReady(state);
        if (!snapshot.ready) {
            return snapshot;
        }

        snapshot.drawSerial = ReadVolatile64(&state->drawSerial);
        snapshot.textureUploadSerial = ReadVolatile64(&state->textureUploadSerial);
        snapshot.textureSubUploadSerial = ReadVolatile64(&state->textureSubUploadSerial);
        snapshot.textureUploadBytes = ReadVolatile64(&state->textureUploadBytes);
        snapshot.bufferUploadSerial = ReadVolatile64(&state->bufferUploadSerial);
        snapshot.bufferSubUploadSerial = ReadVolatile64(&state->bufferSubUploadSerial);
        snapshot.bufferUploadBytes = ReadVolatile64(&state->bufferUploadBytes);
        snapshot.framebufferBindSerial = ReadVolatile64(&state->framebufferBindSerial);
        snapshot.framebufferAttachSerial = ReadVolatile64(&state->framebufferAttachSerial);
        snapshot.framebufferBlitSerial = ReadVolatile64(&state->framebufferBlitSerial);
        snapshot.programUseSerial = ReadVolatile64(&state->programUseSerial);
        snapshot.uniformSerial = ReadVolatile64(&state->uniformSerial);
        snapshot.flushSerial = ReadVolatile64(&state->flushSerial);
        snapshot.textureBindSerial = ReadVolatile64(&state->textureBindSerial);
        snapshot.drawCallTimeUs = ReadVolatile64(&state->drawCallTimeUs);
        snapshot.textureUploadCallTimeUs = ReadVolatile64(&state->textureUploadCallTimeUs);
        snapshot.bufferUploadCallTimeUs = ReadVolatile64(&state->bufferUploadCallTimeUs);
        snapshot.programUseCallTimeUs = ReadVolatile64(&state->programUseCallTimeUs);
        snapshot.uniformCallTimeUs = ReadVolatile64(&state->uniformCallTimeUs);
        snapshot.textureBindCallTimeUs = ReadVolatile64(&state->textureBindCallTimeUs);
        snapshot.framebufferCallTimeUs = ReadVolatile64(&state->framebufferCallTimeUs);
        snapshot.syncCallTimeUs = ReadVolatile64(&state->syncCallTimeUs);
        snapshot.fenceSyncCallTimeUs = ReadVolatile64(&state->fenceSyncCallTimeUs);
        snapshot.clientWaitSyncCallTimeUs = ReadVolatile64(&state->clientWaitSyncCallTimeUs);
        snapshot.waitSyncCallTimeUs = ReadVolatile64(&state->waitSyncCallTimeUs);
        snapshot.deleteSyncCallTimeUs = ReadVolatile64(&state->deleteSyncCallTimeUs);
        snapshot.isSyncCallTimeUs = ReadVolatile64(&state->isSyncCallTimeUs);
        snapshot.flushCallTimeUs = ReadVolatile64(&state->flushCallTimeUs);
        snapshot.lastTextureUploadBytes = ReadVolatile64(&state->lastTextureUploadBytes);
        snapshot.lastBufferSize = ReadVolatile64(&state->lastBufferSize);
        snapshot.lastTextureWidth = state->lastTextureWidth;
        snapshot.lastTextureHeight = state->lastTextureHeight;
        snapshot.lastBufferTarget = state->lastBufferTarget;
        return snapshot;
    }

    static GlFrameCounterDelta DiffGlFrameCounterSnapshot(
        GlFrameCounterSnapshot const& current,
        GlFrameCounterSnapshot const& previous) {
        GlFrameCounterDelta delta{};
        delta.ready = current.ready && previous.ready;
        if (!delta.ready) {
            return delta;
        }

        delta.draws = DeltaNonNegative(current.drawSerial, previous.drawSerial);
        delta.textureUploads = DeltaNonNegative(current.textureUploadSerial, previous.textureUploadSerial);
        delta.textureSubUploads = DeltaNonNegative(current.textureSubUploadSerial, previous.textureSubUploadSerial);
        delta.textureUploadBytes = DeltaNonNegative(current.textureUploadBytes, previous.textureUploadBytes);
        delta.bufferUploads = DeltaNonNegative(current.bufferUploadSerial, previous.bufferUploadSerial);
        delta.bufferSubUploads = DeltaNonNegative(current.bufferSubUploadSerial, previous.bufferSubUploadSerial);
        delta.bufferUploadBytes = DeltaNonNegative(current.bufferUploadBytes, previous.bufferUploadBytes);
        delta.framebufferBinds = DeltaNonNegative(current.framebufferBindSerial, previous.framebufferBindSerial);
        delta.framebufferAttaches = DeltaNonNegative(current.framebufferAttachSerial, previous.framebufferAttachSerial);
        delta.framebufferBlits = DeltaNonNegative(current.framebufferBlitSerial, previous.framebufferBlitSerial);
        delta.programUses = DeltaNonNegative(current.programUseSerial, previous.programUseSerial);
        delta.uniforms = DeltaNonNegative(current.uniformSerial, previous.uniformSerial);
        delta.flushes = DeltaNonNegative(current.flushSerial, previous.flushSerial);
        delta.textureBinds = DeltaNonNegative(current.textureBindSerial, previous.textureBindSerial);
        delta.drawCallTimeUs = DeltaNonNegative(current.drawCallTimeUs, previous.drawCallTimeUs);
        delta.textureUploadCallTimeUs = DeltaNonNegative(current.textureUploadCallTimeUs, previous.textureUploadCallTimeUs);
        delta.bufferUploadCallTimeUs = DeltaNonNegative(current.bufferUploadCallTimeUs, previous.bufferUploadCallTimeUs);
        delta.programUseCallTimeUs = DeltaNonNegative(current.programUseCallTimeUs, previous.programUseCallTimeUs);
        delta.uniformCallTimeUs = DeltaNonNegative(current.uniformCallTimeUs, previous.uniformCallTimeUs);
        delta.textureBindCallTimeUs = DeltaNonNegative(current.textureBindCallTimeUs, previous.textureBindCallTimeUs);
        delta.framebufferCallTimeUs = DeltaNonNegative(current.framebufferCallTimeUs, previous.framebufferCallTimeUs);
        delta.syncCallTimeUs = DeltaNonNegative(current.syncCallTimeUs, previous.syncCallTimeUs);
        delta.fenceSyncCallTimeUs = DeltaNonNegative(current.fenceSyncCallTimeUs, previous.fenceSyncCallTimeUs);
        delta.clientWaitSyncCallTimeUs = DeltaNonNegative(current.clientWaitSyncCallTimeUs, previous.clientWaitSyncCallTimeUs);
        delta.waitSyncCallTimeUs = DeltaNonNegative(current.waitSyncCallTimeUs, previous.waitSyncCallTimeUs);
        delta.deleteSyncCallTimeUs = DeltaNonNegative(current.deleteSyncCallTimeUs, previous.deleteSyncCallTimeUs);
        delta.isSyncCallTimeUs = DeltaNonNegative(current.isSyncCallTimeUs, previous.isSyncCallTimeUs);
        delta.flushCallTimeUs = DeltaNonNegative(current.flushCallTimeUs, previous.flushCallTimeUs);
        return delta;
    }

    static bool IsFrameTimingEnabled() {
        static std::atomic<int> cached{ -1 };
        int value = cached.load(std::memory_order_acquire);
        if (value >= 0) {
            return value != 0;
        }

        wchar_t enabled[16] = {};
        DWORD len = GetEnvironmentVariableW(
            L"MINECRAFT_XBOX_FRAME_TIMING",
            enabled,
            static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
        bool isEnabled =
            len > 0 &&
            len < static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])) &&
            enabled[0] != L'\0' &&
            enabled[0] != L'0' &&
            _wcsicmp(enabled, L"false") != 0 &&
            _wcsicmp(enabled, L"no") != 0;
        cached.store(isEnabled ? 1 : 0, std::memory_order_release);
        if (isEnabled) {
            DebugLine("FRAME-TIMING enabled for xbox-glfw swap path");
        }
        return isEnabled;
    }

    static double FrameTimingNowMs() {
        static LARGE_INTEGER frequency = []() {
            LARGE_INTEGER value = {};
            QueryPerformanceFrequency(&value);
            return value;
        }();
        LARGE_INTEGER counter = {};
        QueryPerformanceCounter(&counter);
        if (frequency.QuadPart <= 0) {
            return static_cast<double>(GetTickCount64());
        }
        return (static_cast<double>(counter.QuadPart) * 1000.0) /
            static_cast<double>(frequency.QuadPart);
    }

    struct FrameTimingStats {
        unsigned long long frameCount = 0;
        unsigned long long sampleCount = 0;
        double previousSwapEndMs = 0.0;
        double drawCpuTotalMs = 0.0;
        double pumpBeforeTotalMs = 0.0;
        double readbackTotalMs = 0.0;
        double makeCurrentTotalMs = 0.0;
        double swapTotalMs = 0.0;
        double pumpAfterTotalMs = 0.0;
        double frameTotalMs = 0.0;
        bool hasLastGlSnapshot = false;
        GlFrameCounterSnapshot lastGlSnapshot = {};
    };

    static std::mutex g_frameTimingMutex;
    static FrameTimingStats g_glfwFrameTimingStats;

    static double BeginGlfwFrameTiming(double frameStartMs) {
        std::lock_guard<std::mutex> lock(g_frameTimingMutex);
        if (g_glfwFrameTimingStats.previousSwapEndMs <= 0.0 ||
            frameStartMs <= g_glfwFrameTimingStats.previousSwapEndMs) {
            return 0.0;
        }
        return frameStartMs - g_glfwFrameTimingStats.previousSwapEndMs;
    }

    static void LogGlfwFrameTiming(
        double frameEndMs,
        double drawCpuMs,
        double pumpBeforeMs,
        double readbackMs,
        double makeCurrentMs,
        double swapMs,
        double pumpAfterMs,
        bool swapAttempted,
        bool swapOk,
        const char* path) {
        std::lock_guard<std::mutex> lock(g_frameTimingMutex);
        FrameTimingStats& stats = g_glfwFrameTimingStats;
        stats.previousSwapEndMs = frameEndMs;
        stats.frameCount++;
        stats.sampleCount++;

        double frameMs = drawCpuMs +
            pumpBeforeMs +
            readbackMs +
            makeCurrentMs +
            swapMs +
            pumpAfterMs;
        stats.drawCpuTotalMs += drawCpuMs;
        stats.pumpBeforeTotalMs += pumpBeforeMs;
        stats.readbackTotalMs += readbackMs;
        stats.makeCurrentTotalMs += makeCurrentMs;
        stats.swapTotalMs += swapMs;
        stats.pumpAfterTotalMs += pumpAfterMs;
        stats.frameTotalMs += frameMs;

        GlFrameCounterSnapshot glSnapshot = CaptureGlFrameCounterSnapshot();
        GlFrameCounterDelta glDelta = stats.hasLastGlSnapshot
            ? DiffGlFrameCounterSnapshot(glSnapshot, stats.lastGlSnapshot)
            : GlFrameCounterDelta{};
        bool uploadBurst =
            glDelta.ready &&
            (glDelta.textureUploadBytes >= 4ll * 1024ll * 1024ll ||
                glDelta.bufferUploadBytes >= 4ll * 1024ll * 1024ll ||
                (glDelta.textureUploads + glDelta.textureSubUploads) >= 64 ||
                (glDelta.bufferUploads + glDelta.bufferSubUploads) >= 64);
        LONG64 trackedGlCallTimeUs =
            glDelta.drawCallTimeUs +
            glDelta.textureUploadCallTimeUs +
            glDelta.bufferUploadCallTimeUs +
            glDelta.programUseCallTimeUs +
            glDelta.uniformCallTimeUs +
            glDelta.textureBindCallTimeUs +
            glDelta.framebufferCallTimeUs +
            glDelta.syncCallTimeUs +
            glDelta.flushCallTimeUs;
        bool glTimingBurst =
            glDelta.ready &&
            (trackedGlCallTimeUs >= 4000 ||
                glDelta.syncCallTimeUs >= 1000 ||
                glDelta.textureUploadCallTimeUs >= 1000 ||
                glDelta.bufferUploadCallTimeUs >= 1000);

        bool slowFrame = frameMs >= 24.0 || swapMs >= 8.0 || makeCurrentMs >= 4.0 || readbackMs >= 4.0;
        bool shouldLog = stats.frameCount <= 12 || stats.sampleCount >= 60 || slowFrame || (swapAttempted && !swapOk);
        shouldLog = shouldLog || uploadBurst || glTimingBurst;
        if (!shouldLog) {
            stats.lastGlSnapshot = glSnapshot;
            stats.hasLastGlSnapshot = glSnapshot.ready;
            return;
        }

        double divisor = stats.sampleCount > 0 ? static_cast<double>(stats.sampleCount) : 1.0;
        char line[2600] = {};
        std::snprintf(
            line,
            sizeof(line),
            "FRAME-TIMING glfw frame=%llu path=%s swapAttempted=%d swapOk=%d drawCpuMs=%.3f pumpBeforeMs=%.3f readbackMs=%.3f makeCurrentMs=%.3f swapMs=%.3f pumpAfterMs=%.3f frameMs=%.3f avg60DrawCpuMs=%.3f avg60SwapMs=%.3f avg60FrameMs=%.3f glReady=%d glDraws=%lld drawGlMs=%.3f texUp=%lld texSub=%lld texKB=%.1f texGlMs=%.3f bufUp=%lld bufSub=%lld bufKB=%.1f bufGlMs=%.3f fbBind=%lld fbAttach=%lld fbBlit=%lld fbGlMs=%.3f programs=%lld progGlMs=%.3f uniforms=%lld uniformGlMs=%.3f flushes=%lld flushGlMs=%.3f texBinds=%lld texBindGlMs=%.3f syncGlMs=%.3f fenceSyncGlMs=%.3f clientWaitSyncGlMs=%.3f waitSyncGlMs=%.3f deleteSyncGlMs=%.3f isSyncGlMs=%.3f trackedGlMs=%.3f lastTex=%dx%d lastTexKB=%.1f lastBufTarget=0x%X lastBufKB=%.1f",
            stats.frameCount,
            path ? path : "<null>",
            swapAttempted ? 1 : 0,
            swapOk ? 1 : 0,
            drawCpuMs,
            pumpBeforeMs,
            readbackMs,
            makeCurrentMs,
            swapMs,
            pumpAfterMs,
            frameMs,
            stats.drawCpuTotalMs / divisor,
            stats.swapTotalMs / divisor,
            stats.frameTotalMs / divisor,
            glSnapshot.ready ? 1 : 0,
            static_cast<long long>(glDelta.draws),
            static_cast<double>(glDelta.drawCallTimeUs) / 1000.0,
            static_cast<long long>(glDelta.textureUploads),
            static_cast<long long>(glDelta.textureSubUploads),
            static_cast<double>(glDelta.textureUploadBytes) / 1024.0,
            static_cast<double>(glDelta.textureUploadCallTimeUs) / 1000.0,
            static_cast<long long>(glDelta.bufferUploads),
            static_cast<long long>(glDelta.bufferSubUploads),
            static_cast<double>(glDelta.bufferUploadBytes) / 1024.0,
            static_cast<double>(glDelta.bufferUploadCallTimeUs) / 1000.0,
            static_cast<long long>(glDelta.framebufferBinds),
            static_cast<long long>(glDelta.framebufferAttaches),
            static_cast<long long>(glDelta.framebufferBlits),
            static_cast<double>(glDelta.framebufferCallTimeUs) / 1000.0,
            static_cast<long long>(glDelta.programUses),
            static_cast<double>(glDelta.programUseCallTimeUs) / 1000.0,
            static_cast<long long>(glDelta.uniforms),
            static_cast<double>(glDelta.uniformCallTimeUs) / 1000.0,
            static_cast<long long>(glDelta.flushes),
            static_cast<double>(glDelta.flushCallTimeUs) / 1000.0,
            static_cast<long long>(glDelta.textureBinds),
            static_cast<double>(glDelta.textureBindCallTimeUs) / 1000.0,
            static_cast<double>(glDelta.syncCallTimeUs) / 1000.0,
            static_cast<double>(glDelta.fenceSyncCallTimeUs) / 1000.0,
            static_cast<double>(glDelta.clientWaitSyncCallTimeUs) / 1000.0,
            static_cast<double>(glDelta.waitSyncCallTimeUs) / 1000.0,
            static_cast<double>(glDelta.deleteSyncCallTimeUs) / 1000.0,
            static_cast<double>(glDelta.isSyncCallTimeUs) / 1000.0,
            static_cast<double>(trackedGlCallTimeUs) / 1000.0,
            glSnapshot.lastTextureWidth,
            glSnapshot.lastTextureHeight,
            static_cast<double>(glSnapshot.lastTextureUploadBytes) / 1024.0,
            glSnapshot.lastBufferTarget,
            static_cast<double>(glSnapshot.lastBufferSize) / 1024.0);
        DebugLine(line);

        stats.lastGlSnapshot = glSnapshot;
        stats.hasLastGlSnapshot = glSnapshot.ready;

        if (stats.sampleCount >= 60) {
            stats.sampleCount = 0;
            stats.drawCpuTotalMs = 0.0;
            stats.pumpBeforeTotalMs = 0.0;
            stats.readbackTotalMs = 0.0;
            stats.makeCurrentTotalMs = 0.0;
            stats.swapTotalMs = 0.0;
            stats.pumpAfterTotalMs = 0.0;
            stats.frameTotalMs = 0.0;
        }
    }

    static IGamepadStatics* GetGamepadStatics() {
        std::lock_guard<std::mutex> lock(g_gamepadStaticsMutex);
        if (g_gamepadStatics) {
            return g_gamepadStatics.Get();
        }

        ComPtr<IGamepadStatics> statics;
        HRESULT hr = RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_Gaming_Input_Gamepad).Get(),
            IID_PPV_ARGS(&statics));
        if (FAILED(hr) || !statics) {
            char line[160] = {};
            std::snprintf(line, sizeof(line), "Gamepad activation factory unavailable hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        g_gamepadStatics = statics;
        DebugLine("Gamepad activation factory acquired for GLFW joystick bridge");
        return g_gamepadStatics.Get();
    }

    static bool TryReadPrimaryGamepad(GamepadReading& reading) {
        IGamepadStatics* statics = GetGamepadStatics();
        if (!statics) {
            return false;
        }

        ComPtr<IVectorView<Gamepad*>> gamepads;
        HRESULT hr = statics->get_Gamepads(gamepads.GetAddressOf());
        if (FAILED(hr) || !gamepads) {
            return false;
        }

        unsigned int size = 0;
        hr = gamepads->get_Size(&size);
        if (FAILED(hr) || size == 0) {
            return false;
        }

        ComPtr<IGamepad> gamepad;
        hr = gamepads->GetAt(0, gamepad.GetAddressOf());
        if (FAILED(hr) || !gamepad) {
            return false;
        }

        memset(&reading, 0, sizeof(reading));
        hr = gamepad->GetCurrentReading(&reading);
        return SUCCEEDED(hr);
    }

    static bool HasPrimaryGamepad() {
        GamepadReading reading = {};
        return TryReadPrimaryGamepad(reading);
    }

    static bool HasButton(GamepadButtons buttons, GamepadButtons bit) {
        return (static_cast<unsigned int>(buttons) & static_cast<unsigned int>(bit)) != 0;
    }

    static float ClampAxis(double value) {
        value = std::max(-1.0, std::min(1.0, value));
        return static_cast<float>(value);
    }

    static void FillGlfwGamepadState(GLFWgamepadstate& state, GamepadReading const& reading) {
        memset(&state, 0, sizeof(state));

        state.buttons[GLFW_GAMEPAD_BUTTON_A] = HasButton(reading.Buttons, GamepadButtons_A) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_B] = HasButton(reading.Buttons, GamepadButtons_B) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_X] = HasButton(reading.Buttons, GamepadButtons_X) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_Y] = HasButton(reading.Buttons, GamepadButtons_Y) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] = HasButton(reading.Buttons, GamepadButtons_LeftShoulder) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] = HasButton(reading.Buttons, GamepadButtons_RightShoulder) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_BACK] = HasButton(reading.Buttons, GamepadButtons_View) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_START] = HasButton(reading.Buttons, GamepadButtons_Menu) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB] = HasButton(reading.Buttons, GamepadButtons_LeftThumbstick) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB] = HasButton(reading.Buttons, GamepadButtons_RightThumbstick) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP] = HasButton(reading.Buttons, GamepadButtons_DPadUp) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] = HasButton(reading.Buttons, GamepadButtons_DPadRight) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] = HasButton(reading.Buttons, GamepadButtons_DPadDown) ? GLFW_PRESS : GLFW_RELEASE;
        state.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] = HasButton(reading.Buttons, GamepadButtons_DPadLeft) ? GLFW_PRESS : GLFW_RELEASE;

        state.axes[GLFW_GAMEPAD_AXIS_LEFT_X] = ClampAxis(reading.LeftThumbstickX);
        state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] = ClampAxis(-reading.LeftThumbstickY);
        state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X] = ClampAxis(reading.RightThumbstickX);
        state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y] = ClampAxis(-reading.RightThumbstickY);
        state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = ClampAxis((reading.LeftTrigger * 2.0) - 1.0);
        state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = ClampAxis((reading.RightTrigger * 2.0) - 1.0);
    }

    static bool IsGamepadMouseModeActive() {
        if (IsTruthyEnvironment(L"MINECRAFT_XBOX_GLFW_DISABLE_GAMEPAD_MOUSE")) {
            return false;
        }
        return g_gamepadMouseMode.load(std::memory_order_acquire) != 0;
    }

    static bool UpdateGamepadMouseModeToggle(GamepadReading const& reading) {
        if (IsTruthyEnvironment(L"MINECRAFT_XBOX_GLFW_DISABLE_GAMEPAD_MOUSE")) {
            g_gamepadMouseMode.store(0, std::memory_order_release);
            g_gamepadMouseToggleLatch.store(0, std::memory_order_release);
            return false;
        }

        const bool chord =
            HasButton(reading.Buttons, GamepadButtons_LeftThumbstick) &&
            HasButton(reading.Buttons, GamepadButtons_RightThumbstick);
        const int previousLatch = g_gamepadMouseToggleLatch.exchange(chord ? 1 : 0, std::memory_order_acq_rel);
        if (!chord || previousLatch != 0) {
            return false;
        }

        const int next = IsGamepadMouseModeActive() ? 0 : 1;
        g_gamepadMouseMode.store(next, std::memory_order_release);
        g_gamepadMouseLastTickMs.store(GetTickCount64(), std::memory_order_release);
        g_gamepadMouseLastWheelMs.store(0, std::memory_order_release);
        DebugLine(next ? "Controller mouse mode enabled (L3+R3)" : "Controller mouse mode disabled (L3+R3)");
        return true;
    }

    static void NeutralizeGlfwGamepadState(GLFWgamepadstate& state) {
        memset(&state, 0, sizeof(state));
        state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = -1.0f;
        state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = -1.0f;
    }

    static bool RefreshCachedGamepadState() {
        GamepadReading reading = {};
        if (!TryReadPrimaryGamepad(reading)) {
            return false;
        }

        GLFWgamepadstate state = {};
        FillGlfwGamepadState(state, reading);
        UpdateGamepadMouseModeToggle(reading);
        if (IsGamepadMouseModeActive()) {
            NeutralizeGlfwGamepadState(state);
        }
        memcpy(g_gamepadAxes, state.axes, sizeof(g_gamepadAxes));
        memcpy(g_gamepadButtons, state.buttons, sizeof(g_gamepadButtons));

        unsigned char hat = GLFW_HAT_CENTERED;
        if (!IsGamepadMouseModeActive()) {
            if (HasButton(reading.Buttons, GamepadButtons_DPadUp)) {
                hat |= GLFW_HAT_UP;
            }
            if (HasButton(reading.Buttons, GamepadButtons_DPadRight)) {
                hat |= GLFW_HAT_RIGHT;
            }
            if (HasButton(reading.Buttons, GamepadButtons_DPadDown)) {
                hat |= GLFW_HAT_DOWN;
            }
            if (HasButton(reading.Buttons, GamepadButtons_DPadLeft)) {
                hat |= GLFW_HAT_LEFT;
            }
        }
        g_gamepadHats[0] = hat;
        return true;
    }

    static int CurrentGamepadPresentFlag() {
        return HasPrimaryGamepad() ? 1 : 0;
    }

    static void SeedGamepadConnectionState() {
        g_lastGamepadPresent.store(CurrentGamepadPresentFlag(), std::memory_order_release);
    }

    static void NotifyGamepadConnectionChange() {
        int present = CurrentGamepadPresentFlag();
        int previous = g_lastGamepadPresent.exchange(present, std::memory_order_acq_rel);
        if (previous < 0 || previous == present) {
            return;
        }

        GLFWjoystickfun callback = g_cbJoystick;
        if (callback) {
            callback(GLFW_JOYSTICK_1, present ? GLFW_CONNECTED : GLFW_DISCONNECTED);
        }
    }

    static unsigned long CurrentThreadIdForLog() {
        return static_cast<unsigned long>(GetCurrentThreadId());
    }

    static bool IsFatalNativeException(DWORD code) {
        switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
            return true;
        default:
            return false;
        }
    }

    static LONG WINAPI NativeFatalExceptionLogger(PEXCEPTION_POINTERS info) {
        if (!info || !info->ExceptionRecord) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const DWORD code = info->ExceptionRecord->ExceptionCode;
        if (!IsFatalNativeException(code)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        static volatile LONG logged = 0;
        if (InterlockedExchange(&logged, 1) == 0) {
            const ULONG_PTR p0 = info->ExceptionRecord->NumberParameters > 0
                ? info->ExceptionRecord->ExceptionInformation[0]
                : 0;
            const ULONG_PTR p1 = info->ExceptionRecord->NumberParameters > 1
                ? info->ExceptionRecord->ExceptionInformation[1]
                : 0;
            char line[320] = {};
            std::snprintf(
                line,
                sizeof(line),
                "native fatal exception code=0x%08lX address=%p info0=0x%llX info1=0x%llX",
                code,
                info->ExceptionRecord->ExceptionAddress,
                static_cast<unsigned long long>(p0),
                static_cast<unsigned long long>(p1));
            DebugLine(line);
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    static void InstallNativeFatalExceptionLogger() {
        static volatile LONG installed = 0;
        if (InterlockedCompareExchange(&installed, 1, 0) == 0) {
            AddVectoredExceptionHandler(1, NativeFatalExceptionLogger);
        }
    }

    static bool IsFakeContextMode() {
        static std::atomic<int> cached{ -1 };
        int value = cached.load(std::memory_order_acquire);
        if (value >= 0) {
            return value != 0;
        }

        wchar_t enabled[16] = {};
        DWORD len = GetEnvironmentVariableW(
            L"MINECRAFT_XBOX_GLFW_FAKE_CONTEXT",
            enabled,
            static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
        bool isEnabled = len > 0 && enabled[0] != L'0';
        cached.store(isEnabled ? 1 : 0, std::memory_order_release);
        return isEnabled;
    }

    static bool IsMesaEglMode() {
        static std::atomic<int> cached{ -1 };
        int value = cached.load(std::memory_order_acquire);
        if (value >= 0) {
            return value != 0;
        }

        wchar_t enabled[16] = {};
        DWORD len = GetEnvironmentVariableW(
            L"MINECRAFT_XBOX_GLFW_MESA_EGL_CONTEXT",
            enabled,
            static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
        bool isEnabled = len > 0 && enabled[0] != L'0';
        cached.store(isEnabled ? 1 : 0, std::memory_order_release);
        return isEnabled;
    }

    static bool IsXboxOneGraphicsRuntimeMode() {
        static std::atomic<int> cached{ -1 };
        int value = cached.load(std::memory_order_acquire);
        if (value >= 0) {
            return value != 0;
        }

        wchar_t runtime[64] = {};
        DWORD len = GetEnvironmentVariableW(
            L"MC_GRAPHICS_RUNTIME",
            runtime,
            static_cast<DWORD>(sizeof(runtime) / sizeof(runtime[0])));
        bool isEnabled = len > 0 && len < (sizeof(runtime) / sizeof(runtime[0])) &&
            (_wcsicmp(runtime, L"xboxone") == 0 ||
             _wcsicmp(runtime, L"gles") == 0 ||
             _wcsicmp(runtime, L"opengles") == 0);
        cached.store(isEnabled ? 1 : 0, std::memory_order_release);
        return isEnabled;
    }

    static bool IsFatalOnSurfacelessMode() {
        static std::atomic<int> cached{ -1 };
        int value = cached.load(std::memory_order_acquire);
        if (value >= 0) {
            return value != 0;
        }

        wchar_t enabled[16] = {};
        DWORD len = GetEnvironmentVariableW(
            L"MINECRAFT_XBOX_FATAL_ON_SURFACELESS",
            enabled,
            static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
        bool isEnabled = len > 0 && enabled[0] != L'0';
        cached.store(isEnabled ? 1 : 0, std::memory_order_release);
        return isEnabled;
    }

    static bool IsMesaEglReadbackEnabled() {
        static std::atomic<int> cached{ -1 };
        int value = cached.load(std::memory_order_acquire);
        if (value >= 0) {
            return value != 0;
        }

        wchar_t enabled[16] = {};
        DWORD len = GetEnvironmentVariableW(
            L"MINECRAFT_XBOX_EGL_READBACK",
            enabled,
            static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
        bool isEnabled = len > 0 && enabled[0] != L'0';
        cached.store(isEnabled ? 1 : 0, std::memory_order_release);
        return isEnabled;
    }

    static bool IsPreferNativeCoreWindowMode() {
        static std::atomic<int> cached{ -1 };
        int value = cached.load(std::memory_order_acquire);
        if (value >= 0) {
            return value != 0;
        }

        wchar_t enabled[16] = {};
        DWORD len = GetEnvironmentVariableW(
            L"MINECRAFT_XBOX_GLFW_PREFER_NATIVE_COREWINDOW",
            enabled,
            static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
        bool isEnabled = len > 0 && enabled[0] != L'0';
        cached.store(isEnabled ? 1 : 0, std::memory_order_release);
        return isEnabled;
    }

    static bool IsPreferNativeEglDescriptorMode() {
        static std::atomic<int> cached{ -1 };
        int value = cached.load(std::memory_order_acquire);
        if (value >= 0) {
            return value != 0;
        }

        wchar_t enabled[16] = {};
        DWORD len = GetEnvironmentVariableW(
            L"MINECRAFT_XBOX_GLFW_PREFER_NATIVE_DESCRIPTOR",
            enabled,
            static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
        bool isEnabled = len > 0 && enabled[0] != L'0';
        cached.store(isEnabled ? 1 : 0, std::memory_order_release);
        return isEnabled;
    }

    static bool IsMesaWglMode() {
        static std::atomic<int> cached{ -1 };
        int value = cached.load(std::memory_order_acquire);
        if (value >= 0) {
            return value != 0;
        }

        wchar_t enabled[16] = {};
        DWORD len = GetEnvironmentVariableW(
            L"MINECRAFT_XBOX_GLFW_MESA_WGL_CONTEXT",
            enabled,
            static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
        bool isEnabled = len > 0 && enabled[0] != L'0';
        cached.store(isEnabled ? 1 : 0, std::memory_order_release);
        return isEnabled;
    }

    static int LogNativeException(const char* call, unsigned long code) {
        char line[256] = {};
        std::snprintf(line, sizeof(line), "%s raised SEH exception 0x%08lx; disabling Mesa WGL context path", call, code);
        DebugLine(line);
        g_wglDisabled.store(1, std::memory_order_release);
        g_contextCurrent.store(0, std::memory_order_release);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    static int LogEglException(const char* call, unsigned long code) {
        char line[256] = {};
        std::snprintf(line, sizeof(line), "%s raised SEH exception 0x%08lx; disabling Mesa EGL context path", call, code);
        DebugLine(line);
        g_eglDisabled.store(1, std::memory_order_release);
        g_contextCurrent.store(0, std::memory_order_release);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    static int LogEglSurfaceFallbackException(const char* call, unsigned long code) {
        char line[256] = {};
        std::snprintf(line, sizeof(line), "%s raised SEH exception 0x%08lx; trying next EGL surface fallback", call, code);
        DebugLine(line);
        g_contextCurrent.store(0, std::memory_order_release);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    static int LogEglRecoverableException(const char* call, unsigned long code) {
        char line[256] = {};
        std::snprintf(line, sizeof(line), "%s raised SEH exception 0x%08lx; continuing Mesa EGL fallback", call, code);
        DebugLine(line);
        g_contextCurrent.store(0, std::memory_order_release);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    static bool TryCallOpenGlProcInit() {
        static std::atomic<int> called{ 0 };
        if (called.exchange(1, std::memory_order_acq_rel) != 0) {
            return true;
        }

        if (!g_openGlModule) {
            DebugLine("opengl32!proc_init skipped; opengl32.dll is not loaded");
            return false;
        }

        auto procInit = reinterpret_cast<OpenGlProcInitProc>(
            GetProcAddress(g_openGlModule, "proc_init"));
        if (!procInit) {
            DebugLine("opengl32!proc_init missing");
            return false;
        }

        DebugLine("opengl32!proc_init found");
        __try {
            procInit();
        }
        __except (LogEglException("opengl32!proc_init", GetExceptionCode())) {
            return false;
        }

        DebugLine("opengl32!proc_init returned");
        return true;
    }

    static FARPROC LoadOpenGlProc(const char* name) {
        return g_openGlModule ? GetProcAddress(g_openGlModule, name) : nullptr;
    }

    static FARPROC LoadEglProc(const char* name) {
        return g_eglModule ? GetProcAddress(g_eglModule, name) : nullptr;
    }

    static bool GetOpenGlProviderPath(wchar_t* path, DWORD pathChars) {
        if (!path || pathChars == 0) {
            return false;
        }

        DWORD len = GetEnvironmentVariableW(L"MINECRAFT_XBOX_OPENGL_DLL", path, pathChars);
        if (len > 0 && len < pathChars) {
            return true;
        }

        wcscpy_s(path, pathChars, L"opengl32.dll");
        return false;
    }

    static void GetDirectoryFromPath(const wchar_t* path, wchar_t* directory, size_t directoryChars) {
        if (!directory || directoryChars == 0) {
            return;
        }

        directory[0] = L'\0';
        if (!path || !path[0]) {
            return;
        }

        const wchar_t* slash = std::wcsrchr(path, L'\\');
        const wchar_t* forward = std::wcsrchr(path, L'/');
        if (forward && (!slash || forward > slash)) {
            slash = forward;
        }
        if (!slash) {
            return;
        }

        size_t count = static_cast<size_t>(slash - path);
        if (count >= directoryChars) {
            count = directoryChars - 1;
        }
        wmemcpy(directory, path, count);
        directory[count] = L'\0';
    }

    static HMODULE LoadDllFromDirectory(const wchar_t* directory, const wchar_t* fileName, bool required) {
        wchar_t path[1024] = {};
        HMODULE module = nullptr;
        if (directory && directory[0]) {
            swprintf_s(path, L"%s\\%s", directory, fileName);
            module = LoadLibraryW(path);
            if (module) {
                char line[320] = {};
                std::snprintf(line, sizeof(line), "LoadLibraryW(%ls) succeeded", path);
                DebugLine(line);
                return module;
            }

            char line[320] = {};
            std::snprintf(line, sizeof(line), "LoadLibraryW(%ls) failed; GetLastError=%lu", path, GetLastError());
            DebugLine(line);
        }

        module = LoadLibraryW(fileName);
        if (!module && required) {
            char line[192] = {};
            std::snprintf(line, sizeof(line), "LoadLibraryW(%ls) failed; GetLastError=%lu", fileName, GetLastError());
            DebugLine(line);
        }
        return module;
    }

    static void DebugMesaWglStage(
        MesaUwpGetWglInitStageProc stageProc,
        MesaUwpGetWglInitDetailProc detailProc,
        MesaUwpGetWglInitHistoryProc historyProc) {
        const char* stage = stageProc ? stageProc() : nullptr;
        const char* detail = detailProc ? detailProc() : nullptr;
        const char* history = historyProc ? historyProc() : nullptr;

        char line[900] = {};
        std::snprintf(
            line,
            sizeof(line),
            "Mesa WGL screen init stage=%s detail=%s history=%s",
            stage ? stage : "<none>",
            detail ? detail : "<none>",
            history ? history : "<none>");
        DebugLine(line);
    }

    static bool SafeEnsureMesaWglInitialized(MesaUwpEnsureWglInitializedProc proc) {
        __try {
            return proc && proc() != 0;
        }
        __except (LogEglException("MesaUwpEnsureWglInitialized", GetExceptionCode())) {
            return false;
        }
    }

    static bool SafeStwInitScreen(MesaStwInitScreenProc proc, void* nativeDisplay) {
        __try {
            return proc && proc(nativeDisplay);
        }
        __except (LogEglException("stw_init_screen(CoreWindow)", GetExceptionCode())) {
            return false;
        }
    }

    static void* SafeStwGetDevice(MesaStwGetDeviceProc proc) {
        void* device = nullptr;
        __try {
            device = proc ? proc() : nullptr;
        }
        __except (LogEglException("stw_get_device", GetExceptionCode())) {
            device = nullptr;
        }
        return device;
    }

    static bool ShouldSkipMesaWglGalliumInit() {
        static std::atomic<int> cached{ -1 };
        int value = cached.load(std::memory_order_acquire);
        if (value >= 0) {
            return value != 0;
        }

        wchar_t enabled[16] = {};
        DWORD len = GetEnvironmentVariableW(
            L"MINECRAFT_XBOX_GLFW_SKIP_WGL_GALLIUM",
            enabled,
            static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
        if (len == 0) {
            len = GetEnvironmentVariableW(
                L"MINECRAFT_XBOX_OPENGL_EGL_PROC_ONLY",
                enabled,
                static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
        }

        const bool isEnabled =
            len > 0 &&
            enabled[0] != L'0' &&
            _wcsicmp(enabled, L"false") != 0 &&
            _wcsicmp(enabled, L"no") != 0;
        cached.store(isEnabled ? 1 : 0, std::memory_order_release);
        return isEnabled;
    }

    static bool TryInitializeMesaWglScreenForCoreWindow(IUnknown* coreWindowUnknown) {
        if (!coreWindowUnknown) {
            return false;
        }

        int cached = g_mesaWglScreenInitResult.load(std::memory_order_acquire);
        if (cached >= 0) {
            return cached != 0;
        }

        wchar_t openGlPath[1024] = {};
        wchar_t providerDirectory[1024] = {};
        GetOpenGlProviderPath(openGlPath, static_cast<DWORD>(sizeof(openGlPath) / sizeof(openGlPath[0])));
        GetDirectoryFromPath(openGlPath, providerDirectory, sizeof(providerDirectory) / sizeof(providerDirectory[0]));

        HMODULE gallium = LoadDllFromDirectory(providerDirectory, L"libgallium_wgl.dll", true);
        if (!gallium) {
            g_mesaWglScreenInitResult.store(0, std::memory_order_release);
            return false;
        }

        auto ensureWglInitialized = reinterpret_cast<MesaUwpEnsureWglInitializedProc>(
            GetProcAddress(gallium, "MesaUwpEnsureWglInitialized"));
        auto getWglInitStage = reinterpret_cast<MesaUwpGetWglInitStageProc>(
            GetProcAddress(gallium, "MesaUwpGetWglInitStage"));
        auto getWglInitDetail = reinterpret_cast<MesaUwpGetWglInitDetailProc>(
            GetProcAddress(gallium, "MesaUwpGetWglInitDetail"));
        auto getWglInitHistory = reinterpret_cast<MesaUwpGetWglInitHistoryProc>(
            GetProcAddress(gallium, "MesaUwpGetWglInitHistory"));
        auto getDevice = reinterpret_cast<MesaStwGetDeviceProc>(
            GetProcAddress(gallium, "stw_get_device"));
        auto initScreen = reinterpret_cast<MesaStwInitScreenProc>(
            GetProcAddress(gallium, "stw_init_screen"));

        if (!ensureWglInitialized || !getDevice || !initScreen) {
            DebugLine("Mesa WGL CoreWindow screen init exports missing");
            DebugMesaWglStage(getWglInitStage, getWglInitDetail, getWglInitHistory);
            g_mesaWglScreenInitResult.store(0, std::memory_order_release);
            return false;
        }

        char beginLine[160] = {};
        std::snprintf(
            beginLine,
            sizeof(beginLine),
            "Mesa WGL CoreWindow screen init begin thread=0x%lx",
            CurrentThreadIdForLog());
        DebugLine(beginLine);
        if (!SafeEnsureMesaWglInitialized(ensureWglInitialized)) {
            DebugLine("MesaUwpEnsureWglInitialized failed before EGL config probe");
            DebugMesaWglStage(getWglInitStage, getWglInitDetail, getWglInitHistory);
            g_mesaWglScreenInitResult.store(0, std::memory_order_release);
            return false;
        }

        void* deviceBefore = SafeStwGetDevice(getDevice);
        char beforeLine[160] = {};
        std::snprintf(beforeLine, sizeof(beforeLine), "Mesa WGL device before CoreWindow init=%p", deviceBefore);
        DebugLine(beforeLine);

        if (!SafeStwInitScreen(initScreen, coreWindowUnknown)) {
            DebugLine("stw_init_screen(CoreWindow) failed before EGL config probe");
            DebugMesaWglStage(getWglInitStage, getWglInitDetail, getWglInitHistory);
            g_mesaWglScreenInitResult.store(0, std::memory_order_release);
            return false;
        }

        void* deviceAfter = SafeStwGetDevice(getDevice);
        char afterLine[160] = {};
        std::snprintf(afterLine, sizeof(afterLine), "Mesa WGL device after CoreWindow init=%p", deviceAfter);
        DebugLine(afterLine);
        g_mesaWglScreenInitResult.store(deviceAfter ? 1 : 0, std::memory_order_release);
        return deviceAfter != nullptr;
    }

    static int EglErrorCode() {
        if (!g_eglGetError) {
            return 0;
        }

        int error = 0;
        __try {
            error = g_eglGetError();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            error = 0;
        }
        return error;
    }

    static IUnknown* QueryInterfaceForEglObject(void* source, REFIID iid, const char* label) {
        if (!source) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "%s QueryInterface skipped; source is null", label ? label : "EGL object");
            DebugLine(line);
            return nullptr;
        }

        auto* unknown = static_cast<IUnknown*>(source);
        void* queried = nullptr;
        HRESULT hr = E_FAIL;
        __try {
            hr = unknown->QueryInterface(iid, &queried);
        }
        __except (LogEglRecoverableException("QueryInterface(EGL native object)", GetExceptionCode())) {
            queried = nullptr;
            hr = E_FAIL;
        }

        char line[240] = {};
        std::snprintf(
            line,
            sizeof(line),
            "%s QueryInterface hr=0x%08lx source=%p iface=%p",
            label ? label : "EGL object",
            static_cast<unsigned long>(hr),
            source,
            queried);
        DebugLine(line);

        return SUCCEEDED(hr) ? static_cast<IUnknown*>(queried) : nullptr;
    }

    static IUnknown* TryAcquireCoreWindowFromApplicationProperties() {
        ComPtr<ICoreApplication> coreApp;
        HRESULT hr = GetActivationFactory(
            HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication).Get(),
            coreApp.GetAddressOf());
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "CoreApplication activation failed while acquiring CoreWindow hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        ComPtr<IPropertySet> props;
        hr = coreApp->get_Properties(props.GetAddressOf());
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "CoreApplication.get_Properties failed while acquiring CoreWindow hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        ComPtr<IMap<HSTRING, IInspectable*>> propMap;
        hr = props.As(&propMap);
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "CoreApplication.Properties As(IMap) failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        boolean hasWindow = false;
        hr = propMap->HasKey(HStringReference(kEglNativeWindowTypeProperty).Get(), &hasWindow);
        if (FAILED(hr) || !hasWindow) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "CoreApplication.Properties missing %ls hr=0x%08lx", kEglNativeWindowTypeProperty, static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        ComPtr<IInspectable> inspectable;
        hr = propMap->Lookup(HStringReference(kEglNativeWindowTypeProperty).Get(), inspectable.GetAddressOf());
        if (FAILED(hr) || !inspectable) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "CoreApplication.Properties lookup CoreWindow failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        IUnknown* coreWindow = QueryInterfaceForEglObject(
            inspectable.Get(),
            kIidCoreWindow,
            "CoreWindow from CoreApplication.Properties");
        if (coreWindow) {
            DebugLine("CoreWindow acquired by GLFW shim from CoreApplication.Properties");
        }
        return coreWindow;
    }

    static IUnknown* TryAcquireCoreWindowForCurrentThread() {
        ComPtr<ICoreWindowStatic> coreWindowStatic;
        HRESULT hr = GetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Core_CoreWindow).Get(),
            coreWindowStatic.GetAddressOf());
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "CoreWindow activation factory failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        ComPtr<ICoreWindow> coreWindow;
        hr = coreWindowStatic->GetForCurrentThread(coreWindow.GetAddressOf());
        if (FAILED(hr) || !coreWindow) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "CoreWindow.GetForCurrentThread failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        IUnknown* nativeWindow = QueryInterfaceForEglObject(
            coreWindow.Get(),
            kIidCoreWindow,
            "CoreWindow from GetForCurrentThread");
        if (nativeWindow) {
            DebugLine("CoreWindow acquired by GLFW shim from GetForCurrentThread");
        }
        return nativeWindow;
    }

    static bool IsSwapDispatcherPumpEnabled() {
        static std::atomic<int> cached{ -1 };
        int value = cached.load(std::memory_order_acquire);
        if (value >= 0) {
            return value != 0;
        }

        wchar_t enabled[16] = {};
        DWORD len = GetEnvironmentVariableW(
            L"MINECRAFT_XBOX_GLFW_PUMP_ON_SWAP",
            enabled,
            static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
        bool isEnabled = len > 0 && enabled[0] != L'0';
        cached.store(isEnabled ? 1 : 0, std::memory_order_release);
        return isEnabled;
    }

    static void PumpCoreWindowEventsForCurrentThread(const char* reason) {
        static thread_local int unavailable = 0;
        static thread_local int inPump = 0;
        static thread_local unsigned int failureCount = 0;
        static thread_local ULONGLONG nextRetryTick = 0;
        if (unavailable) {
            return;
        }
        if (inPump) {
            return;
        }

        ULONGLONG now = GetTickCount64();
        if (nextRetryTick != 0 && now < nextRetryTick) {
            return;
        }

        ComPtr<ICoreWindowStatic> coreWindowStatic;
        HRESULT hr = GetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Core_CoreWindow).Get(),
            coreWindowStatic.GetAddressOf());
        if (FAILED(hr) || !coreWindowStatic) {
            unavailable = 1;
            char line[192] = {};
            std::snprintf(
                line,
                sizeof(line),
                "CoreWindow dispatcher pump unavailable; activation factory hr=0x%08lx reason=%s",
                static_cast<unsigned long>(hr),
                reason ? reason : "<none>");
            DebugLine(line);
            return;
        }

        ComPtr<ICoreWindow> coreWindow;
        hr = coreWindowStatic->GetForCurrentThread(coreWindow.GetAddressOf());
        if (FAILED(hr) || !coreWindow) {
            unavailable = 1;
            char line[192] = {};
            std::snprintf(
                line,
                sizeof(line),
                "CoreWindow dispatcher pump unavailable on this thread hr=0x%08lx reason=%s",
                static_cast<unsigned long>(hr),
                reason ? reason : "<none>");
            DebugLine(line);
            return;
        }

        ComPtr<ICoreDispatcher> dispatcher;
        hr = coreWindow->get_Dispatcher(dispatcher.GetAddressOf());
        if (FAILED(hr) || !dispatcher) {
            unavailable = 1;
            char line[192] = {};
            std::snprintf(
                line,
                sizeof(line),
                "CoreWindow dispatcher pump get_Dispatcher failed hr=0x%08lx reason=%s",
                static_cast<unsigned long>(hr),
                reason ? reason : "<none>");
            DebugLine(line);
            return;
        }

        inPump = 1;
        hr = dispatcher->ProcessEvents(CoreProcessEventsOption_ProcessAllIfPresent);
        inPump = 0;
        static thread_local unsigned int pumpCount = 0;
        ++pumpCount;
        if (FAILED(hr)) {
            ++failureCount;
            now = GetTickCount64();
            nextRetryTick = now + 50;
            if (failureCount <= 4 || (failureCount % 120) == 0) {
                char line[224] = {};
                std::snprintf(
                    line,
                    sizeof(line),
                    "CoreWindow dispatcher ProcessEvents failed hr=0x%08lx reason=%s failureCount=%u; backing off",
                    static_cast<unsigned long>(hr),
                    reason ? reason : "<none>",
                    failureCount);
                DebugLine(line);
            }
        }
        else {
            failureCount = 0;
            nextRetryTick = 0;
        }

        if (SUCCEEDED(hr) && (pumpCount <= 8 || (pumpCount % 600) == 0)) {
            char line[192] = {};
            std::snprintf(
                line,
                sizeof(line),
                "CoreWindow dispatcher pumped by GLFW reason=%s count=%u thread=0x%lx",
                reason ? reason : "<none>",
                pumpCount,
                CurrentThreadIdForLog());
            DebugLine(line);
        }
    }

    static IUnknown* StoreAcquiredCoreWindow(IUnknown* acquired) {
        if (!acquired) {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(g_coreWindowMutex);
        if (g_coreWindowUnknown) {
            acquired->Release();
            return g_coreWindowUnknown;
        }

        g_coreWindowUnknown = acquired;
        char line[160] = {};
        std::snprintf(line, sizeof(line), "CoreWindow published for EGL inside GLFW shim iface=%p", g_coreWindowUnknown);
        DebugLine(line);
        return g_coreWindowUnknown;
    }

    static IUnknown* ReplaceStoredCoreWindow(IUnknown* acquired, const char* reason) {
        if (!acquired) {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(g_coreWindowMutex);
        if (g_coreWindowUnknown) {
            g_coreWindowUnknown->Release();
        }

        g_coreWindowUnknown = acquired;
        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "CoreWindow replaced for EGL inside GLFW shim iface=%p reason=%s",
            g_coreWindowUnknown,
            reason ? reason : "<none>");
        DebugLine(line);
        return g_coreWindowUnknown;
    }

    static IUnknown* AcquireCoreWindowUnknown() {
        if (IsPreferNativeCoreWindowMode()) {
            IUnknown* acquired = TryAcquireCoreWindowForCurrentThread();
            if (acquired) {
                return ReplaceStoredCoreWindow(acquired, "GetForCurrentThread preferred");
            }

            acquired = TryAcquireCoreWindowFromApplicationProperties();
            if (acquired) {
                return ReplaceStoredCoreWindow(acquired, "CoreApplication.Properties preferred");
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_coreWindowMutex);
            if (g_coreWindowUnknown) {
                return g_coreWindowUnknown;
            }
        }

        IUnknown* acquired = TryAcquireCoreWindowFromApplicationProperties();
        if (!acquired) {
            acquired = TryAcquireCoreWindowForCurrentThread();
        }
        return StoreAcquiredCoreWindow(acquired);
    }

    static IUnknown* BuildNativeEglWindowDescriptorUnknown() {
        IUnknown* coreWindowUnknown = AcquireCoreWindowUnknown();
        if (!coreWindowUnknown) {
            DebugLine("Native EGL PropertySet descriptor skipped; no CoreWindow is available");
            return nullptr;
        }

        ComPtr<IActivationFactory> propSetFactory;
        HRESULT hr = GetActivationFactory(
            HStringReference(RuntimeClass_Windows_Foundation_Collections_PropertySet).Get(),
            propSetFactory.GetAddressOf());
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "PropertySet factory failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        ComPtr<IInspectable> propertySetInspectable;
        hr = propSetFactory->ActivateInstance(propertySetInspectable.GetAddressOf());
        if (FAILED(hr) || !propertySetInspectable) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "PropertySet ActivateInstance failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        ComPtr<IPropertySet> propertySet;
        hr = propertySetInspectable.As(&propertySet);
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "PropertySet As(IPropertySet) failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        ComPtr<IMap<HSTRING, IInspectable*>> propMap;
        hr = propertySet.As(&propMap);
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "PropertySet As(IMap) failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        ComPtr<IInspectable> coreWindowInspectable;
        hr = coreWindowUnknown->QueryInterface(kIidInspectable, reinterpret_cast<void**>(coreWindowInspectable.GetAddressOf()));
        if (FAILED(hr) || !coreWindowInspectable) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "CoreWindow As(IInspectable) failed for PropertySet hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        boolean replaced = false;
        hr = propMap->Insert(HStringReference(kEglNativeWindowTypeProperty).Get(), coreWindowInspectable.Get(), &replaced);
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "PropertySet Insert(EGLNativeWindowTypeProperty) failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return nullptr;
        }

        ComPtr<IPropertyValueStatics> valueStatics;
        hr = GetActivationFactory(
            HStringReference(RuntimeClass_Windows_Foundation_PropertyValue).Get(),
            valueStatics.GetAddressOf());
        if (SUCCEEDED(hr) && valueStatics) {
            const int renderWidth = g_eglWidth > 0
                ? g_eglWidth
                : QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_WIDTH", kDefaultWidth);
            const int renderHeight = g_eglHeight > 0
                ? g_eglHeight
                : QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_HEIGHT", kDefaultHeight);
            const int surfaceWidth = QueryLaunchDimension(L"MINECRAFT_XBOX_SURFACE_WIDTH", renderWidth);
            const int surfaceHeight = QueryLaunchDimension(L"MINECRAFT_XBOX_SURFACE_HEIGHT", renderHeight);
            Size size = {};
            size.Width = static_cast<FLOAT>(surfaceWidth);
            size.Height = static_cast<FLOAT>(surfaceHeight);
            ComPtr<IInspectable> sizeInspectable;
            hr = valueStatics->CreateSize(size, sizeInspectable.GetAddressOf());
            if (SUCCEEDED(hr) && sizeInspectable) {
                propMap->Insert(HStringReference(kEglRenderSurfaceSizeProperty).Get(), sizeInspectable.Get(), &replaced);
            }
        }

        IUnknown* descriptor = QueryInterfaceForEglObject(
            propertySetInspectable.Get(),
            kIidInspectable,
            "Native EGL PropertySet IInspectable");
        if (descriptor) {
            char line[176] = {};
            std::snprintf(
                line,
                sizeof(line),
                "Native EGL PropertySet descriptor built surface=%dx%d render=%dx%d iface=%p",
                QueryLaunchDimension(L"MINECRAFT_XBOX_SURFACE_WIDTH", g_eglWidth),
                QueryLaunchDimension(L"MINECRAFT_XBOX_SURFACE_HEIGHT", g_eglHeight),
                g_eglWidth,
                g_eglHeight,
                descriptor);
            DebugLine(line);
        }
        return descriptor;
    }

    static IUnknown* StoreAcquiredEglWindowDescriptor(IUnknown* acquired) {
        if (!acquired) {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(g_coreWindowMutex);
        if (g_eglWindowDescriptorUnknown) {
            acquired->Release();
            return g_eglWindowDescriptorUnknown;
        }

        g_eglWindowDescriptorUnknown = acquired;
        return g_eglWindowDescriptorUnknown;
    }

    static IUnknown* ReplaceStoredEglWindowDescriptor(IUnknown* acquired, const char* reason) {
        if (!acquired) {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(g_coreWindowMutex);
        if (g_eglWindowDescriptorUnknown) {
            g_eglWindowDescriptorUnknown->Release();
        }

        g_eglWindowDescriptorUnknown = acquired;
        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "EGL window descriptor replaced inside GLFW shim iface=%p reason=%s",
            g_eglWindowDescriptorUnknown,
            reason ? reason : "<none>");
        DebugLine(line);
        return g_eglWindowDescriptorUnknown;
    }

    static IUnknown* AcquireEglWindowDescriptorUnknown() {
        if (IsPreferNativeEglDescriptorMode()) {
            IUnknown* descriptor = BuildNativeEglWindowDescriptorUnknown();
            if (descriptor) {
                return ReplaceStoredEglWindowDescriptor(descriptor, "native PropertySet preferred");
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_coreWindowMutex);
            if (g_eglWindowDescriptorUnknown) {
                return g_eglWindowDescriptorUnknown;
            }
        }

        return StoreAcquiredEglWindowDescriptor(BuildNativeEglWindowDescriptorUnknown());
    }

    using CoreWindowKeyEventHandler =
        ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_t;
    using CoreWindowCharEventHandler =
        ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_t;
    using CoreWindowPointerEventHandler =
        ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CPointerEventArgs_t;
    using CoreDispatcherAcceleratorKeyEventHandler =
        ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreDispatcher_Windows__CUI__CCore__CAcceleratorKeyEventArgs_t;
    using BackRequestedEventHandler =
        ABI::Windows::Foundation::__FIEventHandler_1_Windows__CUI__CCore__CBackRequestedEventArgs_t;

    static std::mutex g_keyboardHookMutex;
    static ComPtr<CoreWindowKeyEventHandler> g_keyDownHandler;
    static ComPtr<CoreWindowKeyEventHandler> g_keyUpHandler;
    static ComPtr<CoreWindowCharEventHandler> g_charReceivedHandler;
    static ComPtr<CoreDispatcherAcceleratorKeyEventHandler> g_acceleratorKeyHandler;
    static ComPtr<ICoreAcceleratorKeys> g_acceleratorKeys;
    static ComPtr<BackRequestedEventHandler> g_backRequestedHandler;
    static EventRegistrationToken g_keyDownToken = {};
    static EventRegistrationToken g_keyUpToken = {};
    static EventRegistrationToken g_charReceivedToken = {};
    static EventRegistrationToken g_acceleratorKeyToken = {};
    static EventRegistrationToken g_backRequestedToken = {};
    static int g_keyboardHooksInstalled = 0;
    static int g_characterHookInstalled = 0;
    static int g_acceleratorKeyHookInstalled = 0;
    static int g_backRequestedHookInstalled = 0;
    static std::atomic<int> g_keyboardEventLogCount{ 0 };
    static std::atomic<int> g_backRequestedLogCount{ 0 };

    static std::mutex g_mouseHookMutex;
    static std::mutex g_mousePositionMutex;
    static ComPtr<CoreWindowPointerEventHandler> g_pointerMovedHandler;
    static ComPtr<CoreWindowPointerEventHandler> g_pointerPressedHandler;
    static ComPtr<CoreWindowPointerEventHandler> g_pointerReleasedHandler;
    static ComPtr<CoreWindowPointerEventHandler> g_pointerWheelHandler;
    static ComPtr<CoreWindowPointerEventHandler> g_pointerEnteredHandler;
    static ComPtr<CoreWindowPointerEventHandler> g_pointerExitedHandler;
    static EventRegistrationToken g_pointerMovedToken = {};
    static EventRegistrationToken g_pointerPressedToken = {};
    static EventRegistrationToken g_pointerReleasedToken = {};
    static EventRegistrationToken g_pointerWheelToken = {};
    static EventRegistrationToken g_pointerEnteredToken = {};
    static EventRegistrationToken g_pointerExitedToken = {};
    static int g_mouseHooksInstalled = 0;
    static double g_cursorX = 0.0;
static double g_cursorY = 0.0;
static std::atomic<int> g_cursorInside{ 0 };
static std::atomic<int> g_cursorMode{ GLFW_CURSOR_NORMAL };
static std::atomic<int> g_stickyMouseButtons{ 0 };
static std::atomic<int> g_rawMouseMotion{ 0 };
static std::atomic<int> g_mouseEventLogCount{ 0 };

    static bool IsGlfwKeyIndex(int key) {
        return key >= 0 && key <= GLFW_KEY_LAST;
    }

    static bool IsGlfwMouseButtonIndex(int button) {
        return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST;
    }

    static void ResetKeyboardState() {
        for (int key = 0; key <= GLFW_KEY_LAST; ++key) {
            g_keyStates[key].store(GLFW_RELEASE, std::memory_order_release);
        }
    }

    static void ResetMouseState() {
        for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
            g_mouseButtonStates[button].store(GLFW_RELEASE, std::memory_order_release);
        }
        {
            std::lock_guard<std::mutex> lock(g_mousePositionMutex);
            g_cursorX = 0.0;
            g_cursorY = 0.0;
        }
        g_cursorInside.store(0, std::memory_order_release);
        g_gamepadMouseMode.store(0, std::memory_order_release);
        g_gamepadMouseToggleLatch.store(0, std::memory_order_release);
        g_gamepadMouseLastTickMs.store(0, std::memory_order_release);
        g_gamepadMouseLastWheelMs.store(0, std::memory_order_release);
    }

    static bool IsTrackedKeyPressed(int key) {
        return IsGlfwKeyIndex(key) &&
            g_keyStates[key].load(std::memory_order_acquire) == GLFW_PRESS;
    }

    static int CurrentKeyboardModsFromState() {
        int mods = 0;
        if (IsTrackedKeyPressed(GLFW_KEY_LEFT_SHIFT) || IsTrackedKeyPressed(GLFW_KEY_RIGHT_SHIFT)) {
            mods |= GLFW_MOD_SHIFT;
        }
        if (IsTrackedKeyPressed(GLFW_KEY_LEFT_CONTROL) || IsTrackedKeyPressed(GLFW_KEY_RIGHT_CONTROL)) {
            mods |= GLFW_MOD_CONTROL;
        }
        if (IsTrackedKeyPressed(GLFW_KEY_LEFT_ALT) || IsTrackedKeyPressed(GLFW_KEY_RIGHT_ALT)) {
            mods |= GLFW_MOD_ALT;
        }
        if (IsTrackedKeyPressed(GLFW_KEY_LEFT_SUPER) || IsTrackedKeyPressed(GLFW_KEY_RIGHT_SUPER)) {
            mods |= GLFW_MOD_SUPER;
        }
        return mods;
    }

    static bool IsCoreWindowGamepadVirtualKey(VirtualKey virtualKey) {
        const int vk = static_cast<int>(virtualKey);
        return (vk >= 138 && vk <= 143) || // NavigationUp..NavigationCancel
            (vk >= 195 && vk <= 206);      // GamepadA..GamepadDPadRight
    }

    static bool IsGamepadKeyboardBridgeDisabled() {
        wchar_t value[16] = {};
        DWORD length = GetEnvironmentVariableW(
            L"MINECRAFT_XBOX_DISABLE_GAMEPAD_KEYBOARD_BRIDGE",
            value,
            static_cast<DWORD>(std::size(value)));
        return length > 0 && length < std::size(value) && value[0] != L'\0' && value[0] != L'0';
    }

    static bool ShouldMarkCoreWindowGamepadEventHandled(VirtualKey virtualKey) {
        return IsCoreWindowGamepadVirtualKey(virtualKey);
    }

    static bool ShouldSuppressCoreWindowGamepadKeyboardEvent(VirtualKey virtualKey) {
        return (IsGamepadKeyboardBridgeDisabled() || IsGamepadMouseModeActive()) &&
            IsCoreWindowGamepadVirtualKey(virtualKey);
    }

    static int MapVirtualKeyToGlfw(VirtualKey virtualKey) {
        const int vk = static_cast<int>(virtualKey);
        if ((vk >= 65 && vk <= 90) || (vk >= 48 && vk <= 57)) {
            return vk;
        }
        if (vk >= 112 && vk <= 135) {
            return GLFW_KEY_F1 + (vk - 112);
        }
        if (vk >= 96 && vk <= 105) {
            return GLFW_KEY_KP_0 + (vk - 96);
        }

        switch (vk) {
        case 8: return GLFW_KEY_BACKSPACE;
        case 9: return GLFW_KEY_TAB;
        case 13: return GLFW_KEY_ENTER;
        case 16: return GLFW_KEY_LEFT_SHIFT;
        case 17: return GLFW_KEY_LEFT_CONTROL;
        case 18: return GLFW_KEY_LEFT_ALT;
        case 19: return GLFW_KEY_PAUSE;
        case 20: return GLFW_KEY_CAPS_LOCK;
        case 27: return GLFW_KEY_ESCAPE;
        case 30: return GLFW_KEY_ENTER; // Accept
        case 32: return GLFW_KEY_SPACE;
        case 33: return GLFW_KEY_PAGE_UP;
        case 34: return GLFW_KEY_PAGE_DOWN;
        case 35: return GLFW_KEY_END;
        case 36: return GLFW_KEY_HOME;
        case 37: return GLFW_KEY_LEFT;
        case 38: return GLFW_KEY_UP;
        case 39: return GLFW_KEY_RIGHT;
        case 40: return GLFW_KEY_DOWN;
        case 44: return GLFW_KEY_PRINT_SCREEN;
        case 45: return GLFW_KEY_INSERT;
        case 46: return GLFW_KEY_DELETE;
        case 91: return GLFW_KEY_LEFT_SUPER;
        case 92: return GLFW_KEY_RIGHT_SUPER;
        case 93: return GLFW_KEY_MENU;
        case 106: return GLFW_KEY_KP_MULTIPLY;
        case 107: return GLFW_KEY_KP_ADD;
        case 109: return GLFW_KEY_KP_SUBTRACT;
        case 110: return GLFW_KEY_KP_DECIMAL;
        case 111: return GLFW_KEY_KP_DIVIDE;
        case 138: return GLFW_KEY_UP;    // NavigationUp
        case 139: return GLFW_KEY_DOWN;  // NavigationDown
        case 140: return GLFW_KEY_LEFT;  // NavigationLeft
        case 141: return GLFW_KEY_RIGHT; // NavigationRight
        case 142: return GLFW_KEY_ENTER; // NavigationAccept
        case 143: return GLFW_KEY_ESCAPE; // NavigationCancel
        case 144: return GLFW_KEY_NUM_LOCK;
        case 145: return GLFW_KEY_SCROLL_LOCK;
        case 160: return GLFW_KEY_LEFT_SHIFT;
        case 161: return GLFW_KEY_RIGHT_SHIFT;
        case 162: return GLFW_KEY_LEFT_CONTROL;
        case 163: return GLFW_KEY_RIGHT_CONTROL;
        case 164: return GLFW_KEY_LEFT_ALT;
        case 165: return GLFW_KEY_RIGHT_ALT;
        case 186: return GLFW_KEY_SEMICOLON;
        case 187: return GLFW_KEY_EQUAL;
        case 188: return GLFW_KEY_COMMA;
        case 189: return GLFW_KEY_MINUS;
        case 190: return GLFW_KEY_PERIOD;
        case 191: return GLFW_KEY_SLASH;
        case 192: return GLFW_KEY_GRAVE_ACCENT;
        case 195: return GLFW_KEY_ENTER; // GamepadA
        case 196: return GLFW_KEY_ESCAPE; // GamepadB
        case 203: return GLFW_KEY_UP;    // GamepadDPadUp
        case 204: return GLFW_KEY_DOWN;  // GamepadDPadDown
        case 205: return GLFW_KEY_LEFT;  // GamepadDPadLeft
        case 206: return GLFW_KEY_RIGHT; // GamepadDPadRight
        case 219: return GLFW_KEY_LEFT_BRACKET;
        case 220: return GLFW_KEY_BACKSLASH;
        case 221: return GLFW_KEY_RIGHT_BRACKET;
        case 222: return GLFW_KEY_APOSTROPHE;
        default: return GLFW_KEY_UNKNOWN;
        }
    }

    static ICoreWindow* AcquireKeyboardCoreWindow(bool acquireIfMissing) {
        ICoreWindow* coreWindow = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_coreWindowMutex);
            if (g_coreWindowUnknown) {
                g_coreWindowUnknown->QueryInterface(
                    kIidCoreWindow,
                    reinterpret_cast<void**>(&coreWindow));
            }
        }

        if (!coreWindow && acquireIfMissing) {
            IUnknown* unknown = AcquireCoreWindowUnknown();
            if (unknown) {
                unknown->QueryInterface(
                    kIidCoreWindow,
                    reinterpret_cast<void**>(&coreWindow));
            }
        }
        return coreWindow;
    }

    struct MousePointerSample {
        double x = 0.0;
        double y = 0.0;
        unsigned char buttons[GLFW_MOUSE_BUTTON_LAST + 1] = {};
        int wheelDelta = 0;
        bool horizontalWheel = false;
    };

    static void ScaleCoreWindowPointer(ICoreWindow* coreWindow, Point point, double& x, double& y) {
        double targetWidth = static_cast<double>(g_eglWidth > 0
            ? g_eglWidth
            : QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_WIDTH", kDefaultWidth));
        double targetHeight = static_cast<double>(g_eglHeight > 0
            ? g_eglHeight
            : QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_HEIGHT", kDefaultHeight));

        Rect bounds = {};
        if (coreWindow) {
            coreWindow->get_Bounds(&bounds);
        }

        double boundsWidth = bounds.Width > 1.0f ? static_cast<double>(bounds.Width) : targetWidth;
        double boundsHeight = bounds.Height > 1.0f ? static_cast<double>(bounds.Height) : targetHeight;
        x = static_cast<double>(point.X) * targetWidth / boundsWidth;
        y = static_cast<double>(point.Y) * targetHeight / boundsHeight;

        if (x < 0.0) x = 0.0;
        if (y < 0.0) y = 0.0;
        if (targetWidth > 0.0 && x > targetWidth) x = targetWidth;
        if (targetHeight > 0.0 && y > targetHeight) y = targetHeight;
    }

    static bool ReadCoreWindowPointerSample(
        ICoreWindow* coreWindow,
        IPointerEventArgs* args,
        MousePointerSample& sample) {
        if (!args) {
            return false;
        }

        ComPtr<ABI::Windows::UI::Input::IPointerPoint> point;
        HRESULT hr = args->get_CurrentPoint(point.GetAddressOf());
        if (FAILED(hr) || !point) {
            return false;
        }

        Point position = {};
        point->get_Position(&position);
        ScaleCoreWindowPointer(coreWindow, position, sample.x, sample.y);

        ComPtr<ABI::Windows::UI::Input::IPointerPointProperties> properties;
        hr = point->get_Properties(properties.GetAddressOf());
        if (FAILED(hr) || !properties) {
            return true;
        }

        boolean pressed = false;
        if (SUCCEEDED(properties->get_IsLeftButtonPressed(&pressed))) {
            sample.buttons[GLFW_MOUSE_BUTTON_LEFT] = pressed ? GLFW_PRESS : GLFW_RELEASE;
        }
        pressed = false;
        if (SUCCEEDED(properties->get_IsRightButtonPressed(&pressed))) {
            sample.buttons[GLFW_MOUSE_BUTTON_RIGHT] = pressed ? GLFW_PRESS : GLFW_RELEASE;
        }
        pressed = false;
        if (SUCCEEDED(properties->get_IsMiddleButtonPressed(&pressed))) {
            sample.buttons[GLFW_MOUSE_BUTTON_MIDDLE] = pressed ? GLFW_PRESS : GLFW_RELEASE;
        }
        pressed = false;
        if (SUCCEEDED(properties->get_IsXButton1Pressed(&pressed))) {
            sample.buttons[GLFW_MOUSE_BUTTON_4] = pressed ? GLFW_PRESS : GLFW_RELEASE;
        }
        pressed = false;
        if (SUCCEEDED(properties->get_IsXButton2Pressed(&pressed))) {
            sample.buttons[GLFW_MOUSE_BUTTON_5] = pressed ? GLFW_PRESS : GLFW_RELEASE;
        }

        INT32 wheelDelta = 0;
        if (SUCCEEDED(properties->get_MouseWheelDelta(&wheelDelta))) {
            sample.wheelDelta = static_cast<int>(wheelDelta);
        }
        pressed = false;
        if (SUCCEEDED(properties->get_IsHorizontalMouseWheel(&pressed))) {
            sample.horizontalWheel = pressed != false;
        }

        return true;
    }

    static void DispatchCursorEnter(bool entered) {
        int next = entered ? 1 : 0;
        int previous = g_cursorInside.exchange(next, std::memory_order_acq_rel);
        if (previous == next) {
            return;
        }

        GLFWcursorenterfun callback = g_window.cbCursorEnter;
        if (callback && g_window_created.load(std::memory_order_acquire)) {
            callback(kPrimaryWindow, entered ? GLFW_TRUE : GLFW_FALSE);
        }
    }

    static void DispatchCursorPosition(double x, double y) {
        {
            std::lock_guard<std::mutex> lock(g_mousePositionMutex);
            g_cursorX = x;
            g_cursorY = y;
        }

        DispatchCursorEnter(true);
        GLFWcursorposfun callback = g_window.cbCursorPos;
        if (callback && g_window_created.load(std::memory_order_acquire)) {
            callback(kPrimaryWindow, x, y);
        }
    }

    static void DispatchMouseButtons(MousePointerSample const& sample) {
        const int mods = CurrentKeyboardModsFromState();
        GLFWmousebuttonfun callback = g_window.cbMouseButton;
        for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
            unsigned char next = sample.buttons[button];
            unsigned char previous = g_mouseButtonStates[button].exchange(next, std::memory_order_acq_rel);
            if (previous == next) {
                continue;
            }
            if (callback && g_window_created.load(std::memory_order_acquire)) {
                callback(kPrimaryWindow, button, next == GLFW_PRESS ? GLFW_PRESS : GLFW_RELEASE, mods);
            }
        }
    }

    static void DispatchMouseWheel(MousePointerSample const& sample) {
        if (sample.wheelDelta == 0) {
            return;
        }

        GLFWscrollfun callback = g_window.cbScroll;
        if (callback && g_window_created.load(std::memory_order_acquire)) {
            double offset = static_cast<double>(sample.wheelDelta) / 120.0;
            callback(
                kPrimaryWindow,
                sample.horizontalWheel ? offset : 0.0,
                sample.horizontalWheel ? 0.0 : offset);
        }
    }

    static double ApplyGamepadMouseDeadzone(double value) {
        const double deadzone = 0.16;
        double magnitude = value < 0.0 ? -value : value;
        if (magnitude <= deadzone) {
            return 0.0;
        }

        double scaled = (magnitude - deadzone) / (1.0 - deadzone);
        return value < 0.0 ? -scaled : scaled;
    }

    static void DispatchGamepadMouseModeTick() {
        GamepadReading reading = {};
        if (!TryReadPrimaryGamepad(reading)) {
            return;
        }

        const bool toggled = UpdateGamepadMouseModeToggle(reading);
        if (!IsGamepadMouseModeActive()) {
            if (toggled) {
                MousePointerSample releaseSample = {};
                DispatchMouseButtons(releaseSample);
            }
            return;
        }

        const unsigned long long now = GetTickCount64();
        unsigned long long previousTick = g_gamepadMouseLastTickMs.exchange(now, std::memory_order_acq_rel);
        double deltaSeconds = previousTick == 0 ? (1.0 / 60.0) : static_cast<double>(now - previousTick) / 1000.0;
        if (deltaSeconds <= 0.0) {
            deltaSeconds = 1.0 / 120.0;
        }
        if (deltaSeconds > 0.05) {
            deltaSeconds = 0.05;
        }

        double targetWidth = static_cast<double>(g_eglWidth > 0
            ? g_eglWidth
            : QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_WIDTH", kDefaultWidth));
        double targetHeight = static_cast<double>(g_eglHeight > 0
            ? g_eglHeight
            : QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_HEIGHT", kDefaultHeight));
        if (targetWidth < 1.0) {
            targetWidth = static_cast<double>(kDefaultWidth);
        }
        if (targetHeight < 1.0) {
            targetHeight = static_cast<double>(kDefaultHeight);
        }

        double cursorX = 0.0;
        double cursorY = 0.0;
        {
            std::lock_guard<std::mutex> lock(g_mousePositionMutex);
            cursorX = g_cursorX;
            cursorY = g_cursorY;
        }

        if (g_cursorInside.load(std::memory_order_acquire) == 0 && cursorX == 0.0 && cursorY == 0.0) {
            cursorX = targetWidth * 0.5;
            cursorY = targetHeight * 0.5;
        }

        double xAxis = ApplyGamepadMouseDeadzone(reading.LeftThumbstickX);
        double yAxis = ApplyGamepadMouseDeadzone(-reading.LeftThumbstickY);
        xAxis += ApplyGamepadMouseDeadzone(reading.RightThumbstickX) * 0.75;
        yAxis += ApplyGamepadMouseDeadzone(-reading.RightThumbstickY) * 0.75;
        if (xAxis > 1.0) xAxis = 1.0;
        if (xAxis < -1.0) xAxis = -1.0;
        if (yAxis > 1.0) yAxis = 1.0;
        if (yAxis < -1.0) yAxis = -1.0;

        const double baseSpeed = (std::max)(targetWidth, targetHeight) * 0.85;
        cursorX += xAxis * baseSpeed * deltaSeconds;
        cursorY += yAxis * baseSpeed * deltaSeconds;
        if (cursorX < 0.0) cursorX = 0.0;
        if (cursorY < 0.0) cursorY = 0.0;
        if (cursorX > targetWidth) cursorX = targetWidth;
        if (cursorY > targetHeight) cursorY = targetHeight;

        MousePointerSample sample = {};
        sample.x = cursorX;
        sample.y = cursorY;
        sample.buttons[GLFW_MOUSE_BUTTON_LEFT] =
            (HasButton(reading.Buttons, GamepadButtons_A) || reading.RightTrigger > 0.55)
                ? GLFW_PRESS
                : GLFW_RELEASE;
        sample.buttons[GLFW_MOUSE_BUTTON_RIGHT] =
            (HasButton(reading.Buttons, GamepadButtons_B) || reading.LeftTrigger > 0.55)
                ? GLFW_PRESS
                : GLFW_RELEASE;
        sample.buttons[GLFW_MOUSE_BUTTON_MIDDLE] =
            HasButton(reading.Buttons, GamepadButtons_X) ? GLFW_PRESS : GLFW_RELEASE;

        unsigned long long lastWheel = g_gamepadMouseLastWheelMs.load(std::memory_order_acquire);
        if (now - lastWheel >= 120ull) {
            if (HasButton(reading.Buttons, GamepadButtons_DPadUp)) {
                sample.wheelDelta = 120;
                g_gamepadMouseLastWheelMs.store(now, std::memory_order_release);
            }
            else if (HasButton(reading.Buttons, GamepadButtons_DPadDown)) {
                sample.wheelDelta = -120;
                g_gamepadMouseLastWheelMs.store(now, std::memory_order_release);
            }
        }

        DispatchCursorPosition(sample.x, sample.y);
        DispatchMouseButtons(sample);
        DispatchMouseWheel(sample);

        const int logCount = g_gamepadMouseEventLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (toggled || logCount <= 16 || (logCount % 300) == 0) {
            char line[224] = {};
            std::snprintf(
                line,
                sizeof(line),
                "Controller mouse mode tick x=%.1f y=%.1f axis=%.2f/%.2f buttons=%d%d%d wheel=%d callback=%d/%d/%d",
                sample.x,
                sample.y,
                xAxis,
                yAxis,
                sample.buttons[GLFW_MOUSE_BUTTON_LEFT] == GLFW_PRESS ? 1 : 0,
                sample.buttons[GLFW_MOUSE_BUTTON_RIGHT] == GLFW_PRESS ? 1 : 0,
                sample.buttons[GLFW_MOUSE_BUTTON_MIDDLE] == GLFW_PRESS ? 1 : 0,
                sample.wheelDelta,
                g_window.cbCursorPos ? 1 : 0,
                g_window.cbMouseButton ? 1 : 0,
                g_window.cbScroll ? 1 : 0);
            DebugLine(line);
        }
    }

    static void DispatchPointerSample(
        ICoreWindow* coreWindow,
        IPointerEventArgs* args,
        bool updateButtons,
        bool updateWheel,
        const char* reason) {
        MousePointerSample sample = {};
        if (!ReadCoreWindowPointerSample(coreWindow, args, sample)) {
            return;
        }

        DispatchCursorPosition(sample.x, sample.y);
        if (updateButtons) {
            DispatchMouseButtons(sample);
        }
        if (updateWheel) {
            DispatchMouseWheel(sample);
        }

        const int logCount = g_mouseEventLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (logCount <= 64 || (logCount % 300) == 0) {
            char line[224] = {};
            std::snprintf(
                line,
                sizeof(line),
                "CoreWindow mouse %s x=%.1f y=%.1f buttons=%d%d%d%d%d wheel=%d callback=%d/%d/%d",
                reason ? reason : "event",
                sample.x,
                sample.y,
                sample.buttons[GLFW_MOUSE_BUTTON_LEFT] == GLFW_PRESS ? 1 : 0,
                sample.buttons[GLFW_MOUSE_BUTTON_RIGHT] == GLFW_PRESS ? 1 : 0,
                sample.buttons[GLFW_MOUSE_BUTTON_MIDDLE] == GLFW_PRESS ? 1 : 0,
                sample.buttons[GLFW_MOUSE_BUTTON_4] == GLFW_PRESS ? 1 : 0,
                sample.buttons[GLFW_MOUSE_BUTTON_5] == GLFW_PRESS ? 1 : 0,
                sample.wheelDelta,
                g_window.cbCursorPos ? 1 : 0,
                g_window.cbMouseButton ? 1 : 0,
                g_window.cbScroll ? 1 : 0);
            DebugLine(line);
        }
    }

    static void DispatchCoreWindowKey(VirtualKey virtualKey, const CorePhysicalKeyStatus& status, bool keyDown) {
        if (ShouldSuppressCoreWindowGamepadKeyboardEvent(virtualKey)) {
            const int logCount = g_keyboardEventLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (logCount <= 64 || (logCount % 300) == 0) {
                char line[160] = {};
                std::snprintf(
                    line,
                    sizeof(line),
                    "CoreWindow gamepad keyboard event suppressed vk=%d action=%d",
                    static_cast<int>(virtualKey),
                    keyDown ? 1 : 0);
                DebugLine(line);
            }
            return;
        }

        const int key = MapVirtualKeyToGlfw(virtualKey);
        if (!IsGlfwKeyIndex(key)) {
            return;
        }

        const int action = keyDown
            ? ((status.WasKeyDown || status.RepeatCount > 1) ? GLFW_REPEAT : GLFW_PRESS)
            : GLFW_RELEASE;
        g_keyStates[key].store(action == GLFW_RELEASE ? GLFW_RELEASE : GLFW_PRESS, std::memory_order_release);

        const int mods = CurrentKeyboardModsFromState();
        GLFWkeyfun callback = g_window.cbKey;
        if (callback && g_window_created.load(std::memory_order_acquire)) {
            callback(kPrimaryWindow, key, static_cast<int>(status.ScanCode), action, mods);
        }

        const int logCount = g_keyboardEventLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (logCount <= 64 || (logCount % 300) == 0) {
            char line[192] = {};
            std::snprintf(
                line,
                sizeof(line),
                "CoreWindow keyboard event vk=%d glfw=%d scan=%u action=%d mods=0x%x callback=%d",
                static_cast<int>(virtualKey),
                key,
                status.ScanCode,
                action,
                mods,
                callback ? 1 : 0);
            DebugLine(line);
        }
    }

    static void DispatchCoreWindowChar(UINT32 codepoint) {
        if (codepoint < 0x20 || codepoint == 0x7f) {
            return;
        }

        GLFWcharfun charCallback = g_window.cbChar;
        if (charCallback && g_window_created.load(std::memory_order_acquire)) {
            charCallback(kPrimaryWindow, codepoint);
        }

        GLFWcharmodsfun charModsCallback = g_window.cbCharMods;
        if (charModsCallback && g_window_created.load(std::memory_order_acquire)) {
            charModsCallback(kPrimaryWindow, codepoint, CurrentKeyboardModsFromState());
        }
    }

    static void MarkCoreWindowEventHandled(IInspectable* args, const char* reason, VirtualKey virtualKey) {
        if (!args) {
            return;
        }

        ComPtr<ICoreWindowEventArgs> coreWindowArgs;
        HRESULT hr = args->QueryInterface(__uuidof(ICoreWindowEventArgs), reinterpret_cast<void**>(coreWindowArgs.GetAddressOf()));
        if (FAILED(hr) || !coreWindowArgs) {
            static std::atomic<int> queryLogCount{ 0 };
            int count = queryLogCount.fetch_add(1, std::memory_order_relaxed);
            if (count < 8) {
                char line[192] = {};
                std::snprintf(
                    line,
                    sizeof(line),
                    "CoreWindow %s event could not be marked handled vk=%d hr=0x%08lx",
                    reason ? reason : "key",
                    static_cast<int>(virtualKey),
                    static_cast<unsigned long>(hr));
                DebugLine(line);
            }
            return;
        }

        hr = coreWindowArgs->put_Handled(true);
        const int logCount = g_keyboardEventLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (logCount <= 64 || (logCount % 300) == 0 || FAILED(hr)) {
            char line[192] = {};
            std::snprintf(
                line,
                sizeof(line),
                "CoreWindow %s event marked handled vk=%d hr=0x%08lx",
                reason ? reason : "key",
                static_cast<int>(virtualKey),
                static_cast<unsigned long>(hr));
            DebugLine(line);
        }
    }

    static bool InstallBackRequestedHook() {
        if (g_backRequestedHookInstalled) {
            return true;
        }

        ComPtr<ISystemNavigationManagerStatics> navigationStatics;
        HRESULT hr = GetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Core_SystemNavigationManager).Get(),
            navigationStatics.GetAddressOf());
        if (FAILED(hr) || !navigationStatics) {
            char line[192] = {};
            std::snprintf(
                line,
                sizeof(line),
                "SystemNavigationManager BackRequested hook skipped; activation factory hr=0x%08lx",
                static_cast<unsigned long>(hr));
            DebugLine(line);
            return false;
        }

        ComPtr<ISystemNavigationManager> navigationManager;
        hr = navigationStatics->GetForCurrentView(navigationManager.GetAddressOf());
        if (FAILED(hr) || !navigationManager) {
            char line[192] = {};
            std::snprintf(
                line,
                sizeof(line),
                "SystemNavigationManager BackRequested hook skipped; GetForCurrentView hr=0x%08lx",
                static_cast<unsigned long>(hr));
            DebugLine(line);
            return false;
        }

        g_backRequestedHandler = Microsoft::WRL::Callback<BackRequestedEventHandler>(
            [](IInspectable*, IBackRequestedEventArgs* args) -> HRESULT {
                if (args) {
                    HRESULT hr = args->put_Handled(true);
                    const int logCount = g_backRequestedLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (logCount <= 16 || (logCount % 120) == 0 || FAILED(hr)) {
                        char line[176] = {};
                        std::snprintf(
                            line,
                            sizeof(line),
                            "SystemNavigationManager BackRequested handled while GLFW is active hr=0x%08lx",
                            static_cast<unsigned long>(hr));
                        DebugLine(line);
                    }
                }
                return S_OK;
            });
        if (!g_backRequestedHandler) {
            DebugLine("SystemNavigationManager BackRequested hook failed; handler allocation returned null");
            return false;
        }

        hr = navigationManager->add_BackRequested(g_backRequestedHandler.Get(), &g_backRequestedToken);
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "SystemNavigationManager add_BackRequested failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            g_backRequestedHandler.Reset();
            return false;
        }

        g_backRequestedHookInstalled = 1;
        DebugLine("SystemNavigationManager BackRequested hook installed");
        return true;
    }

    static void RemoveBackRequestedHook() {
        if (!g_backRequestedHookInstalled) {
            g_backRequestedHandler.Reset();
            return;
        }

        ComPtr<ISystemNavigationManagerStatics> navigationStatics;
        HRESULT hr = GetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Core_SystemNavigationManager).Get(),
            navigationStatics.GetAddressOf());
        if (SUCCEEDED(hr) && navigationStatics) {
            ComPtr<ISystemNavigationManager> navigationManager;
            hr = navigationStatics->GetForCurrentView(navigationManager.GetAddressOf());
            if (SUCCEEDED(hr) && navigationManager) {
                navigationManager->remove_BackRequested(g_backRequestedToken);
            }
        }

        g_backRequestedToken.value = 0;
        g_backRequestedHookInstalled = 0;
        g_backRequestedHandler.Reset();
        DebugLine("SystemNavigationManager BackRequested hook removed");
    }

    static bool InstallAcceleratorKeyHook(ICoreWindow* coreWindow) {
        if (g_acceleratorKeyHookInstalled) {
            return true;
        }
        if (!coreWindow) {
            DebugLine("CoreDispatcher accelerator key hook skipped; no CoreWindow is available");
            return false;
        }

        ComPtr<ICoreDispatcher> dispatcher;
        HRESULT hr = coreWindow->get_Dispatcher(dispatcher.GetAddressOf());
        if (FAILED(hr) || !dispatcher) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "CoreDispatcher accelerator key hook skipped; get_Dispatcher hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return false;
        }

        ComPtr<ICoreAcceleratorKeys> acceleratorKeys;
        hr = dispatcher.As(&acceleratorKeys);
        if (FAILED(hr) || !acceleratorKeys) {
            char line[192] = {};
            std::snprintf(line, sizeof(line), "CoreDispatcher accelerator key hook skipped; ICoreAcceleratorKeys hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            return false;
        }

        g_acceleratorKeyHandler = Microsoft::WRL::Callback<CoreDispatcherAcceleratorKeyEventHandler>(
            [](ICoreDispatcher*, IAcceleratorKeyEventArgs* args) -> HRESULT {
                if (!args) {
                    return S_OK;
                }

                VirtualKey virtualKey = VirtualKey_None;
                CoreAcceleratorKeyEventType eventType = CoreAcceleratorKeyEventType_KeyDown;
                args->get_VirtualKey(&virtualKey);
                args->get_EventType(&eventType);

                if (ShouldMarkCoreWindowGamepadEventHandled(virtualKey)) {
                    MarkCoreWindowEventHandled(args, "Accelerator", virtualKey);
                    const int logCount = g_keyboardEventLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (logCount <= 64 || (logCount % 300) == 0) {
                        char line[192] = {};
                        std::snprintf(
                            line,
                            sizeof(line),
                            "CoreDispatcher accelerator gamepad event handled vk=%d eventType=%d",
                            static_cast<int>(virtualKey),
                            static_cast<int>(eventType));
                        DebugLine(line);
                    }
                }
                return S_OK;
            });
        if (!g_acceleratorKeyHandler) {
            DebugLine("CoreDispatcher accelerator key hook failed; handler allocation returned null");
            return false;
        }

        hr = acceleratorKeys->add_AcceleratorKeyActivated(g_acceleratorKeyHandler.Get(), &g_acceleratorKeyToken);
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(line, sizeof(line), "CoreDispatcher add_AcceleratorKeyActivated failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            g_acceleratorKeyHandler.Reset();
            return false;
        }

        g_acceleratorKeys = acceleratorKeys;
        g_acceleratorKeyHookInstalled = 1;
        DebugLine("CoreDispatcher accelerator key hook installed");
        return true;
    }

    static void RemoveAcceleratorKeyHook() {
        if (g_acceleratorKeyHookInstalled && g_acceleratorKeys) {
            g_acceleratorKeys->remove_AcceleratorKeyActivated(g_acceleratorKeyToken);
        }

        g_acceleratorKeyToken.value = 0;
        g_acceleratorKeyHookInstalled = 0;
        g_acceleratorKeyHandler.Reset();
        g_acceleratorKeys.Reset();
        DebugLine("CoreDispatcher accelerator key hook removed");
    }

    static bool InstallKeyboardHooks() {
        std::lock_guard<std::mutex> lock(g_keyboardHookMutex);
        if (g_keyboardHooksInstalled) {
            return true;
        }

        ComPtr<ICoreWindow> coreWindow;
        coreWindow.Attach(AcquireKeyboardCoreWindow(true));
        if (!coreWindow) {
            DebugLine("CoreWindow keyboard hooks skipped; no CoreWindow is available");
            return false;
        }

        g_keyDownHandler = Microsoft::WRL::Callback<CoreWindowKeyEventHandler>(
            [](ICoreWindow*, IKeyEventArgs* args) -> HRESULT {
                if (!args) {
                    return S_OK;
                }
                VirtualKey virtualKey = VirtualKey_None;
                CorePhysicalKeyStatus status = {};
                args->get_VirtualKey(&virtualKey);
                args->get_KeyStatus(&status);
                if (ShouldMarkCoreWindowGamepadEventHandled(virtualKey)) {
                    MarkCoreWindowEventHandled(args, "KeyDown", virtualKey);
                }
                DispatchCoreWindowKey(virtualKey, status, true);
                return S_OK;
            });
        g_keyUpHandler = Microsoft::WRL::Callback<CoreWindowKeyEventHandler>(
            [](ICoreWindow*, IKeyEventArgs* args) -> HRESULT {
                if (!args) {
                    return S_OK;
                }
                VirtualKey virtualKey = VirtualKey_None;
                CorePhysicalKeyStatus status = {};
                args->get_VirtualKey(&virtualKey);
                args->get_KeyStatus(&status);
                if (ShouldMarkCoreWindowGamepadEventHandled(virtualKey)) {
                    MarkCoreWindowEventHandled(args, "KeyUp", virtualKey);
                }
                DispatchCoreWindowKey(virtualKey, status, false);
                return S_OK;
            });
        g_charReceivedHandler = Microsoft::WRL::Callback<CoreWindowCharEventHandler>(
            [](ICoreWindow*, ICharacterReceivedEventArgs* args) -> HRESULT {
                if (!args) {
                    return S_OK;
                }
                UINT32 codepoint = 0;
                args->get_KeyCode(&codepoint);
                DispatchCoreWindowChar(codepoint);
                return S_OK;
            });

        if (!g_keyDownHandler || !g_keyUpHandler) {
            DebugLine("CoreWindow keyboard hooks failed; event handler allocation returned null");
            g_keyDownHandler.Reset();
            g_keyUpHandler.Reset();
            g_charReceivedHandler.Reset();
            return false;
        }

        HRESULT hr = coreWindow->add_KeyDown(g_keyDownHandler.Get(), &g_keyDownToken);
        if (FAILED(hr)) {
            char line[160] = {};
            std::snprintf(line, sizeof(line), "CoreWindow add_KeyDown failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            g_keyDownHandler.Reset();
            g_keyUpHandler.Reset();
            g_charReceivedHandler.Reset();
            return false;
        }

        hr = coreWindow->add_KeyUp(g_keyUpHandler.Get(), &g_keyUpToken);
        if (FAILED(hr)) {
            char line[160] = {};
            std::snprintf(line, sizeof(line), "CoreWindow add_KeyUp failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            coreWindow->remove_KeyDown(g_keyDownToken);
            g_keyDownToken.value = 0;
            g_keyDownHandler.Reset();
            g_keyUpHandler.Reset();
            g_charReceivedHandler.Reset();
            return false;
        }

        g_characterHookInstalled = 0;
        if (g_charReceivedHandler) {
            hr = coreWindow->add_CharacterReceived(g_charReceivedHandler.Get(), &g_charReceivedToken);
            if (SUCCEEDED(hr)) {
                g_characterHookInstalled = 1;
            }
            else {
                char line[176] = {};
                std::snprintf(
                    line,
                    sizeof(line),
                    "CoreWindow add_CharacterReceived failed hr=0x%08lx; key navigation remains enabled",
                    static_cast<unsigned long>(hr));
                DebugLine(line);
                g_charReceivedHandler.Reset();
            }
        }

        g_keyboardHooksInstalled = 1;
        InstallAcceleratorKeyHook(coreWindow.Get());
        InstallBackRequestedHook();
        DebugLine(g_characterHookInstalled
            ? "CoreWindow keyboard hooks installed with character input"
            : "CoreWindow keyboard hooks installed without character input");
        return true;
    }

    static void RemoveKeyboardHooks() {
        std::lock_guard<std::mutex> lock(g_keyboardHookMutex);
        if (!g_keyboardHooksInstalled && !g_characterHookInstalled && !g_acceleratorKeyHookInstalled) {
            ResetKeyboardState();
            return;
        }

        ComPtr<ICoreWindow> coreWindow;
        coreWindow.Attach(AcquireKeyboardCoreWindow(false));
        if (coreWindow) {
            if (g_characterHookInstalled) {
                coreWindow->remove_CharacterReceived(g_charReceivedToken);
            }
            if (g_keyboardHooksInstalled) {
                coreWindow->remove_KeyUp(g_keyUpToken);
                coreWindow->remove_KeyDown(g_keyDownToken);
            }
        }

        g_keyDownToken.value = 0;
        g_keyUpToken.value = 0;
        g_charReceivedToken.value = 0;
        g_keyboardHooksInstalled = 0;
        g_characterHookInstalled = 0;
        g_keyDownHandler.Reset();
        g_keyUpHandler.Reset();
        g_charReceivedHandler.Reset();
        RemoveAcceleratorKeyHook();
        RemoveBackRequestedHook();
        ResetKeyboardState();
        DebugLine("CoreWindow keyboard hooks removed");
    }

    static bool InstallMouseHooks() {
        std::lock_guard<std::mutex> lock(g_mouseHookMutex);
        if (g_mouseHooksInstalled) {
            return true;
        }

        ComPtr<ICoreWindow> coreWindow;
        coreWindow.Attach(AcquireKeyboardCoreWindow(true));
        if (!coreWindow) {
            DebugLine("CoreWindow mouse hooks skipped; no CoreWindow is available");
            return false;
        }

        g_pointerMovedHandler = Microsoft::WRL::Callback<CoreWindowPointerEventHandler>(
            [](ICoreWindow* sender, IPointerEventArgs* args) -> HRESULT {
                DispatchPointerSample(sender, args, false, false, "move");
                return S_OK;
            });
        g_pointerPressedHandler = Microsoft::WRL::Callback<CoreWindowPointerEventHandler>(
            [](ICoreWindow* sender, IPointerEventArgs* args) -> HRESULT {
                if (sender) {
                    sender->SetPointerCapture();
                }
                DispatchPointerSample(sender, args, true, false, "press");
                return S_OK;
            });
        g_pointerReleasedHandler = Microsoft::WRL::Callback<CoreWindowPointerEventHandler>(
            [](ICoreWindow* sender, IPointerEventArgs* args) -> HRESULT {
                DispatchPointerSample(sender, args, true, false, "release");
                bool anyPressed = false;
                for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
                    if (g_mouseButtonStates[button].load(std::memory_order_acquire) == GLFW_PRESS) {
                        anyPressed = true;
                        break;
                    }
                }
                if (sender && !anyPressed) {
                    sender->ReleasePointerCapture();
                }
                return S_OK;
            });
        g_pointerWheelHandler = Microsoft::WRL::Callback<CoreWindowPointerEventHandler>(
            [](ICoreWindow* sender, IPointerEventArgs* args) -> HRESULT {
                DispatchPointerSample(sender, args, false, true, "wheel");
                return S_OK;
            });
        g_pointerEnteredHandler = Microsoft::WRL::Callback<CoreWindowPointerEventHandler>(
            [](ICoreWindow* sender, IPointerEventArgs* args) -> HRESULT {
                DispatchPointerSample(sender, args, false, false, "enter");
                DispatchCursorEnter(true);
                return S_OK;
            });
        g_pointerExitedHandler = Microsoft::WRL::Callback<CoreWindowPointerEventHandler>(
            [](ICoreWindow* sender, IPointerEventArgs* args) -> HRESULT {
                DispatchPointerSample(sender, args, true, false, "exit");
                DispatchCursorEnter(false);
                return S_OK;
            });

        if (!g_pointerMovedHandler || !g_pointerPressedHandler || !g_pointerReleasedHandler ||
            !g_pointerWheelHandler || !g_pointerEnteredHandler || !g_pointerExitedHandler) {
            DebugLine("CoreWindow mouse hooks failed; event handler allocation returned null");
            g_pointerMovedHandler.Reset();
            g_pointerPressedHandler.Reset();
            g_pointerReleasedHandler.Reset();
            g_pointerWheelHandler.Reset();
            g_pointerEnteredHandler.Reset();
            g_pointerExitedHandler.Reset();
            return false;
        }

        HRESULT hr = coreWindow->add_PointerMoved(g_pointerMovedHandler.Get(), &g_pointerMovedToken);
        if (FAILED(hr)) {
            char line[160] = {};
            std::snprintf(line, sizeof(line), "CoreWindow add_PointerMoved failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            g_pointerMovedHandler.Reset();
            g_pointerPressedHandler.Reset();
            g_pointerReleasedHandler.Reset();
            g_pointerWheelHandler.Reset();
            g_pointerEnteredHandler.Reset();
            g_pointerExitedHandler.Reset();
            return false;
        }

        hr = coreWindow->add_PointerPressed(g_pointerPressedHandler.Get(), &g_pointerPressedToken);
        if (FAILED(hr)) {
            char line[160] = {};
            std::snprintf(line, sizeof(line), "CoreWindow add_PointerPressed failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            coreWindow->remove_PointerMoved(g_pointerMovedToken);
            g_pointerMovedToken.value = 0;
            g_pointerMovedHandler.Reset();
            g_pointerPressedHandler.Reset();
            g_pointerReleasedHandler.Reset();
            g_pointerWheelHandler.Reset();
            g_pointerEnteredHandler.Reset();
            g_pointerExitedHandler.Reset();
            return false;
        }

        hr = coreWindow->add_PointerReleased(g_pointerReleasedHandler.Get(), &g_pointerReleasedToken);
        if (FAILED(hr)) {
            char line[160] = {};
            std::snprintf(line, sizeof(line), "CoreWindow add_PointerReleased failed hr=0x%08lx", static_cast<unsigned long>(hr));
            DebugLine(line);
            coreWindow->remove_PointerPressed(g_pointerPressedToken);
            coreWindow->remove_PointerMoved(g_pointerMovedToken);
            g_pointerPressedToken.value = 0;
            g_pointerMovedToken.value = 0;
            g_pointerMovedHandler.Reset();
            g_pointerPressedHandler.Reset();
            g_pointerReleasedHandler.Reset();
            g_pointerWheelHandler.Reset();
            g_pointerEnteredHandler.Reset();
            g_pointerExitedHandler.Reset();
            return false;
        }

        hr = coreWindow->add_PointerWheelChanged(g_pointerWheelHandler.Get(), &g_pointerWheelToken);
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(
                line,
                sizeof(line),
                "CoreWindow add_PointerWheelChanged failed hr=0x%08lx; mouse movement/buttons remain enabled",
                static_cast<unsigned long>(hr));
            DebugLine(line);
            g_pointerWheelHandler.Reset();
        }

        hr = coreWindow->add_PointerEntered(g_pointerEnteredHandler.Get(), &g_pointerEnteredToken);
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(
                line,
                sizeof(line),
                "CoreWindow add_PointerEntered failed hr=0x%08lx; cursor enter is inferred from movement",
                static_cast<unsigned long>(hr));
            DebugLine(line);
            g_pointerEnteredHandler.Reset();
        }

        hr = coreWindow->add_PointerExited(g_pointerExitedHandler.Get(), &g_pointerExitedToken);
        if (FAILED(hr)) {
            char line[176] = {};
            std::snprintf(
                line,
                sizeof(line),
                "CoreWindow add_PointerExited failed hr=0x%08lx; cursor exit events disabled",
                static_cast<unsigned long>(hr));
            DebugLine(line);
            g_pointerExitedHandler.Reset();
        }

        g_mouseHooksInstalled = 1;
        DebugLine("CoreWindow mouse hooks installed");
        return true;
    }

    static void RemoveMouseHooks() {
        std::lock_guard<std::mutex> lock(g_mouseHookMutex);
        if (!g_mouseHooksInstalled) {
            ResetMouseState();
            return;
        }

        ComPtr<ICoreWindow> coreWindow;
        coreWindow.Attach(AcquireKeyboardCoreWindow(false));
        if (coreWindow) {
            if (g_pointerExitedHandler) {
                coreWindow->remove_PointerExited(g_pointerExitedToken);
            }
            if (g_pointerEnteredHandler) {
                coreWindow->remove_PointerEntered(g_pointerEnteredToken);
            }
            if (g_pointerWheelHandler) {
                coreWindow->remove_PointerWheelChanged(g_pointerWheelToken);
            }
            if (g_pointerReleasedHandler) {
                coreWindow->remove_PointerReleased(g_pointerReleasedToken);
            }
            if (g_pointerPressedHandler) {
                coreWindow->remove_PointerPressed(g_pointerPressedToken);
            }
            if (g_pointerMovedHandler) {
                coreWindow->remove_PointerMoved(g_pointerMovedToken);
            }
        }

        g_pointerMovedToken.value = 0;
        g_pointerPressedToken.value = 0;
        g_pointerReleasedToken.value = 0;
        g_pointerWheelToken.value = 0;
        g_pointerEnteredToken.value = 0;
        g_pointerExitedToken.value = 0;
        g_mouseHooksInstalled = 0;
        g_pointerMovedHandler.Reset();
        g_pointerPressedHandler.Reset();
        g_pointerReleasedHandler.Reset();
        g_pointerWheelHandler.Reset();
        g_pointerEnteredHandler.Reset();
        g_pointerExitedHandler.Reset();
        ResetMouseState();
        DebugLine("CoreWindow mouse hooks removed");
    }

    static bool EnsureOpenGlModule() {
        if (IsFakeContextMode()) {
            static std::atomic<int> logged{ 0 };
            int expected = 0;
            if (logged.compare_exchange_strong(expected, 1)) {
                DebugLine("GLFW fake context mode active; skipping Mesa WGL LoadLibrary path");
            }
            return false;
        }
        if (g_wglDisabled.load(std::memory_order_acquire)) {
            return false;
        }
        if (g_openGlModule) {
            return true;
        }

        DebugLine("LoadLibraryW(opengl32.dll)");
        g_openGlModule = LoadLibraryW(L"opengl32.dll");
        if (!g_openGlModule) {
            char line[128] = {};
            std::snprintf(line, sizeof(line), "LoadLibraryW(opengl32.dll) failed; GetLastError=%lu", GetLastError());
            DebugLine(line);
            return false;
        }
        TryCallOpenGlProcInit();

        g_wglChoosePixelFormat = reinterpret_cast<WglChoosePixelFormatProc>(LoadOpenGlProc("wglChoosePixelFormat"));
        g_wglSetPixelFormat = reinterpret_cast<WglSetPixelFormatProc>(LoadOpenGlProc("wglSetPixelFormat"));
        g_wglCreateContext = reinterpret_cast<WglCreateContextProc>(LoadOpenGlProc("wglCreateContext"));
        g_wglDeleteContext = reinterpret_cast<WglDeleteContextProc>(LoadOpenGlProc("wglDeleteContext"));
        g_wglMakeCurrent = reinterpret_cast<WglMakeCurrentProc>(LoadOpenGlProc("wglMakeCurrent"));
        g_wglGetCurrentContext = reinterpret_cast<WglGetCurrentContextProc>(LoadOpenGlProc("wglGetCurrentContext"));
        g_wglGetProcAddress = reinterpret_cast<WglGetProcAddressProc>(LoadOpenGlProc("wglGetProcAddress"));
        g_wglSwapBuffers = reinterpret_cast<WglSwapBuffersProc>(LoadOpenGlProc("wglSwapBuffers"));

        if (!g_wglCreateContext || !g_wglMakeCurrent || !g_wglGetProcAddress) {
            DebugLine("Mesa WGL exports are incomplete");
            return false;
        }
        if (!g_wglGetCurrentContext) {
            DebugLine("Mesa WGL wglGetCurrentContext export missing; trusting wglMakeCurrent success");
        }

        DebugLine("Mesa WGL exports loaded");
        return true;
    }

    static bool EnsureMesaEglModule() {
        if (g_eglDisabled.load(std::memory_order_acquire)) {
            return false;
        }
        if (g_eglModule && g_openGlModule) {
            return true;
        }

        wchar_t openGlPath[1024] = {};
        wchar_t providerDirectory[1024] = {};
        const bool explicitOpenGlPath = GetOpenGlProviderPath(openGlPath, static_cast<DWORD>(sizeof(openGlPath) / sizeof(openGlPath[0])));
        GetDirectoryFromPath(openGlPath, providerDirectory, sizeof(providerDirectory) / sizeof(providerDirectory[0]));

        if (explicitOpenGlPath) {
            char line[320] = {};
            std::snprintf(line, sizeof(line), "LoadLibraryW(MINECRAFT_XBOX_OPENGL_DLL=%ls) for Mesa EGL", openGlPath);
            DebugLine(line);
        }
        else {
            DebugLine("LoadLibraryW(opengl32.dll) for Mesa EGL");
        }

        g_openGlModule = LoadLibraryW(openGlPath);
        if (!g_openGlModule) {
            char line[320] = {};
            std::snprintf(line, sizeof(line), "Mesa EGL LoadLibraryW(%ls) failed; GetLastError=%lu", openGlPath, GetLastError());
            DebugLine(line);
            return false;
        }
        TryCallOpenGlProcInit();

        DebugLine("LoadLibraryW(libEGL.dll)");
        g_eglModule = LoadDllFromDirectory(providerDirectory, L"libEGL.dll", true);
        if (!g_eglModule) {
            return false;
        }

        g_glesModule = LoadDllFromDirectory(providerDirectory, L"libGLESv2.dll", false);
        g_eglGetDisplay = reinterpret_cast<EglGetDisplayProc>(LoadEglProc("eglGetDisplay"));
        g_eglGetPlatformDisplay = reinterpret_cast<EglGetPlatformDisplayProc>(LoadEglProc("eglGetPlatformDisplay"));
        g_eglGetPlatformDisplayEXT = reinterpret_cast<EglGetPlatformDisplayExtProc>(LoadEglProc("eglGetPlatformDisplayEXT"));
        g_eglInitialize = reinterpret_cast<EglInitializeProc>(LoadEglProc("eglInitialize"));
        g_eglGetConfigs = reinterpret_cast<EglGetConfigsProc>(LoadEglProc("eglGetConfigs"));
        g_eglChooseConfig = reinterpret_cast<EglChooseConfigProc>(LoadEglProc("eglChooseConfig"));
        g_eglBindApi = reinterpret_cast<EglBindApiProc>(LoadEglProc("eglBindAPI"));
        g_eglCreatePbufferSurface = reinterpret_cast<EglCreatePbufferSurfaceProc>(LoadEglProc("eglCreatePbufferSurface"));
        g_eglCreateWindowSurface = reinterpret_cast<EglCreateWindowSurfaceProc>(LoadEglProc("eglCreateWindowSurface"));
        g_eglCreatePlatformWindowSurface = reinterpret_cast<EglCreatePlatformWindowSurfaceProc>(LoadEglProc("eglCreatePlatformWindowSurface"));
        g_eglCreatePlatformWindowSurfaceEXT = reinterpret_cast<EglCreatePlatformWindowSurfaceExtProc>(LoadEglProc("eglCreatePlatformWindowSurfaceEXT"));
        g_eglCreateContext = reinterpret_cast<EglCreateContextProc>(LoadEglProc("eglCreateContext"));
        g_eglMakeCurrent = reinterpret_cast<EglMakeCurrentProc>(LoadEglProc("eglMakeCurrent"));
        g_eglGetCurrentContext = reinterpret_cast<EglGetCurrentContextProc>(LoadEglProc("eglGetCurrentContext"));
        g_eglSwapBuffers = reinterpret_cast<EglSwapBuffersProc>(LoadEglProc("eglSwapBuffers"));
        g_eglSwapInterval = reinterpret_cast<EglSwapIntervalProc>(LoadEglProc("eglSwapInterval"));
        g_eglDestroyContext = reinterpret_cast<EglDestroyContextProc>(LoadEglProc("eglDestroyContext"));
        g_eglDestroySurface = reinterpret_cast<EglDestroySurfaceProc>(LoadEglProc("eglDestroySurface"));
        g_eglTerminate = reinterpret_cast<EglTerminateProc>(LoadEglProc("eglTerminate"));
        g_eglGetError = reinterpret_cast<EglGetErrorProc>(LoadEglProc("eglGetError"));
        g_eglGetProcAddress = reinterpret_cast<EglGetProcAddressProc>(LoadEglProc("eglGetProcAddress"));
        g_eglQueryString = reinterpret_cast<EglQueryStringProc>(LoadEglProc("eglQueryString"));

        if (!g_eglGetPlatformDisplayEXT && g_eglGetProcAddress) {
            __try {
                g_eglGetPlatformDisplayEXT = reinterpret_cast<EglGetPlatformDisplayExtProc>(
                    g_eglGetProcAddress("eglGetPlatformDisplayEXT"));
            }
            __except (LogEglException("eglGetProcAddress(eglGetPlatformDisplayEXT)", GetExceptionCode())) {
                return false;
            }
        }
        if (!g_eglCreatePlatformWindowSurfaceEXT && g_eglGetProcAddress) {
            __try {
                g_eglCreatePlatformWindowSurfaceEXT = reinterpret_cast<EglCreatePlatformWindowSurfaceExtProc>(
                    g_eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT"));
            }
            __except (LogEglException("eglGetProcAddress(eglCreatePlatformWindowSurfaceEXT)", GetExceptionCode())) {
                return false;
            }
        }
        if (!g_eglCreatePlatformWindowSurface && g_eglGetProcAddress) {
            __try {
                g_eglCreatePlatformWindowSurface = reinterpret_cast<EglCreatePlatformWindowSurfaceProc>(
                    g_eglGetProcAddress("eglCreatePlatformWindowSurface"));
            }
            __except (LogEglException("eglGetProcAddress(eglCreatePlatformWindowSurface)", GetExceptionCode())) {
                return false;
            }
        }

        if (!g_eglGetDisplay || !g_eglInitialize || !g_eglGetConfigs || !g_eglChooseConfig ||
            !g_eglBindApi || !g_eglCreatePbufferSurface || !g_eglCreateContext ||
            !g_eglMakeCurrent || !g_eglGetProcAddress) {
            DebugLine("Mesa EGL exports are incomplete");
            return false;
        }
        if (!g_eglCreateWindowSurface &&
            !g_eglCreatePlatformWindowSurface &&
            !g_eglCreatePlatformWindowSurfaceEXT) {
            DebugLine("Mesa EGL window surface exports missing; CoreWindow EGL surfaces unavailable");
        }

        DebugLine("Mesa EGL exports loaded");
        return true;
    }

    static bool CreateMesaEglContextForCurrentThread(const char* label) {
        if (g_eglDisplay == EGL_NO_DISPLAY || !g_eglConfig || !g_eglBindApi || !g_eglCreateContext || !g_eglMakeCurrent) {
            char line[192] = {};
            std::snprintf(line, sizeof(line), "Cannot create Mesa EGL context for %s; display/config/exports missing", label);
            DebugLine(line);
            return false;
        }
        if (g_eglSurface == EGL_NO_SURFACE && !g_eglSurfacelessContext.load(std::memory_order_acquire)) {
            char line[192] = {};
            std::snprintf(line, sizeof(line), "Cannot create Mesa EGL context for %s; no EGL surface is ready", label);
            DebugLine(line);
            return false;
        }

        const bool useGlesMode = IsXboxOneGraphicsRuntimeMode();
        EGLBoolean boundApi = EGL_FALSE_VALUE;
        DebugLine(useGlesMode ? "eglBindAPI(OpenGL_ES existing surface)" : "eglBindAPI(OpenGL existing surface)");
        __try {
            boundApi = g_eglBindApi(useGlesMode ? EGL_OPENGL_ES_API_VALUE : EGL_OPENGL_API_VALUE);
        }
        __except (LogEglException(useGlesMode ? "eglBindAPI(OpenGL_ES existing surface)" : "eglBindAPI(OpenGL existing surface)", GetExceptionCode())) {
            return false;
        }
        if (!boundApi) {
            char line[160] = {};
            std::snprintf(line, sizeof(line), "eglBindAPI(existing surface) failed err=0x%04x", EglErrorCode());
            DebugLine(line);
            return false;
        }

        EGLContext nextContext = EGL_NO_CONTEXT;
        if (useGlesMode) {
            const EGLint contextAttribsGles3[] = {
                EGL_CONTEXT_CLIENT_VERSION_VALUE, 3,
                EGL_NONE_VALUE
            };
            const EGLint contextAttribsGles2[] = {
                EGL_CONTEXT_CLIENT_VERSION_VALUE, 2,
                EGL_NONE_VALUE
            };
            DebugLine("eglCreateContext(existing surface GLES3)");
            __try {
                nextContext = g_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, contextAttribsGles3);
            }
            __except (LogEglException("eglCreateContext(existing surface GLES3)", GetExceptionCode())) {
                return false;
            }
            if (nextContext == EGL_NO_CONTEXT) {
                DebugLine("eglCreateContext(existing surface GLES2)");
                __try {
                    nextContext = g_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, contextAttribsGles2);
                }
                __except (LogEglException("eglCreateContext(existing surface GLES2)", GetExceptionCode())) {
                    return false;
                }
            }
        }
        else {
            const EGLint major = g_window_hint_context_major.load(std::memory_order_acquire);
            const EGLint minor = g_window_hint_context_minor.load(std::memory_order_acquire);
            const EGLint contextAttribsCore[] = {
                EGL_CONTEXT_MAJOR_VERSION_VALUE, major,
                EGL_CONTEXT_MINOR_VERSION_VALUE, minor,
                EGL_CONTEXT_OPENGL_PROFILE_MASK_VALUE, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_VALUE,
                EGL_NONE_VALUE
            };
            const EGLint contextAttribsVersion[] = {
                EGL_CONTEXT_MAJOR_VERSION_VALUE, major,
                EGL_CONTEXT_MINOR_VERSION_VALUE, minor,
                EGL_NONE_VALUE
            };
            DebugLine("eglCreateContext(existing surface core)");
            __try {
                nextContext = g_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, contextAttribsCore);
            }
            __except (LogEglException("eglCreateContext(existing surface core)", GetExceptionCode())) {
                return false;
            }
            if (nextContext == EGL_NO_CONTEXT) {
                DebugLine("eglCreateContext(existing surface version)");
                __try {
                    nextContext = g_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, contextAttribsVersion);
                }
                __except (LogEglException("eglCreateContext(existing surface version)", GetExceptionCode())) {
                    return false;
                }
            }
        }

        if (nextContext == EGL_NO_CONTEXT) {
            DebugLine("eglCreateContext(existing surface default)");
            __try {
                nextContext = g_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, nullptr);
            }
            __except (LogEglException("eglCreateContext(existing surface default)", GetExceptionCode())) {
                return false;
            }
        }
        if (nextContext == EGL_NO_CONTEXT) {
            char line[160] = {};
            std::snprintf(line, sizeof(line), "eglCreateContext(existing surface) failed err=0x%04x", EglErrorCode());
            DebugLine(line);
            return false;
        }

        EGLBoolean madeCurrent = EGL_FALSE_VALUE;
        DebugLine(g_eglSurfacelessContext.load(std::memory_order_acquire)
            ? "eglMakeCurrent(existing surface surfaceless new context)"
            : "eglMakeCurrent(existing CoreWindow surface new context)");
        __try {
            if (g_eglSurfacelessContext.load(std::memory_order_acquire)) {
                madeCurrent = g_eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, nextContext);
            }
            else {
                madeCurrent = g_eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, nextContext);
            }
        }
        __except (LogEglException("eglMakeCurrent(existing surface new context)", GetExceptionCode())) {
            if (g_eglDestroyContext) {
                __try {
                    g_eglDestroyContext(g_eglDisplay, nextContext);
                }
                __except (LogEglRecoverableException("eglDestroyContext(failed new context)", GetExceptionCode())) {
                }
            }
            return false;
        }
        if (!madeCurrent) {
            char line[160] = {};
            std::snprintf(line, sizeof(line), "eglMakeCurrent(existing surface new context) failed err=0x%04x", EglErrorCode());
            DebugLine(line);
            if (g_eglDestroyContext) {
                __try {
                    g_eglDestroyContext(g_eglDisplay, nextContext);
                }
                __except (LogEglRecoverableException("eglDestroyContext(unmade new context)", GetExceptionCode())) {
                }
            }
            return false;
        }

        g_eglContext = nextContext;
        g_glReadPixels = nullptr;
        if (g_eglGetProcAddress) {
            __try {
                g_glReadPixels = reinterpret_cast<GlReadPixelsProc>(g_eglGetProcAddress("glReadPixels"));
            }
            __except (LogEglException("eglGetProcAddress(glReadPixels existing surface)", GetExceptionCode())) {
                g_glReadPixels = nullptr;
            }
        }
        if (!g_glReadPixels && g_openGlModule) {
            g_glReadPixels = reinterpret_cast<GlReadPixelsProc>(GetProcAddress(g_openGlModule, "glReadPixels"));
        }
        if (!g_glReadPixels && g_glesModule) {
            g_glReadPixels = reinterpret_cast<GlReadPixelsProc>(GetProcAddress(g_glesModule, "glReadPixels"));
        }
        g_contextCurrent.store(1, std::memory_order_release);
        ApplyMesaEglSwapInterval("existing-surface-context-ready");

        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "Mesa EGL context ready on current thread for %s %dx%d surface=%s thread=0x%lx",
            label,
            g_eglWidth,
            g_eglHeight,
            g_eglSurfacelessContext.load(std::memory_order_acquire) ? "surfaceless" : "CoreWindow",
            CurrentThreadIdForLog());
        DebugLine(line);
        return true;
    }

    static bool EnsureMesaEglContext() {
        if (g_eglDisabled.load(std::memory_order_acquire)) {
            return false;
        }

        if (g_eglContext == EGL_NO_CONTEXT) {
            char line[192] = {};
            std::snprintf(
                line,
                sizeof(line),
                "EnsureMesaEglContext cold start thread=0x%lx",
                CurrentThreadIdForLog());
            DebugLine(line);
        }

        if (g_eglContext != EGL_NO_CONTEXT &&
            (g_eglSurface != EGL_NO_SURFACE || g_eglSurfacelessContext.load(std::memory_order_acquire)) &&
            g_eglDisplay != EGL_NO_DISPLAY) {
            if (g_contextCurrent.load(std::memory_order_acquire)) {
                return true;
            }
            EGLBoolean madeCurrent = EGL_FALSE_VALUE;
            DebugLine("eglMakeCurrent(existing)");
            __try {
                if (g_eglSurfacelessContext.load(std::memory_order_acquire)) {
                    madeCurrent = g_eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, g_eglContext);
                }
                else {
                    madeCurrent = g_eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext);
                }
            }
            __except (LogEglException("eglMakeCurrent(existing)", GetExceptionCode())) {
                return false;
            }
            if (madeCurrent) {
                g_contextCurrent.store(1, std::memory_order_release);
                return true;
            }

            char line[128] = {};
            std::snprintf(line, sizeof(line), "eglMakeCurrent(existing) failed err=0x%04x", EglErrorCode());
            DebugLine(line);
            return false;
        }

        if (g_eglContext == EGL_NO_CONTEXT &&
            g_eglDisplay != EGL_NO_DISPLAY &&
            g_eglSurface != EGL_NO_SURFACE &&
            g_eglConfig) {
            DebugLine("Mesa EGL CoreWindow surface already exists; creating context on current thread");
            return CreateMesaEglContextForCurrentThread("existing CoreWindow surface");
        }

        if (!EnsureMesaEglModule()) {
            return false;
        }
        if (g_eglDisabled.load(std::memory_order_acquire)) {
            return false;
        }

        bool usedSurfacelessDisplay = false;
        bool useCoreWindowSurface = false;
        bool defaultDisplayTried = false;
        IUnknown* coreWindowUnknown = AcquireCoreWindowUnknown();
        IUnknown* eglWindowDescriptorUnknown = AcquireEglWindowDescriptorUnknown();
        const bool haveWindowSurfaceObject = coreWindowUnknown || eglWindowDescriptorUnknown;
        const bool fatalOnSurfaceless = IsFatalOnSurfacelessMode();
        const bool useGlesMode = IsXboxOneGraphicsRuntimeMode();
        const bool allowCoreWindowSurface = haveWindowSurfaceObject &&
            !g_eglCoreWindowSurfaceDisabled.load(std::memory_order_acquire);
        DebugLine(useGlesMode
            ? "Mesa EGL graphics runtime=xboxone; using OpenGL ES3 config/context"
            : "Mesa EGL graphics runtime=desktop; using OpenGL config/context");
        if (coreWindowUnknown && !useGlesMode && !ShouldSkipMesaWglGalliumInit()) {
            TryInitializeMesaWglScreenForCoreWindow(coreWindowUnknown);
        }
        else if (coreWindowUnknown && ShouldSkipMesaWglGalliumInit()) {
            DebugLine("Skipping Mesa WGL gallium init for EGL-only presentation path");
        }
        else if (coreWindowUnknown) {
            DebugLine("Skipping Mesa WGL CoreWindow screen init for OpenGL ES runtime");
        }
        if (fatalOnSurfaceless && !haveWindowSurfaceObject) {
            DebugLine("CoreWindow host fatal mode has no CoreWindow/PropertySet; stopping before surfaceless fallback");
            return false;
        }
        if (haveWindowSurfaceObject && !allowCoreWindowSurface) {
            DebugLine("CoreWindow EGL surface disabled after previous failure; trying pbuffer display paths");
            if (fatalOnSurfaceless) {
                DebugLine("CoreWindow host fatal mode stops because the window surface path is disabled");
                return false;
            }
        }

        if (g_eglDisplay == EGL_NO_DISPLAY && allowCoreWindowSurface) {
            SetEnvironmentVariableA("EGL_PLATFORM", "windows");
            defaultDisplayTried = true;
            DebugLine("eglGetDisplay(EGL_DEFAULT_DISPLAY for CoreWindow surface)");
            __try {
                g_eglDisplay = g_eglGetDisplay(nullptr);
            }
            __except (LogEglException("eglGetDisplay(EGL_DEFAULT_DISPLAY for CoreWindow surface)", GetExceptionCode())) {
                return false;
            }

            if (g_eglDisplay != EGL_NO_DISPLAY) {
                useCoreWindowSurface = true;
                DebugLine("eglGetDisplay(EGL_DEFAULT_DISPLAY) returned display for CoreWindow surface");
            }
            else {
                char line[160] = {};
                std::snprintf(line, sizeof(line), "eglGetDisplay(EGL_DEFAULT_DISPLAY for CoreWindow surface) failed err=0x%04x", EglErrorCode());
                DebugLine(line);
            }
        }

        if (fatalOnSurfaceless && allowCoreWindowSurface && g_eglDisplay == EGL_NO_DISPLAY) {
            DebugLine("CoreWindow host fatal mode stops because EGL_DEFAULT_DISPLAY for CoreWindow failed");
            return false;
        }

        if (g_eglDisplay == EGL_NO_DISPLAY && g_eglGetPlatformDisplayEXT) {
            if (fatalOnSurfaceless) {
                DebugLine("CoreWindow host fatal mode skips EGL_PLATFORM_SURFACELESS_MESA display fallback");
                return false;
            }
            DebugLine("eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA)");
            __try {
                g_eglDisplay = g_eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA_VALUE, nullptr, nullptr);
            }
            __except (LogEglException("eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA)", GetExceptionCode())) {
                return false;
            }

            if (g_eglDisplay != EGL_NO_DISPLAY) {
                usedSurfacelessDisplay = true;
                DebugLine("eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA) returned display");
            }
            else {
                char line[160] = {};
                std::snprintf(line, sizeof(line), "eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA) failed err=0x%04x", EglErrorCode());
                DebugLine(line);
            }
        }

        if (g_eglDisplay == EGL_NO_DISPLAY && g_eglGetPlatformDisplay) {
            if (fatalOnSurfaceless) {
                DebugLine("CoreWindow host fatal mode skips EGL_PLATFORM_SURFACELESS_MESA display fallback");
                return false;
            }
            DebugLine("eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA)");
            __try {
                g_eglDisplay = g_eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA_VALUE, nullptr, nullptr);
            }
            __except (LogEglException("eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA)", GetExceptionCode())) {
                return false;
            }

            if (g_eglDisplay != EGL_NO_DISPLAY) {
                usedSurfacelessDisplay = true;
                DebugLine("eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA) returned display");
            }
            else {
                char line[160] = {};
                std::snprintf(line, sizeof(line), "eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA) failed err=0x%04x", EglErrorCode());
                DebugLine(line);
            }
        }

        if (g_eglDisplay == EGL_NO_DISPLAY) {
            defaultDisplayTried = true;
            DebugLine("eglGetDisplay(EGL_DEFAULT_DISPLAY)");
            __try {
                g_eglDisplay = g_eglGetDisplay(nullptr);
            }
            __except (LogEglException("eglGetDisplay(EGL_DEFAULT_DISPLAY)", GetExceptionCode())) {
                return false;
            }
        }
        if (g_eglDisplay == EGL_NO_DISPLAY) {
            char line[128] = {};
            std::snprintf(line, sizeof(line), "Mesa EGL display creation failed err=0x%04x", EglErrorCode());
            DebugLine(line);
            return false;
        }

        EGLint eglMajor = 0;
        EGLint eglMinor = 0;
        EGLBoolean initialized = EGL_FALSE_VALUE;
        DebugLine("eglInitialize");
        __try {
            initialized = g_eglInitialize(g_eglDisplay, &eglMajor, &eglMinor);
        }
        __except (LogEglException("eglInitialize", GetExceptionCode())) {
            return false;
        }
        if (!initialized) {
            char line[128] = {};
            std::snprintf(line, sizeof(line), "eglInitialize failed err=0x%04x", EglErrorCode());
            DebugLine(line);
            return false;
        }
        if (g_eglQueryString) {
            const char* vendor = nullptr;
            const char* version = nullptr;
            const char* clientApis = nullptr;
            const char* extensions = nullptr;
            __try {
                vendor = g_eglQueryString(g_eglDisplay, EGL_VENDOR_VALUE);
                version = g_eglQueryString(g_eglDisplay, EGL_VERSION_VALUE);
                clientApis = g_eglQueryString(g_eglDisplay, EGL_CLIENT_APIS_VALUE);
                extensions = g_eglQueryString(g_eglDisplay, EGL_EXTENSIONS_VALUE);
            }
            __except (LogEglException("eglQueryString", GetExceptionCode())) {
                vendor = nullptr;
                version = nullptr;
                clientApis = nullptr;
                extensions = nullptr;
            }

            char queryLine[512] = {};
            std::snprintf(
                queryLine,
                sizeof(queryLine),
                "eglQueryString vendor=%s version=%s clientApis=%s",
                vendor ? vendor : "<null>",
                version ? version : "<null>",
                clientApis ? clientApis : "<null>");
            DebugLine(queryLine);
            if (extensions) {
                char extensionsLine[900] = {};
                std::snprintf(extensionsLine, sizeof(extensionsLine), "eglQueryString extensions=%.760s", extensions);
                DebugLine(extensionsLine);
            }
        }
        if (g_eglGetConfigs) {
            EGLint totalConfigs = 0;
            EGLBoolean gotConfigs = EGL_FALSE_VALUE;
            DebugLine("eglGetConfigs(count)");
            __try {
                gotConfigs = g_eglGetConfigs(g_eglDisplay, nullptr, 0, &totalConfigs);
            }
            __except (LogEglException("eglGetConfigs", GetExceptionCode())) {
                gotConfigs = EGL_FALSE_VALUE;
            }

            char configsLine[160] = {};
            std::snprintf(
                configsLine,
                sizeof(configsLine),
                "eglGetConfigs count=%d ok=%d err=0x%04x",
                totalConfigs,
                gotConfigs ? 1 : 0,
                EglErrorCode());
            DebugLine(configsLine);
        }

        struct ConfigAttempt {
            const char* name;
            const EGLint* attribs;
        };

        auto chooseConfigWithFallbacks = [&](EGLint surfaceBit, const char* surfaceName) -> bool {
            const EGLint renderableType = useGlesMode ? EGL_OPENGL_ES3_BIT_VALUE : EGL_OPENGL_BIT_VALUE;
            const EGLint configAttribsStrict[] = {
                EGL_SURFACE_TYPE_VALUE, surfaceBit,
                EGL_RENDERABLE_TYPE_VALUE, renderableType,
                EGL_RED_SIZE_VALUE, 8,
                EGL_GREEN_SIZE_VALUE, 8,
                EGL_BLUE_SIZE_VALUE, 8,
                EGL_ALPHA_SIZE_VALUE, 8,
                EGL_DEPTH_SIZE_VALUE, 24,
                EGL_STENCIL_SIZE_VALUE, 8,
                EGL_NONE_VALUE
            };

            const EGLint configAttribsNoStencil[] = {
                EGL_SURFACE_TYPE_VALUE, surfaceBit,
                EGL_RENDERABLE_TYPE_VALUE, renderableType,
                EGL_RED_SIZE_VALUE, 8,
                EGL_GREEN_SIZE_VALUE, 8,
                EGL_BLUE_SIZE_VALUE, 8,
                EGL_ALPHA_SIZE_VALUE, 8,
                EGL_DEPTH_SIZE_VALUE, 24,
                EGL_NONE_VALUE
            };

            const EGLint configAttribsColorOnly[] = {
                EGL_SURFACE_TYPE_VALUE, surfaceBit,
                EGL_RENDERABLE_TYPE_VALUE, renderableType,
                EGL_RED_SIZE_VALUE, 8,
                EGL_GREEN_SIZE_VALUE, 8,
                EGL_BLUE_SIZE_VALUE, 8,
                EGL_ALPHA_SIZE_VALUE, 8,
                EGL_NONE_VALUE
            };

            const EGLint configAttribsRgbOnly[] = {
                EGL_SURFACE_TYPE_VALUE, surfaceBit,
                EGL_RENDERABLE_TYPE_VALUE, renderableType,
                EGL_RED_SIZE_VALUE, 8,
                EGL_GREEN_SIZE_VALUE, 8,
                EGL_BLUE_SIZE_VALUE, 8,
                EGL_NONE_VALUE
            };

            const EGLint configAttribsSurfaceOnly[] = {
                EGL_SURFACE_TYPE_VALUE, surfaceBit,
                EGL_NONE_VALUE
            };

            const EGLint configAttribsAny[] = {
                EGL_NONE_VALUE
            };

            const ConfigAttempt configAttempts[] = {
                { "rgba8 depth24 stencil8", configAttribsStrict },
                { "rgba8 depth24", configAttribsNoStencil },
                { "rgba8", configAttribsColorOnly },
                { "rgb8", configAttribsRgbOnly },
                { "any renderable", configAttribsSurfaceOnly },
                { "any config", configAttribsAny },
            };

            for (const auto& attempt : configAttempts) {
                EGLint configCount = 0;
                EGLBoolean choseConfig = EGL_FALSE_VALUE;
                g_eglConfig = nullptr;
                char attemptName[160] = {};
                std::snprintf(attemptName, sizeof(attemptName), "%s %s", surfaceName, attempt.name);
                char attemptLine[160] = {};
                std::snprintf(attemptLine, sizeof(attemptLine), "eglChooseConfig(%s)", attemptName);
                DebugLine(attemptLine);
                __try {
                    choseConfig = g_eglChooseConfig(g_eglDisplay, attempt.attribs, &g_eglConfig, 1, &configCount);
                }
                __except (LogEglException("eglChooseConfig", GetExceptionCode())) {
                    return false;
                }

                if (choseConfig && configCount > 0 && g_eglConfig) {
                    char successLine[160] = {};
                    std::snprintf(successLine, sizeof(successLine), "eglChooseConfig selected %s count=%d", attemptName, configCount);
                    DebugLine(successLine);
                    return true;
                }

                char failedLine[160] = {};
                std::snprintf(failedLine, sizeof(failedLine), "eglChooseConfig(%s) failed count=%d err=0x%04x", attemptName, configCount, EglErrorCode());
                DebugLine(failedLine);
            }

            return false;
        };

        auto createPbufferSurfaceWithFallbacks = [&](const char* label) -> bool {
            struct PbufferAttempt {
                int width;
                int height;
                const char* name;
            };
            const PbufferAttempt attempts[] = {
                { g_eglWidth, g_eglHeight, "requested" },
                { 1280, 720, "720p" },
                { 640, 360, "360p" },
                { 64, 64, "64x64" },
            };

            int lastWidth = 0;
            int lastHeight = 0;
            for (const auto& attempt : attempts) {
                if (attempt.width <= 0 || attempt.height <= 0) {
                    continue;
                }
                if (attempt.width == lastWidth && attempt.height == lastHeight) {
                    continue;
                }
                lastWidth = attempt.width;
                lastHeight = attempt.height;

                const EGLint surfaceAttribs[] = {
                    EGL_WIDTH_VALUE, attempt.width,
                    EGL_HEIGHT_VALUE, attempt.height,
                    EGL_NONE_VALUE
                };
                char createLine[192] = {};
                std::snprintf(
                    createLine,
                    sizeof(createLine),
                    "eglCreatePbufferSurface(%s %s %dx%d)",
                    label,
                    attempt.name,
                    attempt.width,
                    attempt.height);
                DebugLine(createLine);
                __try {
                    g_eglSurface = g_eglCreatePbufferSurface(g_eglDisplay, g_eglConfig, surfaceAttribs);
                }
                __except (LogEglException("eglCreatePbufferSurface", GetExceptionCode())) {
                    return false;
                }

                if (g_eglSurface != EGL_NO_SURFACE) {
                    if (attempt.width != g_eglWidth || attempt.height != g_eglHeight) {
                        g_eglWidth = attempt.width;
                        g_eglHeight = attempt.height;
                    }
                    char successLine[192] = {};
                    std::snprintf(successLine, sizeof(successLine), "eglCreatePbufferSurface selected %s %dx%d", attempt.name, attempt.width, attempt.height);
                    DebugLine(successLine);
                    return true;
                }

                char failedLine[192] = {};
                std::snprintf(
                    failedLine,
                    sizeof(failedLine),
                    "eglCreatePbufferSurface(%s %s %dx%d) failed err=0x%04x",
                    label,
                    attempt.name,
                    attempt.width,
                    attempt.height,
                    EglErrorCode());
                DebugLine(failedLine);
            }

            return false;
        };

        auto abandonEglDisplayForPbufferRetry = [&]() {
            DebugLine("Abandoning failed EGL display without reset before pbuffer retry");
            g_eglDisplay = EGL_NO_DISPLAY;
            g_eglConfig = nullptr;
            g_eglSurface = EGL_NO_SURFACE;
            g_eglContext = EGL_NO_CONTEXT;
            g_eglSurfacelessContext.store(0, std::memory_order_release);
            g_contextCurrent.store(0, std::memory_order_release);
        };

        auto logCurrentDisplayConfigCount = [&](const char* label) {
            if (!g_eglGetConfigs || g_eglDisplay == EGL_NO_DISPLAY) {
                return;
            }

            EGLint totalConfigs = 0;
            EGLBoolean gotConfigs = EGL_FALSE_VALUE;
            char countLine[192] = {};
            std::snprintf(countLine, sizeof(countLine), "eglGetConfigs(count %s)", label);
            DebugLine(countLine);
            __try {
                gotConfigs = g_eglGetConfigs(g_eglDisplay, nullptr, 0, &totalConfigs);
            }
            __except (LogEglRecoverableException("eglGetConfigs(count fallback)", GetExceptionCode())) {
                gotConfigs = EGL_FALSE_VALUE;
            }

            char configsLine[192] = {};
            std::snprintf(
                configsLine,
                sizeof(configsLine),
                "eglGetConfigs %s count=%d ok=%d err=0x%04x",
                label,
                totalConfigs,
                gotConfigs ? 1 : 0,
                EglErrorCode());
            DebugLine(configsLine);
        };

        auto tryFreshPbufferDisplay = [&]() -> bool {
            abandonEglDisplayForPbufferRetry();
            SetEnvironmentVariableA("EGL_PLATFORM", "surfaceless");

            if (g_eglGetPlatformDisplayEXT) {
                DebugLine("eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA) for pbuffer retry");
                __try {
                    g_eglDisplay = g_eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA_VALUE, nullptr, nullptr);
                }
                __except (LogEglRecoverableException("eglGetPlatformDisplayEXT(pbuffer retry)", GetExceptionCode())) {
                    g_eglDisplay = EGL_NO_DISPLAY;
                }
                if (g_eglDisplay != EGL_NO_DISPLAY) {
                    DebugLine("eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA) returned pbuffer retry display");
                }
                else {
                    char line[192] = {};
                    std::snprintf(line, sizeof(line), "eglGetPlatformDisplayEXT pbuffer retry failed err=0x%04x", EglErrorCode());
                    DebugLine(line);
                }
            }

            if (g_eglDisplay == EGL_NO_DISPLAY && g_eglGetPlatformDisplay) {
                DebugLine("eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA) for pbuffer retry");
                __try {
                    g_eglDisplay = g_eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA_VALUE, nullptr, nullptr);
                }
                __except (LogEglRecoverableException("eglGetPlatformDisplay(pbuffer retry)", GetExceptionCode())) {
                    g_eglDisplay = EGL_NO_DISPLAY;
                }
                if (g_eglDisplay != EGL_NO_DISPLAY) {
                    DebugLine("eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA) returned pbuffer retry display");
                }
                else {
                    char line[192] = {};
                    std::snprintf(line, sizeof(line), "eglGetPlatformDisplay pbuffer retry failed err=0x%04x", EglErrorCode());
                    DebugLine(line);
                }
            }

            if (g_eglDisplay == EGL_NO_DISPLAY) {
                DebugLine("eglGetDisplay(EGL_DEFAULT_DISPLAY) for pbuffer retry");
                __try {
                    g_eglDisplay = g_eglGetDisplay(nullptr);
                }
                __except (LogEglRecoverableException("eglGetDisplay(pbuffer retry)", GetExceptionCode())) {
                    g_eglDisplay = EGL_NO_DISPLAY;
                }
            }

            if (g_eglDisplay == EGL_NO_DISPLAY) {
                char line[160] = {};
                std::snprintf(line, sizeof(line), "pbuffer retry display creation failed err=0x%04x", EglErrorCode());
                DebugLine(line);
                return false;
            }

            eglMajor = 0;
            eglMinor = 0;
            initialized = EGL_FALSE_VALUE;
            DebugLine("eglInitialize(pbuffer retry display)");
            __try {
                initialized = g_eglInitialize(g_eglDisplay, &eglMajor, &eglMinor);
            }
            __except (LogEglException("eglInitialize(pbuffer retry display)", GetExceptionCode())) {
                return false;
            }
            if (!initialized) {
                char line[160] = {};
                std::snprintf(line, sizeof(line), "eglInitialize(pbuffer retry display) failed err=0x%04x", EglErrorCode());
                DebugLine(line);
                return false;
            }

            logCurrentDisplayConfigCount("pbuffer retry");
            if (!chooseConfigWithFallbacks(EGL_PBUFFER_BIT_VALUE, "pbuffer retry")) {
                DebugLine("pbuffer retry config selection failed");
                return false;
            }

            return createPbufferSurfaceWithFallbacks("retry");
        };

        EGLint desiredSurfaceBit = useCoreWindowSurface ? EGL_WINDOW_BIT_VALUE : EGL_PBUFFER_BIT_VALUE;
        const char* desiredSurfaceName = useCoreWindowSurface ? "window" : "pbuffer";
        bool usingPbufferSurface = !useCoreWindowSurface;

        if (!chooseConfigWithFallbacks(desiredSurfaceBit, desiredSurfaceName)) {
            if (useCoreWindowSurface) {
                if (fatalOnSurfaceless) {
                    DebugLine("CoreWindow host fatal mode stops because no CoreWindow window EGL config was found");
                    return false;
                }
                DebugLine("eglChooseConfig found no CoreWindow window configs; trying Mesa EGL pbuffer fallback");
                g_eglCoreWindowSurfaceDisabled.store(1, std::memory_order_release);
                useCoreWindowSurface = false;
                usingPbufferSurface = true;
                desiredSurfaceBit = EGL_PBUFFER_BIT_VALUE;
                desiredSurfaceName = "pbuffer fallback";

                if (!chooseConfigWithFallbacks(desiredSurfaceBit, desiredSurfaceName)) {
                    DebugLine("Mesa EGL pbuffer fallback config selection failed");
                    tryFreshPbufferDisplay();
                }
            }
            else if (!useCoreWindowSurface && usedSurfacelessDisplay && !defaultDisplayTried) {
                DebugLine("eglChooseConfig found no surfaceless configs; retrying EGL_DEFAULT_DISPLAY");
                if (g_eglTerminate && g_eglDisplay != EGL_NO_DISPLAY) {
                    __try {
                        g_eglTerminate(g_eglDisplay);
                    }
                    __except (LogEglException("eglTerminate(surfaceless)", GetExceptionCode())) {
                        return false;
                    }
                }

                g_eglDisplay = EGL_NO_DISPLAY;
                g_eglConfig = nullptr;
                usedSurfacelessDisplay = false;
                defaultDisplayTried = true;

                DebugLine("eglGetDisplay(EGL_DEFAULT_DISPLAY)");
                __try {
                    g_eglDisplay = g_eglGetDisplay(nullptr);
                }
                __except (LogEglException("eglGetDisplay(EGL_DEFAULT_DISPLAY)", GetExceptionCode())) {
                    return false;
                }
                if (g_eglDisplay == EGL_NO_DISPLAY) {
                    char line[128] = {};
                    std::snprintf(line, sizeof(line), "EGL_DEFAULT_DISPLAY creation failed err=0x%04x", EglErrorCode());
                    DebugLine(line);
                    return false;
                }

                eglMajor = 0;
                eglMinor = 0;
                initialized = EGL_FALSE_VALUE;
                DebugLine("eglInitialize(default display)");
                __try {
                    initialized = g_eglInitialize(g_eglDisplay, &eglMajor, &eglMinor);
                }
                __except (LogEglException("eglInitialize(default display)", GetExceptionCode())) {
                    return false;
                }
                if (!initialized) {
                    char line[128] = {};
                    std::snprintf(line, sizeof(line), "eglInitialize(default display) failed err=0x%04x", EglErrorCode());
                    DebugLine(line);
                    return false;
                }

                if (chooseConfigWithFallbacks(desiredSurfaceBit, desiredSurfaceName)) {
                    DebugLine("EGL_DEFAULT_DISPLAY provided a usable config");
                }
            }

            if (!g_eglConfig) {
                DebugLine("eglChooseConfig failed for all display/config fallback attempts");
                return false;
            }
        }

        if (!g_eglConfig) {
            DebugLine("eglChooseConfig failed for all display/config fallback attempts");
            return false;
        }

        EGLBoolean boundApi = EGL_FALSE_VALUE;
        DebugLine(useGlesMode ? "eglBindAPI(OpenGL_ES)" : "eglBindAPI(OpenGL)");
        __try {
            boundApi = g_eglBindApi(useGlesMode ? EGL_OPENGL_ES_API_VALUE : EGL_OPENGL_API_VALUE);
        }
        __except (LogEglException(useGlesMode ? "eglBindAPI(OpenGL_ES)" : "eglBindAPI(OpenGL)", GetExceptionCode())) {
            return false;
        }
        if (!boundApi) {
            char line[128] = {};
            std::snprintf(
                line,
                sizeof(line),
                "%s failed err=0x%04x",
                useGlesMode ? "eglBindAPI(OpenGL_ES)" : "eglBindAPI(OpenGL)",
                EglErrorCode());
            DebugLine(line);
            return false;
        }

        auto createWindowSurfaceWithFallbacks = [&]() -> bool {
            enum SurfaceMethod {
                SurfaceMethodWindow,
                SurfaceMethodPlatformExt,
                SurfaceMethodPlatformCore,
            };

            struct WindowSurfaceAttempt {
                const char* name;
                void* nativeWindow;
                SurfaceMethod method;
            };

            DebugLine("VISIBLE-LAUNCH-FIX: preferring raw CoreWindow EGL surface before PropertySet fallback");
            const WindowSurfaceAttempt attempts[] = {
                { "eglCreateWindowSurface(CoreWindow)", coreWindowUnknown, SurfaceMethodWindow },
                { "eglCreatePlatformWindowSurfaceEXT(CoreWindow)", coreWindowUnknown, SurfaceMethodPlatformExt },
                { "eglCreatePlatformWindowSurface(CoreWindow)", coreWindowUnknown, SurfaceMethodPlatformCore },
                { "eglCreateWindowSurface(PropertySet)", eglWindowDescriptorUnknown, SurfaceMethodWindow },
                { "eglCreatePlatformWindowSurfaceEXT(PropertySet)", eglWindowDescriptorUnknown, SurfaceMethodPlatformExt },
                { "eglCreatePlatformWindowSurface(PropertySet)", eglWindowDescriptorUnknown, SurfaceMethodPlatformCore },
            };

            for (const auto& attempt : attempts) {
                if (!attempt.nativeWindow) {
                    continue;
                }

                if (attempt.method == SurfaceMethodWindow && !g_eglCreateWindowSurface) {
                    continue;
                }
                if (attempt.method == SurfaceMethodPlatformExt && !g_eglCreatePlatformWindowSurfaceEXT) {
                    continue;
                }
                if (attempt.method == SurfaceMethodPlatformCore && !g_eglCreatePlatformWindowSurface) {
                    continue;
                }

                char attemptLine[224] = {};
                std::snprintf(
                    attemptLine,
                    sizeof(attemptLine),
                    "%s thread=0x%lx nativeWindow=%p",
                    attempt.name,
                    CurrentThreadIdForLog(),
                    attempt.nativeWindow);
                DebugLine(attemptLine);
                g_eglSurface = EGL_NO_SURFACE;
                __try {
                    if (attempt.method == SurfaceMethodWindow) {
                        g_eglSurface = g_eglCreateWindowSurface(g_eglDisplay, g_eglConfig, attempt.nativeWindow, nullptr);
                    }
                    else if (attempt.method == SurfaceMethodPlatformExt) {
                        g_eglSurface = g_eglCreatePlatformWindowSurfaceEXT(g_eglDisplay, g_eglConfig, attempt.nativeWindow, nullptr);
                    }
                    else {
                        g_eglSurface = g_eglCreatePlatformWindowSurface(g_eglDisplay, g_eglConfig, attempt.nativeWindow, nullptr);
                    }
                }
                __except (LogEglSurfaceFallbackException(attempt.name, GetExceptionCode())) {
                    g_eglSurface = EGL_NO_SURFACE;
                }

                if (g_eglSurface != EGL_NO_SURFACE) {
                    g_eglSurfacePath = attempt.name;
                    char line[192] = {};
                    std::snprintf(line, sizeof(line), "%s succeeded", attempt.name);
                    DebugLine(line);
                    return true;
                }

                char line[192] = {};
                std::snprintf(line, sizeof(line), "%s failed err=0x%04x", attempt.name, EglErrorCode());
                DebugLine(line);
            }

            DebugLine("All CoreWindow/PropertySet EGL window surface attempts failed");
            g_eglSurfacePath = "none";
            return false;
        };

        if (useCoreWindowSurface) {
            if (!createWindowSurfaceWithFallbacks()) {
                g_eglCoreWindowSurfaceDisabled.store(1, std::memory_order_release);
                if (fatalOnSurfaceless) {
                    DebugLine("CoreWindow window surface unavailable; fatal mode stops before pbuffer/surfaceless fallback");
                    return false;
                }
                DebugLine("CoreWindow window surface unavailable; trying Mesa EGL pbuffer fallback");
                if (!chooseConfigWithFallbacks(EGL_PBUFFER_BIT_VALUE, "pbuffer fallback")) {
                    DebugLine("Mesa EGL pbuffer fallback config selection failed");
                    tryFreshPbufferDisplay();
                }
                else if (!createPbufferSurfaceWithFallbacks("fallback")) {
                    DebugLine("Mesa EGL pbuffer fallback failed after CoreWindow surface attempt; retrying on fresh pbuffer display");
                    tryFreshPbufferDisplay();
                }
                usingPbufferSurface = true;
            }
        }
        else if (g_eglSurface == EGL_NO_SURFACE) {
            if (!createPbufferSurfaceWithFallbacks("primary")) {
                DebugLine("Mesa EGL pbuffer creation failed on current display; retrying on fresh pbuffer display");
                tryFreshPbufferDisplay();
            }
        }
        if (g_eglDisplay == EGL_NO_DISPLAY || !g_eglConfig) {
            DebugLine("Mesa EGL has no display/config after all surface fallback attempts");
            return false;
        }
        if (g_eglSurface == EGL_NO_SURFACE) {
            if (fatalOnSurfaceless) {
                DebugLine("No EGL window surface available; fatal mode stops before surfaceless context");
                return false;
            }
            DebugLine("No EGL surface available; trying surfaceless EGL context");
            g_eglSurfacePath = "surfaceless";
            g_eglSurfacelessContext.store(1, std::memory_order_release);
        }

        if (useGlesMode) {
            const EGLint contextAttribsGles3[] = {
                EGL_CONTEXT_CLIENT_VERSION_VALUE, 3,
                EGL_NONE_VALUE
            };
            const EGLint contextAttribsGles2[] = {
                EGL_CONTEXT_CLIENT_VERSION_VALUE, 2,
                EGL_NONE_VALUE
            };
            DebugLine("eglCreateContext(GLES3)");
            __try {
                g_eglContext = g_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, contextAttribsGles3);
            }
            __except (LogEglException("eglCreateContext(GLES3)", GetExceptionCode())) {
                return false;
            }
            if (g_eglContext == EGL_NO_CONTEXT) {
                DebugLine("eglCreateContext(GLES2)");
                __try {
                    g_eglContext = g_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, contextAttribsGles2);
                }
                __except (LogEglException("eglCreateContext(GLES2)", GetExceptionCode())) {
                    return false;
                }
            }
        }
        else {
            const EGLint major = g_window_hint_context_major.load(std::memory_order_acquire);
            const EGLint minor = g_window_hint_context_minor.load(std::memory_order_acquire);
            const EGLint contextAttribsCore[] = {
                EGL_CONTEXT_MAJOR_VERSION_VALUE, major,
                EGL_CONTEXT_MINOR_VERSION_VALUE, minor,
                EGL_CONTEXT_OPENGL_PROFILE_MASK_VALUE, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_VALUE,
                EGL_NONE_VALUE
            };
            const EGLint contextAttribsVersion[] = {
                EGL_CONTEXT_MAJOR_VERSION_VALUE, major,
                EGL_CONTEXT_MINOR_VERSION_VALUE, minor,
                EGL_NONE_VALUE
            };
            DebugLine("eglCreateContext(core)");
            __try {
                g_eglContext = g_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, contextAttribsCore);
            }
            __except (LogEglException("eglCreateContext(core)", GetExceptionCode())) {
                return false;
            }
            if (g_eglContext == EGL_NO_CONTEXT) {
                DebugLine("eglCreateContext(version)");
                __try {
                    g_eglContext = g_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, contextAttribsVersion);
                }
                __except (LogEglException("eglCreateContext(version)", GetExceptionCode())) {
                    return false;
                }
            }
        }
        if (g_eglContext == EGL_NO_CONTEXT) {
            DebugLine("eglCreateContext(default)");
            __try {
                g_eglContext = g_eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, nullptr);
            }
            __except (LogEglException("eglCreateContext(default)", GetExceptionCode())) {
                return false;
            }
        }
        if (g_eglContext == EGL_NO_CONTEXT) {
            char line[128] = {};
            std::snprintf(line, sizeof(line), "eglCreateContext failed err=0x%04x", EglErrorCode());
            DebugLine(line);
            return false;
        }

        EGLBoolean madeCurrent = EGL_FALSE_VALUE;
        DebugLine(g_eglSurfacelessContext.load(std::memory_order_acquire) ? "eglMakeCurrent(surfaceless)" : "eglMakeCurrent");
        __try {
            if (g_eglSurfacelessContext.load(std::memory_order_acquire)) {
                madeCurrent = g_eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, g_eglContext);
            }
            else {
                madeCurrent = g_eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext);
            }
        }
        __except (LogEglException("eglMakeCurrent", GetExceptionCode())) {
            return false;
        }
        if (!madeCurrent) {
            char line[128] = {};
            std::snprintf(line, sizeof(line), "eglMakeCurrent failed err=0x%04x", EglErrorCode());
            DebugLine(line);
            return false;
        }

        __try {
            g_glReadPixels = reinterpret_cast<GlReadPixelsProc>(g_eglGetProcAddress("glReadPixels"));
        }
        __except (LogEglException("eglGetProcAddress(glReadPixels)", GetExceptionCode())) {
            return false;
        }
        if (!g_glReadPixels && g_openGlModule) {
            g_glReadPixels = reinterpret_cast<GlReadPixelsProc>(GetProcAddress(g_openGlModule, "glReadPixels"));
        }
        if (!g_glReadPixels && g_glesModule) {
            g_glReadPixels = reinterpret_cast<GlReadPixelsProc>(GetProcAddress(g_glesModule, "glReadPixels"));
        }
        g_contextCurrent.store(1, std::memory_order_release);
        ApplyMesaEglSwapInterval("context-ready");
        char line[256] = {};
        std::snprintf(
            line,
            sizeof(line),
            "Mesa EGL context ready %dx%d EGL %d.%d surface=%s path=%s",
            g_eglWidth,
            g_eglHeight,
            eglMajor,
            eglMinor,
            g_eglSurfacelessContext.load(std::memory_order_acquire)
                ? "surfaceless"
                : (usingPbufferSurface ? (useCoreWindowSurface ? "pbuffer-fallback" : "pbuffer") : "CoreWindow"),
            g_eglSurfacePath ? g_eglSurfacePath : "<null>");
        DebugLine(line);
        return true;
    }

    static MinecraftXboxGlCommandState* EnsureGlCommandState() {
        if (MinecraftXboxIsGlCommandStateReady(g_glCommandState)) {
            return g_glCommandState;
        }

        if (!g_glCommandStateMapping) {
            g_glCommandStateMapping = CreateFileMappingW(
                INVALID_HANDLE_VALUE,
                nullptr,
                PAGE_READWRITE,
                0,
                static_cast<DWORD>(sizeof(MinecraftXboxGlCommandState)),
                kMinecraftXboxGlCommandStateName);
            if (!g_glCommandStateMapping) {
                return nullptr;
            }
        }

        if (!g_glCommandState) {
            g_glCommandState = static_cast<MinecraftXboxGlCommandState*>(
                MapViewOfFile(
                    g_glCommandStateMapping,
                    FILE_MAP_ALL_ACCESS,
                    0,
                    0,
                    sizeof(MinecraftXboxGlCommandState)));
            if (!g_glCommandState) {
                return nullptr;
            }
        }

        MinecraftXboxInitializeGlCommandState(g_glCommandState);
        return g_glCommandState;
    }

    using XglfwGLboolean = unsigned char;
    using XglfwGLbitfield = unsigned int;
    using XglfwGLenum = unsigned int;
    using XglfwGLuint = unsigned int;
    using XglfwGLint = int;
    using XglfwGLsizei = int;
    using XglfwGLsizeiptr = intptr_t;
    using XglfwGLintptr = intptr_t;
    using XglfwGLfloat = float;
    using XglfwGLdouble = double;

    static constexpr XglfwGLenum XGLFW_GL_BYTE = 0x1400;
    static constexpr XglfwGLenum XGLFW_GL_UNSIGNED_BYTE = 0x1401;
    static constexpr XglfwGLenum XGLFW_GL_SHORT = 0x1402;
    static constexpr XglfwGLenum XGLFW_GL_UNSIGNED_SHORT = 0x1403;
    static constexpr XglfwGLenum XGLFW_GL_INT = 0x1404;
    static constexpr XglfwGLenum XGLFW_GL_UNSIGNED_INT = 0x1405;
    static constexpr XglfwGLenum XGLFW_GL_FLOAT = 0x1406;
    static constexpr XglfwGLenum XGLFW_GL_UNSIGNED_SHORT_5_6_5 = 0x8363;
    static constexpr XglfwGLenum XGLFW_GL_UNSIGNED_SHORT_4_4_4_4 = 0x8033;
    static constexpr XglfwGLenum XGLFW_GL_UNSIGNED_SHORT_5_5_5_1 = 0x8034;
    static constexpr XglfwGLenum XGLFW_GL_UNSIGNED_INT_24_8 = 0x84FA;
    static constexpr XglfwGLenum XGLFW_GL_DEPTH_COMPONENT = 0x1902;
    static constexpr XglfwGLenum XGLFW_GL_DEPTH_STENCIL = 0x84F9;
    static constexpr XglfwGLenum XGLFW_GL_RED = 0x1903;
    static constexpr XglfwGLenum XGLFW_GL_RG = 0x8227;
    static constexpr XglfwGLenum XGLFW_GL_RGB = 0x1907;
    static constexpr XglfwGLenum XGLFW_GL_RGBA = 0x1908;
    static constexpr XglfwGLenum XGLFW_GL_BGRA = 0x80E1;
    static constexpr XglfwGLenum XGLFW_GL_TEXTURE0 = 0x84C0;
    static constexpr XglfwGLenum XGLFW_GL_TEXTURE_1D = 0x0DE0;
    static constexpr XglfwGLenum XGLFW_GL_TEXTURE_2D = 0x0DE1;
    static constexpr XglfwGLenum XGLFW_GL_TEXTURE_3D = 0x806F;
    static constexpr XglfwGLenum XGLFW_GL_TEXTURE_CUBE_MAP = 0x8513;
    static constexpr XglfwGLenum XGLFW_GL_TEXTURE_2D_ARRAY = 0x8C1A;
    static constexpr XglfwGLenum XGLFW_GL_ARRAY_BUFFER = 0x8892;
    static constexpr XglfwGLenum XGLFW_GL_ELEMENT_ARRAY_BUFFER = 0x8893;
    static constexpr XglfwGLenum XGLFW_GL_PIXEL_UNPACK_BUFFER = 0x88EC;
    static constexpr XglfwGLenum XGLFW_GL_UNIFORM_BUFFER = 0x8A11;
    static constexpr XglfwGLenum XGLFW_GL_COPY_READ_BUFFER = 0x8F36;
    static constexpr XglfwGLenum XGLFW_GL_COPY_WRITE_BUFFER = 0x8F37;
    static constexpr XglfwGLenum XGLFW_GL_DRAW_INDIRECT_BUFFER = 0x8F3F;
    static constexpr XglfwGLenum XGLFW_GL_SHADER_STORAGE_BUFFER = 0x90D2;
    static constexpr XglfwGLenum XGLFW_GL_FRAMEBUFFER = 0x8D40;
    static constexpr XglfwGLenum XGLFW_GL_READ_FRAMEBUFFER = 0x8CA8;
    static constexpr XglfwGLenum XGLFW_GL_DRAW_FRAMEBUFFER = 0x8CA9;
    static constexpr XglfwGLenum XGLFW_GL_SCISSOR_BOX = 0x0C10;
    static constexpr XglfwGLenum XGLFW_GL_SCISSOR_TEST = 0x0C11;
    static constexpr XglfwGLenum XGLFW_GL_COLOR_CLEAR_VALUE = 0x0C22;
    static constexpr XglfwGLenum XGLFW_GL_COLOR_WRITEMASK = 0x0C23;
    static constexpr XglfwGLenum XGLFW_GL_DRAW_FRAMEBUFFER_BINDING = 0x8CA6;
    static constexpr XglfwGLbitfield XGLFW_GL_COLOR_BUFFER_BIT = 0x00004000;

    static std::atomic<unsigned int> g_timingActiveTextureUnit{ 0 };
    static std::atomic<unsigned int> g_timingBoundTexture1D[32] = {};
    static std::atomic<unsigned int> g_timingBoundTexture2D[32] = {};
    static std::atomic<unsigned int> g_timingBoundTexture3D[32] = {};
    static std::atomic<unsigned int> g_timingBoundTextureCube[32] = {};
    static std::atomic<unsigned int> g_timingBoundTexture2DArray[32] = {};
    static std::atomic<unsigned int> g_timingBoundArrayBuffer{ 0 };
    static std::atomic<unsigned int> g_timingBoundElementArrayBuffer{ 0 };
    static std::atomic<unsigned int> g_timingBoundPixelUnpackBuffer{ 0 };
    static std::atomic<unsigned int> g_timingBoundUniformBuffer{ 0 };
    static std::atomic<unsigned int> g_timingBoundCopyReadBuffer{ 0 };
    static std::atomic<unsigned int> g_timingBoundCopyWriteBuffer{ 0 };
    static std::atomic<unsigned int> g_timingBoundDrawIndirectBuffer{ 0 };
    static std::atomic<unsigned int> g_timingBoundShaderStorageBuffer{ 0 };
    static std::atomic<unsigned int> g_timingBoundDrawFramebuffer{ 0 };
    static std::atomic<unsigned int> g_timingBoundReadFramebuffer{ 0 };

    static LONG64 TrackedPositiveSize(XglfwGLsizeiptr size) {
        return size > 0 ? static_cast<LONG64>(size) : 0;
    }

    static int TrackedComponentsForFormat(XglfwGLenum format) {
        switch (format) {
            case XGLFW_GL_RED:
            case XGLFW_GL_DEPTH_COMPONENT:
                return 1;
            case XGLFW_GL_RG:
                return 2;
            case XGLFW_GL_RGB:
                return 3;
            case XGLFW_GL_RGBA:
            case XGLFW_GL_BGRA:
            case XGLFW_GL_DEPTH_STENCIL:
                return 4;
            default:
                return 4;
        }
    }

    static int TrackedBytesForType(XglfwGLenum type) {
        switch (type) {
            case XGLFW_GL_UNSIGNED_BYTE:
            case XGLFW_GL_BYTE:
                return 1;
            case XGLFW_GL_UNSIGNED_SHORT:
            case XGLFW_GL_SHORT:
            case XGLFW_GL_UNSIGNED_SHORT_5_6_5:
            case XGLFW_GL_UNSIGNED_SHORT_4_4_4_4:
            case XGLFW_GL_UNSIGNED_SHORT_5_5_5_1:
                return 2;
            case XGLFW_GL_UNSIGNED_INT:
            case XGLFW_GL_INT:
            case XGLFW_GL_FLOAT:
            case XGLFW_GL_UNSIGNED_INT_24_8:
                return 4;
            default:
                return 1;
        }
    }

    static LONG64 TrackedEstimateImageBytes(
        XglfwGLsizei width,
        XglfwGLsizei height,
        XglfwGLsizei depth,
        XglfwGLenum format,
        XglfwGLenum type) {
        if (width <= 0 || height <= 0 || depth <= 0 ||
            width > 32768 || height > 32768 || depth > 4096) {
            return 0;
        }

        const auto pixels =
            static_cast<LONG64>(width) *
            static_cast<LONG64>(height) *
            static_cast<LONG64>(depth);
        const auto bytes =
            pixels *
            static_cast<LONG64>(TrackedComponentsForFormat(format)) *
            static_cast<LONG64>(TrackedBytesForType(type));
        const auto maxDiagnosticBytes = 1024ll * 1024ll * 1024ll;
        return bytes > maxDiagnosticBytes ? maxDiagnosticBytes : bytes;
    }

    static LONG64 TrackedEstimateStorageBytes(
        XglfwGLsizei width,
        XglfwGLsizei height,
        XglfwGLsizei depth,
        XglfwGLsizei levels) {
        if (width <= 0 || height <= 0 || depth <= 0) {
            return 0;
        }

        const int safeLevels = levels > 0 ? levels : 1;
        const auto bytes =
            static_cast<LONG64>(width) *
            static_cast<LONG64>(height) *
            static_cast<LONG64>(depth) *
            4ll *
            static_cast<LONG64>(safeLevels);
        const auto maxDiagnosticBytes = 1024ll * 1024ll * 1024ll;
        return bytes > maxDiagnosticBytes ? maxDiagnosticBytes : bytes;
    }

    static unsigned int TrackedTextureUnitIndex() {
        unsigned int unit = g_timingActiveTextureUnit.load(std::memory_order_acquire);
        return unit < 32u ? unit : 0u;
    }

    static std::atomic<unsigned int>* TrackedTextureSlotForTarget(XglfwGLenum target, unsigned int unit) {
        const unsigned int safeUnit = unit < 32u ? unit : 0u;
        switch (target) {
            case XGLFW_GL_TEXTURE_1D: return &g_timingBoundTexture1D[safeUnit];
            case XGLFW_GL_TEXTURE_2D: return &g_timingBoundTexture2D[safeUnit];
            case XGLFW_GL_TEXTURE_3D: return &g_timingBoundTexture3D[safeUnit];
            case XGLFW_GL_TEXTURE_CUBE_MAP: return &g_timingBoundTextureCube[safeUnit];
            case XGLFW_GL_TEXTURE_2D_ARRAY: return &g_timingBoundTexture2DArray[safeUnit];
            default: return nullptr;
        }
    }

    static unsigned int TrackedBoundTextureName(XglfwGLenum target) {
        auto* slot = TrackedTextureSlotForTarget(target, TrackedTextureUnitIndex());
        return slot ? slot->load(std::memory_order_acquire) : 0u;
    }

    static std::atomic<unsigned int>* TrackedBufferSlotForTarget(XglfwGLenum target) {
        switch (target) {
            case XGLFW_GL_ARRAY_BUFFER: return &g_timingBoundArrayBuffer;
            case XGLFW_GL_ELEMENT_ARRAY_BUFFER: return &g_timingBoundElementArrayBuffer;
            case XGLFW_GL_PIXEL_UNPACK_BUFFER: return &g_timingBoundPixelUnpackBuffer;
            case XGLFW_GL_UNIFORM_BUFFER: return &g_timingBoundUniformBuffer;
            case XGLFW_GL_COPY_READ_BUFFER: return &g_timingBoundCopyReadBuffer;
            case XGLFW_GL_COPY_WRITE_BUFFER: return &g_timingBoundCopyWriteBuffer;
            case XGLFW_GL_DRAW_INDIRECT_BUFFER: return &g_timingBoundDrawIndirectBuffer;
            case XGLFW_GL_SHADER_STORAGE_BUFFER: return &g_timingBoundShaderStorageBuffer;
            default: return nullptr;
        }
    }

    static unsigned int TrackedBoundBufferName(XglfwGLenum target) {
        auto* slot = TrackedBufferSlotForTarget(target);
        return slot ? slot->load(std::memory_order_acquire) : 0u;
    }

    static void RecordTrackedDraw(
        XglfwGLenum mode,
        XglfwGLint first,
        XglfwGLsizei count,
        XglfwGLenum type) {
        auto* state = EnsureGlCommandState();
        if (!state) {
            return;
        }

        state->lastDrawMode = mode;
        state->lastDrawFirst = first;
        state->lastDrawCount = count;
        state->lastDrawType = type;
        InterlockedIncrement64(&state->drawSerial);
    }

    static void RecordTrackedTextureBind(XglfwGLenum target, XglfwGLuint texture, unsigned int unit) {
        auto* slot = TrackedTextureSlotForTarget(target, unit);
        if (slot) {
            slot->store(texture, std::memory_order_release);
        }

        auto* state = EnsureGlCommandState();
        if (!state) {
            return;
        }

        state->activeTextureUnit = unit;
        state->lastTextureTarget = target;
        state->lastTextureName = texture;
        InterlockedIncrement64(&state->textureBindSerial);
    }

    static void RecordTrackedTextureUpload(
        bool subUpload,
        XglfwGLenum target,
        XglfwGLuint textureName,
        XglfwGLint level,
        XglfwGLint internalFormat,
        XglfwGLsizei width,
        XglfwGLsizei height,
        XglfwGLsizei depth,
        XglfwGLenum format,
        XglfwGLenum type,
        LONG64 explicitBytes) {
        auto* state = EnsureGlCommandState();
        if (!state) {
            return;
        }

        const auto bytes = explicitBytes >= 0
            ? explicitBytes
            : TrackedEstimateImageBytes(width, height, depth, format, type);
        state->activeTextureUnit = TrackedTextureUnitIndex();
        state->lastTextureTarget = target;
        state->lastTextureName = textureName;
        state->lastTextureLevel = level;
        state->lastTextureWidth = width;
        state->lastTextureHeight = height;
        state->lastTextureDepth = depth;
        state->lastTextureInternalFormat = internalFormat;
        state->lastTextureFormat = format;
        state->lastTextureType = type;
        state->lastTextureUploadBytes = bytes;
        if (bytes > 0) {
            InterlockedAdd64(&state->textureUploadBytes, bytes);
        }
        InterlockedIncrement64(subUpload ? &state->textureSubUploadSerial : &state->textureUploadSerial);
    }

    static void RecordTrackedBufferBind(XglfwGLenum target, XglfwGLuint buffer) {
        auto* slot = TrackedBufferSlotForTarget(target);
        if (slot) {
            slot->store(buffer, std::memory_order_release);
        }

        auto* state = EnsureGlCommandState();
        if (!state) {
            return;
        }

        state->lastBufferTarget = target;
        state->lastBufferName = buffer;
        InterlockedIncrement64(&state->bufferBindSerial);
    }

    static void RecordTrackedBufferUpload(
        bool subUpload,
        XglfwGLenum target,
        XglfwGLuint buffer,
        XglfwGLsizeiptr size,
        XglfwGLenum usage) {
        auto* state = EnsureGlCommandState();
        if (!state) {
            return;
        }

        const auto bytes = TrackedPositiveSize(size);
        state->lastBufferTarget = target;
        state->lastBufferName = buffer;
        state->lastBufferSize = bytes;
        state->lastBufferUsage = usage;
        if (bytes > 0) {
            InterlockedAdd64(&state->bufferUploadBytes, bytes);
        }
        InterlockedIncrement64(subUpload ? &state->bufferSubUploadSerial : &state->bufferUploadSerial);
    }

    static void RecordTrackedFramebufferBind(XglfwGLenum target, XglfwGLuint framebuffer) {
        if (target == XGLFW_GL_FRAMEBUFFER) {
            g_timingBoundDrawFramebuffer.store(framebuffer, std::memory_order_release);
            g_timingBoundReadFramebuffer.store(framebuffer, std::memory_order_release);
        } else if (target == XGLFW_GL_DRAW_FRAMEBUFFER) {
            g_timingBoundDrawFramebuffer.store(framebuffer, std::memory_order_release);
        } else if (target == XGLFW_GL_READ_FRAMEBUFFER) {
            g_timingBoundReadFramebuffer.store(framebuffer, std::memory_order_release);
        }

        auto* state = EnsureGlCommandState();
        if (!state) {
            return;
        }

        state->framebufferDrawName = g_timingBoundDrawFramebuffer.load(std::memory_order_acquire);
        state->framebufferReadName = g_timingBoundReadFramebuffer.load(std::memory_order_acquire);
        InterlockedIncrement64(&state->framebufferBindSerial);
    }

    static void RecordTrackedFramebufferAttach(
        XglfwGLenum target,
        XglfwGLuint texture,
        XglfwGLint width,
        XglfwGLint height) {
        auto* state = EnsureGlCommandState();
        if (!state) {
            return;
        }

        state->framebufferDrawName = g_timingBoundDrawFramebuffer.load(std::memory_order_acquire);
        state->framebufferReadName = g_timingBoundReadFramebuffer.load(std::memory_order_acquire);
        state->framebufferColorTextureName = texture;
        state->framebufferColorTextureWidth = width;
        state->framebufferColorTextureHeight = height;
        (void)target;
        InterlockedIncrement64(&state->framebufferAttachSerial);
    }

    static void RecordTrackedFramebufferBlit(
        XglfwGLint srcX0,
        XglfwGLint srcY0,
        XglfwGLint srcX1,
        XglfwGLint srcY1,
        XglfwGLint dstX0,
        XglfwGLint dstY0,
        XglfwGLint dstX1,
        XglfwGLint dstY1) {
        auto* state = EnsureGlCommandState();
        if (!state) {
            return;
        }

        const int srcWidth = std::abs(srcX1 - srcX0);
        const int srcHeight = std::abs(srcY1 - srcY0);
        const int dstWidth = std::abs(dstX1 - dstX0);
        const int dstHeight = std::abs(dstY1 - dstY0);
        state->framebufferDrawName = g_timingBoundDrawFramebuffer.load(std::memory_order_acquire);
        state->framebufferReadName = g_timingBoundReadFramebuffer.load(std::memory_order_acquire);
        state->framebufferLastBlitWidth = dstWidth > 0 ? dstWidth : srcWidth;
        state->framebufferLastBlitHeight = dstHeight > 0 ? dstHeight : srcHeight;
        InterlockedIncrement64(&state->framebufferBlitSerial);
    }

    static void RecordTrackedProgramUse(XglfwGLuint program) {
        auto* state = EnsureGlCommandState();
        if (!state) {
            return;
        }

        state->currentProgram = program;
        InterlockedIncrement64(&state->programUseSerial);
    }

    static void RecordTrackedUniform(XglfwGLint location) {
        auto* state = EnsureGlCommandState();
        if (!state) {
            return;
        }

        state->lastUniformLocation = location;
        InterlockedIncrement64(&state->uniformSerial);
    }

    static void RecordTrackedFlush() {
        auto* state = EnsureGlCommandState();
        if (state) {
            InterlockedIncrement64(&state->flushSerial);
        }
    }

    using GlDrawArraysProc = void(__stdcall*)(XglfwGLenum, XglfwGLint, XglfwGLsizei);
    using GlDrawElementsProc = void(__stdcall*)(XglfwGLenum, XglfwGLsizei, XglfwGLenum, const void*);
    using GlDrawRangeElementsProc = void(__stdcall*)(XglfwGLenum, XglfwGLuint, XglfwGLuint, XglfwGLsizei, XglfwGLenum, const void*);
    using GlDrawElementsBaseVertexProc = void(__stdcall*)(XglfwGLenum, XglfwGLsizei, XglfwGLenum, const void*, XglfwGLint);
    using GlDrawRangeElementsBaseVertexProc = void(__stdcall*)(XglfwGLenum, XglfwGLuint, XglfwGLuint, XglfwGLsizei, XglfwGLenum, const void*, XglfwGLint);
    using GlDrawArraysInstancedProc = void(__stdcall*)(XglfwGLenum, XglfwGLint, XglfwGLsizei, XglfwGLsizei);
    using GlDrawElementsInstancedProc = void(__stdcall*)(XglfwGLenum, XglfwGLsizei, XglfwGLenum, const void*, XglfwGLsizei);
    using GlDrawElementsInstancedBaseVertexProc = void(__stdcall*)(XglfwGLenum, XglfwGLsizei, XglfwGLenum, const void*, XglfwGLsizei, XglfwGLint);
    using GlDrawArraysInstancedBaseInstanceProc = void(__stdcall*)(XglfwGLenum, XglfwGLint, XglfwGLsizei, XglfwGLsizei, XglfwGLuint);
    using GlDrawElementsInstancedBaseVertexBaseInstanceProc = void(__stdcall*)(XglfwGLenum, XglfwGLsizei, XglfwGLenum, const void*, XglfwGLsizei, XglfwGLint, XglfwGLuint);
    using GlMultiDrawArraysProc = void(__stdcall*)(XglfwGLenum, const XglfwGLint*, const XglfwGLsizei*, XglfwGLsizei);
    using GlMultiDrawElementsProc = void(__stdcall*)(XglfwGLenum, const XglfwGLsizei*, XglfwGLenum, const void* const*, XglfwGLsizei);
    using GlMultiDrawElementsBaseVertexProc = void(__stdcall*)(XglfwGLenum, const XglfwGLsizei*, XglfwGLenum, const void* const*, XglfwGLsizei, const XglfwGLint*);
    using GlActiveTextureProc = void(__stdcall*)(XglfwGLenum);
    using GlBindTextureProc = void(__stdcall*)(XglfwGLenum, XglfwGLuint);
    using GlBindTextureUnitProc = void(__stdcall*)(XglfwGLuint, XglfwGLuint);
    using GlTexImage2DProc = void(__stdcall*)(XglfwGLenum, XglfwGLint, XglfwGLint, XglfwGLsizei, XglfwGLsizei, XglfwGLint, XglfwGLenum, XglfwGLenum, const void*);
    using GlTexSubImage2DProc = void(__stdcall*)(XglfwGLenum, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLsizei, XglfwGLsizei, XglfwGLenum, XglfwGLenum, const void*);
    using GlTexImage3DProc = void(__stdcall*)(XglfwGLenum, XglfwGLint, XglfwGLint, XglfwGLsizei, XglfwGLsizei, XglfwGLsizei, XglfwGLint, XglfwGLenum, XglfwGLenum, const void*);
    using GlTexSubImage3DProc = void(__stdcall*)(XglfwGLenum, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLsizei, XglfwGLsizei, XglfwGLsizei, XglfwGLenum, XglfwGLenum, const void*);
    using GlCompressedTexImage2DProc = void(__stdcall*)(XglfwGLenum, XglfwGLint, XglfwGLenum, XglfwGLsizei, XglfwGLsizei, XglfwGLint, XglfwGLsizei, const void*);
    using GlCompressedTexSubImage2DProc = void(__stdcall*)(XglfwGLenum, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLsizei, XglfwGLsizei, XglfwGLenum, XglfwGLsizei, const void*);
    using GlCompressedTexImage3DProc = void(__stdcall*)(XglfwGLenum, XglfwGLint, XglfwGLenum, XglfwGLsizei, XglfwGLsizei, XglfwGLsizei, XglfwGLint, XglfwGLsizei, const void*);
    using GlCompressedTexSubImage3DProc = void(__stdcall*)(XglfwGLenum, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLsizei, XglfwGLsizei, XglfwGLsizei, XglfwGLenum, XglfwGLsizei, const void*);
    using GlTexStorage2DProc = void(__stdcall*)(XglfwGLenum, XglfwGLsizei, XglfwGLenum, XglfwGLsizei, XglfwGLsizei);
    using GlTexStorage3DProc = void(__stdcall*)(XglfwGLenum, XglfwGLsizei, XglfwGLenum, XglfwGLsizei, XglfwGLsizei, XglfwGLsizei);
    using GlTextureStorage2DProc = void(__stdcall*)(XglfwGLuint, XglfwGLsizei, XglfwGLenum, XglfwGLsizei, XglfwGLsizei);
    using GlTextureStorage3DProc = void(__stdcall*)(XglfwGLuint, XglfwGLsizei, XglfwGLenum, XglfwGLsizei, XglfwGLsizei, XglfwGLsizei);
    using GlTextureSubImage2DProc = void(__stdcall*)(XglfwGLuint, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLsizei, XglfwGLsizei, XglfwGLenum, XglfwGLenum, const void*);
    using GlTextureSubImage3DProc = void(__stdcall*)(XglfwGLuint, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLsizei, XglfwGLsizei, XglfwGLsizei, XglfwGLenum, XglfwGLenum, const void*);
    using GlBindBufferProc = void(__stdcall*)(XglfwGLenum, XglfwGLuint);
    using GlBindBufferBaseProc = void(__stdcall*)(XglfwGLenum, XglfwGLuint, XglfwGLuint);
    using GlBindBufferRangeProc = void(__stdcall*)(XglfwGLenum, XglfwGLuint, XglfwGLuint, XglfwGLintptr, XglfwGLsizeiptr);
    using GlBufferDataProc = void(__stdcall*)(XglfwGLenum, XglfwGLsizeiptr, const void*, XglfwGLenum);
    using GlBufferSubDataProc = void(__stdcall*)(XglfwGLenum, XglfwGLintptr, XglfwGLsizeiptr, const void*);
    using GlBufferStorageProc = void(__stdcall*)(XglfwGLenum, XglfwGLsizeiptr, const void*, XglfwGLbitfield);
    using GlNamedBufferDataProc = void(__stdcall*)(XglfwGLuint, XglfwGLsizeiptr, const void*, XglfwGLenum);
    using GlNamedBufferSubDataProc = void(__stdcall*)(XglfwGLuint, XglfwGLintptr, XglfwGLsizeiptr, const void*);
    using GlNamedBufferStorageProc = void(__stdcall*)(XglfwGLuint, XglfwGLsizeiptr, const void*, XglfwGLbitfield);
    using GlBindFramebufferProc = void(__stdcall*)(XglfwGLenum, XglfwGLuint);
    using GlFramebufferTextureProc = void(__stdcall*)(XglfwGLenum, XglfwGLenum, XglfwGLuint, XglfwGLint);
    using GlFramebufferTexture2DProc = void(__stdcall*)(XglfwGLenum, XglfwGLenum, XglfwGLenum, XglfwGLuint, XglfwGLint);
    using GlFramebufferTextureLayerProc = void(__stdcall*)(XglfwGLenum, XglfwGLenum, XglfwGLuint, XglfwGLint, XglfwGLint);
    using GlNamedFramebufferTextureProc = void(__stdcall*)(XglfwGLuint, XglfwGLenum, XglfwGLuint, XglfwGLint);
    using GlNamedFramebufferTextureLayerProc = void(__stdcall*)(XglfwGLuint, XglfwGLenum, XglfwGLuint, XglfwGLint, XglfwGLint);
    using GlBlitFramebufferProc = void(__stdcall*)(XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLbitfield, XglfwGLenum);
    using GlUseProgramProc = void(__stdcall*)(XglfwGLuint);
    using GlUniform1iProc = void(__stdcall*)(XglfwGLint, XglfwGLint);
    using GlUniform1fProc = void(__stdcall*)(XglfwGLint, XglfwGLfloat);
    using GlUniform2fProc = void(__stdcall*)(XglfwGLint, XglfwGLfloat, XglfwGLfloat);
    using GlUniform3fProc = void(__stdcall*)(XglfwGLint, XglfwGLfloat, XglfwGLfloat, XglfwGLfloat);
    using GlUniform4fProc = void(__stdcall*)(XglfwGLint, XglfwGLfloat, XglfwGLfloat, XglfwGLfloat, XglfwGLfloat);
    using GlUniform4iProc = void(__stdcall*)(XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLint, XglfwGLint);
    using GlUniform1ivProc = void(__stdcall*)(XglfwGLint, XglfwGLsizei, const XglfwGLint*);
    using GlUniform1fvProc = void(__stdcall*)(XglfwGLint, XglfwGLsizei, const XglfwGLfloat*);
    using GlUniform2fvProc = void(__stdcall*)(XglfwGLint, XglfwGLsizei, const XglfwGLfloat*);
    using GlUniform3fvProc = void(__stdcall*)(XglfwGLint, XglfwGLsizei, const XglfwGLfloat*);
    using GlUniform4fvProc = void(__stdcall*)(XglfwGLint, XglfwGLsizei, const XglfwGLfloat*);
    using GlUniformMatrix3fvProc = void(__stdcall*)(XglfwGLint, XglfwGLsizei, XglfwGLboolean, const XglfwGLfloat*);
    using GlUniformMatrix4fvProc = void(__stdcall*)(XglfwGLint, XglfwGLsizei, XglfwGLboolean, const XglfwGLfloat*);
    using GlProgramUniform1iProc = void(__stdcall*)(XglfwGLuint, XglfwGLint, XglfwGLint);
    using GlProgramUniform4fProc = void(__stdcall*)(XglfwGLuint, XglfwGLint, XglfwGLfloat, XglfwGLfloat, XglfwGLfloat, XglfwGLfloat);
    using GlGetIntegervProc = void(__stdcall*)(XglfwGLenum, XglfwGLint*);
    using GlGetFloatvProc = void(__stdcall*)(XglfwGLenum, XglfwGLfloat*);
    using GlGetBooleanvProc = void(__stdcall*)(XglfwGLenum, XglfwGLboolean*);
    using GlIsEnabledProc = XglfwGLboolean(__stdcall*)(XglfwGLenum);
    using GlEnableProc = void(__stdcall*)(XglfwGLenum);
    using GlDisableProc = void(__stdcall*)(XglfwGLenum);
    using GlScissorProc = void(__stdcall*)(XglfwGLint, XglfwGLint, XglfwGLsizei, XglfwGLsizei);
    using GlClearColorProc = void(__stdcall*)(XglfwGLfloat, XglfwGLfloat, XglfwGLfloat, XglfwGLfloat);
    using GlClearProc = void(__stdcall*)(XglfwGLbitfield);
    using GlColorMaskProc = void(__stdcall*)(XglfwGLboolean, XglfwGLboolean, XglfwGLboolean, XglfwGLboolean);
    using GlFlushProc = void(__stdcall*)();
    using GlFinishProc = void(__stdcall*)();

    static GlDrawArraysProc g_real_glDrawArrays = nullptr;
    static GlDrawElementsProc g_real_glDrawElements = nullptr;
    static GlDrawRangeElementsProc g_real_glDrawRangeElements = nullptr;
    static GlDrawElementsBaseVertexProc g_real_glDrawElementsBaseVertex = nullptr;
    static GlDrawRangeElementsBaseVertexProc g_real_glDrawRangeElementsBaseVertex = nullptr;
    static GlDrawArraysInstancedProc g_real_glDrawArraysInstanced = nullptr;
    static GlDrawElementsInstancedProc g_real_glDrawElementsInstanced = nullptr;
    static GlDrawElementsInstancedBaseVertexProc g_real_glDrawElementsInstancedBaseVertex = nullptr;
    static GlDrawArraysInstancedBaseInstanceProc g_real_glDrawArraysInstancedBaseInstance = nullptr;
    static GlDrawElementsInstancedBaseVertexBaseInstanceProc g_real_glDrawElementsInstancedBaseVertexBaseInstance = nullptr;
    static GlMultiDrawArraysProc g_real_glMultiDrawArrays = nullptr;
    static GlMultiDrawElementsProc g_real_glMultiDrawElements = nullptr;
    static GlMultiDrawElementsBaseVertexProc g_real_glMultiDrawElementsBaseVertex = nullptr;
    static GlActiveTextureProc g_real_glActiveTexture = nullptr;
    static GlBindTextureProc g_real_glBindTexture = nullptr;
    static GlBindTextureUnitProc g_real_glBindTextureUnit = nullptr;
    static GlTexImage2DProc g_real_glTexImage2D = nullptr;
    static GlTexSubImage2DProc g_real_glTexSubImage2D = nullptr;
    static GlTexImage3DProc g_real_glTexImage3D = nullptr;
    static GlTexSubImage3DProc g_real_glTexSubImage3D = nullptr;
    static GlCompressedTexImage2DProc g_real_glCompressedTexImage2D = nullptr;
    static GlCompressedTexSubImage2DProc g_real_glCompressedTexSubImage2D = nullptr;
    static GlCompressedTexImage3DProc g_real_glCompressedTexImage3D = nullptr;
    static GlCompressedTexSubImage3DProc g_real_glCompressedTexSubImage3D = nullptr;
    static GlTexStorage2DProc g_real_glTexStorage2D = nullptr;
    static GlTexStorage3DProc g_real_glTexStorage3D = nullptr;
    static GlTextureStorage2DProc g_real_glTextureStorage2D = nullptr;
    static GlTextureStorage3DProc g_real_glTextureStorage3D = nullptr;
    static GlTextureSubImage2DProc g_real_glTextureSubImage2D = nullptr;
    static GlTextureSubImage3DProc g_real_glTextureSubImage3D = nullptr;
    static GlBindBufferProc g_real_glBindBuffer = nullptr;
    static GlBindBufferBaseProc g_real_glBindBufferBase = nullptr;
    static GlBindBufferRangeProc g_real_glBindBufferRange = nullptr;
    static GlBufferDataProc g_real_glBufferData = nullptr;
    static GlBufferSubDataProc g_real_glBufferSubData = nullptr;
    static GlBufferStorageProc g_real_glBufferStorage = nullptr;
    static GlNamedBufferDataProc g_real_glNamedBufferData = nullptr;
    static GlNamedBufferSubDataProc g_real_glNamedBufferSubData = nullptr;
    static GlNamedBufferStorageProc g_real_glNamedBufferStorage = nullptr;
    static GlBindFramebufferProc g_real_glBindFramebuffer = nullptr;
    static GlFramebufferTextureProc g_real_glFramebufferTexture = nullptr;
    static GlFramebufferTexture2DProc g_real_glFramebufferTexture2D = nullptr;
    static GlFramebufferTextureLayerProc g_real_glFramebufferTextureLayer = nullptr;
    static GlNamedFramebufferTextureProc g_real_glNamedFramebufferTexture = nullptr;
    static GlNamedFramebufferTextureLayerProc g_real_glNamedFramebufferTextureLayer = nullptr;
    static GlBlitFramebufferProc g_real_glBlitFramebuffer = nullptr;
    static GlUseProgramProc g_real_glUseProgram = nullptr;
    static GlUniform1iProc g_real_glUniform1i = nullptr;
    static GlUniform1fProc g_real_glUniform1f = nullptr;
    static GlUniform2fProc g_real_glUniform2f = nullptr;
    static GlUniform3fProc g_real_glUniform3f = nullptr;
    static GlUniform4fProc g_real_glUniform4f = nullptr;
    static GlUniform4iProc g_real_glUniform4i = nullptr;
    static GlUniform1ivProc g_real_glUniform1iv = nullptr;
    static GlUniform1fvProc g_real_glUniform1fv = nullptr;
    static GlUniform2fvProc g_real_glUniform2fv = nullptr;
    static GlUniform3fvProc g_real_glUniform3fv = nullptr;
    static GlUniform4fvProc g_real_glUniform4fv = nullptr;
    static GlUniformMatrix3fvProc g_real_glUniformMatrix3fv = nullptr;
    static GlUniformMatrix4fvProc g_real_glUniformMatrix4fv = nullptr;
    static GlProgramUniform1iProc g_real_glProgramUniform1i = nullptr;
    static GlProgramUniform4fProc g_real_glProgramUniform4f = nullptr;
    static GlFlushProc g_real_glFlush = nullptr;
    static GlFinishProc g_real_glFinish = nullptr;

    static void __stdcall XglfwTimed_glDrawArrays(XglfwGLenum mode, XglfwGLint first, XglfwGLsizei count) {
        RecordTrackedDraw(mode, first, count, 0);
        g_real_glDrawArrays(mode, first, count);
    }

    static void __stdcall XglfwTimed_glDrawElements(XglfwGLenum mode, XglfwGLsizei count, XglfwGLenum type, const void* indices) {
        RecordTrackedDraw(mode, 0, count, type);
        g_real_glDrawElements(mode, count, type, indices);
    }

    static void __stdcall XglfwTimed_glDrawRangeElements(XglfwGLenum mode, XglfwGLuint start, XglfwGLuint end, XglfwGLsizei count, XglfwGLenum type, const void* indices) {
        RecordTrackedDraw(mode, static_cast<XglfwGLint>(start), count, type);
        g_real_glDrawRangeElements(mode, start, end, count, type, indices);
    }

    static void __stdcall XglfwTimed_glDrawElementsBaseVertex(XglfwGLenum mode, XglfwGLsizei count, XglfwGLenum type, const void* indices, XglfwGLint basevertex) {
        RecordTrackedDraw(mode, basevertex, count, type);
        g_real_glDrawElementsBaseVertex(mode, count, type, indices, basevertex);
    }

    static void __stdcall XglfwTimed_glDrawRangeElementsBaseVertex(XglfwGLenum mode, XglfwGLuint start, XglfwGLuint end, XglfwGLsizei count, XglfwGLenum type, const void* indices, XglfwGLint basevertex) {
        RecordTrackedDraw(mode, basevertex, count, type);
        g_real_glDrawRangeElementsBaseVertex(mode, start, end, count, type, indices, basevertex);
    }

    static void __stdcall XglfwTimed_glDrawArraysInstanced(XglfwGLenum mode, XglfwGLint first, XglfwGLsizei count, XglfwGLsizei instancecount) {
        RecordTrackedDraw(mode, first, count * (instancecount > 0 ? instancecount : 1), 0);
        g_real_glDrawArraysInstanced(mode, first, count, instancecount);
    }

    static void __stdcall XglfwTimed_glDrawElementsInstanced(XglfwGLenum mode, XglfwGLsizei count, XglfwGLenum type, const void* indices, XglfwGLsizei instancecount) {
        RecordTrackedDraw(mode, 0, count * (instancecount > 0 ? instancecount : 1), type);
        g_real_glDrawElementsInstanced(mode, count, type, indices, instancecount);
    }

    static void __stdcall XglfwTimed_glDrawElementsInstancedBaseVertex(XglfwGLenum mode, XglfwGLsizei count, XglfwGLenum type, const void* indices, XglfwGLsizei instancecount, XglfwGLint basevertex) {
        RecordTrackedDraw(mode, basevertex, count * (instancecount > 0 ? instancecount : 1), type);
        g_real_glDrawElementsInstancedBaseVertex(mode, count, type, indices, instancecount, basevertex);
    }

    static void __stdcall XglfwTimed_glDrawArraysInstancedBaseInstance(XglfwGLenum mode, XglfwGLint first, XglfwGLsizei count, XglfwGLsizei instancecount, XglfwGLuint baseinstance) {
        RecordTrackedDraw(mode, first, count * (instancecount > 0 ? instancecount : 1), 0);
        g_real_glDrawArraysInstancedBaseInstance(mode, first, count, instancecount, baseinstance);
    }

    static void __stdcall XglfwTimed_glDrawElementsInstancedBaseVertexBaseInstance(XglfwGLenum mode, XglfwGLsizei count, XglfwGLenum type, const void* indices, XglfwGLsizei instancecount, XglfwGLint basevertex, XglfwGLuint baseinstance) {
        RecordTrackedDraw(mode, basevertex, count * (instancecount > 0 ? instancecount : 1), type);
        g_real_glDrawElementsInstancedBaseVertexBaseInstance(mode, count, type, indices, instancecount, basevertex, baseinstance);
    }

    static void __stdcall XglfwTimed_glMultiDrawArrays(XglfwGLenum mode, const XglfwGLint* first, const XglfwGLsizei* count, XglfwGLsizei drawcount) {
        const XglfwGLsizei limited = drawcount > 256 ? 256 : drawcount;
        for (XglfwGLsizei i = 0; first && count && i < limited; ++i) {
            RecordTrackedDraw(mode, first[i], count[i], 0);
        }
        g_real_glMultiDrawArrays(mode, first, count, drawcount);
    }

    static void __stdcall XglfwTimed_glMultiDrawElements(XglfwGLenum mode, const XglfwGLsizei* count, XglfwGLenum type, const void* const* indices, XglfwGLsizei drawcount) {
        const XglfwGLsizei limited = drawcount > 256 ? 256 : drawcount;
        for (XglfwGLsizei i = 0; count && i < limited; ++i) {
            RecordTrackedDraw(mode, 0, count[i], type);
        }
        g_real_glMultiDrawElements(mode, count, type, indices, drawcount);
    }

    static void __stdcall XglfwTimed_glMultiDrawElementsBaseVertex(XglfwGLenum mode, const XglfwGLsizei* count, XglfwGLenum type, const void* const* indices, XglfwGLsizei drawcount, const XglfwGLint* basevertex) {
        const XglfwGLsizei limited = drawcount > 256 ? 256 : drawcount;
        for (XglfwGLsizei i = 0; count && i < limited; ++i) {
            RecordTrackedDraw(mode, basevertex ? basevertex[i] : 0, count[i], type);
        }
        g_real_glMultiDrawElementsBaseVertex(mode, count, type, indices, drawcount, basevertex);
    }

    static void __stdcall XglfwTimed_glActiveTexture(XglfwGLenum texture) {
        if (texture >= XGLFW_GL_TEXTURE0 && texture < XGLFW_GL_TEXTURE0 + 32) {
            g_timingActiveTextureUnit.store(texture - XGLFW_GL_TEXTURE0, std::memory_order_release);
        }
        g_real_glActiveTexture(texture);
    }

    static void __stdcall XglfwTimed_glBindTexture(XglfwGLenum target, XglfwGLuint texture) {
        RecordTrackedTextureBind(target, texture, TrackedTextureUnitIndex());
        g_real_glBindTexture(target, texture);
    }

    static void __stdcall XglfwTimed_glBindTextureUnit(XglfwGLuint unit, XglfwGLuint texture) {
        RecordTrackedTextureBind(XGLFW_GL_TEXTURE_2D, texture, unit);
        g_real_glBindTextureUnit(unit, texture);
    }

    static void __stdcall XglfwTimed_glTexImage2D(XglfwGLenum target, XglfwGLint level, XglfwGLint internalFormat, XglfwGLsizei width, XglfwGLsizei height, XglfwGLint border, XglfwGLenum format, XglfwGLenum type, const void* pixels) {
        RecordTrackedTextureUpload(false, target, TrackedBoundTextureName(target), level, internalFormat, width, height, 1, format, type, -1);
        g_real_glTexImage2D(target, level, internalFormat, width, height, border, format, type, pixels);
    }

    static void __stdcall XglfwTimed_glTexSubImage2D(XglfwGLenum target, XglfwGLint level, XglfwGLint xoffset, XglfwGLint yoffset, XglfwGLsizei width, XglfwGLsizei height, XglfwGLenum format, XglfwGLenum type, const void* pixels) {
        RecordTrackedTextureUpload(true, target, TrackedBoundTextureName(target), level, 0, width, height, 1, format, type, -1);
        g_real_glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
    }

    static void __stdcall XglfwTimed_glTexImage3D(XglfwGLenum target, XglfwGLint level, XglfwGLint internalFormat, XglfwGLsizei width, XglfwGLsizei height, XglfwGLsizei depth, XglfwGLint border, XglfwGLenum format, XglfwGLenum type, const void* pixels) {
        RecordTrackedTextureUpload(false, target, TrackedBoundTextureName(target), level, internalFormat, width, height, depth, format, type, -1);
        g_real_glTexImage3D(target, level, internalFormat, width, height, depth, border, format, type, pixels);
    }

    static void __stdcall XglfwTimed_glTexSubImage3D(XglfwGLenum target, XglfwGLint level, XglfwGLint xoffset, XglfwGLint yoffset, XglfwGLint zoffset, XglfwGLsizei width, XglfwGLsizei height, XglfwGLsizei depth, XglfwGLenum format, XglfwGLenum type, const void* pixels) {
        RecordTrackedTextureUpload(true, target, TrackedBoundTextureName(target), level, 0, width, height, depth, format, type, -1);
        g_real_glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
    }

    static void __stdcall XglfwTimed_glCompressedTexImage2D(XglfwGLenum target, XglfwGLint level, XglfwGLenum internalFormat, XglfwGLsizei width, XglfwGLsizei height, XglfwGLint border, XglfwGLsizei imageSize, const void* data) {
        RecordTrackedTextureUpload(false, target, TrackedBoundTextureName(target), level, static_cast<XglfwGLint>(internalFormat), width, height, 1, XGLFW_GL_RGBA, XGLFW_GL_UNSIGNED_BYTE, imageSize);
        g_real_glCompressedTexImage2D(target, level, internalFormat, width, height, border, imageSize, data);
    }

    static void __stdcall XglfwTimed_glCompressedTexSubImage2D(XglfwGLenum target, XglfwGLint level, XglfwGLint xoffset, XglfwGLint yoffset, XglfwGLsizei width, XglfwGLsizei height, XglfwGLenum format, XglfwGLsizei imageSize, const void* data) {
        RecordTrackedTextureUpload(true, target, TrackedBoundTextureName(target), level, 0, width, height, 1, XGLFW_GL_RGBA, XGLFW_GL_UNSIGNED_BYTE, imageSize);
        g_real_glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data);
    }

    static void __stdcall XglfwTimed_glCompressedTexImage3D(XglfwGLenum target, XglfwGLint level, XglfwGLenum internalFormat, XglfwGLsizei width, XglfwGLsizei height, XglfwGLsizei depth, XglfwGLint border, XglfwGLsizei imageSize, const void* data) {
        RecordTrackedTextureUpload(false, target, TrackedBoundTextureName(target), level, static_cast<XglfwGLint>(internalFormat), width, height, depth, XGLFW_GL_RGBA, XGLFW_GL_UNSIGNED_BYTE, imageSize);
        g_real_glCompressedTexImage3D(target, level, internalFormat, width, height, depth, border, imageSize, data);
    }

    static void __stdcall XglfwTimed_glCompressedTexSubImage3D(XglfwGLenum target, XglfwGLint level, XglfwGLint xoffset, XglfwGLint yoffset, XglfwGLint zoffset, XglfwGLsizei width, XglfwGLsizei height, XglfwGLsizei depth, XglfwGLenum format, XglfwGLsizei imageSize, const void* data) {
        RecordTrackedTextureUpload(true, target, TrackedBoundTextureName(target), level, 0, width, height, depth, XGLFW_GL_RGBA, XGLFW_GL_UNSIGNED_BYTE, imageSize);
        g_real_glCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data);
    }

    static void __stdcall XglfwTimed_glTexStorage2D(XglfwGLenum target, XglfwGLsizei levels, XglfwGLenum internalFormat, XglfwGLsizei width, XglfwGLsizei height) {
        RecordTrackedTextureUpload(false, target, TrackedBoundTextureName(target), levels, static_cast<XglfwGLint>(internalFormat), width, height, 1, XGLFW_GL_RGBA, XGLFW_GL_UNSIGNED_BYTE, TrackedEstimateStorageBytes(width, height, 1, levels));
        g_real_glTexStorage2D(target, levels, internalFormat, width, height);
    }

    static void __stdcall XglfwTimed_glTexStorage3D(XglfwGLenum target, XglfwGLsizei levels, XglfwGLenum internalFormat, XglfwGLsizei width, XglfwGLsizei height, XglfwGLsizei depth) {
        RecordTrackedTextureUpload(false, target, TrackedBoundTextureName(target), levels, static_cast<XglfwGLint>(internalFormat), width, height, depth, XGLFW_GL_RGBA, XGLFW_GL_UNSIGNED_BYTE, TrackedEstimateStorageBytes(width, height, depth, levels));
        g_real_glTexStorage3D(target, levels, internalFormat, width, height, depth);
    }

    static void __stdcall XglfwTimed_glTextureStorage2D(XglfwGLuint texture, XglfwGLsizei levels, XglfwGLenum internalFormat, XglfwGLsizei width, XglfwGLsizei height) {
        RecordTrackedTextureUpload(false, XGLFW_GL_TEXTURE_2D, texture, levels, static_cast<XglfwGLint>(internalFormat), width, height, 1, XGLFW_GL_RGBA, XGLFW_GL_UNSIGNED_BYTE, TrackedEstimateStorageBytes(width, height, 1, levels));
        g_real_glTextureStorage2D(texture, levels, internalFormat, width, height);
    }

    static void __stdcall XglfwTimed_glTextureStorage3D(XglfwGLuint texture, XglfwGLsizei levels, XglfwGLenum internalFormat, XglfwGLsizei width, XglfwGLsizei height, XglfwGLsizei depth) {
        RecordTrackedTextureUpload(false, XGLFW_GL_TEXTURE_3D, texture, levels, static_cast<XglfwGLint>(internalFormat), width, height, depth, XGLFW_GL_RGBA, XGLFW_GL_UNSIGNED_BYTE, TrackedEstimateStorageBytes(width, height, depth, levels));
        g_real_glTextureStorage3D(texture, levels, internalFormat, width, height, depth);
    }

    static void __stdcall XglfwTimed_glTextureSubImage2D(XglfwGLuint texture, XglfwGLint level, XglfwGLint xoffset, XglfwGLint yoffset, XglfwGLsizei width, XglfwGLsizei height, XglfwGLenum format, XglfwGLenum type, const void* pixels) {
        RecordTrackedTextureUpload(true, XGLFW_GL_TEXTURE_2D, texture, level, 0, width, height, 1, format, type, -1);
        g_real_glTextureSubImage2D(texture, level, xoffset, yoffset, width, height, format, type, pixels);
    }

    static void __stdcall XglfwTimed_glTextureSubImage3D(XglfwGLuint texture, XglfwGLint level, XglfwGLint xoffset, XglfwGLint yoffset, XglfwGLint zoffset, XglfwGLsizei width, XglfwGLsizei height, XglfwGLsizei depth, XglfwGLenum format, XglfwGLenum type, const void* pixels) {
        RecordTrackedTextureUpload(true, XGLFW_GL_TEXTURE_3D, texture, level, 0, width, height, depth, format, type, -1);
        g_real_glTextureSubImage3D(texture, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
    }

    static void __stdcall XglfwTimed_glBindBuffer(XglfwGLenum target, XglfwGLuint buffer) {
        RecordTrackedBufferBind(target, buffer);
        g_real_glBindBuffer(target, buffer);
    }

    static void __stdcall XglfwTimed_glBindBufferBase(XglfwGLenum target, XglfwGLuint index, XglfwGLuint buffer) {
        RecordTrackedBufferBind(target, buffer);
        g_real_glBindBufferBase(target, index, buffer);
    }

    static void __stdcall XglfwTimed_glBindBufferRange(XglfwGLenum target, XglfwGLuint index, XglfwGLuint buffer, XglfwGLintptr offset, XglfwGLsizeiptr size) {
        RecordTrackedBufferBind(target, buffer);
        g_real_glBindBufferRange(target, index, buffer, offset, size);
    }

    static void __stdcall XglfwTimed_glBufferData(XglfwGLenum target, XglfwGLsizeiptr size, const void* data, XglfwGLenum usage) {
        RecordTrackedBufferUpload(false, target, TrackedBoundBufferName(target), size, usage);
        g_real_glBufferData(target, size, data, usage);
    }

    static void __stdcall XglfwTimed_glBufferSubData(XglfwGLenum target, XglfwGLintptr offset, XglfwGLsizeiptr size, const void* data) {
        RecordTrackedBufferUpload(true, target, TrackedBoundBufferName(target), size, 0);
        g_real_glBufferSubData(target, offset, size, data);
    }

    static void __stdcall XglfwTimed_glBufferStorage(XglfwGLenum target, XglfwGLsizeiptr size, const void* data, XglfwGLbitfield flags) {
        RecordTrackedBufferUpload(false, target, TrackedBoundBufferName(target), size, flags);
        g_real_glBufferStorage(target, size, data, flags);
    }

    static void __stdcall XglfwTimed_glNamedBufferData(XglfwGLuint buffer, XglfwGLsizeiptr size, const void* data, XglfwGLenum usage) {
        RecordTrackedBufferUpload(false, 0, buffer, size, usage);
        g_real_glNamedBufferData(buffer, size, data, usage);
    }

    static void __stdcall XglfwTimed_glNamedBufferSubData(XglfwGLuint buffer, XglfwGLintptr offset, XglfwGLsizeiptr size, const void* data) {
        RecordTrackedBufferUpload(true, 0, buffer, size, 0);
        g_real_glNamedBufferSubData(buffer, offset, size, data);
    }

    static void __stdcall XglfwTimed_glNamedBufferStorage(XglfwGLuint buffer, XglfwGLsizeiptr size, const void* data, XglfwGLbitfield flags) {
        RecordTrackedBufferUpload(false, 0, buffer, size, flags);
        g_real_glNamedBufferStorage(buffer, size, data, flags);
    }

    static void __stdcall XglfwTimed_glBindFramebuffer(XglfwGLenum target, XglfwGLuint framebuffer) {
        RecordTrackedFramebufferBind(target, framebuffer);
        g_real_glBindFramebuffer(target, framebuffer);
    }

    static void __stdcall XglfwTimed_glFramebufferTexture(XglfwGLenum target, XglfwGLenum attachment, XglfwGLuint texture, XglfwGLint level) {
        RecordTrackedFramebufferAttach(target, texture, 0, 0);
        g_real_glFramebufferTexture(target, attachment, texture, level);
    }

    static void __stdcall XglfwTimed_glFramebufferTexture2D(XglfwGLenum target, XglfwGLenum attachment, XglfwGLenum textarget, XglfwGLuint texture, XglfwGLint level) {
        RecordTrackedFramebufferAttach(target, texture, 0, 0);
        g_real_glFramebufferTexture2D(target, attachment, textarget, texture, level);
    }

    static void __stdcall XglfwTimed_glFramebufferTextureLayer(XglfwGLenum target, XglfwGLenum attachment, XglfwGLuint texture, XglfwGLint level, XglfwGLint layer) {
        RecordTrackedFramebufferAttach(target, texture, 0, 0);
        g_real_glFramebufferTextureLayer(target, attachment, texture, level, layer);
    }

    static void __stdcall XglfwTimed_glNamedFramebufferTexture(XglfwGLuint framebuffer, XglfwGLenum attachment, XglfwGLuint texture, XglfwGLint level) {
        RecordTrackedFramebufferAttach(XGLFW_GL_FRAMEBUFFER, texture, 0, 0);
        g_real_glNamedFramebufferTexture(framebuffer, attachment, texture, level);
    }

    static void __stdcall XglfwTimed_glNamedFramebufferTextureLayer(XglfwGLuint framebuffer, XglfwGLenum attachment, XglfwGLuint texture, XglfwGLint level, XglfwGLint layer) {
        RecordTrackedFramebufferAttach(XGLFW_GL_FRAMEBUFFER, texture, 0, 0);
        g_real_glNamedFramebufferTextureLayer(framebuffer, attachment, texture, level, layer);
    }

    static void __stdcall XglfwTimed_glBlitFramebuffer(XglfwGLint srcX0, XglfwGLint srcY0, XglfwGLint srcX1, XglfwGLint srcY1, XglfwGLint dstX0, XglfwGLint dstY0, XglfwGLint dstX1, XglfwGLint dstY1, XglfwGLbitfield mask, XglfwGLenum filter) {
        RecordTrackedFramebufferBlit(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1);
        g_real_glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
    }

    static void __stdcall XglfwTimed_glUseProgram(XglfwGLuint program) {
        RecordTrackedProgramUse(program);
        g_real_glUseProgram(program);
    }

    static void __stdcall XglfwTimed_glUniform1i(XglfwGLint location, XglfwGLint v0) {
        RecordTrackedUniform(location);
        g_real_glUniform1i(location, v0);
    }

    static void __stdcall XglfwTimed_glUniform1f(XglfwGLint location, XglfwGLfloat v0) {
        RecordTrackedUniform(location);
        g_real_glUniform1f(location, v0);
    }

    static void __stdcall XglfwTimed_glUniform2f(XglfwGLint location, XglfwGLfloat v0, XglfwGLfloat v1) {
        RecordTrackedUniform(location);
        g_real_glUniform2f(location, v0, v1);
    }

    static void __stdcall XglfwTimed_glUniform3f(XglfwGLint location, XglfwGLfloat v0, XglfwGLfloat v1, XglfwGLfloat v2) {
        RecordTrackedUniform(location);
        g_real_glUniform3f(location, v0, v1, v2);
    }

    static void __stdcall XglfwTimed_glUniform4f(XglfwGLint location, XglfwGLfloat v0, XglfwGLfloat v1, XglfwGLfloat v2, XglfwGLfloat v3) {
        RecordTrackedUniform(location);
        g_real_glUniform4f(location, v0, v1, v2, v3);
    }

    static void __stdcall XglfwTimed_glUniform4i(XglfwGLint location, XglfwGLint v0, XglfwGLint v1, XglfwGLint v2, XglfwGLint v3) {
        RecordTrackedUniform(location);
        g_real_glUniform4i(location, v0, v1, v2, v3);
    }

    static void __stdcall XglfwTimed_glUniform1iv(XglfwGLint location, XglfwGLsizei count, const XglfwGLint* value) {
        RecordTrackedUniform(location);
        g_real_glUniform1iv(location, count, value);
    }

    static void __stdcall XglfwTimed_glUniform1fv(XglfwGLint location, XglfwGLsizei count, const XglfwGLfloat* value) {
        RecordTrackedUniform(location);
        g_real_glUniform1fv(location, count, value);
    }

    static void __stdcall XglfwTimed_glUniform2fv(XglfwGLint location, XglfwGLsizei count, const XglfwGLfloat* value) {
        RecordTrackedUniform(location);
        g_real_glUniform2fv(location, count, value);
    }

    static void __stdcall XglfwTimed_glUniform3fv(XglfwGLint location, XglfwGLsizei count, const XglfwGLfloat* value) {
        RecordTrackedUniform(location);
        g_real_glUniform3fv(location, count, value);
    }

    static void __stdcall XglfwTimed_glUniform4fv(XglfwGLint location, XglfwGLsizei count, const XglfwGLfloat* value) {
        RecordTrackedUniform(location);
        g_real_glUniform4fv(location, count, value);
    }

    static void __stdcall XglfwTimed_glUniformMatrix3fv(XglfwGLint location, XglfwGLsizei count, XglfwGLboolean transpose, const XglfwGLfloat* value) {
        RecordTrackedUniform(location);
        g_real_glUniformMatrix3fv(location, count, transpose, value);
    }

    static void __stdcall XglfwTimed_glUniformMatrix4fv(XglfwGLint location, XglfwGLsizei count, XglfwGLboolean transpose, const XglfwGLfloat* value) {
        RecordTrackedUniform(location);
        g_real_glUniformMatrix4fv(location, count, transpose, value);
    }

    static void __stdcall XglfwTimed_glProgramUniform1i(XglfwGLuint program, XglfwGLint location, XglfwGLint v0) {
        RecordTrackedUniform(location);
        g_real_glProgramUniform1i(program, location, v0);
    }

    static void __stdcall XglfwTimed_glProgramUniform4f(XglfwGLuint program, XglfwGLint location, XglfwGLfloat v0, XglfwGLfloat v1, XglfwGLfloat v2, XglfwGLfloat v3) {
        RecordTrackedUniform(location);
        g_real_glProgramUniform4f(program, location, v0, v1, v2, v3);
    }

    static void __stdcall XglfwTimed_glFlush() {
        RecordTrackedFlush();
        g_real_glFlush();
    }

    static void __stdcall XglfwTimed_glFinish() {
        RecordTrackedFlush();
        g_real_glFinish();
    }

    static void LogGlProcWrapper(const char* name, PROC real, PROC wrapper) {
        static std::atomic<int> logCount{ 0 };
        int count = logCount.fetch_add(1, std::memory_order_relaxed);
        if (count < 96) {
            char line[256] = {};
            std::snprintf(line, sizeof(line), "glfwGetProcAddress timing-wrap %s real=%p wrapper=%p", name, real, wrapper);
            DebugLine(line);
        }
    }

    static PROC WrapGlProcForFrameTiming(const char* name, PROC proc) {
        if (!name || !proc) {
            return proc;
        }

#define XGLFW_WRAP_TIMED_PROC(symbol, procType) \
        if (std::strcmp(name, #symbol) == 0) { \
            g_real_##symbol = reinterpret_cast<procType>(proc); \
            PROC wrapper = reinterpret_cast<PROC>(&XglfwTimed_##symbol); \
            LogGlProcWrapper(name, proc, wrapper); \
            return wrapper; \
        }

        XGLFW_WRAP_TIMED_PROC(glDrawArrays, GlDrawArraysProc)
        XGLFW_WRAP_TIMED_PROC(glDrawElements, GlDrawElementsProc)
        XGLFW_WRAP_TIMED_PROC(glDrawRangeElements, GlDrawRangeElementsProc)
        XGLFW_WRAP_TIMED_PROC(glDrawElementsBaseVertex, GlDrawElementsBaseVertexProc)
        XGLFW_WRAP_TIMED_PROC(glDrawRangeElementsBaseVertex, GlDrawRangeElementsBaseVertexProc)
        XGLFW_WRAP_TIMED_PROC(glDrawArraysInstanced, GlDrawArraysInstancedProc)
        XGLFW_WRAP_TIMED_PROC(glDrawElementsInstanced, GlDrawElementsInstancedProc)
        XGLFW_WRAP_TIMED_PROC(glDrawElementsInstancedBaseVertex, GlDrawElementsInstancedBaseVertexProc)
        XGLFW_WRAP_TIMED_PROC(glDrawArraysInstancedBaseInstance, GlDrawArraysInstancedBaseInstanceProc)
        XGLFW_WRAP_TIMED_PROC(glDrawElementsInstancedBaseVertexBaseInstance, GlDrawElementsInstancedBaseVertexBaseInstanceProc)
        XGLFW_WRAP_TIMED_PROC(glMultiDrawArrays, GlMultiDrawArraysProc)
        XGLFW_WRAP_TIMED_PROC(glMultiDrawElements, GlMultiDrawElementsProc)
        XGLFW_WRAP_TIMED_PROC(glMultiDrawElementsBaseVertex, GlMultiDrawElementsBaseVertexProc)
        XGLFW_WRAP_TIMED_PROC(glActiveTexture, GlActiveTextureProc)
        XGLFW_WRAP_TIMED_PROC(glBindTexture, GlBindTextureProc)
        XGLFW_WRAP_TIMED_PROC(glBindTextureUnit, GlBindTextureUnitProc)
        XGLFW_WRAP_TIMED_PROC(glTexImage2D, GlTexImage2DProc)
        XGLFW_WRAP_TIMED_PROC(glTexSubImage2D, GlTexSubImage2DProc)
        XGLFW_WRAP_TIMED_PROC(glTexImage3D, GlTexImage3DProc)
        XGLFW_WRAP_TIMED_PROC(glTexSubImage3D, GlTexSubImage3DProc)
        XGLFW_WRAP_TIMED_PROC(glCompressedTexImage2D, GlCompressedTexImage2DProc)
        XGLFW_WRAP_TIMED_PROC(glCompressedTexSubImage2D, GlCompressedTexSubImage2DProc)
        XGLFW_WRAP_TIMED_PROC(glCompressedTexImage3D, GlCompressedTexImage3DProc)
        XGLFW_WRAP_TIMED_PROC(glCompressedTexSubImage3D, GlCompressedTexSubImage3DProc)
        XGLFW_WRAP_TIMED_PROC(glTexStorage2D, GlTexStorage2DProc)
        XGLFW_WRAP_TIMED_PROC(glTexStorage3D, GlTexStorage3DProc)
        XGLFW_WRAP_TIMED_PROC(glTextureStorage2D, GlTextureStorage2DProc)
        XGLFW_WRAP_TIMED_PROC(glTextureStorage3D, GlTextureStorage3DProc)
        XGLFW_WRAP_TIMED_PROC(glTextureSubImage2D, GlTextureSubImage2DProc)
        XGLFW_WRAP_TIMED_PROC(glTextureSubImage3D, GlTextureSubImage3DProc)
        XGLFW_WRAP_TIMED_PROC(glBindBuffer, GlBindBufferProc)
        XGLFW_WRAP_TIMED_PROC(glBindBufferBase, GlBindBufferBaseProc)
        XGLFW_WRAP_TIMED_PROC(glBindBufferRange, GlBindBufferRangeProc)
        XGLFW_WRAP_TIMED_PROC(glBufferData, GlBufferDataProc)
        XGLFW_WRAP_TIMED_PROC(glBufferSubData, GlBufferSubDataProc)
        XGLFW_WRAP_TIMED_PROC(glBufferStorage, GlBufferStorageProc)
        XGLFW_WRAP_TIMED_PROC(glNamedBufferData, GlNamedBufferDataProc)
        XGLFW_WRAP_TIMED_PROC(glNamedBufferSubData, GlNamedBufferSubDataProc)
        XGLFW_WRAP_TIMED_PROC(glNamedBufferStorage, GlNamedBufferStorageProc)
        XGLFW_WRAP_TIMED_PROC(glBindFramebuffer, GlBindFramebufferProc)
        XGLFW_WRAP_TIMED_PROC(glFramebufferTexture, GlFramebufferTextureProc)
        XGLFW_WRAP_TIMED_PROC(glFramebufferTexture2D, GlFramebufferTexture2DProc)
        XGLFW_WRAP_TIMED_PROC(glFramebufferTextureLayer, GlFramebufferTextureLayerProc)
        XGLFW_WRAP_TIMED_PROC(glNamedFramebufferTexture, GlNamedFramebufferTextureProc)
        XGLFW_WRAP_TIMED_PROC(glNamedFramebufferTextureLayer, GlNamedFramebufferTextureLayerProc)
        XGLFW_WRAP_TIMED_PROC(glBlitFramebuffer, GlBlitFramebufferProc)
        XGLFW_WRAP_TIMED_PROC(glUseProgram, GlUseProgramProc)
        XGLFW_WRAP_TIMED_PROC(glUniform1i, GlUniform1iProc)
        XGLFW_WRAP_TIMED_PROC(glUniform1f, GlUniform1fProc)
        XGLFW_WRAP_TIMED_PROC(glUniform2f, GlUniform2fProc)
        XGLFW_WRAP_TIMED_PROC(glUniform3f, GlUniform3fProc)
        XGLFW_WRAP_TIMED_PROC(glUniform4f, GlUniform4fProc)
        XGLFW_WRAP_TIMED_PROC(glUniform4i, GlUniform4iProc)
        XGLFW_WRAP_TIMED_PROC(glUniform1iv, GlUniform1ivProc)
        XGLFW_WRAP_TIMED_PROC(glUniform1fv, GlUniform1fvProc)
        XGLFW_WRAP_TIMED_PROC(glUniform2fv, GlUniform2fvProc)
        XGLFW_WRAP_TIMED_PROC(glUniform3fv, GlUniform3fvProc)
        XGLFW_WRAP_TIMED_PROC(glUniform4fv, GlUniform4fvProc)
        XGLFW_WRAP_TIMED_PROC(glUniformMatrix3fv, GlUniformMatrix3fvProc)
        XGLFW_WRAP_TIMED_PROC(glUniformMatrix4fv, GlUniformMatrix4fvProc)
        XGLFW_WRAP_TIMED_PROC(glProgramUniform1i, GlProgramUniform1iProc)
        XGLFW_WRAP_TIMED_PROC(glProgramUniform4f, GlProgramUniform4fProc)
        XGLFW_WRAP_TIMED_PROC(glFlush, GlFlushProc)
        XGLFW_WRAP_TIMED_PROC(glFinish, GlFinishProc)

#undef XGLFW_WRAP_TIMED_PROC
        return proc;
    }

    static bool EnsureReadbackBuffer(size_t bytes) {
        if (bytes == 0) {
            return false;
        }
        if (g_eglReadbackBytes >= bytes && g_eglReadback) {
            return true;
        }

        void* resized = std::realloc(g_eglReadback, bytes);
        if (!resized) {
            return false;
        }

        g_eglReadback = static_cast<unsigned char*>(resized);
        g_eglReadbackBytes = bytes;
        return true;
    }

    static void PublishMesaEglFramebuffer() {
        if (g_eglSurfacelessContext.load(std::memory_order_acquire) || g_eglSurface == EGL_NO_SURFACE) {
            static std::atomic<int> surfacelessLogCount{ 0 };
            int count = surfacelessLogCount.fetch_add(1, std::memory_order_relaxed);
            if (count < 6) {
                DebugLine("Mesa EGL framebuffer readback skipped; surfaceless context has no default framebuffer");
            }
            return;
        }

        if (!EnsureMesaEglContext() || !g_glReadPixels || g_eglWidth <= 0 || g_eglHeight <= 0) {
            static std::atomic<int> missingLogCount{ 0 };
            int count = missingLogCount.fetch_add(1, std::memory_order_relaxed);
            if (count < 6) {
                DebugLine("Mesa EGL framebuffer readback skipped; context or glReadPixels missing");
            }
            return;
        }

        auto* state = EnsureGlCommandState();
        if (!state) {
            return;
        }

        const size_t sourceBytes =
            static_cast<size_t>(g_eglWidth) *
            static_cast<size_t>(g_eglHeight) *
            4u;
        if (!EnsureReadbackBuffer(sourceBytes)) {
            DebugLine("Mesa EGL framebuffer readback allocation failed");
            return;
        }

        // glReadPixels provides the synchronization this diagnostic readback
        // needs.  Calling Mesa's glFinish on Xbox UWP can throw before the
        // pixel read, which disables the otherwise working context path.
        __try {
            g_glReadPixels(0, 0, g_eglWidth, g_eglHeight, GL_RGBA_VALUE, GL_UNSIGNED_BYTE_VALUE, g_eglReadback);
        }
        __except (LogEglRecoverableException("glReadPixels", GetExceptionCode())) {
            return;
        }

        size_t visiblePixels = 0;
        for (size_t i = 0; i + 3 < sourceBytes; i += 4) {
            if (g_eglReadback[i + 0] != 0 ||
                g_eglReadback[i + 1] != 0 ||
                g_eglReadback[i + 2] != 0) {
                ++visiblePixels;
                if (visiblePixels > 64) {
                    break;
                }
            }
        }
        if (visiblePixels == 0) {
            static std::atomic<int> blackReadbackLogCount{ 0 };
            int count = blackReadbackLogCount.fetch_add(1, std::memory_order_relaxed);
            if (count < 8) {
                DebugLine("Mesa EGL framebuffer readback skipped; all-black frame");
            }
            return;
        }

        const float scale = (g_eglWidth > static_cast<int>(kMinecraftXboxGuiFramebufferMaxWidth) ||
            g_eglHeight > static_cast<int>(kMinecraftXboxGuiFramebufferMaxHeight))
            ? ((static_cast<float>(kMinecraftXboxGuiFramebufferMaxWidth) / static_cast<float>(g_eglWidth)) <
                (static_cast<float>(kMinecraftXboxGuiFramebufferMaxHeight) / static_cast<float>(g_eglHeight))
                ? (static_cast<float>(kMinecraftXboxGuiFramebufferMaxWidth) / static_cast<float>(g_eglWidth))
                : (static_cast<float>(kMinecraftXboxGuiFramebufferMaxHeight) / static_cast<float>(g_eglHeight)))
            : 1.0f;
        const int destWidth = scale > 0.0f
            ? static_cast<int>(static_cast<float>(g_eglWidth) * scale)
            : static_cast<int>(kMinecraftXboxGuiFramebufferMaxWidth);
        const int destHeight = scale > 0.0f
            ? static_cast<int>(static_cast<float>(g_eglHeight) * scale)
            : static_cast<int>(kMinecraftXboxGuiFramebufferMaxHeight);
        const int clampedWidth = destWidth > 0
            ? (destWidth < static_cast<int>(kMinecraftXboxGuiFramebufferMaxWidth) ? destWidth : static_cast<int>(kMinecraftXboxGuiFramebufferMaxWidth))
            : 1;
        const int clampedHeight = destHeight > 0
            ? (destHeight < static_cast<int>(kMinecraftXboxGuiFramebufferMaxHeight) ? destHeight : static_cast<int>(kMinecraftXboxGuiFramebufferMaxHeight))
            : 1;

        InterlockedExchange(&state->guiFramebufferReady, 0);
        state->guiFramebufferWidth = clampedWidth;
        state->guiFramebufferHeight = clampedHeight;
        state->guiFramebufferBytes = static_cast<LONG64>(clampedWidth) * static_cast<LONG64>(clampedHeight) * 4;

        for (int y = 0; y < clampedHeight; ++y) {
            const int sourceY = g_eglHeight - 1 - static_cast<int>((static_cast<long long>(y) * g_eglHeight) / clampedHeight);
            unsigned char* dstRow = state->guiFramebufferRgba + static_cast<size_t>(y) * clampedWidth * 4;
            for (int x = 0; x < clampedWidth; ++x) {
                const int sourceX = static_cast<int>((static_cast<long long>(x) * g_eglWidth) / clampedWidth);
                const unsigned char* src = g_eglReadback + (static_cast<size_t>(sourceY) * g_eglWidth + sourceX) * 4;
                unsigned char* dst = dstRow + static_cast<size_t>(x) * 4;
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 255;
            }
        }

        MemoryBarrier();
        InterlockedIncrement64(&state->guiFramebufferSerial);
        InterlockedExchange(&state->guiFramebufferReady, 1);

        static std::atomic<int> readbackLogCount{ 0 };
        int count = readbackLogCount.fetch_add(1, std::memory_order_relaxed);
        if (count < 8) {
            char line[160] = {};
            std::snprintf(line, sizeof(line), "Mesa EGL framebuffer published %dx%d -> %dx%d", g_eglWidth, g_eglHeight, clampedWidth, clampedHeight);
            DebugLine(line);
        }
    }

    static bool EnsureWglDeviceContext() {
        if (g_wglDc) {
            return true;
        }

        if (IsMesaWglMode()) {
            g_wglDc = reinterpret_cast<HDC>(kPrimaryWindow);
            g_wglSyntheticDc = true;
            DebugLine("Mesa WGL using synthetic HDC; skipping CreateCompatibleDC on Xbox UWP");
            return true;
        }

        DebugLine("CreateCompatibleDC(nullptr)");
        g_wglDc = CreateCompatibleDC(nullptr);
        if (!g_wglDc) {
            char line[128] = {};
            std::snprintf(line, sizeof(line), "CreateCompatibleDC failed; GetLastError=%lu", GetLastError());
            DebugLine(line);
            return false;
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = 16;
        bitmapInfo.bmiHeader.biHeight = -16;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        void* pixels = nullptr;
        g_wglBitmap = CreateDIBSection(g_wglDc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!g_wglBitmap) {
            char line[128] = {};
            std::snprintf(line, sizeof(line), "CreateDIBSection failed; GetLastError=%lu", GetLastError());
            DebugLine(line);
            DeleteDC(g_wglDc);
            g_wglDc = nullptr;
            return false;
        }

        g_wglOldBitmap = SelectObject(g_wglDc, g_wglBitmap);
        DebugLine("Memory-backed HDC ready for Mesa WGL");
        return true;
    }

    static PIXELFORMATDESCRIPTOR CreatePixelFormatDescriptor() {
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cAlphaBits = 8;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;
        pfd.iLayerType = PFD_MAIN_PLANE;
        return pfd;
    }

    static bool EnsureWglContext() {
        if (!EnsureOpenGlModule()) {
            return false;
        }
        if (!EnsureWglDeviceContext()) {
            return false;
        }

        DebugLine("EnsureWglContext start");
        PIXELFORMATDESCRIPTOR pfd = CreatePixelFormatDescriptor();
        const bool skipSyntheticMesaPixelFormat = IsMesaWglMode() && g_wglSyntheticDc;
        if (skipSyntheticMesaPixelFormat) {
            DebugLine("Mesa WGL synthetic HDC: skipping wglChoosePixelFormat/wglSetPixelFormat diagnostic");
        }
        else if (g_wglChoosePixelFormat && g_wglSetPixelFormat) {
            int pixelFormat = 0;
            __try {
                pixelFormat = g_wglChoosePixelFormat(g_wglDc, &pfd);
            }
            __except (LogNativeException("wglChoosePixelFormat", GetExceptionCode())) {
                return false;
            }

            char line[128] = {};
            std::snprintf(line, sizeof(line), "wglChoosePixelFormat -> %d", pixelFormat);
            DebugLine(line);

            if (pixelFormat > 0) {
                BOOL setOk = FALSE;
                __try {
                    setOk = g_wglSetPixelFormat(g_wglDc, pixelFormat, &pfd);
                }
                __except (LogNativeException("wglSetPixelFormat", GetExceptionCode())) {
                    return false;
                }
                std::snprintf(line, sizeof(line), "wglSetPixelFormat -> %d", setOk ? 1 : 0);
                DebugLine(line);
            }
        }

        if (!g_wglContext) {
            __try {
                g_wglContext = g_wglCreateContext(g_wglDc);
            }
            __except (LogNativeException("wglCreateContext", GetExceptionCode())) {
                return false;
            }
            if (!g_wglContext) {
                DebugLine("wglCreateContext failed");
                return false;
            }
            DebugLine("wglCreateContext succeeded");
        }

        BOOL makeCurrent = FALSE;
        __try {
            makeCurrent = g_wglMakeCurrent(g_wglDc, g_wglContext);
        }
        __except (LogNativeException("wglMakeCurrent", GetExceptionCode())) {
            return false;
        }
        if (!makeCurrent) {
            DebugLine("wglMakeCurrent failed");
            g_contextCurrent.store(0, std::memory_order_release);
            return false;
        }

        HGLRC currentContext = nullptr;
        if (g_wglGetCurrentContext) {
            __try {
                currentContext = g_wglGetCurrentContext();
            }
            __except (LogNativeException("wglGetCurrentContext", GetExceptionCode())) {
                return false;
            }
        }
        else {
            currentContext = g_wglContext;
            DebugLine("wglGetCurrentContext unavailable; accepting wglMakeCurrent success");
        }

        g_contextCurrent.store(currentContext ? 1 : 0, std::memory_order_release);
        if (!g_contextCurrent.load(std::memory_order_acquire)) {
            DebugLine("wglMakeCurrent returned success but wglGetCurrentContext is null");
        }
        else {
            DebugLine("wglMakeCurrent succeeded and context is current");
        }
        return g_contextCurrent.load(std::memory_order_acquire) != 0;
    }
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        InstallNativeFatalExceptionLogger();
    }
    return TRUE;
}

XGLFW_API void minecraft_xbox_glfw_set_core_window(void* coreWindowUnknown) {
    IUnknown* next = QueryInterfaceForEglObject(
        coreWindowUnknown,
        kIidCoreWindow,
        "CoreWindow ICoreWindow");

    {
        std::lock_guard<std::mutex> lock(g_coreWindowMutex);
        if (g_coreWindowUnknown) {
            g_coreWindowUnknown->Release();
        }
        g_coreWindowUnknown = next;
    }

    char line[192] = {};
    std::snprintf(
        line,
        sizeof(line),
        "CoreWindow handoff set source=%p iface=%p thread=0x%lx",
        coreWindowUnknown,
        next,
        CurrentThreadIdForLog());
    DebugLine(line);
    InstallKeyboardHooks();
    InstallMouseHooks();
}

XGLFW_API void minecraft_xbox_glfw_set_egl_window_descriptor(void* descriptorUnknown) {
    IUnknown* next = QueryInterfaceForEglObject(
        descriptorUnknown,
        kIidInspectable,
        "EGL PropertySet IInspectable");

    {
        std::lock_guard<std::mutex> lock(g_coreWindowMutex);
        if (g_eglWindowDescriptorUnknown) {
            g_eglWindowDescriptorUnknown->Release();
        }
        g_eglWindowDescriptorUnknown = next;
    }

    char line[208] = {};
    std::snprintf(
        line,
        sizeof(line),
        "EGL window descriptor handoff set source=%p iface=%p thread=0x%lx",
        descriptorUnknown,
        next,
        CurrentThreadIdForLog());
    DebugLine(line);
}

XGLFW_API int minecraft_xbox_glfw_prepare_mesa_egl_on_core_window_thread(int releaseCurrentAfterWarmup) {
    (void)releaseCurrentAfterWarmup;
    IUnknown* coreWindowUnknown = AcquireCoreWindowUnknown();
    IUnknown* descriptorUnknown = AcquireEglWindowDescriptorUnknown();
    {
        char line[256] = {};
        std::snprintf(
            line,
            sizeof(line),
            "VISIBLE-LAUNCH-FIX: Mesa EGL warmup export skipped thread=0x%lx coreWindow=%p descriptor=%p",
            CurrentThreadIdForLog(),
            coreWindowUnknown,
            descriptorUnknown);
        DebugLine(line);
    }

    if (!IsMesaEglMode()) {
        DebugLine("VISIBLE-LAUNCH-FIX: Mesa EGL warmup export skipped; Mesa EGL mode is disabled");
        return 0;
    }

    if (!coreWindowUnknown && !descriptorUnknown) {
        DebugLine("VISIBLE-LAUNCH-FIX: Mesa EGL warmup export skipped; no window object is ready");
        return 0;
    }

    DebugLine("VISIBLE-LAUNCH-FIX: warmup disabled; glfwCreateWindow owns the first EGL surface/context");
    return 2;
}

static void EnsureTimeBase() {
    static std::atomic<int> init{ 0 };
    int expected = 0;
    if (init.compare_exchange_strong(expected, 1)) {
        QueryPerformanceFrequency(&g_qpcFreq);
        QueryPerformanceCounter(&g_qpcStart);
    }
}

static void SetError(int code) {
    g_last_error_code.store(code, std::memory_order_relaxed);
}

// -----------------------------------------------------------------------------
// Init / version / error
// -----------------------------------------------------------------------------

XGLFW_API int glfwInit(void) {
    DebugLine("glfwInit");
    {
        char line[128] = {};
        std::snprintf(
            line,
            sizeof(line),
            "glfwInit launch resolution %dx%d",
            QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_WIDTH", kDefaultWidth),
            QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_HEIGHT", kDefaultHeight));
        DebugLine(line);
    }
    EnsureTimeBase();
    if (IsFakeContextMode()) {
        DebugLine("GLFW fake OpenGL context mode enabled");
    }
    else if (IsMesaEglMode()) {
        DebugLine("GLFW Mesa EGL context mode enabled");
        EnsureMesaEglModule();
    }
    else {
        if (IsMesaWglMode()) {
            DebugLine("GLFW Mesa WGL context mode enabled");
        }
        EnsureOpenGlModule();
    }
    g_initialized.store(1, std::memory_order_release);
    InstallKeyboardHooks();
    InstallMouseHooks();
    SetError(GLFW_NO_ERROR);
    return GLFW_TRUE;
}

XGLFW_API void glfwTerminate(void) {
    RemoveMouseHooks();
    RemoveKeyboardHooks();
    if (IsMesaEglMode() && g_eglMakeCurrent && g_eglDisplay != EGL_NO_DISPLAY) {
        g_eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (IsMesaEglMode() && g_eglContext != EGL_NO_CONTEXT && g_eglDestroyContext) {
        g_eglDestroyContext(g_eglDisplay, g_eglContext);
    }
    if (IsMesaEglMode() && g_eglSurface != EGL_NO_SURFACE && g_eglDestroySurface) {
        g_eglDestroySurface(g_eglDisplay, g_eglSurface);
    }
    if (IsMesaEglMode() && g_eglDisplay != EGL_NO_DISPLAY && g_eglTerminate) {
        g_eglTerminate(g_eglDisplay);
    }
    g_eglContext = EGL_NO_CONTEXT;
    g_eglSurface = EGL_NO_SURFACE;
    g_eglDisplay = EGL_NO_DISPLAY;
    if (!IsFakeContextMode() && !IsMesaEglMode() && g_wglMakeCurrent) {
        g_wglMakeCurrent(nullptr, nullptr);
    }
    if (!IsFakeContextMode() && !IsMesaEglMode() && g_wglContext && g_wglDeleteContext) {
        g_wglDeleteContext(g_wglContext);
    }
    g_wglContext = nullptr;
    if (!g_wglSyntheticDc && g_wglDc && g_wglOldBitmap) {
        SelectObject(g_wglDc, g_wglOldBitmap);
    }
    if (!g_wglSyntheticDc && g_wglBitmap) {
        DeleteObject(g_wglBitmap);
    }
    if (!g_wglSyntheticDc && g_wglDc) {
        DeleteDC(g_wglDc);
    }
    g_wglOldBitmap = nullptr;
    g_wglBitmap = nullptr;
    g_wglDc = nullptr;
    g_wglSyntheticDc = false;
    if (g_eglReadback) {
        std::free(g_eglReadback);
        g_eglReadback = nullptr;
        g_eglReadbackBytes = 0;
    }
    if (g_glCommandState) {
        UnmapViewOfFile(g_glCommandState);
        g_glCommandState = nullptr;
    }
    if (g_glCommandStateMapping) {
        CloseHandle(g_glCommandStateMapping);
        g_glCommandStateMapping = nullptr;
    }
    g_contextCurrent.store(0, std::memory_order_release);
    g_initialized.store(0, std::memory_order_release);
}

XGLFW_API void glfwInitHint(int hint, int value) {
    char line[128] = {};
    std::snprintf(line, sizeof(line), "glfwInitHint hint=0x%08x value=%d", hint, value);
    DebugLine(line);
}
XGLFW_API void glfwInitAllocator(const GLFWallocator* /*allocator*/) {}
XGLFW_API void glfwInitVulkanLoader(PFN_vkGetInstanceProcAddr /*loader*/) {}

XGLFW_API void glfwGetVersion(int* major, int* minor, int* rev) {
    if (major) *major = 3;
    if (minor) *minor = 3;
    if (rev)   *rev   = 8;
}

XGLFW_API const char* glfwGetVersionString(void) {
    return kVersionString;
}

XGLFW_API int glfwGetError(const char** description) {
    int code = g_last_error_code.exchange(GLFW_NO_ERROR, std::memory_order_relaxed);
    if (description) {
        *description = (code == GLFW_NO_ERROR) ? nullptr : "xbox-glfw stub: no detail";
    }
    return code;
}

XGLFW_API GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun cb) {
    return XSWAP(g_cbError, cb);
}

XGLFW_API int glfwGetPlatform(void) {
    return GLFW_PLATFORM_WIN32;
}

XGLFW_API int glfwPlatformSupported(int platform) {
    return platform == GLFW_PLATFORM_WIN32 ? GLFW_TRUE : GLFW_FALSE;
}

// -----------------------------------------------------------------------------
// Monitor
// -----------------------------------------------------------------------------

static GLFWmonitor* g_monitorList[1] = { kPrimaryMonitor };

XGLFW_API GLFWmonitor** glfwGetMonitors(int* count) {
    if (count) *count = 1;
    return g_monitorList;
}

XGLFW_API GLFWmonitor* glfwGetPrimaryMonitor(void) {
    return kPrimaryMonitor;
}

XGLFW_API void glfwGetMonitorPos(GLFWmonitor*, int* xpos, int* ypos) {
    if (xpos) *xpos = 0;
    if (ypos) *ypos = 0;
}

XGLFW_API void glfwGetMonitorWorkarea(GLFWmonitor*, int* xpos, int* ypos, int* width, int* height) {
    if (xpos)   *xpos   = 0;
    if (ypos)   *ypos   = 0;
    if (width)  *width  = QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_WIDTH", kDefaultWidth);
    if (height) *height = QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_HEIGHT", kDefaultHeight);
}

XGLFW_API void glfwGetMonitorPhysicalSize(GLFWmonitor*, int* widthMM, int* heightMM) {
    if (widthMM)  *widthMM  = 600;
    if (heightMM) *heightMM = 340;
}

XGLFW_API void glfwGetMonitorContentScale(GLFWmonitor*, float* xscale, float* yscale) {
    if (xscale) *xscale = 1.0f;
    if (yscale) *yscale = 1.0f;
}

XGLFW_API const char* glfwGetMonitorName(GLFWmonitor*) {
    return kMonitorName;
}

XGLFW_API void  glfwSetMonitorUserPointer(GLFWmonitor*, void*) {}
XGLFW_API void* glfwGetMonitorUserPointer(GLFWmonitor*) { return nullptr; }

XGLFW_API GLFWmonitorfun glfwSetMonitorCallback(GLFWmonitorfun cb) {
    return XSWAP(g_cbMonitor, cb);
}

static GLFWvidmode kVidMode = { kDefaultWidth, kDefaultHeight, 8, 8, 8, kDefaultRefresh };

XGLFW_API const GLFWvidmode* glfwGetVideoModes(GLFWmonitor*, int* count) {
    if (count) *count = 1;
    kVidMode.width = QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_WIDTH", kDefaultWidth);
    kVidMode.height = QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_HEIGHT", kDefaultHeight);
    kVidMode.refreshRate = QueryMonitorRefreshRate();
    return &kVidMode;
}

XGLFW_API const GLFWvidmode* glfwGetVideoMode(GLFWmonitor*) {
    kVidMode.width = QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_WIDTH", kDefaultWidth);
    kVidMode.height = QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_HEIGHT", kDefaultHeight);
    kVidMode.refreshRate = QueryMonitorRefreshRate();
    return &kVidMode;
}

XGLFW_API void glfwSetGamma(GLFWmonitor*, float) {}
XGLFW_API const GLFWgammaramp* glfwGetGammaRamp(GLFWmonitor*) { return nullptr; }
XGLFW_API void glfwSetGammaRamp(GLFWmonitor*, const GLFWgammaramp*) {}

// -----------------------------------------------------------------------------
// Window
// -----------------------------------------------------------------------------

XGLFW_API void glfwDefaultWindowHints(void) {
    DebugLine("glfwDefaultWindowHints");
    g_window_hint_client_api.store(0x00030001, std::memory_order_release); // GLFW_OPENGL_API
    g_window_hint_context_major.store(3, std::memory_order_release);
    g_window_hint_context_minor.store(2, std::memory_order_release);
}

XGLFW_API void glfwWindowHint(int hint, int value) {
    switch (hint) {
        case 0x00022001: // GLFW_CLIENT_API
            g_window_hint_client_api.store(value, std::memory_order_release);
            break;
        case 0x00022002: // GLFW_CONTEXT_VERSION_MAJOR
            g_window_hint_context_major.store(value, std::memory_order_release);
            break;
        case 0x00022003: // GLFW_CONTEXT_VERSION_MINOR
            g_window_hint_context_minor.store(value, std::memory_order_release);
            break;
        default:
            break;
    }

    if (hint == 0x00022001 || hint == 0x00022002 || hint == 0x00022003 || hint == 0x00022008 || hint == 0x0002200B) {
        char line[160] = {};
        std::snprintf(line, sizeof(line), "glfwWindowHint hint=0x%08x value=%d", hint, value);
        DebugLine(line);
    }
}

XGLFW_API void glfwWindowHintString(int hint, const char* value) {
    char line[256] = {};
    std::snprintf(line, sizeof(line), "glfwWindowHintString hint=0x%08x value=%s", hint, value ? value : "<null>");
    DebugLine(line);
}

XGLFW_API GLFWwindow* glfwCreateWindow(int width, int height, const char* title, GLFWmonitor*, GLFWwindow*) {
    PumpCoreWindowEventsForCurrentThread("glfwCreateWindow");
    g_window = WindowState{};
    ResetKeyboardState();
    ResetMouseState();
    InstallKeyboardHooks();
    InstallMouseHooks();
    g_window_created.store(1, std::memory_order_release);
    g_eglWidth = width > 0 ? width : QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_WIDTH", kDefaultWidth);
    g_eglHeight = height > 0 ? height : QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_HEIGHT", kDefaultHeight);
    char line[256] = {};
    std::snprintf(
        line,
        sizeof(line),
        "glfwCreateWindow render=%dx%d surface=%dx%d title=%s clientApi=0x%08x context=%d.%d",
        g_eglWidth,
        g_eglHeight,
        QueryLaunchDimension(L"MINECRAFT_XBOX_SURFACE_WIDTH", g_eglWidth),
        QueryLaunchDimension(L"MINECRAFT_XBOX_SURFACE_HEIGHT", g_eglHeight),
        title ? title : "<null>",
        g_window_hint_client_api.load(std::memory_order_acquire),
        g_window_hint_context_major.load(std::memory_order_acquire),
        g_window_hint_context_minor.load(std::memory_order_acquire));
    DebugLine(line);
    if (IsFakeContextMode()) {
        DebugLine("glfwCreateWindow using fake OpenGL context path; WGL context creation skipped");
    }
    else if (IsMesaEglMode()) {
        if (!EnsureMesaEglContext()) {
            DebugLine("glfwCreateWindow could not make Mesa EGL context current");
            if (IsFatalOnSurfacelessMode()) {
                g_window_created.store(0, std::memory_order_release);
                return nullptr;
            }
        }
    }
    else if (!EnsureWglContext()) {
        DebugLine("glfwCreateWindow could not make Mesa WGL context current");
    }
    return kPrimaryWindow;
}

XGLFW_API void glfwDestroyWindow(GLFWwindow*) {
    DebugLine("glfwDestroyWindow");
    if (IsMesaEglMode() && g_eglMakeCurrent && g_eglDisplay != EGL_NO_DISPLAY) {
        __try {
            g_eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        __except (LogEglException("eglMakeCurrent(destroy null)", GetExceptionCode())) {
        }
    }
    else if (!IsFakeContextMode() && g_wglMakeCurrent) {
        g_wglMakeCurrent(nullptr, nullptr);
    }
    g_contextCurrent.store(0, std::memory_order_release);
    g_window_created.store(0, std::memory_order_release);
    g_window = WindowState{};
    ResetKeyboardState();
    ResetMouseState();
}

XGLFW_API int glfwWindowShouldClose(GLFWwindow*) {
    return g_window.shouldClose;
}

XGLFW_API void glfwSetWindowShouldClose(GLFWwindow*, int value) {
    g_window.shouldClose = value ? GLFW_TRUE : GLFW_FALSE;
}

XGLFW_API void glfwSetWindowTitle(GLFWwindow*, const char*) {}
XGLFW_API void glfwSetWindowIcon(GLFWwindow*, int, const GLFWimage*) {}

XGLFW_API void glfwGetWindowPos(GLFWwindow*, int* xpos, int* ypos) {
    if (xpos) *xpos = 0;
    if (ypos) *ypos = 0;
}

XGLFW_API void glfwSetWindowPos(GLFWwindow*, int, int) {}

XGLFW_API void glfwGetWindowSize(GLFWwindow*, int* width, int* height) {
    if (width)  *width  = QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_WIDTH", kDefaultWidth);
    if (height) *height = QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_HEIGHT", kDefaultHeight);
}

XGLFW_API void glfwSetWindowSizeLimits(GLFWwindow*, int, int, int, int) {}
XGLFW_API void glfwSetWindowAspectRatio(GLFWwindow*, int, int) {}
XGLFW_API void glfwSetWindowSize(GLFWwindow*, int, int) {}

XGLFW_API void glfwGetFramebufferSize(GLFWwindow*, int* width, int* height) {
    if (width)  *width  = QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_WIDTH", kDefaultWidth);
    if (height) *height = QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_HEIGHT", kDefaultHeight);
}

XGLFW_API void glfwGetWindowFrameSize(GLFWwindow*, int* left, int* top, int* right, int* bottom) {
    if (left)   *left   = 0;
    if (top)    *top    = 0;
    if (right)  *right  = 0;
    if (bottom) *bottom = 0;
}

XGLFW_API void glfwGetWindowContentScale(GLFWwindow*, float* xscale, float* yscale) {
    if (xscale) *xscale = 1.0f;
    if (yscale) *yscale = 1.0f;
}

XGLFW_API float glfwGetWindowOpacity(GLFWwindow*) { return 1.0f; }
XGLFW_API void  glfwSetWindowOpacity(GLFWwindow*, float) {}
XGLFW_API void  glfwIconifyWindow(GLFWwindow*) {}
XGLFW_API void  glfwRestoreWindow(GLFWwindow*) {}
XGLFW_API void  glfwMaximizeWindow(GLFWwindow*) {}
XGLFW_API void  glfwShowWindow(GLFWwindow*) {}
XGLFW_API void  glfwHideWindow(GLFWwindow*) {}
XGLFW_API void  glfwFocusWindow(GLFWwindow*) {}
XGLFW_API void  glfwRequestWindowAttention(GLFWwindow*) {}

XGLFW_API GLFWmonitor* glfwGetWindowMonitor(GLFWwindow*) {
    return kPrimaryMonitor;
}

XGLFW_API void glfwSetWindowMonitor(GLFWwindow*, GLFWmonitor*, int, int, int, int, int) {}

XGLFW_API int glfwGetWindowAttrib(GLFWwindow*, int attrib) {
    // Standard GLFW attribute IDs; defaults are "focused, visible, not iconified".
    switch (attrib) {
        case 0x00020001: return GLFW_TRUE;  // FOCUSED
        case 0x00020002: return GLFW_FALSE; // ICONIFIED
        case 0x00020003: return GLFW_TRUE;  // RESIZABLE
        case 0x00020004: return GLFW_TRUE;  // VISIBLE
        case 0x00020005: return GLFW_TRUE;  // DECORATED
        case 0x00020006: return GLFW_FALSE; // AUTO_ICONIFY
        case 0x00020007: return GLFW_FALSE; // FLOATING
        case 0x00020008: return GLFW_FALSE; // MAXIMIZED
        case 0x00020009: return GLFW_TRUE;  // CENTER_CURSOR
        case 0x0002000A: return GLFW_FALSE; // TRANSPARENT_FRAMEBUFFER
        case 0x0002000B: return GLFW_TRUE;  // HOVERED
        case 0x0002000C: return GLFW_TRUE;  // FOCUS_ON_SHOW
        case 0x0002000D: return GLFW_FALSE; // MOUSE_PASSTHROUGH
        case 0x00022001: return g_window_hint_client_api.load(std::memory_order_acquire); // CLIENT_API
        case 0x00022002: return g_window_hint_context_major.load(std::memory_order_acquire); // CONTEXT_VERSION_MAJOR
        case 0x00022003: return g_window_hint_context_minor.load(std::memory_order_acquire); // CONTEXT_VERSION_MINOR
        case 0x00022008: return 0x00032001; // OPENGL_PROFILE = GLFW_OPENGL_CORE_PROFILE
        default:         return 0;
    }
}

XGLFW_API void glfwSetWindowAttrib(GLFWwindow*, int, int) {}

XGLFW_API void  glfwSetWindowUserPointer(GLFWwindow*, void* ptr) { g_window.userPointer = ptr; }
XGLFW_API void* glfwGetWindowUserPointer(GLFWwindow*) { return g_window.userPointer; }

XGLFW_API GLFWwindowposfun         glfwSetWindowPosCallback(GLFWwindow*, GLFWwindowposfun cb)                 { return XSWAP(g_window.cbPos, cb); }
XGLFW_API GLFWwindowsizefun        glfwSetWindowSizeCallback(GLFWwindow*, GLFWwindowsizefun cb)               { return XSWAP(g_window.cbSize, cb); }
XGLFW_API GLFWwindowclosefun       glfwSetWindowCloseCallback(GLFWwindow*, GLFWwindowclosefun cb)             { return XSWAP(g_window.cbClose, cb); }
XGLFW_API GLFWwindowrefreshfun     glfwSetWindowRefreshCallback(GLFWwindow*, GLFWwindowrefreshfun cb)         { return XSWAP(g_window.cbRefresh, cb); }
XGLFW_API GLFWwindowfocusfun       glfwSetWindowFocusCallback(GLFWwindow*, GLFWwindowfocusfun cb)             { return XSWAP(g_window.cbFocus, cb); }
XGLFW_API GLFWwindowiconifyfun     glfwSetWindowIconifyCallback(GLFWwindow*, GLFWwindowiconifyfun cb)         { return XSWAP(g_window.cbIconify, cb); }
XGLFW_API GLFWwindowmaximizefun    glfwSetWindowMaximizeCallback(GLFWwindow*, GLFWwindowmaximizefun cb)       { return XSWAP(g_window.cbMaximize, cb); }
XGLFW_API GLFWframebuffersizefun   glfwSetFramebufferSizeCallback(GLFWwindow*, GLFWframebuffersizefun cb)     { return XSWAP(g_window.cbFramebuffer, cb); }
XGLFW_API GLFWwindowcontentscalefun glfwSetWindowContentScaleCallback(GLFWwindow*, GLFWwindowcontentscalefun cb){ return XSWAP(g_window.cbContentScale, cb); }

// -----------------------------------------------------------------------------
// Events
// -----------------------------------------------------------------------------

XGLFW_API void glfwPollEvents(void) {
    PumpCoreWindowEventsForCurrentThread("glfwPollEvents");
    NotifyGamepadConnectionChange();
    DispatchGamepadMouseModeTick();
}

XGLFW_API void glfwWaitEvents(void) {
    PumpCoreWindowEventsForCurrentThread("glfwWaitEvents");
    NotifyGamepadConnectionChange();
    DispatchGamepadMouseModeTick();
    Sleep(1);
}

XGLFW_API void glfwWaitEventsTimeout(double timeout) {
    PumpCoreWindowEventsForCurrentThread("glfwWaitEventsTimeout");
    NotifyGamepadConnectionChange();
    DispatchGamepadMouseModeTick();
    DWORD ms = static_cast<DWORD>(timeout * 1000.0);
    if (ms > 1000) ms = 1000;
    Sleep(ms);
}
XGLFW_API void glfwPostEmptyEvent(void) {}

// -----------------------------------------------------------------------------
// Input
// -----------------------------------------------------------------------------

XGLFW_API int  glfwGetInputMode(GLFWwindow*, int mode) {
    switch (mode) {
    case GLFW_CURSOR:
        if (IsGamepadMouseModeActive()) {
            return GLFW_CURSOR_NORMAL;
        }
        return g_cursorMode.load(std::memory_order_acquire);
    case GLFW_STICKY_MOUSE_BUTTONS:
        return g_stickyMouseButtons.load(std::memory_order_acquire);
    case GLFW_RAW_MOUSE_MOTION:
        return g_rawMouseMotion.load(std::memory_order_acquire);
    default:
        return 0;
    }
}

XGLFW_API void glfwSetInputMode(GLFWwindow*, int mode, int value) {
    switch (mode) {
    case GLFW_CURSOR:
        if (value == GLFW_CURSOR_NORMAL || value == GLFW_CURSOR_HIDDEN || value == GLFW_CURSOR_DISABLED) {
            g_cursorMode.store(value, std::memory_order_release);
        }
        break;
    case GLFW_STICKY_MOUSE_BUTTONS:
        g_stickyMouseButtons.store(value == GLFW_TRUE ? GLFW_TRUE : GLFW_FALSE, std::memory_order_release);
        break;
    case GLFW_RAW_MOUSE_MOTION:
        g_rawMouseMotion.store(value == GLFW_TRUE ? GLFW_TRUE : GLFW_FALSE, std::memory_order_release);
        break;
    default:
        break;
    }
}
XGLFW_API int  glfwRawMouseMotionSupported(void) { return GLFW_FALSE; }
XGLFW_API const char* glfwGetKeyName(int, int) { return nullptr; }
XGLFW_API int  glfwGetKeyScancode(int key) { return key; }
XGLFW_API int  glfwGetKey(GLFWwindow*, int key) {
    return IsGlfwKeyIndex(key)
        ? static_cast<int>(g_keyStates[key].load(std::memory_order_acquire))
        : GLFW_RELEASE;
}
XGLFW_API int  glfwGetMouseButton(GLFWwindow*, int button) {
    return IsGlfwMouseButtonIndex(button)
        ? static_cast<int>(g_mouseButtonStates[button].load(std::memory_order_acquire))
        : GLFW_RELEASE;
}

XGLFW_API void glfwGetCursorPos(GLFWwindow*, double* xpos, double* ypos) {
    std::lock_guard<std::mutex> lock(g_mousePositionMutex);
    if (xpos) *xpos = g_cursorX;
    if (ypos) *ypos = g_cursorY;
}
XGLFW_API void glfwSetCursorPos(GLFWwindow*, double xpos, double ypos) {
    {
        std::lock_guard<std::mutex> lock(g_mousePositionMutex);
        g_cursorX = xpos;
        g_cursorY = ypos;
    }
}

XGLFW_API GLFWcursor* glfwCreateCursor(const GLFWimage*, int, int) { return kSharedCursor; }
XGLFW_API GLFWcursor* glfwCreateStandardCursor(int) { return kSharedCursor; }
XGLFW_API void        glfwDestroyCursor(GLFWcursor*) {}
XGLFW_API void        glfwSetCursor(GLFWwindow*, GLFWcursor*) {}

XGLFW_API GLFWkeyfun         glfwSetKeyCallback(GLFWwindow*, GLFWkeyfun cb)                 { return XSWAP(g_window.cbKey, cb); }
XGLFW_API GLFWcharfun        glfwSetCharCallback(GLFWwindow*, GLFWcharfun cb)               { return XSWAP(g_window.cbChar, cb); }
XGLFW_API GLFWcharmodsfun    glfwSetCharModsCallback(GLFWwindow*, GLFWcharmodsfun cb)       { return XSWAP(g_window.cbCharMods, cb); }
XGLFW_API GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow*, GLFWmousebuttonfun cb) { return XSWAP(g_window.cbMouseButton, cb); }
XGLFW_API GLFWcursorposfun   glfwSetCursorPosCallback(GLFWwindow*, GLFWcursorposfun cb)     { return XSWAP(g_window.cbCursorPos, cb); }
XGLFW_API GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow*, GLFWcursorenterfun cb) { return XSWAP(g_window.cbCursorEnter, cb); }
XGLFW_API GLFWscrollfun      glfwSetScrollCallback(GLFWwindow*, GLFWscrollfun cb)           { return XSWAP(g_window.cbScroll, cb); }
XGLFW_API GLFWdropfun        glfwSetDropCallback(GLFWwindow*, GLFWdropfun cb)               { return XSWAP(g_window.cbDrop, cb); }

XGLFW_API int glfwJoystickPresent(int jid) {
    if (jid != GLFW_JOYSTICK_1) {
        return GLFW_FALSE;
    }

    int present = CurrentGamepadPresentFlag();
    g_lastGamepadPresent.store(present, std::memory_order_release);
    return present ? GLFW_TRUE : GLFW_FALSE;
}

XGLFW_API const float* glfwGetJoystickAxes(int jid, int* count) {
    if (jid != GLFW_JOYSTICK_1 || !RefreshCachedGamepadState()) {
        if (count) *count = 0;
        return nullptr;
    }

    if (count) *count = GLFW_GAMEPAD_AXIS_LAST + 1;
    return g_gamepadAxes;
}

XGLFW_API const unsigned char* glfwGetJoystickButtons(int jid, int* count) {
    if (jid != GLFW_JOYSTICK_1 || !RefreshCachedGamepadState()) {
        if (count) *count = 0;
        return nullptr;
    }

    if (count) *count = GLFW_GAMEPAD_BUTTON_LAST + 1;
    return g_gamepadButtons;
}

XGLFW_API const unsigned char* glfwGetJoystickHats(int jid, int* count) {
    if (jid != GLFW_JOYSTICK_1 || !RefreshCachedGamepadState()) {
        if (count) *count = 0;
        return nullptr;
    }

    if (count) *count = 1;
    return g_gamepadHats;
}

XGLFW_API const char* glfwGetJoystickName(int jid) {
    return jid == GLFW_JOYSTICK_1 && HasPrimaryGamepad() ? "Xbox Gamepad" : nullptr;
}

XGLFW_API const char* glfwGetJoystickGUID(int jid) {
    return jid == GLFW_JOYSTICK_1 && HasPrimaryGamepad() ? "78626f782d7577702d67616d65706164" : nullptr;
}

XGLFW_API void glfwSetJoystickUserPointer(int jid, void* pointer) {
    if (jid == GLFW_JOYSTICK_1) {
        g_gamepadUserPointer = pointer;
    }
}

XGLFW_API void* glfwGetJoystickUserPointer(int jid) {
    return jid == GLFW_JOYSTICK_1 ? g_gamepadUserPointer : nullptr;
}

XGLFW_API int glfwJoystickIsGamepad(int jid) {
    return glfwJoystickPresent(jid);
}

XGLFW_API const char* glfwGetGamepadName(int jid) {
    return jid == GLFW_JOYSTICK_1 && HasPrimaryGamepad() ? "Xbox Gamepad" : nullptr;
}

XGLFW_API int glfwGetGamepadState(int jid, GLFWgamepadstate* state) {
    if (state) memset(state, 0, sizeof(*state));
    if (jid != GLFW_JOYSTICK_1 || !state) {
        return GLFW_FALSE;
    }

    GamepadReading reading = {};
    if (!TryReadPrimaryGamepad(reading)) {
        return GLFW_FALSE;
    }

    FillGlfwGamepadState(*state, reading);
    UpdateGamepadMouseModeToggle(reading);
    if (IsGamepadMouseModeActive()) {
        NeutralizeGlfwGamepadState(*state);
    }
    return GLFW_TRUE;
}
XGLFW_API int  glfwUpdateGamepadMappings(const char*) { return GLFW_TRUE; }
XGLFW_API GLFWjoystickfun glfwSetJoystickCallback(GLFWjoystickfun cb) {
    GLFWjoystickfun previous = XSWAP(g_cbJoystick, cb);
    if (cb) {
        SeedGamepadConnectionState();
    }
    return previous;
}

XGLFW_API const char* glfwGetClipboardString(GLFWwindow*) { return ""; }
XGLFW_API void glfwSetClipboardString(GLFWwindow*, const char*) {}

// -----------------------------------------------------------------------------
// Time
// -----------------------------------------------------------------------------

XGLFW_API double glfwGetTime(void) {
    EnsureTimeBase();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double elapsed = double(now.QuadPart - g_qpcStart.QuadPart) / double(g_qpcFreq.QuadPart);
    return elapsed - g_timeOffset;
}

XGLFW_API void glfwSetTime(double time) {
    EnsureTimeBase();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double raw = double(now.QuadPart - g_qpcStart.QuadPart) / double(g_qpcFreq.QuadPart);
    g_timeOffset = raw - time;
}

XGLFW_API uint64_t glfwGetTimerValue(void) {
    EnsureTimeBase();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<uint64_t>(now.QuadPart);
}

XGLFW_API uint64_t glfwGetTimerFrequency(void) {
    EnsureTimeBase();
    return static_cast<uint64_t>(g_qpcFreq.QuadPart);
}

// -----------------------------------------------------------------------------
// Context / GL loader
// -----------------------------------------------------------------------------

XGLFW_API void glfwMakeContextCurrent(GLFWwindow* window) {
    if (IsFakeContextMode()) {
        if (!window) {
            DebugLine("glfwMakeContextCurrent(fake null)");
            g_contextCurrent.store(0, std::memory_order_release);
            return;
        }

        DebugLine("glfwMakeContextCurrent(fake primary)");
        g_contextCurrent.store(1, std::memory_order_release);
        return;
    }

    if (!window) {
        DebugLine("glfwMakeContextCurrent(null)");
        if (IsMesaEglMode() && g_eglMakeCurrent && g_eglDisplay != EGL_NO_DISPLAY) {
            __try {
                g_eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            }
            __except (LogEglException("eglMakeCurrent(null)", GetExceptionCode())) {
            }
        }
        else if (EnsureOpenGlModule() && g_wglMakeCurrent) {
            __try {
                g_wglMakeCurrent(nullptr, nullptr);
            }
            __except (LogNativeException("wglMakeCurrent(null)", GetExceptionCode())) {
            }
        }
        g_contextCurrent.store(0, std::memory_order_release);
        return;
    }

    DebugLine("glfwMakeContextCurrent(primary)");
    if (IsMesaEglMode()) {
        if (!EnsureMesaEglContext()) {
            DebugLine("glfwMakeContextCurrent primary failed to make Mesa EGL context current");
        }
    }
    else if (!EnsureWglContext()) {
        DebugLine("glfwMakeContextCurrent primary failed to make context current");
    }
}

XGLFW_API GLFWwindow* glfwGetCurrentContext(void) {
    if (IsFakeContextMode()) {
        return g_contextCurrent.load(std::memory_order_acquire) ? kPrimaryWindow : nullptr;
    }
    if (IsMesaEglMode()) {
        if (!g_contextCurrent.load(std::memory_order_acquire) && g_window_created.load(std::memory_order_acquire)) {
            EnsureMesaEglContext();
        }
        return g_contextCurrent.load(std::memory_order_acquire) ? kPrimaryWindow : nullptr;
    }

    static std::atomic<int> logCount{ 0 };
    if (!g_contextCurrent.load(std::memory_order_acquire) && g_window_created.load(std::memory_order_acquire)) {
        int count = logCount.fetch_add(1, std::memory_order_relaxed);
        if (count < 4) {
            DebugLine("glfwGetCurrentContext saw no current context; trying EnsureWglContext");
        }
        EnsureWglContext();
    }
    return g_contextCurrent.load(std::memory_order_acquire) ? kPrimaryWindow : nullptr;
}

namespace {
    HANDLE g_presentEvent = nullptr;

    void EnsurePresentEventHandle() {
        if (g_presentEvent) {
            return;
        }

        wchar_t name[260] = L"Local\\MinecraftXboxJavaPresent";
        wchar_t* envName = nullptr;
        size_t envLen = 0;
        if (_wdupenv_s(&envName, &envLen, L"MINECRAFT_XBOX_PRESENT_EVENT") == 0 && envName && envName[0]) {
            wcsncpy_s(name, envName, _TRUNCATE);
        }
        free(envName);
        g_presentEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    }

    void SignalPresentEvent() {
        EnsurePresentEventHandle();
        if (g_presentEvent) {
            SetEvent(g_presentEvent);
        }
    }

    bool MesaEglContextIsCurrent() {
        if (!g_eglGetCurrentContext || g_eglContext == EGL_NO_CONTEXT) {
            return g_contextCurrent.load(std::memory_order_acquire) != 0;
        }

        EGLContext current = EGL_NO_CONTEXT;
        __try {
            current = g_eglGetCurrentContext();
        }
        __except (LogEglRecoverableException("eglGetCurrentContext", GetExceptionCode())) {
            current = EGL_NO_CONTEXT;
        }

        const bool isCurrent = current == g_eglContext;
        g_contextCurrent.store(isCurrent ? 1 : 0, std::memory_order_release);
        return isCurrent;
    }

    bool EnsureMesaEglCurrentForPresent() {
        if (!g_eglMakeCurrent ||
            g_eglDisplay == EGL_NO_DISPLAY ||
            g_eglSurface == EGL_NO_SURFACE ||
            g_eglContext == EGL_NO_CONTEXT) {
            static std::atomic<int> missingLogCount{ 0 };
            int count = missingLogCount.fetch_add(1, std::memory_order_relaxed);
            if (count < 8) {
                DebugLine("eglSwapBuffers skipped; EGL display/surface/context missing");
            }
            return false;
        }

        if (MesaEglContextIsCurrent()) {
            static std::atomic<int> alreadyCurrentLogCount{ 0 };
            int count = alreadyCurrentLogCount.fetch_add(1, std::memory_order_relaxed);
            if (count < 4 || (count % 300) == 0) {
                DebugLine("eglMakeCurrent(before swap) skipped; EGL context already current");
            }
            return true;
        }

        EGLBoolean madeCurrent = EGL_FALSE_VALUE;
        __try {
            madeCurrent = g_eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext);
        }
        __except (LogEglRecoverableException("eglMakeCurrent(before swap)", GetExceptionCode())) {
            madeCurrent = EGL_FALSE_VALUE;
        }

        if (!madeCurrent) {
            static std::atomic<int> failedLogCount{ 0 };
            int count = failedLogCount.fetch_add(1, std::memory_order_relaxed);
            if (count < 8) {
                char line[160] = {};
                std::snprintf(line, sizeof(line), "eglMakeCurrent(before swap) failed err=0x%04x", EglErrorCode());
                DebugLine(line);
            }
            g_contextCurrent.store(0, std::memory_order_release);
            return false;
        }

        g_contextCurrent.store(1, std::memory_order_release);
        static std::atomic<int> successLogCount{ 0 };
        int count = successLogCount.fetch_add(1, std::memory_order_relaxed);
        if (count < 8 || (count % 300) == 0) {
            char line[192] = {};
            std::snprintf(
                line,
                sizeof(line),
                "eglMakeCurrent(before swap) succeeded count=%d display=%p surface=%p context=%p",
                count + 1,
                g_eglDisplay,
                g_eglSurface,
                g_eglContext);
            DebugLine(line);
        }
        return true;
    }

    bool RecoverMesaEglSurfaceAfterSwapFailure() {
        if (!g_eglMakeCurrent ||
            !g_eglDestroySurface ||
            g_eglDisplay == EGL_NO_DISPLAY ||
            !g_eglConfig ||
            g_eglContext == EGL_NO_CONTEXT) {
            DebugLine("eglSwapBuffers recovery skipped; EGL display/config/context missing");
            return false;
        }

        static std::atomic<int> recoveryAttemptCount{ 0 };
        int recoveryAttempt = recoveryAttemptCount.fetch_add(1, std::memory_order_relaxed) + 1;
        char beginLine[192] = {};
        std::snprintf(
            beginLine,
            sizeof(beginLine),
            "eglSwapBuffers recovery attempt=%d oldSurface=%p path=%s",
            recoveryAttempt,
            g_eglSurface,
            g_eglSurfacePath ? g_eglSurfacePath : "<null>");
        DebugLine(beginLine);

        __try {
            g_eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        __except (LogEglRecoverableException("eglMakeCurrent(recover null)", GetExceptionCode())) {
        }
        g_contextCurrent.store(0, std::memory_order_release);

        EGLSurface oldSurface = g_eglSurface;
        g_eglSurface = EGL_NO_SURFACE;
        if (oldSurface != EGL_NO_SURFACE) {
            __try {
                g_eglDestroySurface(g_eglDisplay, oldSurface);
            }
            __except (LogEglRecoverableException("eglDestroySurface(recover)", GetExceptionCode())) {
            }
        }

        IUnknown* coreWindowUnknown = AcquireCoreWindowUnknown();
        IUnknown* eglWindowDescriptorUnknown = AcquireEglWindowDescriptorUnknown();
        struct RecoverySurfaceAttempt {
            const char* name;
            void* nativeWindow;
            int method;
        };
        const RecoverySurfaceAttempt attempts[] = {
            { "eglCreateWindowSurface(recover CoreWindow)", coreWindowUnknown, 0 },
            { "eglCreatePlatformWindowSurfaceEXT(recover CoreWindow)", coreWindowUnknown, 1 },
            { "eglCreatePlatformWindowSurface(recover CoreWindow)", coreWindowUnknown, 2 },
            { "eglCreateWindowSurface(recover PropertySet)", eglWindowDescriptorUnknown, 0 },
            { "eglCreatePlatformWindowSurfaceEXT(recover PropertySet)", eglWindowDescriptorUnknown, 1 },
            { "eglCreatePlatformWindowSurface(recover PropertySet)", eglWindowDescriptorUnknown, 2 },
        };

        for (const auto& attempt : attempts) {
            if (!attempt.nativeWindow) {
                continue;
            }
            if (attempt.method == 0 && !g_eglCreateWindowSurface) {
                continue;
            }
            if (attempt.method == 1 && !g_eglCreatePlatformWindowSurfaceEXT) {
                continue;
            }
            if (attempt.method == 2 && !g_eglCreatePlatformWindowSurface) {
                continue;
            }

            char attemptLine[224] = {};
            std::snprintf(
                attemptLine,
                sizeof(attemptLine),
                "%s thread=0x%lx nativeWindow=%p",
                attempt.name,
                CurrentThreadIdForLog(),
                attempt.nativeWindow);
            DebugLine(attemptLine);

            EGLSurface newSurface = EGL_NO_SURFACE;
            __try {
                if (attempt.method == 0) {
                    newSurface = g_eglCreateWindowSurface(g_eglDisplay, g_eglConfig, attempt.nativeWindow, nullptr);
                }
                else if (attempt.method == 1) {
                    newSurface = g_eglCreatePlatformWindowSurfaceEXT(g_eglDisplay, g_eglConfig, attempt.nativeWindow, nullptr);
                }
                else {
                    newSurface = g_eglCreatePlatformWindowSurface(g_eglDisplay, g_eglConfig, attempt.nativeWindow, nullptr);
                }
            }
            __except (LogEglRecoverableException(attempt.name, GetExceptionCode())) {
                newSurface = EGL_NO_SURFACE;
            }

            if (newSurface == EGL_NO_SURFACE) {
                char failedLine[192] = {};
                std::snprintf(failedLine, sizeof(failedLine), "%s failed err=0x%04x", attempt.name, EglErrorCode());
                DebugLine(failedLine);
                continue;
            }

            EGLBoolean madeCurrent = EGL_FALSE_VALUE;
            __try {
                madeCurrent = g_eglMakeCurrent(g_eglDisplay, newSurface, newSurface, g_eglContext);
            }
            __except (LogEglRecoverableException("eglMakeCurrent(recover new surface)", GetExceptionCode())) {
                madeCurrent = EGL_FALSE_VALUE;
            }

            if (!madeCurrent) {
                char failedLine[192] = {};
                std::snprintf(failedLine, sizeof(failedLine), "eglMakeCurrent(recover new surface) failed err=0x%04x", EglErrorCode());
                DebugLine(failedLine);
                __try {
                    g_eglDestroySurface(g_eglDisplay, newSurface);
                }
                __except (LogEglRecoverableException("eglDestroySurface(recover failed new)", GetExceptionCode())) {
                }
                continue;
            }

            g_eglSurface = newSurface;
            g_eglSurfacePath = attempt.name;
            g_eglSurfacelessContext.store(0, std::memory_order_release);
            g_contextCurrent.store(1, std::memory_order_release);
            DebugLine("eglSwapBuffers recovery succeeded with fresh EGL window surface");
            return true;
        }

        DebugLine("eglSwapBuffers recovery failed; no fresh EGL window surface could be created");
        return false;
    }

    PROC LoadOverlayGlProc(const char* name) {
        PROC proc = nullptr;
        if (g_eglGetProcAddress) {
            proc = reinterpret_cast<PROC>(g_eglGetProcAddress(name));
        }
        if (!proc) {
            proc = LoadOpenGlProc(name);
        }
        return proc;
    }

    bool EnsureGamepadMouseOverlayProcs(
        GlGetIntegervProc& glGetIntegerv,
        GlGetFloatvProc& glGetFloatv,
        GlGetBooleanvProc& glGetBooleanv,
        GlIsEnabledProc& glIsEnabled,
        GlEnableProc& glEnable,
        GlDisableProc& glDisable,
        GlScissorProc& glScissor,
        GlClearColorProc& glClearColor,
        GlClearProc& glClear,
        GlColorMaskProc& glColorMask,
        GlBindFramebufferProc& glBindFramebuffer) {
        static GlGetIntegervProc cachedGetIntegerv = nullptr;
        static GlGetFloatvProc cachedGetFloatv = nullptr;
        static GlGetBooleanvProc cachedGetBooleanv = nullptr;
        static GlIsEnabledProc cachedIsEnabled = nullptr;
        static GlEnableProc cachedEnable = nullptr;
        static GlDisableProc cachedDisable = nullptr;
        static GlScissorProc cachedScissor = nullptr;
        static GlClearColorProc cachedClearColor = nullptr;
        static GlClearProc cachedClear = nullptr;
        static GlColorMaskProc cachedColorMask = nullptr;
        static GlBindFramebufferProc cachedBindFramebuffer = nullptr;
        static std::atomic<int> loadAttempted{ 0 };
        static std::atomic<int> missingLogCount{ 0 };

        if (loadAttempted.exchange(1, std::memory_order_acq_rel) == 0) {
            cachedGetIntegerv = reinterpret_cast<GlGetIntegervProc>(LoadOverlayGlProc("glGetIntegerv"));
            cachedGetFloatv = reinterpret_cast<GlGetFloatvProc>(LoadOverlayGlProc("glGetFloatv"));
            cachedGetBooleanv = reinterpret_cast<GlGetBooleanvProc>(LoadOverlayGlProc("glGetBooleanv"));
            cachedIsEnabled = reinterpret_cast<GlIsEnabledProc>(LoadOverlayGlProc("glIsEnabled"));
            cachedEnable = reinterpret_cast<GlEnableProc>(LoadOverlayGlProc("glEnable"));
            cachedDisable = reinterpret_cast<GlDisableProc>(LoadOverlayGlProc("glDisable"));
            cachedScissor = reinterpret_cast<GlScissorProc>(LoadOverlayGlProc("glScissor"));
            cachedClearColor = reinterpret_cast<GlClearColorProc>(LoadOverlayGlProc("glClearColor"));
            cachedClear = reinterpret_cast<GlClearProc>(LoadOverlayGlProc("glClear"));
            cachedColorMask = reinterpret_cast<GlColorMaskProc>(LoadOverlayGlProc("glColorMask"));
            cachedBindFramebuffer = reinterpret_cast<GlBindFramebufferProc>(LoadOverlayGlProc("glBindFramebuffer"));
        }

        glGetIntegerv = cachedGetIntegerv;
        glGetFloatv = cachedGetFloatv;
        glGetBooleanv = cachedGetBooleanv;
        glIsEnabled = cachedIsEnabled;
        glEnable = cachedEnable;
        glDisable = cachedDisable;
        glScissor = cachedScissor;
        glClearColor = cachedClearColor;
        glClear = cachedClear;
        glColorMask = cachedColorMask;
        glBindFramebuffer = cachedBindFramebuffer;

        const bool ready = glGetIntegerv && glGetFloatv && glGetBooleanv && glIsEnabled &&
            glEnable && glDisable && glScissor && glClearColor && glClear && glColorMask;
        if (!ready) {
            int count = missingLogCount.fetch_add(1, std::memory_order_relaxed);
            if (count < 4) {
                DebugLine("Controller mouse cursor overlay skipped; GL overlay procs missing");
            }
        }
        return ready;
    }

    int ClampOverlayInt(int value, int low, int high) {
        if (value < low) return low;
        if (value > high) return high;
        return value;
    }

    void DrawGamepadMouseOverlayRect(
        GlScissorProc glScissor,
        GlClearColorProc glClearColor,
        GlClearProc glClear,
        int framebufferWidth,
        int framebufferHeight,
        int left,
        int top,
        int width,
        int height,
        float red,
        float green,
        float blue,
        float alpha) {
        if (width <= 0 || height <= 0 || framebufferWidth <= 0 || framebufferHeight <= 0) {
            return;
        }

        int x0 = ClampOverlayInt(left, 0, framebufferWidth);
        int y0 = ClampOverlayInt(top, 0, framebufferHeight);
        int x1 = ClampOverlayInt(left + width, 0, framebufferWidth);
        int y1 = ClampOverlayInt(top + height, 0, framebufferHeight);
        if (x1 <= x0 || y1 <= y0) {
            return;
        }

        glScissor(x0, framebufferHeight - y1, x1 - x0, y1 - y0);
        glClearColor(red, green, blue, alpha);
        glClear(XGLFW_GL_COLOR_BUFFER_BIT);
    }

    void DrawGamepadMouseCursorOverlay() {
        if (!IsGamepadMouseModeActive()) {
            return;
        }

        const int framebufferWidth = g_eglWidth > 0
            ? g_eglWidth
            : QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_WIDTH", kDefaultWidth);
        const int framebufferHeight = g_eglHeight > 0
            ? g_eglHeight
            : QueryLaunchDimension(L"MINECRAFT_XBOX_LAUNCH_HEIGHT", kDefaultHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0) {
            return;
        }

        GlGetIntegervProc glGetIntegerv = nullptr;
        GlGetFloatvProc glGetFloatv = nullptr;
        GlGetBooleanvProc glGetBooleanv = nullptr;
        GlIsEnabledProc glIsEnabled = nullptr;
        GlEnableProc glEnable = nullptr;
        GlDisableProc glDisable = nullptr;
        GlScissorProc glScissor = nullptr;
        GlClearColorProc glClearColor = nullptr;
        GlClearProc glClear = nullptr;
        GlColorMaskProc glColorMask = nullptr;
        GlBindFramebufferProc glBindFramebuffer = nullptr;
        if (!EnsureGamepadMouseOverlayProcs(
                glGetIntegerv,
                glGetFloatv,
                glGetBooleanv,
                glIsEnabled,
                glEnable,
                glDisable,
                glScissor,
                glClearColor,
                glClear,
                glColorMask,
                glBindFramebuffer)) {
            return;
        }

        double cursorX = 0.0;
        double cursorY = 0.0;
        {
            std::lock_guard<std::mutex> lock(g_mousePositionMutex);
            cursorX = g_cursorX;
            cursorY = g_cursorY;
        }

        const int x = ClampOverlayInt(static_cast<int>(cursorX + 0.5), 0, framebufferWidth - 1);
        const int y = ClampOverlayInt(static_cast<int>(cursorY + 0.5), 0, framebufferHeight - 1);
        const int scale = (std::max)(1, (std::max)(framebufferWidth / 1920, framebufferHeight / 1080));
        const int radius = 13 * scale;
        const int line = (std::max)(2, 2 * scale);
        const int gap = (std::max)(3, 3 * scale);

        XglfwGLint previousFramebuffer = 0;
        XglfwGLint previousScissor[4] = {};
        XglfwGLfloat previousClearColor[4] = {};
        XglfwGLboolean previousColorMask[4] = {};
        XglfwGLboolean scissorWasEnabled = 0;

        glGetIntegerv(XGLFW_GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glGetIntegerv(XGLFW_GL_SCISSOR_BOX, previousScissor);
        glGetFloatv(XGLFW_GL_COLOR_CLEAR_VALUE, previousClearColor);
        glGetBooleanv(XGLFW_GL_COLOR_WRITEMASK, previousColorMask);
        scissorWasEnabled = glIsEnabled(XGLFW_GL_SCISSOR_TEST);

        if (glBindFramebuffer) {
            glBindFramebuffer(XGLFW_GL_DRAW_FRAMEBUFFER, 0);
        }
        glColorMask(1, 1, 1, 1);
        glEnable(XGLFW_GL_SCISSOR_TEST);

        // Draw a high-contrast software cursor.  It is intentionally a
        // cross instead of a pointer so it stays visible over mod menus
        // and Minecraft's bright UI textures at 4K.
        DrawGamepadMouseOverlayRect(
            glScissor,
            glClearColor,
            glClear,
            framebufferWidth,
            framebufferHeight,
            x - line - 1,
            y - radius,
            (line * 2) + 2,
            radius - gap,
            0.0f,
            0.0f,
            0.0f,
            1.0f);
        DrawGamepadMouseOverlayRect(
            glScissor,
            glClearColor,
            glClear,
            framebufferWidth,
            framebufferHeight,
            x - line - 1,
            y + gap,
            (line * 2) + 2,
            radius - gap,
            0.0f,
            0.0f,
            0.0f,
            1.0f);
        DrawGamepadMouseOverlayRect(
            glScissor,
            glClearColor,
            glClear,
            framebufferWidth,
            framebufferHeight,
            x - radius,
            y - line - 1,
            radius - gap,
            (line * 2) + 2,
            0.0f,
            0.0f,
            0.0f,
            1.0f);
        DrawGamepadMouseOverlayRect(
            glScissor,
            glClearColor,
            glClear,
            framebufferWidth,
            framebufferHeight,
            x + gap,
            y - line - 1,
            radius - gap,
            (line * 2) + 2,
            0.0f,
            0.0f,
            0.0f,
            1.0f);
        DrawGamepadMouseOverlayRect(
            glScissor,
            glClearColor,
            glClear,
            framebufferWidth,
            framebufferHeight,
            x - line,
            y - radius,
            line,
            radius - gap,
            1.0f,
            1.0f,
            1.0f,
            1.0f);
        DrawGamepadMouseOverlayRect(
            glScissor,
            glClearColor,
            glClear,
            framebufferWidth,
            framebufferHeight,
            x - line,
            y + gap,
            line,
            radius - gap,
            1.0f,
            1.0f,
            1.0f,
            1.0f);
        DrawGamepadMouseOverlayRect(
            glScissor,
            glClearColor,
            glClear,
            framebufferWidth,
            framebufferHeight,
            x - radius,
            y - line,
            radius - gap,
            line,
            1.0f,
            1.0f,
            1.0f,
            1.0f);
        DrawGamepadMouseOverlayRect(
            glScissor,
            glClearColor,
            glClear,
            framebufferWidth,
            framebufferHeight,
            x + gap,
            y - line,
            radius - gap,
            line,
            1.0f,
            1.0f,
            1.0f,
            1.0f);

        if (!scissorWasEnabled) {
            glDisable(XGLFW_GL_SCISSOR_TEST);
        }
        glScissor(previousScissor[0], previousScissor[1], previousScissor[2], previousScissor[3]);
        glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
        glColorMask(previousColorMask[0], previousColorMask[1], previousColorMask[2], previousColorMask[3]);
        if (glBindFramebuffer) {
            glBindFramebuffer(XGLFW_GL_DRAW_FRAMEBUFFER, static_cast<XglfwGLuint>(previousFramebuffer));
        }
    }
}

XGLFW_API void glfwSwapBuffers(GLFWwindow*) {
    const bool frameTimingEnabled = IsFrameTimingEnabled();
    const double frameStartMs = frameTimingEnabled ? FrameTimingNowMs() : 0.0;
    const double drawCpuMs = frameTimingEnabled ? BeginGlfwFrameTiming(frameStartMs) : 0.0;
    double pumpBeforeMs = 0.0;
    double readbackMs = 0.0;
    double makeCurrentMs = 0.0;
    double swapMs = 0.0;
    double pumpAfterMs = 0.0;
    bool swapAttempted = false;
    bool swapOk = false;
    const char* timingPath = IsMesaEglMode() ? "egl" : "wgl";

    if (IsMesaEglMode()) {
        if (IsSwapDispatcherPumpEnabled()) {
            double beginMs = frameTimingEnabled ? FrameTimingNowMs() : 0.0;
            PumpCoreWindowEventsForCurrentThread("glfwSwapBuffers-before");
            if (frameTimingEnabled) {
                pumpBeforeMs += FrameTimingNowMs() - beginMs;
            }
        }
        if (IsMesaEglReadbackEnabled()) {
            double beginMs = frameTimingEnabled ? FrameTimingNowMs() : 0.0;
            PublishMesaEglFramebuffer();
            if (frameTimingEnabled) {
                readbackMs += FrameTimingNowMs() - beginMs;
            }
        }
        bool madeCurrent = false;
        {
            double beginMs = frameTimingEnabled ? FrameTimingNowMs() : 0.0;
            madeCurrent = EnsureMesaEglCurrentForPresent();
            if (frameTimingEnabled) {
                makeCurrentMs += FrameTimingNowMs() - beginMs;
            }
        }
        if (madeCurrent &&
            g_eglSwapBuffers &&
            g_eglDisplay != EGL_NO_DISPLAY &&
            g_eglSurface != EGL_NO_SURFACE) {
            EGLBoolean swapped = EGL_FALSE_VALUE;
            bool swapException = false;
            swapAttempted = true;
            double beginMs = frameTimingEnabled ? FrameTimingNowMs() : 0.0;
            __try {
                DrawGamepadMouseCursorOverlay();
                swapped = g_eglSwapBuffers(g_eglDisplay, g_eglSurface);
            }
            __except (LogEglRecoverableException("eglSwapBuffers", GetExceptionCode())) {
                swapped = EGL_FALSE_VALUE;
                swapException = true;
            }
            if (frameTimingEnabled) {
                swapMs += FrameTimingNowMs() - beginMs;
            }
            swapOk = swapped != EGL_FALSE_VALUE;

            static std::atomic<int> swapLogCount{ 0 };
            int count = swapLogCount.fetch_add(1, std::memory_order_relaxed);
            if (count < 8 || (count % 300) == 0 || !swapped) {
                char line[256] = {};
                std::snprintf(
                    line,
                    sizeof(line),
                    "eglSwapBuffers result=%d count=%d err=0x%04x display=%p surface=%p path=%s",
                    swapped ? 1 : 0,
                    count + 1,
                    EglErrorCode(),
                    g_eglDisplay,
                    g_eglSurface,
                    g_eglSurfacePath ? g_eglSurfacePath : "<null>");
                DebugLine(line);
            }
            if (swapException) {
                bool recovered = RecoverMesaEglSurfaceAfterSwapFailure();
                char recoveryLine[128] = {};
                std::snprintf(
                    recoveryLine,
                    sizeof(recoveryLine),
                    "eglSwapBuffers exception recovery result=%d",
                    recovered ? 1 : 0);
                DebugLine(recoveryLine);
            }
        }
    }
    else if (!IsFakeContextMode() && EnsureWglContext() && g_wglSwapBuffers) {
        swapAttempted = true;
        double beginMs = frameTimingEnabled ? FrameTimingNowMs() : 0.0;
        BOOL swapped = FALSE;
        __try {
            swapped = g_wglSwapBuffers(g_wglDc);
        }
        __except (LogNativeException("wglSwapBuffers", GetExceptionCode())) {
            swapped = FALSE;
        }
        if (frameTimingEnabled) {
            swapMs += FrameTimingNowMs() - beginMs;
        }
        swapOk = swapped != FALSE;
    }
    if (IsSwapDispatcherPumpEnabled()) {
        double beginMs = frameTimingEnabled ? FrameTimingNowMs() : 0.0;
        PumpCoreWindowEventsForCurrentThread("glfwSwapBuffers");
        if (frameTimingEnabled) {
            pumpAfterMs += FrameTimingNowMs() - beginMs;
        }
    }
    SignalPresentEvent();
    if (frameTimingEnabled) {
        LogGlfwFrameTiming(
            FrameTimingNowMs(),
            drawCpuMs,
            pumpBeforeMs,
            readbackMs,
            makeCurrentMs,
            swapMs,
            pumpAfterMs,
            swapAttempted,
            swapOk,
            timingPath);
    }
}
XGLFW_API void glfwSwapInterval(int interval) {
    interval = interval > 0 ? 1 : 0;
    g_requestedSwapInterval.store(interval, std::memory_order_release);
    char line[128] = {};
    std::snprintf(line, sizeof(line), "glfwSwapInterval requested=%d", interval);
    DebugLine(line);
    if (IsMesaEglMode()) {
        ApplyMesaEglSwapInterval("glfwSwapInterval");
    }
}
XGLFW_API int  glfwExtensionSupported(const char*) { return GLFW_TRUE; }
XGLFW_API GLFWglproc glfwGetProcAddress(const char* name) {
    if (!name) {
        return nullptr;
    }

    if (IsFakeContextMode()) {
        HMODULE module = GetModuleHandleW(L"xbox-opengl.dll");
        if (!module) {
            module = GetModuleHandleW(L"opengl32.dll");
        }

        PROC proc = module ? GetProcAddress(module, name) : nullptr;
        static std::atomic<int> fakeLogCount{ 0 };
        int count = fakeLogCount.fetch_add(1, std::memory_order_relaxed);
        if (count < 48) {
            char line[256] = {};
            std::snprintf(line, sizeof(line), "glfwGetProcAddress fake %s -> %p", name, proc);
            DebugLine(line);
        }
        return reinterpret_cast<GLFWglproc>(proc);
    }

    if (IsMesaEglMode()) {
        EnsureMesaEglContext();
        PROC proc = nullptr;
        if (g_eglGetProcAddress) {
            __try {
                proc = reinterpret_cast<PROC>(g_eglGetProcAddress(name));
            }
            __except (LogEglException("eglGetProcAddress", GetExceptionCode())) {
                proc = nullptr;
            }
        }
        if (!proc && g_openGlModule) {
            proc = GetProcAddress(g_openGlModule, name);
        }
        if (!proc && g_glesModule) {
            proc = GetProcAddress(g_glesModule, name);
        }
        static std::atomic<int> eglLogCount{ 0 };
        int count = eglLogCount.fetch_add(1, std::memory_order_relaxed);
        if (count < 64) {
            char line[256] = {};
            std::snprintf(line, sizeof(line), "glfwGetProcAddress egl %s -> %p", name, proc);
            DebugLine(line);
        }
        proc = WrapGlProcForFrameTiming(name, proc);
        return reinterpret_cast<GLFWglproc>(proc);
    }

    if (!EnsureOpenGlModule()) {
        return nullptr;
    }

    PROC proc = nullptr;
    if (g_wglGetProcAddress) {
        __try {
            proc = g_wglGetProcAddress(name);
        }
        __except (LogNativeException("wglGetProcAddress", GetExceptionCode())) {
            proc = nullptr;
        }
    }
    if (!proc && g_openGlModule) {
        proc = GetProcAddress(g_openGlModule, name);
    }
    static std::atomic<int> logCount{ 0 };
    int count = logCount.fetch_add(1, std::memory_order_relaxed);
    if (count < 48) {
        char line[256] = {};
        std::snprintf(line, sizeof(line), "glfwGetProcAddress %s -> %p", name, proc);
        DebugLine(line);
    }
    proc = WrapGlProcForFrameTiming(name, proc);
    return reinterpret_cast<GLFWglproc>(proc);
}

// -----------------------------------------------------------------------------
// Vulkan
// -----------------------------------------------------------------------------

XGLFW_API int glfwVulkanSupported(void) { return GLFW_FALSE; }
XGLFW_API const char** glfwGetRequiredInstanceExtensions(uint32_t* count) {
    if (count) *count = 0;
    return nullptr;
}
XGLFW_API GLFWvkproc glfwGetInstanceProcAddress(VkInstance, const char*) { return nullptr; }
XGLFW_API int glfwGetPhysicalDevicePresentationSupport(VkInstance, VkPhysicalDevice, uint32_t) { return GLFW_FALSE; }
XGLFW_API VkResult glfwCreateWindowSurface(VkInstance, GLFWwindow*, const VkAllocationCallbacks*, VkSurfaceKHR*) {
    return GLFW_VK_ERR_INIT_FAILED;
}

// -----------------------------------------------------------------------------
// Win32 / WGL / EGL / OSMesa native access (no-op on Xbox UWP)
// -----------------------------------------------------------------------------

XGLFW_API const char* glfwGetWin32Adapter(GLFWmonitor*) { return nullptr; }
XGLFW_API const char* glfwGetWin32Monitor(GLFWmonitor*) { return nullptr; }
XGLFW_API HWND        glfwGetWin32Window(GLFWwindow*) { return nullptr; }
XGLFW_API GLFWwindow* glfwAttachWin32Window(HWND, GLFWwindow*) { return nullptr; }
XGLFW_API void*       glfwGetWGLContext(GLFWwindow*) {
    if (IsFakeContextMode()) {
        return g_contextCurrent.load(std::memory_order_acquire) ? reinterpret_cast<void*>(0x78787878000000F1ull) : nullptr;
    }
    if (IsMesaEglMode()) {
        return nullptr;
    }
    return g_wglContext;
}
XGLFW_API void*       glfwGetEGLDisplay(void) {
    return IsMesaEglMode() && EnsureMesaEglContext() ? g_eglDisplay : nullptr;
}
XGLFW_API void*       glfwGetEGLContext(GLFWwindow*) {
    return IsMesaEglMode() && EnsureMesaEglContext() ? g_eglContext : nullptr;
}
XGLFW_API void*       glfwGetEGLSurface(GLFWwindow*) {
    return IsMesaEglMode() && EnsureMesaEglContext() ? g_eglSurface : nullptr;
}
XGLFW_API void*       glfwGetEGLConfig(GLFWwindow*) {
    return IsMesaEglMode() && EnsureMesaEglContext() ? g_eglConfig : nullptr;
}
XGLFW_API int         glfwGetOSMesaColorBuffer(GLFWwindow*, int*, int*, int*, void**) { return GLFW_FALSE; }
XGLFW_API int         glfwGetOSMesaDepthBuffer(GLFWwindow*, int*, int*, int*, void**) { return GLFW_FALSE; }
XGLFW_API void*       glfwGetOSMesaContext(GLFWwindow*) { return nullptr; }

// -----------------------------------------------------------------------------
// Internal data exports (HMODULEs in the real glfw.dll; null here)
// -----------------------------------------------------------------------------

XGLFW_API HMODULE _glfw_egl_library      = nullptr;
XGLFW_API HMODULE _glfw_mesa_library     = nullptr;
XGLFW_API HMODULE _glfw_opengl_library   = nullptr;
XGLFW_API HMODULE _glfw_opengles_library = nullptr;
XGLFW_API HMODULE _glfw_vulkan_library   = nullptr;

} // extern "C"
