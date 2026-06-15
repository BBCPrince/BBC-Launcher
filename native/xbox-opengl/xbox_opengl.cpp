// xbox_opengl.cpp
//
// Stub replacement for opengl32.dll on Xbox UWP.
//
// Why this exists:
//   Xbox UWP does not ship opengl32.dll. Mojang via LWJGL hits this in
//   org.lwjgl.opengl.GL.create() which calls Library.loadNative("opengl32").
//   That fails with:
//
//     UnsatisfiedLinkError: Failed to locate library: opengl32.dll
//
//   LWJGL honors -Dorg.lwjgl.opengl.libname=<absolute path>, so we point it
//   at this DLL instead.
//
// Goals (milestone 1, same as xbox-glfw):
//   * Export all 368 symbols that the real opengl32.dll exports so every
//     GetProcAddress lookup succeeds.  340 of those alias to universal_no_op
//     via xbox_opengl.def.
//   * The bespoke entrypoints implemented here (glGetString, glGetIntegerv,
//     glGetError, the wgl* family, etc.) return values LWJGL & Mojang accept
//     so RenderSystem.initRenderer can keep moving.
//   * wglGetProcAddress recognises a curated set of GL 2.0+ symbols Mojang
//     actually depends on and hands back smarter stubs (glGenBuffers writes
//     a non-zero ID, glGetShaderiv reports successful compilation, etc.).
//     Unknown names fall through to universal_no_op.
//
// Out of scope:
//   * No real textured rendering yet.  The first translation slice publishes
//     simple GL command state (clear, viewport, draw counters) to the launcher
//     process so the D3D12 presentation bridge can consume it.

#include "../xbox-gl-command-state.h"

#include <windows.h>
#include <Unknwn.h>
#include <roapi.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.foundation.h>
#include <windows.system.h>
#include <windows.ui.core.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <atomic>
#include <cctype>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::System;
using namespace ABI::Windows::UI::Core;

extern "C" {

// -----------------------------------------------------------------------------
// Universal no-op (used by xbox_opengl.def for ~340 of the 368 exports).
//
// All x64 Microsoft ABI callers pass up to 4 integer/pointer args in RCX, RDX,
// R8, R9, and remaining args on the stack.  Return integer/pointer in RAX,
// float/double in XMM0.  A `0`-returning int function clears RAX; XMM0 is left
// undefined by the compiler, which is acceptable because Mojang/LWJGL almost
// never calls a GL function that returns float-by-value (they go through out
// parameters).  glGet*v is bespoke for exactly that reason.
// -----------------------------------------------------------------------------

__declspec(noinline) int64_t universal_no_op(void) {
    return 0;
}

// -----------------------------------------------------------------------------
// OpenGL constants we have to recognise
// -----------------------------------------------------------------------------

typedef unsigned int  GLenum;
typedef unsigned char GLboolean;
typedef signed char   GLbyte;
typedef short         GLshort;
typedef int           GLint;
typedef unsigned int  GLuint;
typedef int           GLsizei;
typedef float         GLfloat;
typedef double        GLdouble;
typedef int64_t       GLint64;
typedef uint64_t      GLuint64;
typedef ptrdiff_t     GLintptr;
typedef ptrdiff_t     GLsizeiptr;
typedef unsigned int  GLbitfield;
typedef unsigned char GLubyte;
typedef int           GLclampx;

#define GL_FALSE  0
#define GL_TRUE   1
#define GL_NO_ERROR 0

#define GL_VENDOR                     0x1F00
#define GL_RENDERER                   0x1F01
#define GL_VERSION                    0x1F02
#define GL_EXTENSIONS                 0x1F03
#define GL_SHADING_LANGUAGE_VERSION   0x8B8C
#define GL_NUM_EXTENSIONS             0x821D
#define GL_MAJOR_VERSION              0x821B
#define GL_MINOR_VERSION              0x821C
#define GL_CONTEXT_FLAGS              0x821E
#define GL_CONTEXT_PROFILE_MASK       0x9126
#define GL_CONTEXT_CORE_PROFILE_BIT   0x00000001
#define GL_MAX_TEXTURE_SIZE           0x0D33
#define GL_MAX_VIEWPORT_DIMS          0x0D3A
#define GL_MAX_TEXTURE_IMAGE_UNITS    0x8872
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#define GL_MAX_VERTEX_ATTRIBS         0x8869
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS 0x8B4A
#define GL_MAX_FRAGMENT_UNIFORM_COMPONENTS 0x8B49
#define GL_MAX_VARYING_FLOATS         0x8B4B
#define GL_MAX_UNIFORM_BUFFER_BINDINGS 0x8A2F
#define GL_MAX_UNIFORM_BLOCK_SIZE     0x8A30
#define GL_MAX_DRAW_BUFFERS           0x8824
#define GL_MAX_COLOR_ATTACHMENTS      0x8CDF
#define GL_MAX_SAMPLES                0x8D57
#define GL_MAX_RENDERBUFFER_SIZE      0x84E8
#define GL_MAX_ARRAY_TEXTURE_LAYERS   0x88FF
#define GL_MAX_3D_TEXTURE_SIZE        0x8073
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE  0x851C
#define GL_MAX_RECTANGLE_TEXTURE_SIZE 0x84F8
#define GL_MAX_TEXTURE_BUFFER_SIZE    0x8C2B

// Buffer alignment queries - critical: Mojang's UBO allocator floor-divs by
// these and crashes if we return 0.  Real GPUs typically report 16, 64, or 256.
#define GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT          0x8A34
#define GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT   0x90DF
#define GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT          0x919F
#define GL_MIN_MAP_BUFFER_ALIGNMENT                 0x90BC

// Pixel store
#define GL_UNPACK_SWAP_BYTES        0x0CF0
#define GL_UNPACK_LSB_FIRST         0x0CF1
#define GL_UNPACK_ROW_LENGTH        0x0CF2
#define GL_UNPACK_SKIP_ROWS         0x0CF3
#define GL_UNPACK_SKIP_PIXELS       0x0CF4
#define GL_UNPACK_ALIGNMENT         0x0CF5

// Uniform blocks
#define GL_MAX_VERTEX_UNIFORM_BLOCKS                0x8A2B
#define GL_MAX_GEOMETRY_UNIFORM_BLOCKS              0x8A2C
#define GL_MAX_FRAGMENT_UNIFORM_BLOCKS              0x8A2D
#define GL_MAX_COMBINED_UNIFORM_BLOCKS              0x8A2E
#define GL_MAX_TESS_CONTROL_UNIFORM_BLOCKS          0x8E89
#define GL_MAX_TESS_EVALUATION_UNIFORM_BLOCKS       0x8E8A
#define GL_MAX_COMPUTE_UNIFORM_BLOCKS               0x91BB

// SSBOs
#define GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS       0x90DD
#define GL_MAX_SHADER_STORAGE_BLOCK_SIZE            0x90DE
#define GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS       0x90DC

// Image units beyond fragment
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS           0x8B4C
#define GL_MAX_GEOMETRY_TEXTURE_IMAGE_UNITS         0x8C29
#define GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS          0x91BC
#define GL_MAX_TESS_CONTROL_TEXTURE_IMAGE_UNITS     0x8E81
#define GL_MAX_TESS_EVALUATION_TEXTURE_IMAGE_UNITS  0x8E82

// Vertex attributes
#define GL_MAX_VERTEX_ATTRIB_BINDINGS               0x82DA
#define GL_MAX_VERTEX_ATTRIB_STRIDE                 0x82E5
#define GL_MAX_VERTEX_ATTRIB_RELATIVE_OFFSET        0x82D9
#define GL_MAX_VERTEX_OUTPUT_COMPONENTS             0x9122
#define GL_MAX_FRAGMENT_INPUT_COMPONENTS            0x9125
#define GL_MAX_GEOMETRY_INPUT_COMPONENTS            0x9123
#define GL_MAX_GEOMETRY_OUTPUT_COMPONENTS           0x9124

// Framebuffer max
#define GL_MAX_FRAMEBUFFER_WIDTH                    0x9315
#define GL_MAX_FRAMEBUFFER_HEIGHT                   0x9316
#define GL_MAX_FRAMEBUFFER_LAYERS                   0x9317
#define GL_MAX_FRAMEBUFFER_SAMPLES                  0x9318

// Element limits (used by indexed draw range calcs)
#define GL_MAX_ELEMENTS_VERTICES                    0x80E8
#define GL_MAX_ELEMENTS_INDICES                     0x80E9

// Format / binary count
#define GL_NUM_PROGRAM_BINARY_FORMATS               0x87FE
#define GL_NUM_SHADER_BINARY_FORMATS                0x8DF9
#define GL_NUM_COMPRESSED_TEXTURE_FORMATS           0x86A2

// Compute work group
#define GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS       0x90EB

#define GL_COMPILE_STATUS             0x8B81
#define GL_LINK_STATUS                0x8B82
#define GL_VALIDATE_STATUS            0x8B83
#define GL_INFO_LOG_LENGTH            0x8B84
#define GL_ATTACHED_SHADERS           0x8B85
#define GL_ACTIVE_UNIFORMS            0x8B86
#define GL_ACTIVE_ATTRIBUTES          0x8B89

#define GL_FRAMEBUFFER_COMPLETE       0x8CD5
#define GL_FRAMEBUFFER                0x8D40
#define GL_READ_FRAMEBUFFER           0x8CA8
#define GL_DRAW_FRAMEBUFFER           0x8CA9
#define GL_COLOR_ATTACHMENT0          0x8CE0
#define GL_COLOR_ATTACHMENT15         0x8CEF
#define GL_DEPTH_ATTACHMENT           0x8D00
#define GL_STENCIL_ATTACHMENT         0x8D20
#define GL_COLOR                      0x1800
#define GL_DEPTH                      0x1801
#define GL_STENCIL                    0x1802
#define GL_COLOR_BUFFER_BIT           0x00004000
#define GL_TRIANGLES                  0x0004
#define GL_TRIANGLE_STRIP             0x0005
#define GL_TRIANGLE_FAN               0x0006

#define GL_TEXTURE0                   0x84C0
#define GL_TEXTURE_1D                 0x0DE0
#define GL_TEXTURE_2D                 0x0DE1
#define GL_TEXTURE_3D                 0x806F
#define GL_TEXTURE_CUBE_MAP           0x8513
#define GL_TEXTURE_CUBE_MAP_SEAMLESS  0x884F
#define GL_TEXTURE_2D_ARRAY           0x8C1A
#define GL_RED                        0x1903
#define GL_RG                         0x8227
#define GL_RGB                        0x1907
#define GL_RGBA                       0x1908
#define GL_BGRA                       0x80E1
#define GL_DEPTH_COMPONENT            0x1902
#define GL_DEPTH_STENCIL              0x84F9
#define GL_UNSIGNED_BYTE              0x1401
#define GL_BYTE                       0x1400
#define GL_UNSIGNED_SHORT             0x1403
#define GL_SHORT                      0x1402
#define GL_UNSIGNED_INT               0x1405
#define GL_INT                        0x1404
#define GL_FLOAT                      0x1406
#define GL_UNSIGNED_SHORT_5_6_5       0x8363
#define GL_UNSIGNED_SHORT_4_4_4_4     0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1     0x8034
#define GL_UNSIGNED_INT_24_8          0x84FA

#define GL_ARRAY_BUFFER               0x8892
#define GL_ELEMENT_ARRAY_BUFFER       0x8893
#define GL_PIXEL_UNPACK_BUFFER        0x88EC
#define GL_UNIFORM_BUFFER             0x8A11
#define GL_SHADER_STORAGE_BUFFER      0x90D2
#define GL_BUFFER_SIZE                0x8764
#define GL_BUFFER_MAPPED              0x88BC
#define GL_BUFFER_MAP_POINTER         0x88BD
#define GL_BUFFER_MAP_LENGTH          0x9120
#define GL_BUFFER_MAP_OFFSET          0x9121
#define GL_PROGRAM_POINT_SIZE         0x8642

// -----------------------------------------------------------------------------
// Strings returned by glGetString
// -----------------------------------------------------------------------------

static const char* kVendor      = "Xbox";
static const char* kRenderer    = "xbox-opengl command bridge";
static const char* kVersion     = "4.6.0 Xbox-command-bridge-1.0.60";
static const char* kGlslVersion = "4.60";
static const char* kExtensions  = "";

static HANDLE g_presentEvent = nullptr;
static std::atomic<uint64_t> g_presentSignalCount{ 0 };
static HANDLE g_glCommandStateMapping = nullptr;
static MinecraftXboxGlCommandState* g_glCommandState = nullptr;
static float g_clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
static GLuint g_activeTextureUnit = 0;
static GLuint g_lastSamplerTextureUnit = 0;
static GLuint g_boundTexture2D[32] = {};
static GLuint g_boundTexture2DArray[32] = {};
static GLuint g_boundArrayBuffer = 0;
static GLuint g_boundElementArrayBuffer = 0;
static GLuint g_boundPixelUnpackBuffer = 0;
static GLuint g_boundUniformBuffer = 0;
static GLuint g_boundShaderStorageBuffer = 0;
static GLuint g_boundVertexArray = 0;
static GLuint g_currentProgram = 0;
static GLuint g_boundDrawFramebuffer = 0;
static GLuint g_boundReadFramebuffer = 0;
static GLint g_viewport[4] = { 0, 0, 1920, 1080 };

// Mesa-UWP compatibility mode:
// LWJGL still loads a Windows OpenGL library and asks WGL-style questions even
// when xbox-glfw created the real context through EGL.  In this mode this DLL is
// only a thin compatibility layer: WGL current-context queries return a stable
// sentinel and GL entry-point lookup prefers Mesa/EGL before falling back to the
// older command-bridge stubs.
typedef void* (WINAPI* EglGetProcAddressProc)(const char*);
typedef void* (WINAPI* EglGetCurrentDisplayProc)();
typedef void* (WINAPI* EglGetCurrentSurfaceProc)(int);
typedef unsigned int (WINAPI* EglSwapBuffersProc)(void*, void*);
typedef unsigned int (WINAPI* EglGetErrorProc)();
typedef PROC (WINAPI* WglGetProcAddressProc)(LPCSTR);
static constexpr int EGL_DRAW_VALUE = 0x3059;

static HMODULE g_realOpenGlModule = nullptr;
static HMODULE g_realEglModule = nullptr;
static HMODULE g_realGlesModule = nullptr;
static HMODULE g_realGles1Module = nullptr;
static EglGetProcAddressProc g_realEglGetProcAddress = nullptr;
static EglGetCurrentDisplayProc g_realEglGetCurrentDisplay = nullptr;
static EglGetCurrentSurfaceProc g_realEglGetCurrentSurface = nullptr;
static EglSwapBuffersProc g_realEglSwapBuffers = nullptr;
static EglGetErrorProc g_realEglGetError = nullptr;
static WglGetProcAddressProc g_realWglGetProcAddress = nullptr;
static std::atomic<int> g_realMesaInit{ 0 };
static std::atomic<int> g_realGlStringUnavailable{ 0 };
static std::atomic<int> g_forwardLogCount{ 0 };
static std::atomic<int> g_fallbackLogCount{ 0 };
static std::atomic<int> g_resolveProbeLogCount{ 0 };
static std::atomic<int> g_swapLogCount{ 0 };
static std::atomic<int> g_glCallProbeLogCount{ 0 };

static void DebugLine(const char* message) {
    if (!message || !message[0]) {
        return;
    }

    char line[1200] = {};
    std::snprintf(line, sizeof(line), "[Native][xbox-opengl] %s\r\n", message);
    OutputDebugStringA(line);

    wchar_t logPath[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(
        L"MINECRAFT_XBOX_NATIVE_LOG",
        logPath,
        static_cast<DWORD>(sizeof(logPath) / sizeof(logPath[0])));
    if (len == 0 || len >= static_cast<DWORD>(sizeof(logPath) / sizeof(logPath[0]))) {
        return;
    }

    HANDLE file = CreateFileW(
        logPath,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
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

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        InstallNativeFatalExceptionLogger();
    }
    return TRUE;
}

static const wchar_t* FileNamePart(const wchar_t* path) {
    if (!path) {
        return L"";
    }

    const wchar_t* slash = std::wcsrchr(path, L'\\');
    const wchar_t* altSlash = std::wcsrchr(path, L'/');
    const wchar_t* last = slash && altSlash
        ? (slash > altSlash ? slash : altSlash)
        : (slash ? slash : altSlash);
    return last ? last + 1 : path;
}

static bool GetRealOpenGlPath(wchar_t* path, DWORD pathChars) {
    if (!path || pathChars == 0) {
        return false;
    }

    DWORD len = GetEnvironmentVariableW(L"MINECRAFT_XBOX_REAL_OPENGL_DLL", path, pathChars);
    if (len == 0 || len >= pathChars) {
        len = GetEnvironmentVariableW(L"MINECRAFT_XBOX_OPENGL_DLL", path, pathChars);
    }

    if (len == 0 || len >= pathChars) {
        return false;
    }

    const wchar_t* fileName = FileNamePart(path);
    if (_wcsicmp(fileName, L"xbox-opengl.dll") == 0) {
        return false;
    }

    return true;
}

static bool CopyDirectoryName(const wchar_t* path, wchar_t* directory, size_t directoryChars) {
    if (!path || !directory || directoryChars == 0) {
        return false;
    }

    wcsncpy_s(directory, directoryChars, path, _TRUNCATE);
    wchar_t* slash = std::wcsrchr(directory, L'\\');
    wchar_t* altSlash = std::wcsrchr(directory, L'/');
    wchar_t* last = slash && altSlash
        ? (slash > altSlash ? slash : altSlash)
        : (slash ? slash : altSlash);
    if (!last) {
        directory[0] = 0;
        return false;
    }

    *last = 0;
    return directory[0] != 0;
}

static HMODULE LoadSiblingDll(const wchar_t* providerPath, const wchar_t* dllName, bool required) {
    wchar_t directory[MAX_PATH] = {};
    wchar_t fullPath[MAX_PATH] = {};
    HMODULE module = nullptr;

    if (CopyDirectoryName(providerPath, directory, sizeof(directory) / sizeof(directory[0]))) {
        _snwprintf_s(fullPath, _TRUNCATE, L"%ls\\%ls", directory, dllName);
        module = LoadLibraryW(fullPath);
    }

    if (!module) {
        module = LoadLibraryW(dllName);
    }

    if (!module && required) {
        char line[384] = {};
        std::snprintf(line, sizeof(line), "Failed to load Mesa sibling DLL %ls (GetLastError=%lu)", dllName, GetLastError());
        DebugLine(line);
    }

    return module;
}

static bool IsEglProcOnlyMode() {
    static std::atomic<int> cached{ -1 };
    int value = cached.load(std::memory_order_acquire);
    if (value >= 0) {
        return value != 0;
    }

    wchar_t enabled[16] = {};
    DWORD len = GetEnvironmentVariableW(
        L"MINECRAFT_XBOX_OPENGL_EGL_PROC_ONLY",
        enabled,
        static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
    const bool isEnabled =
        len > 0 &&
        enabled[0] != L'0' &&
        _wcsicmp(enabled, L"false") != 0 &&
        _wcsicmp(enabled, L"no") != 0;
    cached.store(isEnabled ? 1 : 0, std::memory_order_release);

    if (isEnabled) {
        static std::atomic<int> logCount{ 0 };
        if (logCount.fetch_add(1, std::memory_order_relaxed) == 0) {
            DebugLine("Mesa EGL proc-only mode enabled; WGL proc resolution disabled");
        }
    }

    return isEnabled;
}

static void EnsureRealMesaLoaded() {
    int state = g_realMesaInit.load(std::memory_order_acquire);
    if (state == 2 || state == 3) {
        return;
    }

    int expected = 0;
    if (!g_realMesaInit.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        while (g_realMesaInit.load(std::memory_order_acquire) == 1) {
            Sleep(1);
        }
        return;
    }

    wchar_t openGlPath[MAX_PATH] = {};
    if (!GetRealOpenGlPath(openGlPath, static_cast<DWORD>(sizeof(openGlPath) / sizeof(openGlPath[0])))) {
        g_realMesaInit.store(3, std::memory_order_release);
        return;
    }

    const bool eglProcOnly = IsEglProcOnlyMode();

    LoadSiblingDll(openGlPath, L"xbox_fmalloc.dll", false);
    LoadSiblingDll(openGlPath, L"libglapi.dll", false);
    if (!eglProcOnly) {
        LoadSiblingDll(openGlPath, L"libgallium_wgl.dll", false);
    }
    g_realEglModule = LoadSiblingDll(openGlPath, L"libEGL.dll", false);
    g_realGlesModule = LoadSiblingDll(openGlPath, L"libGLESv2.dll", false);
    g_realGles1Module = LoadSiblingDll(openGlPath, L"libGLESv1_CM.dll", false);
    if (eglProcOnly) {
        __try {
            g_realOpenGlModule = LoadLibraryW(openGlPath);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            g_realOpenGlModule = nullptr;
        }
        if (g_realOpenGlModule) {
            DebugLine("Mesa OpenGL core exports loaded for EGL proc-only forwarding");
        }
        else {
            char line[512] = {};
            std::snprintf(line, sizeof(line), "Mesa OpenGL core exports unavailable in EGL proc-only mode %ls (GetLastError=%lu)", openGlPath, GetLastError());
            DebugLine(line);
        }
    }
    if (!eglProcOnly) {
        g_realOpenGlModule = LoadLibraryW(openGlPath);
        if (!g_realOpenGlModule) {
            char line[512] = {};
            std::snprintf(line, sizeof(line), "Failed to load real Mesa OpenGL provider %ls (GetLastError=%lu)", openGlPath, GetLastError());
            DebugLine(line);
            g_realMesaInit.store(3, std::memory_order_release);
            return;
        }
        g_realWglGetProcAddress = reinterpret_cast<WglGetProcAddressProc>(
            GetProcAddress(g_realOpenGlModule, "wglGetProcAddress"));
    }

    if (g_realEglModule) {
        g_realEglGetProcAddress = reinterpret_cast<EglGetProcAddressProc>(
            GetProcAddress(g_realEglModule, "eglGetProcAddress"));
        g_realEglGetCurrentDisplay = reinterpret_cast<EglGetCurrentDisplayProc>(
            GetProcAddress(g_realEglModule, "eglGetCurrentDisplay"));
        g_realEglGetCurrentSurface = reinterpret_cast<EglGetCurrentSurfaceProc>(
            GetProcAddress(g_realEglModule, "eglGetCurrentSurface"));
        g_realEglSwapBuffers = reinterpret_cast<EglSwapBuffersProc>(
            GetProcAddress(g_realEglModule, "eglSwapBuffers"));
        g_realEglGetError = reinterpret_cast<EglGetErrorProc>(
            GetProcAddress(g_realEglModule, "eglGetError"));
    }

    char line[512] = {};
    std::snprintf(
        line,
        sizeof(line),
        "Mesa forwarder ready opengl=%p egl=%p gles2=%p gles1=%p eglGetProcAddress=%p eglProcOnly=%d",
        g_realOpenGlModule,
        g_realEglModule,
        g_realGlesModule,
        g_realGles1Module,
        reinterpret_cast<void*>(g_realEglGetProcAddress),
        eglProcOnly ? 1 : 0);
    DebugLine(line);
    g_realMesaInit.store(2, std::memory_order_release);
}

static bool IsRealMesaForwardingEnabled() {
    static std::atomic<int> cached{ -1 };
    int value = cached.load(std::memory_order_acquire);
    if (value >= 0) {
        return value != 0;
    }

    wchar_t enabled[16] = {};
    DWORD len = GetEnvironmentVariableW(
        L"MINECRAFT_XBOX_OPENGL_FORWARD_MESA",
        enabled,
        static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
    const bool isEnabled =
        len > 0 &&
        enabled[0] != L'0' &&
        _wcsicmp(enabled, L"false") != 0 &&
        _wcsicmp(enabled, L"no") != 0;
    cached.store(isEnabled ? 1 : 0, std::memory_order_release);

    if (!isEnabled) {
        static std::atomic<int> logCount{ 0 };
        if (logCount.fetch_add(1, std::memory_order_relaxed) == 0) {
            DebugLine("Mesa GL forwarding disabled; using command bridge shims");
        }
    }

    return isEnabled;
}

static int QueryIntEnvironment(const wchar_t* name, int fallback) {
    wchar_t buffer[32] = {};
    DWORD len = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
    if (len == 0 || len >= static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]))) {
        return fallback;
    }

    int value = _wtoi(buffer);
    return value > 0 ? value : fallback;
}

static bool IsTruthyEnvironment(const wchar_t* name) {
    wchar_t buffer[16] = {};
    DWORD len = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
    return len > 0 &&
        len < static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])) &&
        buffer[0] != L'\0' &&
        buffer[0] != L'0' &&
        _wcsicmp(buffer, L"false") != 0 &&
        _wcsicmp(buffer, L"no") != 0;
}

static bool IsFrameTimingEnabled() {
    static std::atomic<int> cached{ -1 };
    int value = cached.load(std::memory_order_acquire);
    if (value >= 0) {
        return value != 0;
    }

    bool isEnabled = IsTruthyEnvironment(L"MINECRAFT_XBOX_FRAME_TIMING");
    cached.store(isEnabled ? 1 : 0, std::memory_order_release);
    if (isEnabled) {
        DebugLine("FRAME-TIMING enabled for xbox-opengl GL call counters");
    }
    return isEnabled;
}

static bool IsVerboseGlLoggingEnabled() {
    static std::atomic<int> cached{ -1 };
    int value = cached.load(std::memory_order_acquire);
    if (value >= 0) {
        return value != 0;
    }

    bool isEnabled = IsTruthyEnvironment(L"MINECRAFT_XBOX_VERBOSE_GL_LOG");
    cached.store(isEnabled ? 1 : 0, std::memory_order_release);
    if (isEnabled) {
        DebugLine("Verbose GL diagnostic logging enabled");
    }
    return isEnabled;
}

static bool UseHybridGlClientWaitSync() {
    static std::atomic<int> cached{ -1 };
    int value = cached.load(std::memory_order_acquire);
    if (value >= 0) {
        return value != 0;
    }

    bool isEnabled = IsTruthyEnvironment(L"MINECRAFT_XBOX_OPENGL_HYBRID_SYNC");
    cached.store(isEnabled ? 1 : 0, std::memory_order_release);
    if (isEnabled) {
        DebugLine("GL sync hybrid client-wait path enabled; zero-timeout polls bypass Mesa waits");
    }
    return isEnabled;
}

static MinecraftXboxGlCommandState* EnsureGlCommandState();

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

enum GlTimingBucket {
    kGlTimingDraw,
    kGlTimingTextureUpload,
    kGlTimingBufferUpload,
    kGlTimingProgramUse,
    kGlTimingUniform,
    kGlTimingTextureBind,
    kGlTimingFramebuffer,
    kGlTimingSync,
    kGlTimingFenceSync,
    kGlTimingClientWaitSync,
    kGlTimingWaitSync,
    kGlTimingDeleteSync,
    kGlTimingIsSync,
    kGlTimingFlush
};

static double BeginGlCallTiming() {
    return IsFrameTimingEnabled() ? FrameTimingNowMs() : 0.0;
}

static void PublishGlCallTiming(GlTimingBucket bucket, double startMs) {
    if (startMs <= 0.0) {
        return;
    }

    double elapsedMs = FrameTimingNowMs() - startMs;
    if (elapsedMs <= 0.0) {
        return;
    }

    LONG64 elapsedUs = static_cast<LONG64>(elapsedMs * 1000.0 + 0.5);
    if (elapsedUs <= 0) {
        return;
    }

    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    volatile LONG64* target = nullptr;
    bool addToSyncTotal = false;
    switch (bucket) {
        case kGlTimingDraw: target = &state->drawCallTimeUs; break;
        case kGlTimingTextureUpload: target = &state->textureUploadCallTimeUs; break;
        case kGlTimingBufferUpload: target = &state->bufferUploadCallTimeUs; break;
        case kGlTimingProgramUse: target = &state->programUseCallTimeUs; break;
        case kGlTimingUniform: target = &state->uniformCallTimeUs; break;
        case kGlTimingTextureBind: target = &state->textureBindCallTimeUs; break;
        case kGlTimingFramebuffer: target = &state->framebufferCallTimeUs; break;
        case kGlTimingSync: target = &state->syncCallTimeUs; break;
        case kGlTimingFenceSync: target = &state->fenceSyncCallTimeUs; addToSyncTotal = true; break;
        case kGlTimingClientWaitSync: target = &state->clientWaitSyncCallTimeUs; addToSyncTotal = true; break;
        case kGlTimingWaitSync: target = &state->waitSyncCallTimeUs; addToSyncTotal = true; break;
        case kGlTimingDeleteSync: target = &state->deleteSyncCallTimeUs; addToSyncTotal = true; break;
        case kGlTimingIsSync: target = &state->isSyncCallTimeUs; addToSyncTotal = true; break;
        case kGlTimingFlush: target = &state->flushCallTimeUs; break;
        default: break;
    }

    if (target) {
        InterlockedAdd64(target, elapsedUs);
    }
    if (addToSyncTotal) {
        InterlockedAdd64(&state->syncCallTimeUs, elapsedUs);
    }
}


struct Lwjgl2FrameTimingStats {
    unsigned long long frameCount = 0;
    unsigned long long sampleCount = 0;
    double previousSwapEndMs = 0.0;
    double drawCpuTotalMs = 0.0;
    double swapTotalMs = 0.0;
    double frameTotalMs = 0.0;
};

static Lwjgl2FrameTimingStats g_lwjgl2FrameTimingStats;

static double BeginLwjgl2FrameTiming(double frameStartMs) {
    if (g_lwjgl2FrameTimingStats.previousSwapEndMs <= 0.0 ||
        frameStartMs <= g_lwjgl2FrameTimingStats.previousSwapEndMs) {
        return 0.0;
    }
    return frameStartMs - g_lwjgl2FrameTimingStats.previousSwapEndMs;
}

static void LogLwjgl2FrameTiming(
    double frameEndMs,
    double drawCpuMs,
    double swapMs,
    unsigned int result,
    unsigned int err) {
    Lwjgl2FrameTimingStats& stats = g_lwjgl2FrameTimingStats;
    stats.previousSwapEndMs = frameEndMs;
    stats.frameCount++;
    stats.sampleCount++;

    double frameMs = drawCpuMs + swapMs;
    stats.drawCpuTotalMs += drawCpuMs;
    stats.swapTotalMs += swapMs;
    stats.frameTotalMs += frameMs;

    bool slowFrame = frameMs >= 24.0 || swapMs >= 8.0;
    bool shouldLog = stats.frameCount <= 12 || stats.sampleCount >= 60 || slowFrame || result == 0;
    if (!shouldLog) {
        return;
    }

    double divisor = stats.sampleCount > 0 ? static_cast<double>(stats.sampleCount) : 1.0;
    char line[480] = {};
    std::snprintf(
        line,
        sizeof(line),
        "FRAME-TIMING lwjgl2 frame=%llu result=%u err=0x%04X drawCpuMs=%.3f swapMs=%.3f frameMs=%.3f avg60DrawCpuMs=%.3f avg60SwapMs=%.3f avg60FrameMs=%.3f",
        stats.frameCount,
        result,
        err,
        drawCpuMs,
        swapMs,
        frameMs,
        stats.drawCpuTotalMs / divisor,
        stats.swapTotalMs / divisor,
        stats.frameTotalMs / divisor);
    DebugLine(line);

    if (stats.sampleCount >= 60) {
        stats.sampleCount = 0;
        stats.drawCpuTotalMs = 0.0;
        stats.swapTotalMs = 0.0;
        stats.frameTotalMs = 0.0;
    }
}

static bool QueryUpscaleToSurfaceConfig(int& sourceWidth, int& sourceHeight, int& targetWidth, int& targetHeight) {
    static std::atomic<int> initialized{ 0 };
    static int enabled = 0;
    static int cachedSourceWidth = 0;
    static int cachedSourceHeight = 0;
    static int cachedTargetWidth = 0;
    static int cachedTargetHeight = 0;

    if (!initialized.load(std::memory_order_acquire)) {
        cachedSourceWidth = QueryIntEnvironment(L"MINECRAFT_XBOX_UPSCALE_SOURCE_WIDTH", 0);
        cachedSourceHeight = QueryIntEnvironment(L"MINECRAFT_XBOX_UPSCALE_SOURCE_HEIGHT", 0);
        cachedTargetWidth = QueryIntEnvironment(L"MINECRAFT_XBOX_UPSCALE_TARGET_WIDTH", 0);
        cachedTargetHeight = QueryIntEnvironment(L"MINECRAFT_XBOX_UPSCALE_TARGET_HEIGHT", 0);
        enabled = IsTruthyEnvironment(L"MINECRAFT_XBOX_UPSCALE_TO_SURFACE") &&
            cachedSourceWidth > 0 &&
            cachedSourceHeight > 0 &&
            cachedTargetWidth > cachedSourceWidth &&
            cachedTargetHeight > cachedSourceHeight;
        initialized.store(1, std::memory_order_release);

        if (enabled) {
            char line[224] = {};
            std::snprintf(
                line,
                sizeof(line),
                "Upscale-to-surface enabled source=%dx%d target=%dx%d",
                cachedSourceWidth,
                cachedSourceHeight,
                cachedTargetWidth,
                cachedTargetHeight);
            DebugLine(line);
        }
    }

    sourceWidth = cachedSourceWidth;
    sourceHeight = cachedSourceHeight;
    targetWidth = cachedTargetWidth;
    targetHeight = cachedTargetHeight;
    return enabled != 0;
}

static bool ShouldScaleDefaultFramebufferRect(
    GLuint drawFramebuffer,
    GLint x,
    GLint y,
    GLsizei width,
    GLsizei height,
    GLint& scaledX,
    GLint& scaledY,
    GLsizei& scaledWidth,
    GLsizei& scaledHeight) {
    int sourceWidth = 0;
    int sourceHeight = 0;
    int targetWidth = 0;
    int targetHeight = 0;
    if (drawFramebuffer != 0 ||
        width <= 0 ||
        height <= 0 ||
        !QueryUpscaleToSurfaceConfig(sourceWidth, sourceHeight, targetWidth, targetHeight)) {
        return false;
    }

    if (x < 0 || y < 0 || width > sourceWidth || height > sourceHeight) {
        return false;
    }

    scaledX = static_cast<GLint>((static_cast<long long>(x) * targetWidth) / sourceWidth);
    scaledY = static_cast<GLint>((static_cast<long long>(y) * targetHeight) / sourceHeight);
    scaledWidth = static_cast<GLsizei>((static_cast<long long>(width) * targetWidth + sourceWidth - 1) / sourceWidth);
    scaledHeight = static_cast<GLsizei>((static_cast<long long>(height) * targetHeight + sourceHeight - 1) / sourceHeight);
    return scaledWidth > 0 && scaledHeight > 0;
}

static void LogUpscaleRectOnce(const char* call, GLint x, GLint y, GLsizei width, GLsizei height, GLint sx, GLint sy, GLsizei sw, GLsizei sh) {
    static std::atomic<int> logCount{ 0 };
    int count = logCount.fetch_add(1, std::memory_order_relaxed);
    if (count >= 64) {
        return;
    }

    char line[224] = {};
    std::snprintf(
        line,
        sizeof(line),
        "%s upscale rect %d,%d %dx%d -> %d,%d %dx%d",
        call ? call : "GL",
        x,
        y,
        width,
        height,
        sx,
        sy,
        sw,
        sh);
    DebugLine(line);
}

static bool ShouldResolveMesaModuleFirst(const char* name) {
    if (!name) {
        return false;
    }

    // Mesa-UWP has been observed throwing first-chance native exceptions while
    // eglGetProcAddress resolves uniform entry points. Prefer already-loaded
    // module exports for those core GL calls, but still fall back to EGL below.
    return std::strncmp(name, "glUniform", 9) == 0 ||
        std::strncmp(name, "glProgramUniform", 16) == 0;
}

static PROC ResolveRealMesaModuleProc(const char* name) {
    PROC proc = nullptr;
    if (g_realOpenGlModule) {
        proc = GetProcAddress(g_realOpenGlModule, name);
    }

    if (!proc && g_realGlesModule) {
        proc = GetProcAddress(g_realGlesModule, name);
    }

    if (!proc && g_realGles1Module) {
        proc = GetProcAddress(g_realGles1Module, name);
    }

    return proc;
}

static bool ShouldLogResolveProbe(const char* name) {
    if (!name) {
        return false;
    }
    static const char* const kNames[] = {
        "glClearColor",
        "glClear",
        "glBegin",
        "glEnd",
        "glVertex3f",
        "glTexCoord2f",
        "glColor4f",
        "glViewport",
        "glDrawArrays",
        "glDrawElements",
        "glMatrixMode",
        "glLoadIdentity",
        "glOrtho",
        "glFrustum",
        "glPushMatrix",
        "glPopMatrix",
        "glTranslatef",
        "glScalef",
        "glRotatef",
        "glVertexPointer",
        "glTexCoordPointer",
        "glColorPointer",
        "glEnableClientState",
        "glDisableClientState",
        "glBlendFunc",
        "glTexEnvf",
        "glTexEnvi",
        "glGenLists",
        "glNewList",
        "glEndList",
        "glCallList",
        "glCallLists",
        "glDeleteLists",
        "glIsList",
        "glListBase",
        "glBindTexture",
        "glTexImage2D",
        "glTexSubImage2D",
        "glFlush",
    };
    for (const char* probe : kNames) {
        if (std::strcmp(name, probe) == 0) {
            return true;
        }
    }
    return false;
}

static void LogResolveProbe(const char* name, PROC proc) {
    if (!ShouldLogResolveProbe(name)) {
        return;
    }
    int count = g_resolveProbeLogCount.fetch_add(1, std::memory_order_relaxed);
    if (count >= 256) {
        return;
    }
    char line[320] = {};
    std::snprintf(line, sizeof(line), "Mesa resolve %s -> %p", name, proc);
    DebugLine(line);
}

static void LogGlCallProbe(const char* name) {
    if (!IsVerboseGlLoggingEnabled()) {
        return;
    }

    int count = g_glCallProbeLogCount.fetch_add(1, std::memory_order_relaxed);
    if (count >= 160) {
        return;
    }
    char line[160] = {};
    std::snprintf(line, sizeof(line), "GL call %s", name ? name : "(null)");
    DebugLine(line);
}

static PROC ResolveRealOpenGlProc(const char* name) {
    if (!name || !name[0]) {
        return nullptr;
    }
    if (!IsRealMesaForwardingEnabled()) {
        return nullptr;
    }

    EnsureRealMesaLoaded();
    if (g_realMesaInit.load(std::memory_order_acquire) != 2) {
        return nullptr;
    }

    PROC proc = nullptr;
    if (ShouldResolveMesaModuleFirst(name)) {
        proc = ResolveRealMesaModuleProc(name);
    }

    if (!proc && g_realEglGetProcAddress) {
        __try {
            proc = reinterpret_cast<PROC>(g_realEglGetProcAddress(name));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            proc = nullptr;
        }
    }

    if (!proc && !IsEglProcOnlyMode() && std::strncmp(name, "wgl", 3) != 0 && g_realWglGetProcAddress) {
        __try {
            proc = g_realWglGetProcAddress(name);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            proc = nullptr;
        }
    }

    if (!proc) {
        proc = ResolveRealMesaModuleProc(name);
    }

    LogResolveProbe(name, proc);
    return proc;
}

#define XGL_CACHED_REAL_PROC(symbolName, procType) \
    ([]() -> procType { \
        static PROC xglCachedProc = nullptr; \
        PROC xglProc = xglCachedProc; \
        if (!xglProc) { \
            xglProc = ResolveRealOpenGlProc(symbolName); \
            if (xglProc) { \
                xglCachedProc = xglProc; \
            } \
        } \
        return reinterpret_cast<procType>(xglProc); \
    }())

static bool ForceCompatGlIdentity() {
    static std::atomic<int> cached{ -1 };
    int value = cached.load(std::memory_order_acquire);
    if (value >= 0) {
        return value != 0;
    }

    wchar_t enabled[16] = {};
    DWORD len = GetEnvironmentVariableW(
        L"MINECRAFT_XBOX_FORCE_COMPAT_GL_IDENTITY",
        enabled,
        static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
    const bool isEnabled =
        len > 0 &&
        enabled[0] != L'0' &&
        _wcsicmp(enabled, L"false") != 0 &&
        _wcsicmp(enabled, L"no") != 0;
    cached.store(isEnabled ? 1 : 0, std::memory_order_release);

    if (isEnabled) {
        DebugLine("Forcing xbox-opengl desktop compatibility GL identity");
    }

    return isEnabled;
}

static void LogForwardedProc(const char* name, PROC proc) {
    int count = g_forwardLogCount.fetch_add(1, std::memory_order_relaxed);
    if (count >= 256) {
        return;
    }

    char line[320] = {};
    std::snprintf(line, sizeof(line), "wglGetProcAddress Mesa/EGL %s -> %p", name ? name : "(null)", proc);
    DebugLine(line);
}

static void LogFallbackProc(const char* name) {
    int count = g_fallbackLogCount.fetch_add(1, std::memory_order_relaxed);
    if (count >= 256) {
        return;
    }

    char line[320] = {};
    std::snprintf(line, sizeof(line), "wglGetProcAddress fallback stub %s", name ? name : "(null)");
    DebugLine(line);
}

#define XGL_CALL_REAL_VOID(symbolName, procType, ...) \
    do { \
        auto xglRealProc = XGL_CACHED_REAL_PROC(symbolName, procType); \
        if (xglRealProc) { \
            __try { \
                xglRealProc(__VA_ARGS__); \
            } \
            __except (EXCEPTION_EXECUTE_HANDLER) { \
            } \
        } \
    } while (0)

#define XGL_CALL_REAL_VOID_TIMED(bucket, symbolName, procType, ...) \
    do { \
        auto xglRealProc = XGL_CACHED_REAL_PROC(symbolName, procType); \
        if (xglRealProc) { \
            double xglTimingStartMs = BeginGlCallTiming(); \
            __try { \
                xglRealProc(__VA_ARGS__); \
            } \
            __except (EXCEPTION_EXECUTE_HANDLER) { \
            } \
            PublishGlCallTiming(bucket, xglTimingStartMs); \
        } \
    } while (0)

static GLint g_unpackRowLength = 0;
static GLint g_unpackSkipRows = 0;
static GLint g_unpackSkipPixels = 0;
static GLint g_unpackAlignment = 4;

struct BufferRecord {
    GLuint name;
    uint8_t* data;
    size_t size;
};

struct TextureRecord {
    GLuint name;
    int width;
    int height;
    uint8_t* rgba;
    size_t bytes;
    bool ready;
    bool hasPixels;
    bool cpuPixels;
    bool renderTarget;
    LONG64 lastUploadSerial;
};

struct FramebufferRecord {
    GLuint name;
    GLuint colorTexture;
    GLenum colorTarget;
    GLint colorLevel;
};

struct AttribRecord {
    bool enabled;
    bool separateBinding;
    bool bindingSet;
    GLint size;
    GLenum type;
    GLboolean normalized;
    GLsizei stride;
    uintptr_t pointer;
    GLuint relativeOffset;
    GLuint bindingIndex;
    GLuint buffer;
};

struct VertexBindingRecord {
    GLuint buffer;
    GLintptr offset;
    GLsizei stride;
};

struct VertexArrayRecord {
    GLuint name;
    bool initialized;
    AttribRecord attribs[16];
    VertexBindingRecord bindings[16];
    GLuint elementArrayBuffer;
};

struct DrawVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
    bool hasUv;
    bool hasColor;
};

struct UniformRecord {
    GLint location;
    GLuint program;
    char name[64];
    bool sampler;
    int samplerIndex;
    GLint value;
    bool valueSet;
    uint64_t updateSerial;
};

static constexpr size_t kBufferRecordCapacity = 16384;
static constexpr size_t kTextureRecordCapacity = 16384;
static constexpr size_t kFramebufferRecordCapacity = 4096;
static constexpr size_t kVertexArrayRecordCapacity = 4096;

static BufferRecord g_buffers[kBufferRecordCapacity] = {};
static TextureRecord g_textures[kTextureRecordCapacity] = {};
static FramebufferRecord g_framebuffers[kFramebufferRecordCapacity] = {};
static VertexArrayRecord g_vertexArrays[kVertexArrayRecordCapacity] = {};
static VertexArrayRecord g_defaultVertexArray = {};
static UniformRecord g_uniforms[2048] = {};
static AttribRecord g_attribs[16] = {};
static VertexBindingRecord g_vertexBindings[16] = {};
static uint8_t g_defaultGuiFramebufferRgba[kMinecraftXboxGuiFramebufferMaxBytes] = {};
static uint8_t g_candidateGuiFramebufferRgba[kMinecraftXboxGuiFramebufferMaxBytes] = {};
static int g_defaultGuiFramebufferWidth = 0;
static int g_defaultGuiFramebufferHeight = 0;
static bool g_defaultGuiFramebufferReady = false;
static bool g_haveAcceptedGuiFramebuffer = false;
static LONG64 g_lastAcceptedGuiPixels = 0;
static LONG64 g_lastAcceptedGuiNonBlackPixels = 0;
static LONG64 g_lastAcceptedGuiNonTransparentPixels = 0;
static TextureRecord* g_bestFullSizeRenderTargetCandidate = nullptr;
static GLuint g_lastFullSizeRenderTargetProbeName = 0;
static LONG64 g_lastFullSizeRenderTargetProbeSerial = -1;
static GLuint g_lastMappedBuffer = 0;
static GLintptr g_lastMappedOffset = 0;
static GLsizeiptr g_lastMappedLength = 0;
static LONG64 g_textureWriteSerial = 0;
static GLint g_nextUniformLocation = 1;
static uint64_t g_uniformUpdateSerial = 0;

enum TextureStoreReason : unsigned int {
    TextureStoreAccepted = 0,
    TextureStoreNoTexture = 1,
    TextureStoreBadLevel = 2,
    TextureStoreNoRecord = 3,
    TextureStoreBadDimensions = 4,
    TextureStoreAllocationFailed = 5,
    TextureStoreUnsupportedFormat = 6,
    TextureStoreBadLayout = 7,
    TextureStoreMissingPixels = 8,
};

static void EnsurePresentEventHandle() {
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

static void SignalPresentEvent() {
    EnsurePresentEventHandle();
    if (g_presentEvent) {
        SetEvent(g_presentEvent);
        g_presentSignalCount.fetch_add(1, std::memory_order_relaxed);
    }
}

static bool IsCoreWindowPumpOnSwapEnabled() {
    static std::atomic<int> cached{ -1 };
    int value = cached.load(std::memory_order_acquire);
    if (value >= 0) {
        return value != 0;
    }

    wchar_t enabled[16] = {};
    DWORD len = GetEnvironmentVariableW(
        L"MINECRAFT_XBOX_OPENGL_PUMP_ON_SWAP",
        enabled,
        static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
    const bool explicitlyDisabled =
        len > 0 &&
        (enabled[0] == L'0' ||
            _wcsicmp(enabled, L"false") == 0 ||
            _wcsicmp(enabled, L"no") == 0);
    cached.store(explicitlyDisabled ? 0 : 1, std::memory_order_release);

    if (!explicitlyDisabled) {
        DebugLine("VISIBLE-LAUNCH-FIX: LWJGL2 wglSwapBuffers will pump CoreWindow dispatcher events");
    }
    return !explicitlyDisabled;
}

static void PumpCoreWindowEventsForCurrentThread(const char* reason) {
    static thread_local int unavailable = 0;
    static thread_local int inPump = 0;
    static thread_local unsigned int failureCount = 0;
    static thread_local ULONGLONG nextRetryTick = 0;
    static thread_local unsigned int pumpCount = 0;

    if (unavailable || inPump) {
        return;
    }

    ULONGLONG now = GetTickCount64();
    if (nextRetryTick != 0 && now < nextRetryTick) {
        return;
    }

    ComPtr<ICoreWindowStatic> coreWindowStatic;
    HRESULT hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_UI_Core_CoreWindow).Get(),
        __uuidof(ICoreWindowStatic),
        reinterpret_cast<void**>(coreWindowStatic.GetAddressOf()));
    if (FAILED(hr) || !coreWindowStatic) {
        unavailable = 1;
        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "CoreWindow dispatcher pump unavailable in xbox-opengl; activation factory hr=0x%08lx reason=%s",
            static_cast<unsigned long>(hr),
            reason ? reason : "<none>");
        DebugLine(line);
        return;
    }

    ComPtr<ICoreWindow> coreWindow;
    hr = coreWindowStatic->GetForCurrentThread(coreWindow.GetAddressOf());
    if (FAILED(hr) || !coreWindow) {
        unavailable = 1;
        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "CoreWindow dispatcher pump unavailable in xbox-opengl on this thread hr=0x%08lx reason=%s",
            static_cast<unsigned long>(hr),
            reason ? reason : "<none>");
        DebugLine(line);
        return;
    }

    ComPtr<ICoreDispatcher> dispatcher;
    hr = coreWindow->get_Dispatcher(dispatcher.GetAddressOf());
    if (FAILED(hr) || !dispatcher) {
        unavailable = 1;
        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "CoreWindow dispatcher pump get_Dispatcher failed in xbox-opengl hr=0x%08lx reason=%s",
            static_cast<unsigned long>(hr),
            reason ? reason : "<none>");
        DebugLine(line);
        return;
    }

    inPump = 1;
    hr = dispatcher->ProcessEvents(CoreProcessEventsOption_ProcessAllIfPresent);
    inPump = 0;
    ++pumpCount;

    if (FAILED(hr)) {
        ++failureCount;
        now = GetTickCount64();
        nextRetryTick = now + 50;
        if (failureCount <= 4 || (failureCount % 120) == 0) {
            char line[256] = {};
            std::snprintf(
                line,
                sizeof(line),
                "CoreWindow dispatcher ProcessEvents failed in xbox-opengl hr=0x%08lx reason=%s failureCount=%u; backing off",
                static_cast<unsigned long>(hr),
                reason ? reason : "<none>",
                failureCount);
            DebugLine(line);
        }
        return;
    }

    failureCount = 0;
    nextRetryTick = 0;
    if (pumpCount <= 8 || (pumpCount % 600) == 0) {
        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "CoreWindow dispatcher pumped by xbox-opengl reason=%s count=%u thread=0x%lx",
            reason ? reason : "<none>",
            pumpCount,
            GetCurrentThreadId());
        DebugLine(line);
    }
}

using LegacyCoreWindowKeyEventHandler =
    ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CKeyEventArgs_t;
using LegacyCoreWindowCharEventHandler =
    ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreWindow_Windows__CUI__CCore__CCharacterReceivedEventArgs_t;
using LegacyCoreDispatcherAcceleratorKeyEventHandler =
    ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CUI__CCore__CCoreDispatcher_Windows__CUI__CCore__CAcceleratorKeyEventArgs_t;
using LegacyBackRequestedEventHandler =
    ABI::Windows::Foundation::__FIEventHandler_1_Windows__CUI__CCore__CBackRequestedEventArgs_t;

struct LegacyKeyboardEvent {
    int key;
    int state;
    int ch;
    int repeat;
};

static INIT_ONCE g_legacyKeyboardInitOnce = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_legacyKeyboardLock = {};
static constexpr int kLegacyKeyboardEventCapacity = 256;
static LegacyKeyboardEvent g_legacyKeyboardEvents[kLegacyKeyboardEventCapacity] = {};
static int g_legacyKeyboardHead = 0;
static int g_legacyKeyboardTail = 0;
static std::atomic<int> g_legacyKeyboardState[256] = {};
static ComPtr<LegacyCoreWindowKeyEventHandler> g_legacyKeyDownHandler;
static ComPtr<LegacyCoreWindowKeyEventHandler> g_legacyKeyUpHandler;
static ComPtr<LegacyCoreWindowCharEventHandler> g_legacyCharHandler;
static ComPtr<LegacyCoreDispatcherAcceleratorKeyEventHandler> g_legacyAcceleratorKeyHandler;
static ComPtr<ICoreAcceleratorKeys> g_legacyAcceleratorKeys;
static ComPtr<LegacyBackRequestedEventHandler> g_legacyBackRequestedHandler;
static EventRegistrationToken g_legacyKeyDownToken = {};
static EventRegistrationToken g_legacyKeyUpToken = {};
static EventRegistrationToken g_legacyCharToken = {};
static EventRegistrationToken g_legacyAcceleratorKeyToken = {};
static EventRegistrationToken g_legacyBackRequestedToken = {};
static int g_legacyKeyboardHooksInstalled = 0;
static int g_legacyKeyboardCharHookInstalled = 0;
static int g_legacyAcceleratorKeyHookInstalled = 0;
static int g_legacyBackRequestedHookInstalled = 0;
static LegacyKeyboardEvent g_legacyKeyboardLastEvent = {};
static std::atomic<int> g_legacyKeyboardEventLogCount{ 0 };
static std::atomic<int> g_legacyHandledEventLogCount{ 0 };
static std::atomic<int> g_legacyBackRequestedLogCount{ 0 };

static BOOL CALLBACK InitLegacyKeyboardLock(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSectionEx(&g_legacyKeyboardLock, 4000, 0);
    return TRUE;
}

static void EnsureLegacyKeyboardLock() {
    InitOnceExecuteOnce(&g_legacyKeyboardInitOnce, InitLegacyKeyboardLock, nullptr, nullptr);
}

static int MapVirtualKeyToLegacyLwjgl(int vk) {
    static const int kLetters[26] = {
        30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
        49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44
    };
    static const int kDigits[10] = {
        11, 2, 3, 4, 5, 6, 7, 8, 9, 10
    };
    static const int kNumpad[10] = {
        82, 79, 80, 81, 75, 76, 77, 71, 72, 73
    };

    if (vk >= 65 && vk <= 90) {
        return kLetters[vk - 65];
    }
    if (vk >= 48 && vk <= 57) {
        return kDigits[vk - 48];
    }
    if (vk >= 96 && vk <= 105) {
        return kNumpad[vk - 96];
    }
    if (vk >= 112 && vk <= 121) {
        return 59 + (vk - 112);
    }
    if (vk == 122) {
        return 87;
    }
    if (vk == 123) {
        return 88;
    }

    switch (vk) {
    case 8: return 14;   // Backspace
    case 9: return 15;   // Tab
    case 13: return 28;  // Return
    case 16: return 42;  // Shift
    case 17: return 29;  // Control
    case 18: return 56;  // Alt
    case 19: return 197; // Pause
    case 20: return 58;  // Caps Lock
    case 27: return 1;   // Escape
    case 30: return 28;  // Accept
    case 32: return 57;  // Space
    case 33: return 201; // Page Up
    case 34: return 209; // Page Down
    case 35: return 207; // End
    case 36: return 199; // Home
    case 37: return 203; // Left
    case 38: return 200; // Up
    case 39: return 205; // Right
    case 40: return 208; // Down
    case 44: return 183; // Print Screen
    case 45: return 210; // Insert
    case 46: return 211; // Delete
    case 91: return 219; // Left Windows
    case 92: return 220; // Right Windows
    case 93: return 221; // Menu
    case 106: return 55; // Numpad multiply
    case 107: return 78; // Numpad add
    case 109: return 74; // Numpad subtract
    case 110: return 83; // Numpad decimal
    case 111: return 181; // Numpad divide
    case 138: return 200; // NavigationUp
    case 139: return 208; // NavigationDown
    case 140: return 203; // NavigationLeft
    case 141: return 205; // NavigationRight
    case 142: return 28;  // NavigationAccept
    case 143: return 1;   // NavigationCancel
    case 144: return 69;  // Num Lock
    case 145: return 70;  // Scroll Lock
    case 160: return 42;  // Left Shift
    case 161: return 54;  // Right Shift
    case 162: return 29;  // Left Control
    case 163: return 157; // Right Control
    case 164: return 56;  // Left Alt
    case 165: return 184; // Right Alt
    case 186: return 39;  // Semicolon
    case 187: return 13;  // Equals
    case 188: return 51;  // Comma
    case 189: return 12;  // Minus
    case 190: return 52;  // Period
    case 191: return 53;  // Slash
    case 192: return 41;  // Grave
    case 195: return 28;  // GamepadA
    case 196: return 1;   // GamepadB
    case 203: return 200; // GamepadDPadUp
    case 204: return 208; // GamepadDPadDown
    case 205: return 203; // GamepadDPadLeft
    case 206: return 205; // GamepadDPadRight
    case 219: return 26;  // Left bracket
    case 220: return 43;  // Backslash
    case 221: return 27;  // Right bracket
    case 222: return 40;  // Apostrophe
    default: return 0;
    }
}

static bool IsLegacyCoreWindowGamepadVirtualKey(VirtualKey virtualKey) {
    const int vk = static_cast<int>(virtualKey);
    return (vk >= 138 && vk <= 143) || (vk >= 195 && vk <= 206);
}

static void MarkLegacyCoreWindowEventHandled(IInspectable* args, const char* reason, VirtualKey virtualKey) {
    if (!args) {
        return;
    }

    ComPtr<ICoreWindowEventArgs> coreWindowArgs;
    HRESULT hr = args->QueryInterface(__uuidof(ICoreWindowEventArgs), reinterpret_cast<void**>(coreWindowArgs.GetAddressOf()));
    if (FAILED(hr) || !coreWindowArgs) {
        static std::atomic<int> queryLogCount{ 0 };
        const int count = queryLogCount.fetch_add(1, std::memory_order_relaxed);
        if (count < 8) {
            char line[224] = {};
            std::snprintf(
                line,
                sizeof(line),
                "LWJGL2 CoreWindow %s event could not be marked handled vk=%d hr=0x%08lx",
                reason ? reason : "key",
                static_cast<int>(virtualKey),
                static_cast<unsigned long>(hr));
            DebugLine(line);
        }
        return;
    }

    hr = coreWindowArgs->put_Handled(true);
    const int logCount = g_legacyHandledEventLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (logCount <= 64 || (logCount % 300) == 0 || FAILED(hr)) {
        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "LWJGL2 CoreWindow %s event marked handled vk=%d hr=0x%08lx",
            reason ? reason : "key",
            static_cast<int>(virtualKey),
            static_cast<unsigned long>(hr));
        DebugLine(line);
    }
}

static void PushLegacyKeyboardEvent(int key, int state, int ch, int repeat) {
    EnsureLegacyKeyboardLock();
    if (key < 0 || key >= 256) {
        key = 0;
    }

    EnterCriticalSection(&g_legacyKeyboardLock);
    int nextTail = (g_legacyKeyboardTail + 1) % kLegacyKeyboardEventCapacity;
    if (nextTail == g_legacyKeyboardHead) {
        g_legacyKeyboardHead = (g_legacyKeyboardHead + 1) % kLegacyKeyboardEventCapacity;
    }
    g_legacyKeyboardEvents[g_legacyKeyboardTail] = { key, state ? 1 : 0, ch, repeat ? 1 : 0 };
    g_legacyKeyboardTail = nextTail;
    LeaveCriticalSection(&g_legacyKeyboardLock);

    int logCount = g_legacyKeyboardEventLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (logCount <= 64 || (logCount % 300) == 0) {
        char line[192] = {};
        std::snprintf(
            line,
            sizeof(line),
            "LWJGL2 keyboard event key=%d state=%d char=%d repeat=%d count=%d",
            key,
            state ? 1 : 0,
            ch,
            repeat ? 1 : 0,
            logCount);
        DebugLine(line);
    }
}

static bool PopLegacyKeyboardEvent(LegacyKeyboardEvent& event) {
    EnsureLegacyKeyboardLock();
    EnterCriticalSection(&g_legacyKeyboardLock);
    if (g_legacyKeyboardHead == g_legacyKeyboardTail) {
        LeaveCriticalSection(&g_legacyKeyboardLock);
        return false;
    }

    event = g_legacyKeyboardEvents[g_legacyKeyboardHead];
    g_legacyKeyboardHead = (g_legacyKeyboardHead + 1) % kLegacyKeyboardEventCapacity;
    LeaveCriticalSection(&g_legacyKeyboardLock);
    return true;
}

static void ResetLegacyKeyboardState() {
    for (int i = 0; i < 256; ++i) {
        g_legacyKeyboardState[i].store(0, std::memory_order_release);
    }

    EnsureLegacyKeyboardLock();
    EnterCriticalSection(&g_legacyKeyboardLock);
    g_legacyKeyboardHead = 0;
    g_legacyKeyboardTail = 0;
    LeaveCriticalSection(&g_legacyKeyboardLock);
}

static bool InstallLegacyAcceleratorKeyHook(ICoreWindow* coreWindow) {
    if (g_legacyAcceleratorKeyHookInstalled) {
        return true;
    }
    if (!coreWindow) {
        DebugLine("LWJGL2 CoreDispatcher accelerator key hook skipped; no CoreWindow is available");
        return false;
    }

    ComPtr<ICoreDispatcher> dispatcher;
    HRESULT hr = coreWindow->get_Dispatcher(dispatcher.GetAddressOf());
    if (FAILED(hr) || !dispatcher) {
        char line[192] = {};
        std::snprintf(
            line,
            sizeof(line),
            "LWJGL2 CoreDispatcher accelerator key hook skipped; get_Dispatcher hr=0x%08lx",
            static_cast<unsigned long>(hr));
        DebugLine(line);
        return false;
    }

    ComPtr<ICoreAcceleratorKeys> acceleratorKeys;
    hr = dispatcher.As(&acceleratorKeys);
    if (FAILED(hr) || !acceleratorKeys) {
        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "LWJGL2 CoreDispatcher accelerator key hook skipped; ICoreAcceleratorKeys hr=0x%08lx",
            static_cast<unsigned long>(hr));
        DebugLine(line);
        return false;
    }

    g_legacyAcceleratorKeyHandler = Microsoft::WRL::Callback<LegacyCoreDispatcherAcceleratorKeyEventHandler>(
        [](ICoreDispatcher*, IAcceleratorKeyEventArgs* args) -> HRESULT {
            if (!args) {
                return S_OK;
            }

            VirtualKey virtualKey = VirtualKey_None;
            CoreAcceleratorKeyEventType eventType = CoreAcceleratorKeyEventType_KeyDown;
            args->get_VirtualKey(&virtualKey);
            args->get_EventType(&eventType);

            if (IsLegacyCoreWindowGamepadVirtualKey(virtualKey)) {
                MarkLegacyCoreWindowEventHandled(args, "Accelerator", virtualKey);
                const int logCount = g_legacyHandledEventLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
                if (logCount <= 64 || (logCount % 300) == 0) {
                    char line[224] = {};
                    std::snprintf(
                        line,
                        sizeof(line),
                        "LWJGL2 CoreDispatcher accelerator gamepad event handled vk=%d eventType=%d",
                        static_cast<int>(virtualKey),
                        static_cast<int>(eventType));
                    DebugLine(line);
                }
            }
            return S_OK;
        });
    if (!g_legacyAcceleratorKeyHandler) {
        DebugLine("LWJGL2 CoreDispatcher accelerator key hook failed; handler allocation returned null");
        return false;
    }

    hr = acceleratorKeys->add_AcceleratorKeyActivated(g_legacyAcceleratorKeyHandler.Get(), &g_legacyAcceleratorKeyToken);
    if (FAILED(hr)) {
        char line[192] = {};
        std::snprintf(
            line,
            sizeof(line),
            "LWJGL2 CoreDispatcher add_AcceleratorKeyActivated failed hr=0x%08lx",
            static_cast<unsigned long>(hr));
        DebugLine(line);
        g_legacyAcceleratorKeyHandler.Reset();
        return false;
    }

    g_legacyAcceleratorKeys = acceleratorKeys;
    g_legacyAcceleratorKeyHookInstalled = 1;
    DebugLine("LWJGL2 CoreDispatcher accelerator key hook installed");
    return true;
}

static void RemoveLegacyAcceleratorKeyHook() {
    if (g_legacyAcceleratorKeyHookInstalled && g_legacyAcceleratorKeys) {
        g_legacyAcceleratorKeys->remove_AcceleratorKeyActivated(g_legacyAcceleratorKeyToken);
    }

    g_legacyAcceleratorKeyToken.value = 0;
    g_legacyAcceleratorKeyHookInstalled = 0;
    g_legacyAcceleratorKeyHandler.Reset();
    g_legacyAcceleratorKeys.Reset();
}

static bool InstallLegacyBackRequestedHook() {
    if (g_legacyBackRequestedHookInstalled) {
        return true;
    }

    ComPtr<ISystemNavigationManagerStatics> navigationStatics;
    HRESULT hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_UI_Core_SystemNavigationManager).Get(),
        __uuidof(ISystemNavigationManagerStatics),
        reinterpret_cast<void**>(navigationStatics.GetAddressOf()));
    if (FAILED(hr) || !navigationStatics) {
        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "LWJGL2 SystemNavigationManager BackRequested hook skipped; activation factory hr=0x%08lx",
            static_cast<unsigned long>(hr));
        DebugLine(line);
        return false;
    }

    ComPtr<ISystemNavigationManager> navigationManager;
    hr = navigationStatics->GetForCurrentView(navigationManager.GetAddressOf());
    if (FAILED(hr) || !navigationManager) {
        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "LWJGL2 SystemNavigationManager BackRequested hook skipped; GetForCurrentView hr=0x%08lx",
            static_cast<unsigned long>(hr));
        DebugLine(line);
        return false;
    }

    g_legacyBackRequestedHandler = Microsoft::WRL::Callback<LegacyBackRequestedEventHandler>(
        [](IInspectable*, IBackRequestedEventArgs* args) -> HRESULT {
            if (args) {
                HRESULT hr = args->put_Handled(true);
                const int logCount = g_legacyBackRequestedLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
                if (logCount <= 16 || (logCount % 120) == 0 || FAILED(hr)) {
                    char line[224] = {};
                    std::snprintf(
                        line,
                        sizeof(line),
                        "LWJGL2 SystemNavigationManager BackRequested handled while legacy keyboard is active hr=0x%08lx",
                        static_cast<unsigned long>(hr));
                    DebugLine(line);
                }
            }
            return S_OK;
        });
    if (!g_legacyBackRequestedHandler) {
        DebugLine("LWJGL2 SystemNavigationManager BackRequested hook failed; handler allocation returned null");
        return false;
    }

    hr = navigationManager->add_BackRequested(g_legacyBackRequestedHandler.Get(), &g_legacyBackRequestedToken);
    if (FAILED(hr)) {
        char line[192] = {};
        std::snprintf(
            line,
            sizeof(line),
            "LWJGL2 SystemNavigationManager add_BackRequested failed hr=0x%08lx",
            static_cast<unsigned long>(hr));
        DebugLine(line);
        g_legacyBackRequestedHandler.Reset();
        return false;
    }

    g_legacyBackRequestedHookInstalled = 1;
    DebugLine("LWJGL2 SystemNavigationManager BackRequested hook installed");
    return true;
}

static void RemoveLegacyBackRequestedHook() {
    if (!g_legacyBackRequestedHookInstalled) {
        g_legacyBackRequestedHandler.Reset();
        return;
    }

    ComPtr<ISystemNavigationManagerStatics> navigationStatics;
    HRESULT hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_UI_Core_SystemNavigationManager).Get(),
        __uuidof(ISystemNavigationManagerStatics),
        reinterpret_cast<void**>(navigationStatics.GetAddressOf()));
    if (SUCCEEDED(hr) && navigationStatics) {
        ComPtr<ISystemNavigationManager> navigationManager;
        hr = navigationStatics->GetForCurrentView(navigationManager.GetAddressOf());
        if (SUCCEEDED(hr) && navigationManager) {
            navigationManager->remove_BackRequested(g_legacyBackRequestedToken);
        }
    }

    g_legacyBackRequestedToken.value = 0;
    g_legacyBackRequestedHookInstalled = 0;
    g_legacyBackRequestedHandler.Reset();
}

static void RemoveLegacyKeyboardHooks() {
    const bool keyHooksActive = g_legacyKeyboardHooksInstalled || g_legacyKeyboardCharHookInstalled;
    if (!keyHooksActive) {
        RemoveLegacyAcceleratorKeyHook();
        RemoveLegacyBackRequestedHook();
        ResetLegacyKeyboardState();
        return;
    }

    ComPtr<ICoreWindowStatic> coreWindowStatic;
    HRESULT hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_UI_Core_CoreWindow).Get(),
        __uuidof(ICoreWindowStatic),
        reinterpret_cast<void**>(coreWindowStatic.GetAddressOf()));
    if (FAILED(hr) || !coreWindowStatic) {
        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "LWJGL2 keyboard hook removal skipped; CoreWindow factory failed hr=0x%08lx",
            static_cast<unsigned long>(hr));
        DebugLine(line);
        ResetLegacyKeyboardState();
        return;
    }

    ComPtr<ICoreWindow> coreWindow;
    hr = coreWindowStatic->GetForCurrentThread(coreWindow.GetAddressOf());
    if (FAILED(hr) || !coreWindow) {
        char line[224] = {};
        std::snprintf(
            line,
            sizeof(line),
            "LWJGL2 keyboard hook removal skipped; CoreWindow.GetForCurrentThread failed hr=0x%08lx",
            static_cast<unsigned long>(hr));
        DebugLine(line);
        ResetLegacyKeyboardState();
        return;
    }

    if (g_legacyKeyboardCharHookInstalled) {
        coreWindow->remove_CharacterReceived(g_legacyCharToken);
    }
    if (g_legacyKeyboardHooksInstalled) {
        coreWindow->remove_KeyUp(g_legacyKeyUpToken);
        coreWindow->remove_KeyDown(g_legacyKeyDownToken);
    }

    g_legacyKeyDownToken.value = 0;
    g_legacyKeyUpToken.value = 0;
    g_legacyCharToken.value = 0;
    g_legacyKeyboardHooksInstalled = 0;
    g_legacyKeyboardCharHookInstalled = 0;
    g_legacyKeyDownHandler.Reset();
    g_legacyKeyUpHandler.Reset();
    g_legacyCharHandler.Reset();
    RemoveLegacyAcceleratorKeyHook();
    RemoveLegacyBackRequestedHook();
    ResetLegacyKeyboardState();
    DebugLine("LWJGL2 CoreWindow keyboard hooks removed");
}

static bool InstallLegacyKeyboardHooks() {
    if (g_legacyKeyboardHooksInstalled) {
        return true;
    }

    ComPtr<ICoreWindowStatic> coreWindowStatic;
    HRESULT hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_UI_Core_CoreWindow).Get(),
        __uuidof(ICoreWindowStatic),
        reinterpret_cast<void**>(coreWindowStatic.GetAddressOf()));
    if (FAILED(hr) || !coreWindowStatic) {
        char line[192] = {};
        std::snprintf(line, sizeof(line), "LWJGL2 keyboard CoreWindow factory failed hr=0x%08lx", static_cast<unsigned long>(hr));
        DebugLine(line);
        return false;
    }

    ComPtr<ICoreWindow> coreWindow;
    hr = coreWindowStatic->GetForCurrentThread(coreWindow.GetAddressOf());
    if (FAILED(hr) || !coreWindow) {
        char line[192] = {};
        std::snprintf(line, sizeof(line), "LWJGL2 keyboard CoreWindow.GetForCurrentThread failed hr=0x%08lx", static_cast<unsigned long>(hr));
        DebugLine(line);
        return false;
    }

    g_legacyKeyDownHandler = Microsoft::WRL::Callback<LegacyCoreWindowKeyEventHandler>(
        [](ICoreWindow*, IKeyEventArgs* args) -> HRESULT {
            if (!args) {
                return S_OK;
            }

            VirtualKey virtualKey = VirtualKey_None;
            CorePhysicalKeyStatus status = {};
            args->get_VirtualKey(&virtualKey);
            args->get_KeyStatus(&status);
            if (IsLegacyCoreWindowGamepadVirtualKey(virtualKey)) {
                MarkLegacyCoreWindowEventHandled(args, "LegacyKeyDown", virtualKey);
            }
            const int key = MapVirtualKeyToLegacyLwjgl(static_cast<int>(virtualKey));
            if (key != 0) {
                const int wasDown = g_legacyKeyboardState[key].exchange(1, std::memory_order_acq_rel);
                const int repeat = wasDown || status.WasKeyDown || status.RepeatCount > 1;
                PushLegacyKeyboardEvent(key, 1, 0, repeat);
            }
            return S_OK;
        });

    g_legacyKeyUpHandler = Microsoft::WRL::Callback<LegacyCoreWindowKeyEventHandler>(
        [](ICoreWindow*, IKeyEventArgs* args) -> HRESULT {
            if (!args) {
                return S_OK;
            }

            VirtualKey virtualKey = VirtualKey_None;
            CorePhysicalKeyStatus status = {};
            (void)status;
            args->get_VirtualKey(&virtualKey);
            if (IsLegacyCoreWindowGamepadVirtualKey(virtualKey)) {
                MarkLegacyCoreWindowEventHandled(args, "LegacyKeyUp", virtualKey);
            }
            const int key = MapVirtualKeyToLegacyLwjgl(static_cast<int>(virtualKey));
            if (key != 0) {
                g_legacyKeyboardState[key].store(0, std::memory_order_release);
                PushLegacyKeyboardEvent(key, 0, 0, 0);
            }
            return S_OK;
        });

    g_legacyCharHandler = Microsoft::WRL::Callback<LegacyCoreWindowCharEventHandler>(
        [](ICoreWindow*, ICharacterReceivedEventArgs* args) -> HRESULT {
            if (!args) {
                return S_OK;
            }

            UINT32 codepoint = 0;
            args->get_KeyCode(&codepoint);
            if (codepoint >= 0x20 && codepoint != 0x7f) {
                PushLegacyKeyboardEvent(0, 0, static_cast<int>(codepoint), 0);
            }
            return S_OK;
        });

    if (!g_legacyKeyDownHandler || !g_legacyKeyUpHandler) {
        g_legacyKeyDownHandler.Reset();
        g_legacyKeyUpHandler.Reset();
        g_legacyCharHandler.Reset();
        DebugLine("LWJGL2 keyboard hook allocation failed");
        return false;
    }

    hr = coreWindow->add_KeyDown(g_legacyKeyDownHandler.Get(), &g_legacyKeyDownToken);
    if (FAILED(hr)) {
        char line[160] = {};
        std::snprintf(line, sizeof(line), "LWJGL2 keyboard add_KeyDown failed hr=0x%08lx", static_cast<unsigned long>(hr));
        DebugLine(line);
        g_legacyKeyDownHandler.Reset();
        g_legacyKeyUpHandler.Reset();
        g_legacyCharHandler.Reset();
        return false;
    }

    hr = coreWindow->add_KeyUp(g_legacyKeyUpHandler.Get(), &g_legacyKeyUpToken);
    if (FAILED(hr)) {
        char line[160] = {};
        std::snprintf(line, sizeof(line), "LWJGL2 keyboard add_KeyUp failed hr=0x%08lx", static_cast<unsigned long>(hr));
        DebugLine(line);
        coreWindow->remove_KeyDown(g_legacyKeyDownToken);
        g_legacyKeyDownToken.value = 0;
        g_legacyKeyDownHandler.Reset();
        g_legacyKeyUpHandler.Reset();
        g_legacyCharHandler.Reset();
        return false;
    }

    g_legacyKeyboardCharHookInstalled = 0;
    if (g_legacyCharHandler) {
        hr = coreWindow->add_CharacterReceived(g_legacyCharHandler.Get(), &g_legacyCharToken);
        if (SUCCEEDED(hr)) {
            g_legacyKeyboardCharHookInstalled = 1;
        }
        else {
            char line[192] = {};
            std::snprintf(
                line,
                sizeof(line),
                "LWJGL2 keyboard add_CharacterReceived failed hr=0x%08lx; navigation keys remain enabled",
                static_cast<unsigned long>(hr));
            DebugLine(line);
            g_legacyCharHandler.Reset();
        }
    }

    ResetLegacyKeyboardState();
    g_legacyKeyboardHooksInstalled = 1;
    InstallLegacyAcceleratorKeyHook(coreWindow.Get());
    InstallLegacyBackRequestedHook();
    DebugLine(g_legacyKeyboardCharHookInstalled
        ? "VISIBLE-LAUNCH-FIX: LWJGL2 CoreWindow keyboard hooks installed with character input"
        : "VISIBLE-LAUNCH-FIX: LWJGL2 CoreWindow keyboard hooks installed");
    return true;
}

extern "C" __declspec(dllexport) int __stdcall xglLegacyKeyboardCreate() {
    return InstallLegacyKeyboardHooks() ? 1 : 0;
}

extern "C" __declspec(dllexport) void __stdcall xglLegacyKeyboardDestroy() {
    RemoveLegacyKeyboardHooks();
}

extern "C" __declspec(dllexport) int __stdcall xglLegacyKeyboardGetKeyState(int key) {
    if (key < 0 || key >= 256) {
        return 0;
    }
    return g_legacyKeyboardState[key].load(std::memory_order_acquire) ? 1 : 0;
}

extern "C" __declspec(dllexport) int __stdcall xglLegacyKeyboardPollEvent() {
    LegacyKeyboardEvent event = {};
    if (!PopLegacyKeyboardEvent(event)) {
        return 0;
    }
    g_legacyKeyboardLastEvent = event;
    return 1;
}

extern "C" __declspec(dllexport) int __stdcall xglLegacyKeyboardGetEventKey() {
    return g_legacyKeyboardLastEvent.key;
}

extern "C" __declspec(dllexport) int __stdcall xglLegacyKeyboardGetEventState() {
    return g_legacyKeyboardLastEvent.state;
}

extern "C" __declspec(dllexport) int __stdcall xglLegacyKeyboardGetEventChar() {
    return g_legacyKeyboardLastEvent.ch;
}

extern "C" __declspec(dllexport) int __stdcall xglLegacyKeyboardGetEventRepeat() {
    return g_legacyKeyboardLastEvent.repeat;
}

static void TrySwapCurrentEglSurface() {
    EnsureRealMesaLoaded();
    const int swapLogIndex = g_swapLogCount.fetch_add(1, std::memory_order_relaxed);
    const bool frameTimingEnabled = IsFrameTimingEnabled();
    const double frameStartMs = frameTimingEnabled ? FrameTimingNowMs() : 0.0;
    const double drawCpuMs = frameTimingEnabled ? BeginLwjgl2FrameTiming(frameStartMs) : 0.0;
    if (!g_realEglGetCurrentDisplay || !g_realEglGetCurrentSurface || !g_realEglSwapBuffers) {
        if (swapLogIndex < 32) {
            char line[256] = {};
            std::snprintf(
                line,
                sizeof(line),
                "eglSwapBuffers skipped exports display=%p surface=%p swap=%p",
                reinterpret_cast<void*>(g_realEglGetCurrentDisplay),
                reinterpret_cast<void*>(g_realEglGetCurrentSurface),
                reinterpret_cast<void*>(g_realEglSwapBuffers));
            DebugLine(line);
        }
        return;
    }

    void* display = nullptr;
    void* surface = nullptr;
    __try {
        display = g_realEglGetCurrentDisplay();
        surface = g_realEglGetCurrentSurface(EGL_DRAW_VALUE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (!display || !surface) {
        if (swapLogIndex < 32) {
            char line[256] = {};
            std::snprintf(line, sizeof(line), "eglSwapBuffers skipped current display=%p surface=%p", display, surface);
            DebugLine(line);
        }
        return;
    }

    unsigned int result = 0;
    bool swapException = false;
    double swapMs = 0.0;
    double swapBeginMs = frameTimingEnabled ? FrameTimingNowMs() : 0.0;
    __try {
        result = g_realEglSwapBuffers(display, surface);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        swapException = true;
        result = 0;
    }
    if (frameTimingEnabled) {
        swapMs = FrameTimingNowMs() - swapBeginMs;
    }

    unsigned int err = 0;
    if (frameTimingEnabled || swapLogIndex < 64 || !result || swapException) {
        if (g_realEglGetError) {
            __try {
                err = g_realEglGetError();
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                err = 0xffffffffu;
            }
        }
    }

    if (swapException) {
        if (swapLogIndex < 32) {
            DebugLine("eglSwapBuffers raised native exception");
        }
        if (frameTimingEnabled) {
            LogLwjgl2FrameTiming(FrameTimingNowMs(), drawCpuMs, swapMs, result, err);
        }
        return;
    }

    if (swapLogIndex < 64) {
        char line[320] = {};
        std::snprintf(line, sizeof(line), "eglSwapBuffers current display=%p surface=%p result=%u err=0x%04X", display, surface, result, err);
        DebugLine(line);
    }
    if (frameTimingEnabled) {
        LogLwjgl2FrameTiming(FrameTimingNowMs(), drawCpuMs, swapMs, result, err);
    }
}

static bool IsCommandBridgeEnabled() {
    static std::atomic<int> cached{ -1 };
    int value = cached.load(std::memory_order_acquire);
    if (value >= 0) {
        return value != 0;
    }

    wchar_t enabled[16] = {};
    DWORD len = GetEnvironmentVariableW(
        L"MINECRAFT_XBOX_OPENGL_COMMAND_BRIDGE",
        enabled,
        static_cast<DWORD>(sizeof(enabled) / sizeof(enabled[0])));
    const bool isDisabled =
        len > 0 &&
        (enabled[0] == L'0' ||
            _wcsicmp(enabled, L"false") == 0 ||
            _wcsicmp(enabled, L"no") == 0);
    cached.store(isDisabled ? 0 : 1, std::memory_order_release);

    if (isDisabled) {
        DebugLine("OpenGL command bridge disabled; direct Mesa EGL presentation owns rendering");
    }
    return !isDisabled;
}

static MinecraftXboxGlCommandState* EnsureGlCommandState() {
    if (!IsCommandBridgeEnabled() && !IsFrameTimingEnabled()) {
        return nullptr;
    }
    if (g_glCommandState) {
        return g_glCommandState;
    }

    g_glCommandStateMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(MinecraftXboxGlCommandState),
        kMinecraftXboxGlCommandStateName);
    if (!g_glCommandStateMapping) {
        return nullptr;
    }

    g_glCommandState = static_cast<MinecraftXboxGlCommandState*>(
        MapViewOfFile(
            g_glCommandStateMapping,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            sizeof(MinecraftXboxGlCommandState)));
    if (!g_glCommandState) {
        CloseHandle(g_glCommandStateMapping);
        g_glCommandStateMapping = nullptr;
        return nullptr;
    }

    MinecraftXboxInitializeGlCommandState(g_glCommandState);
    return g_glCommandState;
}

static float Clamp01(GLfloat value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static LONG64 PositiveSize(GLsizeiptr size) {
    return size > 0 ? static_cast<LONG64>(size) : 0;
}

static int ComponentsForFormat(GLenum format) {
    switch (format) {
        case GL_RED:
        case GL_DEPTH_COMPONENT:
            return 1;
        case GL_RG:
            return 2;
        case GL_RGB:
            return 3;
        case GL_RGBA:
        case GL_BGRA:
        case GL_DEPTH_STENCIL:
            return 4;
        default:
            return 4;
    }
}

static int BytesForType(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_BYTE:
        case GL_BYTE:
            return 1;
        case GL_UNSIGNED_SHORT:
        case GL_SHORT:
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_5_5_5_1:
            return 2;
        case GL_UNSIGNED_INT:
        case GL_INT:
        case GL_FLOAT:
        case GL_UNSIGNED_INT_24_8:
            return 4;
        default:
            return 1;
    }
}

static bool IsReadableTextureFormat(GLenum format) {
    return format == GL_RGBA ||
        format == GL_BGRA ||
        format == GL_RGB ||
        format == GL_RG ||
        format == GL_RED;
}

static LONG64 EstimateImageBytes(GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type) {
    if (width <= 0 || height <= 0 || depth <= 0) {
        return 0;
    }

    if (width > 16384 || height > 16384 || depth > 2048) {
        return 0;
    }

    const auto pixels = static_cast<LONG64>(width) * static_cast<LONG64>(height) * static_cast<LONG64>(depth);
    const auto bytes = pixels * ComponentsForFormat(format) * BytesForType(type);
    const auto maxDiagnosticBytes = 512ll * 1024ll * 1024ll;
    return bytes > maxDiagnosticBytes ? maxDiagnosticBytes : bytes;
}

static BufferRecord* FindBuffer(GLuint name, bool create);

static size_t AlignSize(size_t value, size_t alignment) {
    if (alignment <= 1) {
        return value;
    }

    return ((value + alignment - 1) / alignment) * alignment;
}

static int EffectiveUnpackAlignment() {
    return (g_unpackAlignment == 1 ||
            g_unpackAlignment == 2 ||
            g_unpackAlignment == 4 ||
            g_unpackAlignment == 8)
        ? g_unpackAlignment
        : 4;
}

static size_t TextureSourceRowStride(GLsizei width, GLenum format, GLenum type) {
    const int components = ComponentsForFormat(format);
    const int bytesPerComponent = BytesForType(type);
    const int rowPixels = g_unpackRowLength > 0 ? g_unpackRowLength : width;
    const size_t rowBytes =
        static_cast<size_t>(rowPixels) *
        static_cast<size_t>(components) *
        static_cast<size_t>(bytesPerComponent);
    return AlignSize(rowBytes, static_cast<size_t>(EffectiveUnpackAlignment()));
}

static bool ComputeTextureSourceLayout(
    GLsizei width,
    GLsizei height,
    GLenum format,
    GLenum type,
    size_t& startOffset,
    size_t& rowStride,
    size_t& requiredBytes) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    const int components = ComponentsForFormat(format);
    const int bytesPerComponent = BytesForType(type);
    if (components <= 0 || bytesPerComponent <= 0) {
        return false;
    }

    const int skipPixels = g_unpackSkipPixels > 0 ? g_unpackSkipPixels : 0;
    const int skipRows = g_unpackSkipRows > 0 ? g_unpackSkipRows : 0;
    rowStride = TextureSourceRowStride(width, format, type);
    startOffset =
        static_cast<size_t>(skipRows) * rowStride +
        static_cast<size_t>(skipPixels) *
            static_cast<size_t>(components) *
            static_cast<size_t>(bytesPerComponent);

    const size_t copyRowBytes =
        static_cast<size_t>(width) *
        static_cast<size_t>(components) *
        static_cast<size_t>(bytesPerComponent);
    requiredBytes = startOffset +
        (static_cast<size_t>(height - 1) * rowStride) +
        copyRowBytes;
    return true;
}

static const uint8_t* ResolvePixelDataPointer(
    const void* pixels,
    size_t requiredBytes) {
    if (g_boundPixelUnpackBuffer != 0) {
        auto* buffer = FindBuffer(g_boundPixelUnpackBuffer, false);
        if (!buffer || !buffer->data || buffer->size == 0) {
            return nullptr;
        }

        const size_t offset = static_cast<size_t>(reinterpret_cast<uintptr_t>(pixels));
        if (offset >= buffer->size) {
            return nullptr;
        }
        if (requiredBytes > 0 && requiredBytes > buffer->size - offset) {
            return nullptr;
        }
        return buffer->data + offset;
    }

    if (!pixels || reinterpret_cast<uintptr_t>(pixels) < 0x10000) {
        return nullptr;
    }
    return static_cast<const uint8_t*>(pixels);
}

static GLuint BoundBufferForTarget(GLenum target) {
    switch (target) {
        case GL_ARRAY_BUFFER: return g_boundArrayBuffer;
        case GL_ELEMENT_ARRAY_BUFFER: return g_boundElementArrayBuffer;
        case GL_PIXEL_UNPACK_BUFFER: return g_boundPixelUnpackBuffer;
        case GL_UNIFORM_BUFFER: return g_boundUniformBuffer;
        case GL_SHADER_STORAGE_BUFFER: return g_boundShaderStorageBuffer;
        default: return 0;
    }
}

static void SetBoundBufferForTarget(GLenum target, GLuint buffer) {
    switch (target) {
        case GL_ARRAY_BUFFER: g_boundArrayBuffer = buffer; break;
        case GL_ELEMENT_ARRAY_BUFFER: g_boundElementArrayBuffer = buffer; break;
        case GL_PIXEL_UNPACK_BUFFER: g_boundPixelUnpackBuffer = buffer; break;
        case GL_UNIFORM_BUFFER: g_boundUniformBuffer = buffer; break;
        case GL_SHADER_STORAGE_BUFFER: g_boundShaderStorageBuffer = buffer; break;
        default: break;
    }
}

static BufferRecord* FindBuffer(GLuint name, bool create) {
    if (name == 0) {
        return nullptr;
    }
    if (create && !IsCommandBridgeEnabled()) {
        return nullptr;
    }

    BufferRecord* empty = nullptr;
    for (auto& record : g_buffers) {
        if (record.name == name) {
            return &record;
        }
        if (!empty && record.name == 0) {
            empty = &record;
        }
    }

    if (!empty) {
        if (create) {
            auto* state = EnsureGlCommandState();
            if (state) {
                InterlockedIncrement64(&state->textureTableFullSerial);
            }
        }
        return nullptr;
    }

    if (!create) {
        return nullptr;
    }

    empty->name = name;
    empty->data = nullptr;
    empty->size = 0;
    return empty;
}

static bool EnsureBufferSize(BufferRecord* record, size_t size) {
    if (!record) {
        return false;
    }
    if (record->size >= size && record->data) {
        return true;
    }

    void* resized = std::realloc(record->data, size > 0 ? size : 1);
    if (!resized) {
        return false;
    }

    if (size > record->size) {
        std::memset(static_cast<uint8_t*>(resized) + record->size, 0, size - record->size);
    }
    record->data = static_cast<uint8_t*>(resized);
    record->size = size;
    return true;
}

static void StoreBufferBytes(GLuint buffer, GLsizeiptr size, const void* data) {
    if (!IsCommandBridgeEnabled()) {
        return;
    }
    if (buffer == 0 || size < 0) {
        return;
    }

    auto* record = FindBuffer(buffer, true);
    const size_t byteSize = static_cast<size_t>(size);
    if (!EnsureBufferSize(record, byteSize)) {
        return;
    }

    if (data && byteSize > 0) {
        std::memcpy(record->data, data, byteSize);
    }
}

static void StoreBufferSubBytes(GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data) {
    if (!IsCommandBridgeEnabled()) {
        return;
    }
    if (buffer == 0 || offset < 0 || size <= 0 || !data) {
        return;
    }

    auto* record = FindBuffer(buffer, true);
    const size_t end = static_cast<size_t>(offset) + static_cast<size_t>(size);
    if (!EnsureBufferSize(record, end)) {
        return;
    }

    std::memcpy(record->data + static_cast<size_t>(offset), data, static_cast<size_t>(size));
}

static size_t TrackedBufferSizeForTarget(GLenum target) {
    const GLuint buffer = BoundBufferForTarget(target);
    auto* record = FindBuffer(buffer, false);
    return record ? record->size : 0;
}

static GLint SizeToGlint(size_t size) {
    constexpr size_t maxGlint = static_cast<size_t>(0x7fffffff);
    return static_cast<GLint>(size > maxGlint ? maxGlint : size);
}

static GLint64 SizeToGlint64(size_t size) {
    return static_cast<GLint64>(size);
}

static void LogBufferSizeFallback(GLenum target, long long reportedSize, size_t trackedSize) {
    static std::atomic<int> logCount{ 0 };
    if (logCount.fetch_add(1, std::memory_order_relaxed) >= 16) {
        return;
    }

    char line[192] = {};
    std::snprintf(
        line,
        sizeof(line),
        "glGetBufferParameter GL_BUFFER_SIZE target=0x%04x reported=%lld tracked=%zu",
        target,
        reportedSize,
        trackedSize);
    DebugLine(line);
}

static TextureRecord* FindTexture(GLuint name, bool create) {
    if (name == 0) {
        return nullptr;
    }
    if (create && !IsCommandBridgeEnabled()) {
        return nullptr;
    }

    TextureRecord* empty = nullptr;
    for (auto& record : g_textures) {
        if (record.name == name) {
            return &record;
        }
        if (!empty && record.name == 0) {
            empty = &record;
        }
    }

    if (!create || !empty) {
        return nullptr;
    }

    empty->name = name;
    empty->width = 0;
    empty->height = 0;
    empty->rgba = nullptr;
    empty->bytes = 0;
    empty->ready = false;
    empty->hasPixels = false;
    empty->cpuPixels = false;
    empty->renderTarget = false;
    empty->lastUploadSerial = 0;
    return empty;
}

static FramebufferRecord* FindFramebuffer(GLuint name, bool create) {
    if (name == 0) {
        return nullptr;
    }
    if (create && !IsCommandBridgeEnabled()) {
        return nullptr;
    }

    FramebufferRecord* empty = nullptr;
    for (auto& record : g_framebuffers) {
        if (record.name == name) {
            return &record;
        }
        if (!empty && record.name == 0) {
            empty = &record;
        }
    }

    if (!create || !empty) {
        return nullptr;
    }

    empty->name = name;
    empty->colorTexture = 0;
    empty->colorTarget = GL_TEXTURE_2D;
    empty->colorLevel = 0;
    return empty;
}

static VertexArrayRecord* FindVertexArray(GLuint name, bool create) {
    if (create && !IsCommandBridgeEnabled()) {
        return nullptr;
    }
    if (name == 0) {
        g_defaultVertexArray.name = 0;
        g_defaultVertexArray.initialized = true;
        return &g_defaultVertexArray;
    }

    VertexArrayRecord* empty = nullptr;
    for (auto& record : g_vertexArrays) {
        if (record.name == name) {
            return &record;
        }
        if (!empty && record.name == 0) {
            empty = &record;
        }
    }

    if (!create || !empty) {
        return nullptr;
    }

    ZeroMemory(empty, sizeof(*empty));
    empty->name = name;
    empty->initialized = true;
    return empty;
}

static void SaveCurrentVertexArrayState() {
    auto* record = FindVertexArray(g_boundVertexArray, true);
    if (!record) {
        return;
    }

    record->initialized = true;
    std::memcpy(record->attribs, g_attribs, sizeof(g_attribs));
    std::memcpy(record->bindings, g_vertexBindings, sizeof(g_vertexBindings));
    record->elementArrayBuffer = g_boundElementArrayBuffer;
}

static void LoadVertexArrayState(GLuint array) {
    auto* record = FindVertexArray(array, true);
    if (!record || !record->initialized) {
        ZeroMemory(g_attribs, sizeof(g_attribs));
        ZeroMemory(g_vertexBindings, sizeof(g_vertexBindings));
        g_boundElementArrayBuffer = 0;
        return;
    }

    std::memcpy(g_attribs, record->attribs, sizeof(g_attribs));
    std::memcpy(g_vertexBindings, record->bindings, sizeof(g_vertexBindings));
    g_boundElementArrayBuffer = record->elementArrayBuffer;
}

static void SyncCurrentVertexArrayState() {
    SaveCurrentVertexArrayState();
}

static void ModifyVertexArrayAttrib(GLuint vaobj, GLuint index, const AttribRecord& attrib) {
    if (index >= 16) {
        return;
    }

    if (vaobj == g_boundVertexArray) {
        g_attribs[index] = attrib;
        SyncCurrentVertexArrayState();
        return;
    }

    auto* record = FindVertexArray(vaobj, true);
    if (record) {
        record->attribs[index] = attrib;
    }
}

static void ModifyVertexArrayBinding(GLuint vaobj, GLuint binding, const VertexBindingRecord& bindingRecord) {
    if (binding >= 16) {
        return;
    }

    if (vaobj == g_boundVertexArray) {
        g_vertexBindings[binding] = bindingRecord;
        SyncCurrentVertexArrayState();
        return;
    }

    auto* record = FindVertexArray(vaobj, true);
    if (record) {
        record->bindings[binding] = bindingRecord;
    }
}

static GLuint FramebufferNameForTarget(GLenum target) {
    switch (target) {
        case GL_READ_FRAMEBUFFER: return g_boundReadFramebuffer;
        case GL_DRAW_FRAMEBUFFER:
        case GL_FRAMEBUFFER:
            return g_boundDrawFramebuffer;
        default:
            return 0;
    }
}

static TextureRecord* FramebufferColorTexture(GLuint framebuffer) {
    if (framebuffer == 0) {
        return nullptr;
    }

    auto* record = FindFramebuffer(framebuffer, false);
    if (!record || record->colorTexture == 0 || record->colorLevel != 0) {
        return nullptr;
    }

    return FindTexture(record->colorTexture, false);
}

static TextureRecord* CurrentDrawFramebufferTexture() {
    return FramebufferColorTexture(g_boundDrawFramebuffer);
}

static TextureRecord* CurrentReadFramebufferTexture() {
    return FramebufferColorTexture(g_boundReadFramebuffer);
}

static bool IsTextureUsable(const TextureRecord* texture) {
    return texture &&
        texture->ready &&
        texture->hasPixels &&
        texture->rgba &&
        texture->width > 0 &&
        texture->height > 0;
}

static bool IsFullSizeRenderTargetCandidate(const TextureRecord* texture) {
    if (!IsTextureUsable(texture) || !texture->renderTarget) {
        return false;
    }

    const int viewportWidth = g_viewport[2] > 0 ? g_viewport[2] : 1920;
    const int viewportHeight = g_viewport[3] > 0 ? g_viewport[3] : 1080;
    const int minimumWidth = viewportWidth >= 1280 ? 960 : 640;
    const int minimumHeight = viewportHeight >= 720 ? 540 : 360;
    return texture->width >= minimumWidth && texture->height >= minimumHeight;
}

static void TrackFullSizeRenderTargetCandidate(TextureRecord* texture) {
    if (!IsFullSizeRenderTargetCandidate(texture)) {
        return;
    }

    if (!IsFullSizeRenderTargetCandidate(g_bestFullSizeRenderTargetCandidate) ||
        texture->name > g_bestFullSizeRenderTargetCandidate->name ||
        (texture->name == g_bestFullSizeRenderTargetCandidate->name &&
            texture->lastUploadSerial > g_bestFullSizeRenderTargetCandidate->lastUploadSerial)) {
        g_bestFullSizeRenderTargetCandidate = texture;
    }
}

static bool IsGuiTextureCandidate(const TextureRecord* texture) {
    return IsTextureUsable(texture) &&
        (!texture->renderTarget || texture->cpuPixels);
}

static TextureRecord* TextureForUnit(GLuint unit) {
    if (unit >= 32) {
        return nullptr;
    }

    auto* texture = FindTexture(g_boundTexture2D[unit], false);
    if (!IsGuiTextureCandidate(texture)) {
        texture = FindTexture(g_boundTexture2DArray[unit], false);
    }
    return IsGuiTextureCandidate(texture) ? texture : nullptr;
}

static void AddTextureCandidate(TextureRecord** candidates, int& count, TextureRecord* texture) {
    if (!IsGuiTextureCandidate(texture)) {
        return;
    }

    auto* drawTarget = CurrentDrawFramebufferTexture();
    if (drawTarget && drawTarget == texture) {
        return;
    }

    for (int i = 0; i < count; ++i) {
        if (candidates[i] == texture) {
            return;
        }
    }

    if (count < 32) {
        candidates[count++] = texture;
    }
}

static TextureRecord* FindBestKnownGuiTexture() {
    TextureRecord* exactGuiAtlas = nullptr;
    TextureRecord* fallbackLarge = nullptr;
    for (auto& texture : g_textures) {
        if (!IsGuiTextureCandidate(&texture)) {
            continue;
        }

        if (texture.width == 1024 && texture.height == 1024) {
            if (!exactGuiAtlas || texture.lastUploadSerial > exactGuiAtlas->lastUploadSerial) {
                exactGuiAtlas = &texture;
            }
            continue;
        }

        if (texture.width >= 512 && texture.height >= 256 &&
            texture.width <= 2048 && texture.height <= 2048) {
            if (!fallbackLarge || texture.lastUploadSerial > fallbackLarge->lastUploadSerial) {
                fallbackLarge = &texture;
            }
        }
    }

    return exactGuiAtlas ? exactGuiAtlas : fallbackLarge;
}

static TextureRecord* FindExactGuiTexture() {
    TextureRecord* exactGuiAtlas = nullptr;
    for (auto& texture : g_textures) {
        if (texture.name == 0 ||
            !texture.ready ||
            texture.width != 1024 ||
            texture.height != 1024) {
            continue;
        }

        if (!exactGuiAtlas ||
            (texture.hasPixels && !exactGuiAtlas->hasPixels) ||
            texture.lastUploadSerial > exactGuiAtlas->lastUploadSerial) {
            exactGuiAtlas = &texture;
        }
    }

    return exactGuiAtlas;
}

static TextureRecord* FindBestAllocatedGuiTexture() {
    TextureRecord* best = nullptr;
    LONG64 bestPixels = 0;
    for (auto& texture : g_textures) {
        if (texture.name == 0 ||
            !texture.ready ||
            texture.width < 512 ||
            texture.height < 256 ||
            texture.width > 2048 ||
            texture.height > 2048) {
            continue;
        }

        const LONG64 pixels =
            static_cast<LONG64>(texture.width) *
            static_cast<LONG64>(texture.height);
        if (!best ||
            pixels > bestPixels ||
            (pixels == bestPixels && texture.lastUploadSerial > best->lastUploadSerial)) {
            best = &texture;
            bestPixels = pixels;
        }
    }

    return best;
}

static TextureRecord* FindLargestKnownTexture() {
    TextureRecord* largest = nullptr;
    LONG64 largestPixels = 0;
    for (auto& texture : g_textures) {
        if (texture.name == 0 || !texture.ready || texture.width <= 0 || texture.height <= 0) {
            continue;
        }

        const LONG64 pixels =
            static_cast<LONG64>(texture.width) *
            static_cast<LONG64>(texture.height);
        if (!largest || pixels > largestPixels) {
            largest = &texture;
            largestPixels = pixels;
        }
    }

    return largest;
}

static LONG64 CountKnownTextures() {
    LONG64 count = 0;
    for (auto& texture : g_textures) {
        if (texture.name != 0) {
            ++count;
        }
    }
    return count;
}

static void PublishTextureInventory(TextureRecord* latest) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    TrackFullSizeRenderTargetCandidate(latest);

    auto* largest = FindLargestKnownTexture();
    auto* bestGui = FindBestKnownGuiTexture();
    auto* exactGui = FindExactGuiTexture();
    auto* bestAllocatedGui = FindBestAllocatedGuiTexture();
    state->textureLastStoredName = latest ? latest->name : 0;
    state->textureLastStoredWidth = latest ? latest->width : 0;
    state->textureLastStoredHeight = latest ? latest->height : 0;
    state->textureLargestName = largest ? largest->name : 0;
    state->textureLargestWidth = largest ? largest->width : 0;
    state->textureLargestHeight = largest ? largest->height : 0;
    state->textureBestGuiName = bestGui ? bestGui->name : 0;
    state->textureBestGuiWidth = bestGui ? bestGui->width : 0;
    state->textureBestGuiHeight = bestGui ? bestGui->height : 0;
    state->textureRecordCount = CountKnownTextures();
    state->textureExactGuiName = exactGui ? exactGui->name : 0;
    state->textureExactGuiWidth = exactGui ? exactGui->width : 0;
    state->textureExactGuiHeight = exactGui ? exactGui->height : 0;
    state->textureExactGuiHasPixels = exactGui && exactGui->hasPixels ? 1 : 0;
    state->textureBestAllocatedGuiName = bestAllocatedGui ? bestAllocatedGui->name : 0;
    state->textureBestAllocatedGuiWidth = bestAllocatedGui ? bestAllocatedGui->width : 0;
    state->textureBestAllocatedGuiHeight = bestAllocatedGui ? bestAllocatedGui->height : 0;
}

static void PublishTextureAllocation(TextureRecord* texture, GLsizei width, GLsizei height) {
    auto* state = EnsureGlCommandState();
    if (!state || !texture) {
        return;
    }

    state->textureLastAllocationName = texture->name;
    state->textureLastAllocationWidth = width;
    state->textureLastAllocationHeight = height;
}

static void PublishTextureShrinkPreserved() {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    InterlockedIncrement64(&state->textureShrinkPreservedSerial);
}

static void PublishTextureStoreAttempt(
    GLuint textureName,
    GLint level,
    GLint xoffset,
    GLint yoffset,
    GLsizei width,
    GLsizei height,
    GLenum format,
    GLenum type,
    bool subUpload,
    TextureStoreReason reason) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    state->textureLastAttemptName = textureName;
    state->textureLastAttemptLevel = level;
    state->textureLastAttemptX = xoffset;
    state->textureLastAttemptY = yoffset;
    state->textureLastAttemptWidth = width;
    state->textureLastAttemptHeight = height;
    state->textureLastAttemptFormat = format;
    state->textureLastAttemptType = type;
    state->textureLastAttemptReason = reason;
    state->textureLastAttemptPbo = g_boundPixelUnpackBuffer;
    state->textureLastAttemptUnit = g_activeTextureUnit;

    if (!subUpload || textureName == 0) {
        return;
    }

    auto* texture = FindTexture(textureName, false);
    if (!texture || !texture->ready || texture->width != 1024 || texture->height != 1024) {
        return;
    }

    InterlockedIncrement64(&state->textureExactGuiUploadAttemptSerial);
    if (reason == TextureStoreAccepted) {
        InterlockedIncrement64(&state->textureExactGuiUploadAcceptedSerial);
    } else {
        state->textureExactGuiLastRejectReason = reason;
        InterlockedIncrement64(&state->textureExactGuiUploadRejectedSerial);
    }
}

static void PublishFramebufferState(
    bool bindChanged,
    bool attachmentChanged,
    bool blitChanged,
    GLuint blitSourceTexture,
    GLuint blitDestTexture,
    int blitWidth,
    int blitHeight) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    auto* colorTexture = CurrentDrawFramebufferTexture();
    if (!colorTexture) {
        colorTexture = CurrentReadFramebufferTexture();
    }

    state->framebufferDrawName = g_boundDrawFramebuffer;
    state->framebufferReadName = g_boundReadFramebuffer;
    state->framebufferColorTextureName = colorTexture ? colorTexture->name : 0;
    state->framebufferColorTextureWidth = colorTexture ? colorTexture->width : 0;
    state->framebufferColorTextureHeight = colorTexture ? colorTexture->height : 0;
    if (blitChanged) {
        state->framebufferLastBlitSourceTextureName = blitSourceTexture;
        state->framebufferLastBlitDestTextureName = blitDestTexture;
        state->framebufferLastBlitWidth = blitWidth;
        state->framebufferLastBlitHeight = blitHeight;
        InterlockedIncrement64(&state->framebufferBlitSerial);
    }
    if (attachmentChanged) {
        InterlockedIncrement64(&state->framebufferAttachSerial);
    }
    if (bindChanged) {
        InterlockedIncrement64(&state->framebufferBindSerial);
    }
}

static bool ResizeTexture(TextureRecord* record, GLsizei width, GLsizei height) {
    if (!record || width <= 0 || height <= 0) {
        return false;
    }

    const auto bytes64 = static_cast<LONG64>(width) * static_cast<LONG64>(height) * 4;
    if (bytes64 <= 0 || bytes64 > 64ll * 1024ll * 1024ll) {
        return false;
    }

    const size_t bytes = static_cast<size_t>(bytes64);
    if (record->bytes != bytes) {
        void* resized = std::realloc(record->rgba, bytes);
        if (!resized) {
            return false;
        }
        record->rgba = static_cast<uint8_t*>(resized);
        record->bytes = bytes;
    }

    record->width = width;
    record->height = height;
    record->ready = true;
    record->hasPixels = false;
    record->cpuPixels = false;
    record->lastUploadSerial = 0;
    std::memset(record->rgba, 0, record->bytes);
    return true;
}

static bool GrowTexturePreserving(TextureRecord* record, GLsizei width, GLsizei height) {
    if (!record || width <= 0 || height <= 0) {
        return false;
    }

    if (record->ready &&
        record->rgba &&
        record->width >= width &&
        record->height >= height) {
        return true;
    }

    const auto bytes64 = static_cast<LONG64>(width) * static_cast<LONG64>(height) * 4;
    if (bytes64 <= 0 || bytes64 > 64ll * 1024ll * 1024ll) {
        return false;
    }

    const size_t bytes = static_cast<size_t>(bytes64);
    auto* grown = static_cast<uint8_t*>(std::malloc(bytes));
    if (!grown) {
        return false;
    }
    std::memset(grown, 0, bytes);

    if (record->rgba && record->width > 0 && record->height > 0) {
        const int copyWidth = record->width < width ? record->width : width;
        const int copyHeight = record->height < height ? record->height : height;
        for (int y = 0; y < copyHeight; ++y) {
            std::memcpy(
                grown + (static_cast<size_t>(y) * width) * 4,
                record->rgba + (static_cast<size_t>(y) * record->width) * 4,
                static_cast<size_t>(copyWidth) * 4);
        }
    }

    std::free(record->rgba);
    record->rgba = grown;
    record->bytes = bytes;
    record->width = width;
    record->height = height;
    record->ready = true;
    return true;
}

static uint8_t ClampByteFromFloat(float value) {
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return 255;
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

static bool ReadTexturePixelRgba(
    const uint8_t* src,
    GLenum format,
    GLenum type,
    uint8_t& r,
    uint8_t& g,
    uint8_t& b,
    uint8_t& a) {
    if (!src || type != GL_UNSIGNED_BYTE) {
        return false;
    }

    if (format == GL_RGBA) {
        r = src[0]; g = src[1]; b = src[2]; a = src[3];
        return true;
    }
    if (format == GL_BGRA) {
        r = src[2]; g = src[1]; b = src[0]; a = src[3];
        return true;
    }
    if (format == GL_RGB) {
        r = src[0]; g = src[1]; b = src[2]; a = 255;
        return true;
    }
    if (format == GL_RG) {
        r = src[0]; g = src[0]; b = src[0]; a = src[1];
        return true;
    }
    if (format == GL_RED) {
        r = src[0]; g = src[0]; b = src[0]; a = 255;
        return true;
    }

    return false;
}

static bool Is2DLikeTextureTarget(GLenum target) {
    return target == GL_TEXTURE_2D ||
        target == GL_TEXTURE_2D_ARRAY ||
        target == GL_TEXTURE_3D;
}

static GLuint BoundTextureNameForTarget(GLenum target) {
    if (g_activeTextureUnit >= 32) {
        return 0;
    }

    if (target == GL_TEXTURE_2D) {
        return g_boundTexture2D[g_activeTextureUnit];
    }
    if (target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_3D) {
        return g_boundTexture2DArray[g_activeTextureUnit];
    }
    return 0;
}

static void StoreTextureRegionForName(
    GLuint textureName,
    GLint level,
    GLint xoffset,
    GLint yoffset,
    GLsizei width,
    GLsizei height,
    GLenum format,
    GLenum type,
    const void* pixels,
    bool allocateWholeTexture) {
    if (!IsCommandBridgeEnabled()) {
        return;
    }

    const bool subUpload = !allocateWholeTexture;
    if (textureName == 0) {
        PublishTextureStoreAttempt(
            textureName, level, xoffset, yoffset, width, height, format, type, subUpload, TextureStoreNoTexture);
        return;
    }
    if (level != 0) {
        PublishTextureStoreAttempt(
            textureName, level, xoffset, yoffset, width, height, format, type, subUpload, TextureStoreBadLevel);
        return;
    }

    auto* texture = FindTexture(textureName, true);
    if (!texture) {
        PublishTextureStoreAttempt(
            textureName, level, xoffset, yoffset, width, height, format, type, subUpload, TextureStoreNoRecord);
        return;
    }
    if (width <= 0 || height <= 0) {
        PublishTextureStoreAttempt(
            textureName, level, xoffset, yoffset, width, height, format, type, subUpload, TextureStoreBadDimensions);
        return;
    }

    if (allocateWholeTexture) {
        PublishTextureAllocation(texture, width, height);
        xoffset = 0;
        yoffset = 0;

        if (!pixels &&
            texture->ready &&
            texture->width >= width &&
            texture->height >= height) {
            if (texture->width != width || texture->height != height) {
                PublishTextureShrinkPreserved();
            }
            PublishTextureInventory(texture);
            return;
        } else {
            if (!ResizeTexture(texture, width, height)) {
                PublishTextureStoreAttempt(
                    textureName, level, xoffset, yoffset, width, height, format, type, subUpload, TextureStoreAllocationFailed);
                return;
            }
            if (!pixels) {
                PublishTextureInventory(texture);
                return;
            }
        }
    } else if (!texture->ready || texture->width <= 0 || texture->height <= 0) {
        if (!ResizeTexture(texture, xoffset + width, yoffset + height)) {
            PublishTextureStoreAttempt(
                textureName, level, xoffset, yoffset, width, height, format, type, subUpload, TextureStoreAllocationFailed);
            return;
        }
        if (!pixels) {
            PublishTextureInventory(texture);
        }
    } else {
        const int requiredWidth = xoffset + width;
        const int requiredHeight = yoffset + height;
        if (requiredWidth > texture->width || requiredHeight > texture->height) {
            const int grownWidth = requiredWidth > texture->width ? requiredWidth : texture->width;
            const int grownHeight = requiredHeight > texture->height ? requiredHeight : texture->height;
            if (!GrowTexturePreserving(texture, grownWidth, grownHeight)) {
                PublishTextureStoreAttempt(
                    textureName, level, xoffset, yoffset, width, height, format, type, subUpload, TextureStoreAllocationFailed);
                return;
            }
            PublishTextureInventory(texture);
        }
    }

    if (width <= 0 || height <= 0) {
        PublishTextureStoreAttempt(
            textureName, level, xoffset, yoffset, width, height, format, type, subUpload, TextureStoreBadDimensions);
        return;
    }

    const int components = ComponentsForFormat(format);
    const int bytesPerComponent = BytesForType(type);
    if (bytesPerComponent != 1 ||
        !IsReadableTextureFormat(format)) {
        PublishTextureStoreAttempt(
            textureName, level, xoffset, yoffset, width, height, format, type, subUpload, TextureStoreUnsupportedFormat);
        return;
    }

    size_t srcStartOffset = 0;
    size_t srcRowStride = 0;
    size_t requiredBytes = 0;
    if (!ComputeTextureSourceLayout(
        width,
        height,
        format,
        type,
        srcStartOffset,
        srcRowStride,
        requiredBytes)) {
        PublishTextureStoreAttempt(
            textureName, level, xoffset, yoffset, width, height, format, type, subUpload, TextureStoreBadLayout);
        return;
    }

    const uint8_t* src = ResolvePixelDataPointer(pixels, requiredBytes);
    if (!src) {
        PublishTextureStoreAttempt(
            textureName, level, xoffset, yoffset, width, height, format, type, subUpload, TextureStoreMissingPixels);
        return;
    }
    src += srcStartOffset;

    for (GLsizei y = 0; y < height; ++y) {
        const int dstY = yoffset + y;
        if (dstY < 0 || dstY >= texture->height) {
            continue;
        }

        for (GLsizei x = 0; x < width; ++x) {
            const int dstX = xoffset + x;
            if (dstX < 0 || dstX >= texture->width) {
                continue;
            }

            uint8_t r = 0, g = 0, b = 0, a = 255;
            const uint8_t* srcPixel =
                src +
                (static_cast<size_t>(y) * srcRowStride) +
                (static_cast<size_t>(x) * static_cast<size_t>(components));
            if (!ReadTexturePixelRgba(srcPixel, format, type, r, g, b, a)) {
                continue;
            }

            uint8_t* dst = texture->rgba + (static_cast<size_t>(dstY) * texture->width + dstX) * 4;
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst[3] = a;
        }
    }

    texture->hasPixels = true;
    texture->cpuPixels = true;
    texture->lastUploadSerial = ++g_textureWriteSerial;
    PublishTextureStoreAttempt(
        textureName, level, xoffset, yoffset, width, height, format, type, subUpload, TextureStoreAccepted);
    PublishTextureInventory(texture);
}

static void StoreTextureRegion(
    GLenum target,
    GLint level,
    GLint xoffset,
    GLint yoffset,
    GLsizei width,
    GLsizei height,
    GLenum format,
    GLenum type,
    const void* pixels,
    bool allocateWholeTexture) {
    if (!Is2DLikeTextureTarget(target)) {
        return;
    }

    StoreTextureRegionForName(
        BoundTextureNameForTarget(target),
        level,
        xoffset,
        yoffset,
        width,
        height,
        format,
        type,
        pixels,
        allocateWholeTexture);
}

static unsigned int PackColor(float r, float g, float b, float a) {
    return (static_cast<unsigned int>(ClampByteFromFloat(a)) << 24) |
        (static_cast<unsigned int>(ClampByteFromFloat(r)) << 16) |
        (static_cast<unsigned int>(ClampByteFromFloat(g)) << 8) |
        static_cast<unsigned int>(ClampByteFromFloat(b));
}

static int ClampInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static void FillRgbaBuffer(uint8_t* rgba, int width, int height, unsigned int packedColor) {
    if (!rgba || width <= 0 || height <= 0) {
        return;
    }

    const int pixelCount = width * height;
    const uint8_t r = static_cast<uint8_t>((packedColor >> 16) & 0xFF);
    const uint8_t g = static_cast<uint8_t>((packedColor >> 8) & 0xFF);
    const uint8_t b = static_cast<uint8_t>(packedColor & 0xFF);
    const uint8_t a = static_cast<uint8_t>((packedColor >> 24) & 0xFF);
    for (int i = 0; i < pixelCount; ++i) {
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = a;
    }
}

static void SharedGuiFramebufferSize(int& width, int& height) {
    const int viewportWidth = g_viewport[2] > 0 ? g_viewport[2] : 1920;
    const int viewportHeight = g_viewport[3] > 0 ? g_viewport[3] : 1080;
    const float scale = viewportWidth > 0 && viewportHeight > 0
        ? ((viewportWidth > 960 || viewportHeight > 540)
            ? ((960.0f / viewportWidth) < (540.0f / viewportHeight)
                ? (960.0f / viewportWidth)
                : (540.0f / viewportHeight))
            : 1.0f)
        : 0.5f;
    width = ClampInt(static_cast<int>(viewportWidth * scale), 1, static_cast<int>(kMinecraftXboxGuiFramebufferMaxWidth));
    height = ClampInt(static_cast<int>(viewportHeight * scale), 1, static_cast<int>(kMinecraftXboxGuiFramebufferMaxHeight));
}

static void PrepareDefaultGuiFramebuffer(unsigned int packedColor, bool forceClear) {
    int fbWidth = 0;
    int fbHeight = 0;
    SharedGuiFramebufferSize(fbWidth, fbHeight);
    if (!g_defaultGuiFramebufferReady ||
        g_defaultGuiFramebufferWidth != fbWidth ||
        g_defaultGuiFramebufferHeight != fbHeight ||
        forceClear) {
        g_defaultGuiFramebufferWidth = fbWidth;
        g_defaultGuiFramebufferHeight = fbHeight;
        FillRgbaBuffer(g_defaultGuiFramebufferRgba, fbWidth, fbHeight, packedColor);
        g_defaultGuiFramebufferReady = true;
    }
}

struct GuiFrameQuality {
    bool valid;
    LONG64 pixels;
    LONG64 nonBlackPixels;
    LONG64 nonTransparentPixels;
    LONG64 averageRed;
    LONG64 averageGreen;
    LONG64 averageBlue;
};

static constexpr unsigned int kGuiCandidateAccepted = 0;
static constexpr unsigned int kGuiCandidateInvalid = 1;
static constexpr unsigned int kGuiCandidateTooSparse = 2;
static constexpr unsigned int kGuiCandidateNearlyWhite = 3;
static constexpr unsigned int kGuiCandidateNearlyBlack = 4;
static constexpr unsigned int kGuiCandidateMuchLessFilled = 5;
static constexpr unsigned int kGuiCandidateTinyAfterGood = 6;

static GuiFrameQuality MeasureGuiFrameQuality(const uint8_t* rgba, int width, int height) {
    GuiFrameQuality quality = {};
    if (!rgba || width <= 0 || height <= 0) {
        return quality;
    }

    quality.valid = true;
    quality.pixels = static_cast<LONG64>(width) * static_cast<LONG64>(height);
    LONG64 sumRed = 0;
    LONG64 sumGreen = 0;
    LONG64 sumBlue = 0;
    for (LONG64 i = 0; i < quality.pixels; ++i) {
        const uint8_t* pixel = rgba + static_cast<size_t>(i) * 4;
        const uint8_t r = pixel[0];
        const uint8_t g = pixel[1];
        const uint8_t b = pixel[2];
        const uint8_t a = pixel[3];
        if (r != 0 || g != 0 || b != 0) {
            ++quality.nonBlackPixels;
        }
        if (a != 0) {
            ++quality.nonTransparentPixels;
        }
        sumRed += r;
        sumGreen += g;
        sumBlue += b;
    }

    if (quality.pixels > 0) {
        quality.averageRed = sumRed / quality.pixels;
        quality.averageGreen = sumGreen / quality.pixels;
        quality.averageBlue = sumBlue / quality.pixels;
    }
    return quality;
}

static unsigned int ClassifyPoorGuiFrame(const GuiFrameQuality& quality) {
    if (!quality.valid || quality.pixels <= 0) {
        return kGuiCandidateInvalid;
    }

    const bool mostlyOpaque =
        quality.nonTransparentPixels >= ((quality.pixels * 95) / 100);
    const bool mostlyFilled =
        quality.nonBlackPixels >= ((quality.pixels * 95) / 100);
    const bool tooSparse =
        quality.nonTransparentPixels <= (quality.pixels / 100) ||
        (quality.nonBlackPixels <= (quality.pixels / 100) &&
            quality.averageRed <= 8 &&
            quality.averageGreen <= 8 &&
            quality.averageBlue <= 8);
    const bool nearlyWhite =
        mostlyOpaque &&
        mostlyFilled &&
        quality.averageRed >= 245 &&
        quality.averageGreen >= 245 &&
        quality.averageBlue >= 245;
    const bool nearlyBlack =
        mostlyOpaque &&
        quality.nonBlackPixels <= (quality.pixels / 100) &&
        quality.averageRed <= 8 &&
        quality.averageGreen <= 8 &&
        quality.averageBlue <= 8;

    if (tooSparse) {
        return kGuiCandidateTooSparse;
    }

    if (nearlyWhite) {
        return kGuiCandidateNearlyWhite;
    }

    if (nearlyBlack) {
        return kGuiCandidateNearlyBlack;
    }

    return kGuiCandidateAccepted;
}

static bool ShouldAcceptGuiFrame(const GuiFrameQuality& quality, unsigned int* rejectReason) {
    unsigned int reason = ClassifyPoorGuiFrame(quality);
    if (reason != kGuiCandidateAccepted) {
        if (rejectReason) {
            *rejectReason = reason;
        }
        return false;
    }

    if (!g_haveAcceptedGuiFramebuffer) {
        return true;
    }

    const bool muchLessOpaque =
        g_lastAcceptedGuiNonTransparentPixels > 0 &&
        quality.nonTransparentPixels < (g_lastAcceptedGuiNonTransparentPixels / 4);
    const bool muchLessFilled =
        g_lastAcceptedGuiNonBlackPixels > 0 &&
        quality.nonBlackPixels < (g_lastAcceptedGuiNonBlackPixels / 4);
    if (muchLessOpaque && muchLessFilled) {
        if (rejectReason) {
            *rejectReason = kGuiCandidateMuchLessFilled;
        }
        return false;
    }

    const bool tinyAfterGoodFrame =
        g_lastAcceptedGuiPixels > 0 &&
        quality.nonTransparentPixels < (g_lastAcceptedGuiPixels / 100);
    if (tinyAfterGoodFrame) {
        if (rejectReason) {
            *rejectReason = kGuiCandidateTinyAfterGood;
        }
        return false;
    }

    if (rejectReason) {
        *rejectReason = kGuiCandidateAccepted;
    }
    return true;
}

static void PublishGuiCandidateDiagnostic(
    unsigned int sourceTextureName,
    int sourceWidth,
    int sourceHeight,
    int candidateWidth,
    int candidateHeight,
    const GuiFrameQuality& quality,
    bool accepted,
    unsigned int rejectReason) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    state->guiCandidateLastSourceTextureName = sourceTextureName;
    state->guiCandidateLastSourceWidth = sourceWidth;
    state->guiCandidateLastSourceHeight = sourceHeight;
    state->guiCandidateLastWidth = candidateWidth;
    state->guiCandidateLastHeight = candidateHeight;
    state->guiCandidateLastPixels = quality.pixels;
    state->guiCandidateLastNonBlackPixels = quality.nonBlackPixels;
    state->guiCandidateLastNonTransparentPixels = quality.nonTransparentPixels;
    state->guiCandidateLastAverageRed = quality.averageRed;
    state->guiCandidateLastAverageGreen = quality.averageGreen;
    state->guiCandidateLastAverageBlue = quality.averageBlue;
    state->guiCandidateLastRejectReason = rejectReason;
    if (accepted) {
        state->guiCandidateAcceptedSourceTextureName = sourceTextureName;
        state->guiCandidateAcceptedSourceWidth = sourceWidth;
        state->guiCandidateAcceptedSourceHeight = sourceHeight;
        InterlockedIncrement64(&state->guiCandidateAcceptedSerial);
    } else {
        InterlockedIncrement64(&state->guiCandidateRejectedSerial);
    }
}

static void RememberAcceptedGuiFrame(const GuiFrameQuality& quality) {
    if (!quality.valid) {
        return;
    }

    g_haveAcceptedGuiFramebuffer = true;
    g_lastAcceptedGuiPixels = quality.pixels;
    g_lastAcceptedGuiNonBlackPixels = quality.nonBlackPixels;
    g_lastAcceptedGuiNonTransparentPixels = quality.nonTransparentPixels;
}

static void PublishDefaultGuiFramebuffer() {
    auto* state = EnsureGlCommandState();
    if (!state ||
        !g_defaultGuiFramebufferReady ||
        g_defaultGuiFramebufferWidth <= 0 ||
        g_defaultGuiFramebufferHeight <= 0) {
        return;
    }

    const int pixelCount = g_defaultGuiFramebufferWidth * g_defaultGuiFramebufferHeight;
    const size_t bytes = static_cast<size_t>(pixelCount) * 4;
    const GuiFrameQuality quality = MeasureGuiFrameQuality(
        g_defaultGuiFramebufferRgba,
        g_defaultGuiFramebufferWidth,
        g_defaultGuiFramebufferHeight);
    unsigned int rejectReason = kGuiCandidateAccepted;
    const bool accepted = ShouldAcceptGuiFrame(quality, &rejectReason);
    PublishGuiCandidateDiagnostic(
        0,
        g_defaultGuiFramebufferWidth,
        g_defaultGuiFramebufferHeight,
        g_defaultGuiFramebufferWidth,
        g_defaultGuiFramebufferHeight,
        quality,
        accepted,
        rejectReason);
    if (!accepted) {
        return;
    }

    InterlockedExchange(&state->guiFramebufferReady, 0);
    state->guiFramebufferWidth = g_defaultGuiFramebufferWidth;
    state->guiFramebufferHeight = g_defaultGuiFramebufferHeight;
    state->guiFramebufferBytes = static_cast<LONG64>(bytes);
    std::memcpy(state->guiFramebufferRgba, g_defaultGuiFramebufferRgba, bytes);
    MemoryBarrier();
    InterlockedIncrement64(&state->guiFramebufferSerial);
    InterlockedExchange(&state->guiFramebufferReady, 1);
    RememberAcceptedGuiFrame(quality);
}

static void ClearTextureRecord(TextureRecord* texture, unsigned int packedColor) {
    if (!texture || !texture->ready || !texture->rgba || texture->width <= 0 || texture->height <= 0) {
        return;
    }

    const uint8_t r = static_cast<uint8_t>((packedColor >> 16) & 0xFF);
    const uint8_t g = static_cast<uint8_t>((packedColor >> 8) & 0xFF);
    const uint8_t b = static_cast<uint8_t>(packedColor & 0xFF);
    const uint8_t a = static_cast<uint8_t>((packedColor >> 24) & 0xFF);
    const size_t pixelCount = static_cast<size_t>(texture->width) * static_cast<size_t>(texture->height);
    for (size_t i = 0; i < pixelCount; ++i) {
        texture->rgba[i * 4 + 0] = r;
        texture->rgba[i * 4 + 1] = g;
        texture->rgba[i * 4 + 2] = b;
        texture->rgba[i * 4 + 3] = a;
    }

    texture->hasPixels = true;
    if (texture->renderTarget) {
        texture->cpuPixels = false;
    }
    texture->lastUploadSerial = ++g_textureWriteSerial;
    PublishTextureInventory(texture);
}

static void ClearCurrentDrawTarget(unsigned int packedColor) {
    if (!IsCommandBridgeEnabled()) {
        return;
    }

    if (g_boundDrawFramebuffer != 0) {
        ClearTextureRecord(CurrentDrawFramebufferTexture(), packedColor);
        PublishFramebufferState(false, false, false, 0, 0, 0, 0);
        return;
    }

    PrepareDefaultGuiFramebuffer(packedColor, true);
}

struct PixelSource {
    const uint8_t* rgba;
    int width;
    int height;
    TextureRecord* texture;
    bool cpuPixels;
};

static bool ResolveReadPixelSource(PixelSource& source) {
    source = {};
    if (g_boundReadFramebuffer != 0) {
        auto* texture = CurrentReadFramebufferTexture();
        if (!IsTextureUsable(texture)) {
            return false;
        }
        source.rgba = texture->rgba;
        source.width = texture->width;
        source.height = texture->height;
        source.texture = texture;
        source.cpuPixels = texture->cpuPixels;
        return true;
    }

    if (!g_defaultGuiFramebufferReady ||
        g_defaultGuiFramebufferWidth <= 0 ||
        g_defaultGuiFramebufferHeight <= 0) {
        auto* state = EnsureGlCommandState();
        if (!MinecraftXboxIsGlCommandStateReady(state) ||
            state->guiFramebufferReady == 0 ||
            state->guiFramebufferWidth <= 0 ||
            state->guiFramebufferHeight <= 0) {
            return false;
        }

        source.rgba = state->guiFramebufferRgba;
        source.width = state->guiFramebufferWidth;
        source.height = state->guiFramebufferHeight;
        source.texture = nullptr;
        source.cpuPixels = true;
        return true;
    }

    source.rgba = g_defaultGuiFramebufferRgba;
    source.width = g_defaultGuiFramebufferWidth;
    source.height = g_defaultGuiFramebufferHeight;
    source.texture = nullptr;
    source.cpuPixels = true;
    return true;
}

static void CopyPixelSourceToGuiBuffer(
    const PixelSource& source,
    uint8_t* destination,
    int destWidth,
    int destHeight) {
    if (!source.rgba ||
        source.width <= 0 ||
        source.height <= 0 ||
        !destination ||
        destWidth <= 0 ||
        destHeight <= 0) {
        return;
    }

    for (int y = 0; y < destHeight; ++y) {
        const int sy = ClampInt(static_cast<int>((static_cast<LONG64>(y) * source.height) / destHeight), 0, source.height - 1);
        uint8_t* dstRow = destination + static_cast<size_t>(y) * destWidth * 4;
        for (int x = 0; x < destWidth; ++x) {
            const int sx = ClampInt(static_cast<int>((static_cast<LONG64>(x) * source.width) / destWidth), 0, source.width - 1);
            const uint8_t* src = source.rgba + (static_cast<size_t>(sy) * source.width + sx) * 4;
            uint8_t* dst = dstRow + static_cast<size_t>(x) * 4;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }
    }
}

static bool CopyPixelSourceToDefaultGuiFramebuffer(const PixelSource& source) {
    if (!source.rgba || source.width <= 0 || source.height <= 0) {
        return false;
    }

    int destWidth = 0;
    int destHeight = 0;
    SharedGuiFramebufferSize(destWidth, destHeight);
    const size_t bytes = static_cast<size_t>(destWidth) * static_cast<size_t>(destHeight) * 4;
    if (bytes == 0 || bytes > kMinecraftXboxGuiFramebufferMaxBytes) {
        return false;
    }

    CopyPixelSourceToGuiBuffer(source, g_candidateGuiFramebufferRgba, destWidth, destHeight);
    const GuiFrameQuality quality = MeasureGuiFrameQuality(g_candidateGuiFramebufferRgba, destWidth, destHeight);
    unsigned int rejectReason = kGuiCandidateAccepted;
    const bool accepted = ShouldAcceptGuiFrame(quality, &rejectReason);
    PublishGuiCandidateDiagnostic(
        source.texture ? source.texture->name : 0,
        source.width,
        source.height,
        destWidth,
        destHeight,
        quality,
        accepted,
        rejectReason);
    if (!accepted) {
        return false;
    }

    g_defaultGuiFramebufferWidth = destWidth;
    g_defaultGuiFramebufferHeight = destHeight;
    g_defaultGuiFramebufferReady = true;
    std::memcpy(g_defaultGuiFramebufferRgba, g_candidateGuiFramebufferRgba, bytes);
    RememberAcceptedGuiFrame(quality);
    return true;
}

static TextureRecord* SelectFullSizeRenderTargetCandidate(TextureRecord* preferred) {
    TrackFullSizeRenderTargetCandidate(preferred);
    if (IsFullSizeRenderTargetCandidate(g_bestFullSizeRenderTargetCandidate)) {
        return g_bestFullSizeRenderTargetCandidate;
    }

    if (IsFullSizeRenderTargetCandidate(preferred)) {
        return preferred;
    }

    auto* drawTexture = CurrentDrawFramebufferTexture();
    TrackFullSizeRenderTargetCandidate(drawTexture);
    if (IsFullSizeRenderTargetCandidate(g_bestFullSizeRenderTargetCandidate)) {
        return g_bestFullSizeRenderTargetCandidate;
    }

    auto* readTexture = CurrentReadFramebufferTexture();
    TrackFullSizeRenderTargetCandidate(readTexture);
    return IsFullSizeRenderTargetCandidate(g_bestFullSizeRenderTargetCandidate)
        ? g_bestFullSizeRenderTargetCandidate
        : nullptr;
}

static bool TryPublishFullSizeRenderTargetCandidate(TextureRecord* preferred, bool forceDiagnostic = false) {
    auto* texture = SelectFullSizeRenderTargetCandidate(preferred);
    if (!IsFullSizeRenderTargetCandidate(texture)) {
        return false;
    }

    if (!forceDiagnostic &&
        texture->name == g_lastFullSizeRenderTargetProbeName &&
        texture->lastUploadSerial <= g_lastFullSizeRenderTargetProbeSerial) {
        return false;
    }

    g_lastFullSizeRenderTargetProbeName = texture->name;
    g_lastFullSizeRenderTargetProbeSerial = texture->lastUploadSerial;

    PixelSource source = {};
    source.rgba = texture->rgba;
    source.width = texture->width;
    source.height = texture->height;
    source.texture = texture;
    source.cpuPixels = texture->cpuPixels;
    if (!CopyPixelSourceToDefaultGuiFramebuffer(source)) {
        return false;
    }

    PublishDefaultGuiFramebuffer();
    return true;
}

static void CopyPixelSourceToTexture(
    const PixelSource& source,
    TextureRecord* dest,
    int dstX,
    int dstY,
    int dstWidth,
    int dstHeight) {
    if (!source.rgba || source.width <= 0 || source.height <= 0 || !dest) {
        return;
    }

    if (!dest->ready || !dest->rgba || dest->width <= 0 || dest->height <= 0) {
        if (!ResizeTexture(dest, dstWidth > 0 ? dstWidth : source.width, dstHeight > 0 ? dstHeight : source.height)) {
            return;
        }
    }

    dstX = dstX > 0 ? dstX : 0;
    dstY = dstY > 0 ? dstY : 0;
    if (dstX >= dest->width || dstY >= dest->height) {
        return;
    }
    const int copyWidth = ClampInt(dstWidth > 0 ? dstWidth : source.width, 1, dest->width - dstX);
    const int copyHeight = ClampInt(dstHeight > 0 ? dstHeight : source.height, 1, dest->height - dstY);
    if (copyWidth <= 0 || copyHeight <= 0) {
        return;
    }

    for (int y = 0; y < copyHeight; ++y) {
        const int sy = ClampInt(static_cast<int>((static_cast<LONG64>(y) * source.height) / copyHeight), 0, source.height - 1);
        uint8_t* dstRow = dest->rgba + (static_cast<size_t>(dstY + y) * dest->width + dstX) * 4;
        for (int x = 0; x < copyWidth; ++x) {
            const int sx = ClampInt(static_cast<int>((static_cast<LONG64>(x) * source.width) / copyWidth), 0, source.width - 1);
            const uint8_t* src = source.rgba + (static_cast<size_t>(sy) * source.width + sx) * 4;
            uint8_t* dst = dstRow + static_cast<size_t>(x) * 4;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }
    }

    dest->hasPixels = true;
    dest->cpuPixels = source.cpuPixels;
    dest->lastUploadSerial = ++g_textureWriteSerial;
    PublishTextureInventory(dest);
}

static void CopyPixelSourceToSharedGuiFramebuffer(const PixelSource& source) {
    auto* state = EnsureGlCommandState();
    if (!state || !source.rgba || source.width <= 0 || source.height <= 0) {
        return;
    }

    int destWidth = 0;
    int destHeight = 0;
    SharedGuiFramebufferSize(destWidth, destHeight);
    const int pixelCount = destWidth * destHeight;

    InterlockedExchange(&state->guiFramebufferReady, 0);
    state->guiFramebufferWidth = destWidth;
    state->guiFramebufferHeight = destHeight;
    state->guiFramebufferBytes = static_cast<LONG64>(pixelCount) * 4;
    for (int y = 0; y < destHeight; ++y) {
        const int sy = ClampInt(static_cast<int>((static_cast<LONG64>(y) * source.height) / destHeight), 0, source.height - 1);
        uint8_t* dstRow = state->guiFramebufferRgba + static_cast<size_t>(y) * destWidth * 4;
        for (int x = 0; x < destWidth; ++x) {
            const int sx = ClampInt(static_cast<int>((static_cast<LONG64>(x) * source.width) / destWidth), 0, source.width - 1);
            const uint8_t* src = source.rgba + (static_cast<size_t>(sy) * source.width + sx) * 4;
            uint8_t* dst = dstRow + static_cast<size_t>(x) * 4;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }
    }

    MemoryBarrier();
    InterlockedIncrement64(&state->guiFramebufferSerial);
    InterlockedExchange(&state->guiFramebufferReady, 1);
}

static void BlitFramebufferContents(GLuint readFramebuffer, GLuint drawFramebuffer, int width, int height) {
    if (!IsCommandBridgeEnabled()) {
        return;
    }

    const GLuint previousRead = g_boundReadFramebuffer;
    const GLuint previousDraw = g_boundDrawFramebuffer;
    g_boundReadFramebuffer = readFramebuffer;
    g_boundDrawFramebuffer = drawFramebuffer;

    PixelSource source = {};
    GLuint sourceTextureName = 0;
    GLuint destTextureName = 0;
    if (ResolveReadPixelSource(source)) {
        sourceTextureName = source.texture ? source.texture->name : 0;
        auto* destTexture = FramebufferColorTexture(drawFramebuffer);
        if (drawFramebuffer != 0 && destTexture) {
            destTextureName = destTexture->name;
            CopyPixelSourceToTexture(source, destTexture, 0, 0, width, height);
            TryPublishFullSizeRenderTargetCandidate(destTexture);
        } else if (drawFramebuffer == 0) {
            if (CopyPixelSourceToDefaultGuiFramebuffer(source)) {
                PublishDefaultGuiFramebuffer();
            }
            TryPublishFullSizeRenderTargetCandidate(nullptr, true);
        }
    }

    PublishFramebufferState(false, false, true, sourceTextureName, destTextureName, width, height);
    g_boundReadFramebuffer = previousRead;
    g_boundDrawFramebuffer = previousDraw;
}

static void PublishClear(GLbitfield mask) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    state->clearColor[0] = g_clearColor[0];
    state->clearColor[1] = g_clearColor[1];
    state->clearColor[2] = g_clearColor[2];
    state->clearColor[3] = g_clearColor[3];
    state->clearMask = mask;
    InterlockedIncrement64(&state->clearSerial);
    if (mask & GL_COLOR_BUFFER_BIT) {
        ClearCurrentDrawTarget(PackColor(g_clearColor[0], g_clearColor[1], g_clearColor[2], g_clearColor[3]));
    }
}

static void PublishViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    g_viewport[0] = x;
    g_viewport[1] = y;
    g_viewport[2] = width;
    g_viewport[3] = height;

    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    state->viewport[0] = x;
    state->viewport[1] = y;
    state->viewport[2] = width;
    state->viewport[3] = height;
    InterlockedIncrement64(&state->viewportSerial);
}

static void PublishDraw(GLenum mode, GLint first, GLsizei count, GLenum type) {
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

static void PublishTextureBindForUnit(GLenum target, GLuint texture, GLuint unit) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    state->activeTextureUnit = unit;
    state->lastTextureTarget = target;
    state->lastTextureName = texture;
    InterlockedIncrement64(&state->textureBindSerial);
}

static void PublishTextureBind(GLenum target, GLuint texture) {
    PublishTextureBindForUnit(target, texture, g_activeTextureUnit);
}

static void PublishTextureUploadForName(
    bool subUpload,
    GLenum target,
    GLuint textureName,
    GLint level,
    GLint internalFormat,
    GLsizei width,
    GLsizei height,
    GLsizei depth,
    GLenum format,
    GLenum type,
    LONG64 explicitBytes) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    const auto bytes = explicitBytes >= 0
        ? explicitBytes
        : EstimateImageBytes(width, height, depth, format, type);
    state->activeTextureUnit = g_activeTextureUnit;
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

static void PublishTextureUpload(
    bool subUpload,
    GLenum target,
    GLint level,
    GLint internalFormat,
    GLsizei width,
    GLsizei height,
    GLsizei depth,
    GLenum format,
    GLenum type,
    LONG64 explicitBytes) {
    PublishTextureUploadForName(
        subUpload,
        target,
        BoundTextureNameForTarget(target),
        level,
        internalFormat,
        width,
        height,
        depth,
        format,
        type,
        explicitBytes);
}

static bool ConvertTextureToRgba8(
    const void* pixels,
    GLsizei width,
    GLsizei height,
    GLenum format,
    GLenum type,
    size_t sourceRowStride,
    unsigned char* out,
    size_t outCapacity,
    LONG64* outBytes) {
    if (!pixels || !out || !outBytes || width <= 0 || height <= 0 || type != GL_UNSIGNED_BYTE) {
        return false;
    }

    const auto dstBytes64 = static_cast<LONG64>(width) * static_cast<LONG64>(height) * 4;
    if (dstBytes64 <= 0 || static_cast<unsigned long long>(dstBytes64) > outCapacity) {
        return false;
    }

    int components = ComponentsForFormat(format);
    if (!IsReadableTextureFormat(format)) {
        return false;
    }

    const auto* src = static_cast<const unsigned char*>(pixels);
    for (GLsizei y = 0; y < height; ++y) {
        for (GLsizei x = 0; x < width; ++x) {
            const auto srcIndex = (static_cast<size_t>(y) * sourceRowStride) +
                (static_cast<size_t>(x) * components);
            const auto dstIndex = (static_cast<size_t>(y) * width + x) * 4;
            if (format == GL_RGBA) {
                out[dstIndex + 0] = src[srcIndex + 0];
                out[dstIndex + 1] = src[srcIndex + 1];
                out[dstIndex + 2] = src[srcIndex + 2];
                out[dstIndex + 3] = src[srcIndex + 3];
            } else if (format == GL_BGRA) {
                out[dstIndex + 0] = src[srcIndex + 2];
                out[dstIndex + 1] = src[srcIndex + 1];
                out[dstIndex + 2] = src[srcIndex + 0];
                out[dstIndex + 3] = src[srcIndex + 3];
            } else if (format == GL_RGB) {
                out[dstIndex + 0] = src[srcIndex + 0];
                out[dstIndex + 1] = src[srcIndex + 1];
                out[dstIndex + 2] = src[srcIndex + 2];
                out[dstIndex + 3] = 255;
            } else if (format == GL_RG) {
                out[dstIndex + 0] = src[srcIndex + 0];
                out[dstIndex + 1] = src[srcIndex + 0];
                out[dstIndex + 2] = src[srcIndex + 0];
                out[dstIndex + 3] = src[srcIndex + 1];
            } else {
                out[dstIndex + 0] = src[srcIndex + 0];
                out[dstIndex + 1] = src[srcIndex + 0];
                out[dstIndex + 2] = src[srcIndex + 0];
                out[dstIndex + 3] = 255;
            }
        }
    }

    *outBytes = dstBytes64;
    return true;
}

static bool ShouldReplaceTextureSample(MinecraftXboxGlCommandState* state, GLsizei width, GLsizei height) {
    if (!state || width <= 0 || height <= 0) {
        return false;
    }

    if (state->textureSampleReady == 0 ||
        state->textureSampleWidth <= 0 ||
        state->textureSampleHeight <= 0) {
        return true;
    }

    const auto currentPixels =
        static_cast<LONG64>(state->textureSampleWidth) *
        static_cast<LONG64>(state->textureSampleHeight);
    const auto candidatePixels =
        static_cast<LONG64>(width) *
        static_cast<LONG64>(height);

    return candidatePixels >= currentPixels;
}

static void PublishTextureSampleForName(
    GLenum target,
    GLuint textureName,
    GLint level,
    GLsizei width,
    GLsizei height,
    GLenum format,
    GLenum type,
    const void* pixels) {
    if (!IsCommandBridgeEnabled()) {
        return;
    }
    if (!Is2DLikeTextureTarget(target) || level != 0) {
        return;
    }

    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    if (!ShouldReplaceTextureSample(state, width, height)) {
        return;
    }

    InterlockedExchange(&state->textureSampleReady, 0);
    LONG64 sampleBytes = 0;
    size_t srcStartOffset = 0;
    size_t srcRowStride = 0;
    size_t requiredBytes = 0;
    if (!ComputeTextureSourceLayout(
        width,
        height,
        format,
        type,
        srcStartOffset,
        srcRowStride,
        requiredBytes)) {
        return;
    }

    const uint8_t* src = ResolvePixelDataPointer(pixels, requiredBytes);
    if (!src) {
        return;
    }
    src += srcStartOffset;

    if (!ConvertTextureToRgba8(
        src,
        width,
        height,
        format,
        type,
        srcRowStride,
        state->textureSampleRgba,
        kMinecraftXboxTextureSampleMaxBytes,
        &sampleBytes)) {
        return;
    }

    state->textureSampleWidth = width;
    state->textureSampleHeight = height;
    state->textureSampleSourceFormat = format;
    state->textureSampleSourceType = type;
    state->textureSampleTextureName = textureName;
    state->textureSampleBytes = sampleBytes;
    MemoryBarrier();
    InterlockedIncrement64(&state->textureSampleSerial);
    InterlockedExchange(&state->textureSampleReady, 1);
}

static void PublishTextureSample(
    GLenum target,
    GLint level,
    GLsizei width,
    GLsizei height,
    GLenum format,
    GLenum type,
    const void* pixels) {
    PublishTextureSampleForName(
        target,
        BoundTextureNameForTarget(target),
        level,
        width,
        height,
        format,
        type,
        pixels);
}

static void PublishBufferBind(GLenum target, GLuint buffer) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    state->lastBufferTarget = target;
    state->lastBufferName = buffer;
    InterlockedIncrement64(&state->bufferBindSerial);
}

static void PublishBufferUpload(bool subUpload, GLenum target, GLuint buffer, GLsizeiptr size, GLenum usage) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    const auto bytes = PositiveSize(size);
    state->lastBufferTarget = target;
    state->lastBufferName = buffer;
    state->lastBufferSize = bytes;
    state->lastBufferUsage = usage;
    if (bytes > 0) {
        InterlockedAdd64(&state->bufferUploadBytes, bytes);
    }
    InterlockedIncrement64(subUpload ? &state->bufferSubUploadSerial : &state->bufferUploadSerial);
}

static void PublishVertexAttrib(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    state->lastVertexArray = g_boundVertexArray;
    state->lastVertexAttribIndex = index;
    state->lastVertexAttribSize = size;
    state->lastVertexAttribType = type;
    state->lastVertexAttribNormalized = normalized ? 1u : 0u;
    state->lastVertexAttribStride = stride;
    state->lastVertexAttribPointer = reinterpret_cast<unsigned long long>(pointer);
    InterlockedIncrement64(&state->vertexAttribSerial);
}

static void PublishProgramUse(GLuint program) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    state->currentProgram = program;
    InterlockedIncrement64(&state->programUseSerial);
}

static void PublishUniform(GLint location) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    state->lastUniformLocation = location;
    InterlockedIncrement64(&state->uniformSerial);
}

static size_t TypeByteSize(GLenum type) {
    return static_cast<size_t>(BytesForType(type));
}

static int EffectiveStride(const AttribRecord& attrib) {
    if (attrib.separateBinding && attrib.bindingIndex < 16 &&
        g_vertexBindings[attrib.bindingIndex].stride > 0) {
        return g_vertexBindings[attrib.bindingIndex].stride;
    }
    if (attrib.stride > 0) {
        return attrib.stride;
    }
    return attrib.size > 0 ? attrib.size * static_cast<int>(TypeByteSize(attrib.type)) : 0;
}

static const uint8_t* ResolveAttribPointer(const AttribRecord& attrib, int vertexIndex) {
    const int stride = EffectiveStride(attrib);
    if (!attrib.enabled || attrib.size <= 0 || stride <= 0 || vertexIndex < 0) {
        return nullptr;
    }

    if (attrib.separateBinding && attrib.bindingIndex < 16) {
        const auto& binding = g_vertexBindings[attrib.bindingIndex];
        const size_t byteOffset =
            static_cast<size_t>(binding.offset) +
            static_cast<size_t>(attrib.relativeOffset) +
            static_cast<size_t>(vertexIndex) * static_cast<size_t>(stride);
        auto* buffer = FindBuffer(binding.buffer, false);
        if (!buffer || !buffer->data || byteOffset >= buffer->size) {
            return nullptr;
        }
        return buffer->data + byteOffset;
    }

    const size_t byteOffset = static_cast<size_t>(attrib.pointer) + static_cast<size_t>(vertexIndex) * static_cast<size_t>(stride);
    if (attrib.buffer != 0) {
        auto* buffer = FindBuffer(attrib.buffer, false);
        if (!buffer || !buffer->data || byteOffset >= buffer->size) {
            return nullptr;
        }
        return buffer->data + byteOffset;
    }

    if (attrib.pointer < 0x10000) {
        return nullptr;
    }
    return reinterpret_cast<const uint8_t*>(attrib.pointer) + static_cast<size_t>(vertexIndex) * static_cast<size_t>(stride);
}

static float ReadComponentAsFloat(const uint8_t* data, GLenum type, bool normalized) {
    if (!data) {
        return 0.0f;
    }

    switch (type) {
        case GL_FLOAT:
            return *reinterpret_cast<const float*>(data);
        case GL_UNSIGNED_BYTE:
            return normalized ? data[0] / 255.0f : static_cast<float>(data[0]);
        case GL_BYTE:
            return normalized ? (*reinterpret_cast<const int8_t*>(data)) / 127.0f : static_cast<float>(*reinterpret_cast<const int8_t*>(data));
        case GL_UNSIGNED_SHORT:
            return normalized ? (*reinterpret_cast<const uint16_t*>(data)) / 65535.0f : static_cast<float>(*reinterpret_cast<const uint16_t*>(data));
        case GL_SHORT:
            return normalized ? (*reinterpret_cast<const int16_t*>(data)) / 32767.0f : static_cast<float>(*reinterpret_cast<const int16_t*>(data));
        case GL_UNSIGNED_INT:
            return normalized ? (*reinterpret_cast<const uint32_t*>(data)) / 4294967295.0f : static_cast<float>(*reinterpret_cast<const uint32_t*>(data));
        case GL_INT:
            return static_cast<float>(*reinterpret_cast<const int32_t*>(data));
        default:
            return 0.0f;
    }
}

static int FindUvAttribIndex() {
    if (g_attribs[2].enabled && g_attribs[2].size >= 2) {
        return 2;
    }
    for (int i = 1; i < 16; ++i) {
        if (g_attribs[i].enabled && g_attribs[i].size >= 2 && g_attribs[i].type == GL_FLOAT) {
            return i;
        }
    }
    return -1;
}

static int FindColorAttribIndex() {
    if (g_attribs[1].enabled && g_attribs[1].size >= 3) {
        return 1;
    }
    for (int i = 1; i < 16; ++i) {
        if (g_attribs[i].enabled && g_attribs[i].size >= 3 &&
            (g_attribs[i].type == GL_UNSIGNED_BYTE || g_attribs[i].type == GL_FLOAT)) {
            return i;
        }
    }
    return -1;
}

static bool ExtractDrawVertex(int vertexIndex, DrawVertex& out) {
    if (!g_attribs[0].enabled || g_attribs[0].size < 2) {
        return false;
    }

    const uint8_t* pos = ResolveAttribPointer(g_attribs[0], vertexIndex);
    if (!pos) {
        return false;
    }

    const size_t posStep = TypeByteSize(g_attribs[0].type);
    out.x = ReadComponentAsFloat(pos, g_attribs[0].type, g_attribs[0].normalized != 0);
    out.y = ReadComponentAsFloat(pos + posStep, g_attribs[0].type, g_attribs[0].normalized != 0);
    out.z = g_attribs[0].size >= 3
        ? ReadComponentAsFloat(pos + posStep * 2, g_attribs[0].type, g_attribs[0].normalized != 0)
        : 0.0f;
    out.u = 0.0f;
    out.v = 0.0f;
    out.r = 255;
    out.g = 255;
    out.b = 255;
    out.a = 255;
    out.hasUv = false;
    out.hasColor = false;

    const int uvIndex = FindUvAttribIndex();
    if (uvIndex >= 0) {
        const auto& uvAttrib = g_attribs[uvIndex];
        const uint8_t* uv = ResolveAttribPointer(uvAttrib, vertexIndex);
        if (uv) {
            const size_t uvStep = TypeByteSize(uvAttrib.type);
            out.u = ReadComponentAsFloat(uv, uvAttrib.type, uvAttrib.normalized != 0);
            out.v = ReadComponentAsFloat(uv + uvStep, uvAttrib.type, uvAttrib.normalized != 0);
            out.hasUv = true;
        }
    }

    const int colorIndex = FindColorAttribIndex();
    if (colorIndex >= 0) {
        const auto& colorAttrib = g_attribs[colorIndex];
        const uint8_t* color = ResolveAttribPointer(colorAttrib, vertexIndex);
        if (color) {
            const size_t colorStep = TypeByteSize(colorAttrib.type);
            out.r = ClampByteFromFloat(ReadComponentAsFloat(color, colorAttrib.type, colorAttrib.normalized != 0));
            out.g = ClampByteFromFloat(ReadComponentAsFloat(color + colorStep, colorAttrib.type, colorAttrib.normalized != 0));
            out.b = ClampByteFromFloat(ReadComponentAsFloat(color + colorStep * 2, colorAttrib.type, colorAttrib.normalized != 0));
            out.a = colorAttrib.size >= 4
                ? ClampByteFromFloat(ReadComponentAsFloat(color + colorStep * 3, colorAttrib.type, colorAttrib.normalized != 0))
                : 255;
            out.hasColor = true;
        }
    }

    return true;
}

static float Min3(float a, float b, float c) {
    return a < b ? (a < c ? a : c) : (b < c ? b : c);
}

static float Max3(float a, float b, float c) {
    return a > b ? (a > c ? a : c) : (b > c ? b : c);
}

static char LowerAscii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

static bool ContainsNoCase(const char* haystack, const char* needle) {
    if (!haystack || !needle || !needle[0]) {
        return false;
    }

    for (const char* h = haystack; *h; ++h) {
        const char* a = h;
        const char* b = needle;
        while (*a && *b && LowerAscii(*a) == LowerAscii(*b)) {
            ++a;
            ++b;
        }
        if (!*b) {
            return true;
        }
    }

    return false;
}

static bool StartsWithNoCase(const char* haystack, const char* needle) {
    if (!haystack || !needle) {
        return false;
    }

    while (*needle) {
        if (!*haystack || LowerAscii(*haystack) != LowerAscii(*needle)) {
            return false;
        }
        ++haystack;
        ++needle;
    }

    return true;
}

static bool IsSamplerUniformName(const char* name) {
    return ContainsNoCase(name, "sampler") || ContainsNoCase(name, "texture");
}

static int ParseSamplerIndex(const char* name) {
    if (!name) {
        return 0;
    }

    for (const char* p = name; *p; ++p) {
        if (StartsWithNoCase(p, "sampler")) {
            p += 7;
            while (*p && (*p < '0' || *p > '9')) {
                ++p;
            }
            if (*p >= '0' && *p <= '9') {
                int value = 0;
                while (*p >= '0' && *p <= '9') {
                    value = value * 10 + (*p - '0');
                    ++p;
                }
                return value;
            }
            return 0;
        }
    }

    return 0;
}

static UniformRecord* FindUniformByLocation(GLint location) {
    if (location < 0) {
        return nullptr;
    }

    for (auto& uniform : g_uniforms) {
        if (uniform.location == location && uniform.location != 0) {
            return &uniform;
        }
    }

    return nullptr;
}

static UniformRecord* FindUniformByProgramName(GLuint program, const char* name) {
    if (!name) {
        return nullptr;
    }

    for (auto& uniform : g_uniforms) {
        if (uniform.location != 0 &&
            uniform.program == program &&
            std::strncmp(uniform.name, name, sizeof(uniform.name)) == 0) {
            return &uniform;
        }
    }

    return nullptr;
}

static UniformRecord* CreateUniformRecord(GLuint program, const char* name) {
    if (!name) {
        return nullptr;
    }

    for (auto& uniform : g_uniforms) {
        if (uniform.location == 0) {
            uniform.location = g_nextUniformLocation++;
            uniform.program = program;
            strncpy_s(uniform.name, name, _TRUNCATE);
            uniform.sampler = IsSamplerUniformName(name);
            uniform.samplerIndex = ParseSamplerIndex(name);
            uniform.value = 0;
            uniform.valueSet = false;
            uniform.updateSerial = 0;
            return &uniform;
        }
    }

    return nullptr;
}

static void UpdateSamplerUniform(GLint location, GLint unit) {
    if (unit < 0 || unit >= 32) {
        return;
    }

    auto* uniform = FindUniformByLocation(location);
    if (uniform && uniform->sampler) {
        uniform->value = unit;
        uniform->valueSet = true;
        uniform->updateSerial = ++g_uniformUpdateSerial;
        g_lastSamplerTextureUnit = static_cast<GLuint>(unit);
        return;
    }

    if (!uniform && location == 0) {
        g_lastSamplerTextureUnit = static_cast<GLuint>(unit);
    }
}

static TextureRecord* CurrentProgramSamplerTexture(int samplerIndex) {
    TextureRecord* latest = nullptr;
    uint64_t latestSerial = 0;

    for (auto& uniform : g_uniforms) {
        if (uniform.location == 0 ||
            uniform.program != g_currentProgram ||
            !uniform.sampler ||
            !uniform.valueSet ||
            uniform.value < 0 ||
            uniform.value >= 32) {
            continue;
        }

        auto* texture = TextureForUnit(static_cast<GLuint>(uniform.value));
        if (!texture) {
            continue;
        }

        if (uniform.samplerIndex == samplerIndex) {
            return texture;
        }

        if (!latest || uniform.updateSerial > latestSerial) {
            latest = texture;
            latestSerial = uniform.updateSerial;
        }
    }

    return latest;
}

static bool TriangleUsesNormalizedUv(
    const DrawVertex& a,
    const DrawVertex& b,
    const DrawVertex& c) {
    const float minU = Min3(a.u, b.u, c.u);
    const float maxU = Max3(a.u, b.u, c.u);
    const float minV = Min3(a.v, b.v, c.v);
    const float maxV = Max3(a.v, b.v, c.v);
    return minU >= -0.05f && minV >= -0.05f && maxU <= 1.05f && maxV <= 1.05f;
}

static TextureRecord* SelectTextureForTriangle(
    const DrawVertex& a,
    const DrawVertex& b,
    const DrawVertex& c) {
    if (!a.hasUv || !b.hasUv || !c.hasUv) {
        return nullptr;
    }

    TextureRecord* candidates[32] = {};
    int candidateCount = 0;
    AddTextureCandidate(candidates, candidateCount, CurrentProgramSamplerTexture(0));
    AddTextureCandidate(candidates, candidateCount, TextureForUnit(g_lastSamplerTextureUnit));
    AddTextureCandidate(candidates, candidateCount, TextureForUnit(0));
    AddTextureCandidate(candidates, candidateCount, TextureForUnit(g_activeTextureUnit));
    AddTextureCandidate(candidates, candidateCount, FindBestKnownGuiTexture());
    for (GLuint unit = 0; unit < 32; ++unit) {
        AddTextureCandidate(candidates, candidateCount, TextureForUnit(unit));
    }

    if (candidateCount == 0) {
        return nullptr;
    }

    const float maxU = Max3(a.u, b.u, c.u);
    const float maxV = Max3(a.v, b.v, c.v);
    if (TriangleUsesNormalizedUv(a, b, c)) {
        for (int i = 0; i < candidateCount; ++i) {
            if (candidates[i]->width == 1024 && candidates[i]->height == 1024) {
                return candidates[i];
            }
        }

        TextureRecord* largestGuiLike = nullptr;
        LONG64 largestPixels = 0;
        for (int i = 0; i < candidateCount; ++i) {
            auto* texture = candidates[i];
            if (texture->width < 512 || texture->height < 256) {
                continue;
            }

            const LONG64 pixels =
                static_cast<LONG64>(texture->width) *
                static_cast<LONG64>(texture->height);
            if (!largestGuiLike || pixels > largestPixels) {
                largestGuiLike = texture;
                largestPixels = pixels;
            }
        }

        if (largestGuiLike) {
            return largestGuiLike;
        }
    }

    if (maxU > 2.0f || maxV > 2.0f) {
        TextureRecord* bestFit = nullptr;
        LONG64 bestPixels = 0;
        for (int i = 0; i < candidateCount; ++i) {
            auto* texture = candidates[i];
            if (texture->width <= maxU || texture->height <= maxV) {
                continue;
            }

            const LONG64 pixels =
                static_cast<LONG64>(texture->width) *
                static_cast<LONG64>(texture->height);
            if (!bestFit || pixels < bestPixels) {
                bestFit = texture;
                bestPixels = pixels;
            }
        }

        if (bestFit) {
            return bestFit;
        }
    }

    TextureRecord* largestFallback = nullptr;
    LONG64 largestPixels = 0;
    for (int i = 0; i < candidateCount; ++i) {
        auto* texture = candidates[i];
        if (texture->width < 512 || texture->height < 256) {
            continue;
        }

        const LONG64 pixels =
            static_cast<LONG64>(texture->width) *
            static_cast<LONG64>(texture->height);
        if (!largestFallback || pixels > largestPixels) {
            largestFallback = texture;
            largestPixels = pixels;
        }
    }

    if (largestFallback) {
        return largestFallback;
    }

    return candidates[0];
}

static float Edge(float ax, float ay, float bx, float by, float cx, float cy) {
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

struct RasterTarget {
    uint8_t* rgba;
    int width;
    int height;
    TextureRecord* texture;
    bool sharedGui;
};

static bool ResolveCurrentRasterTarget(RasterTarget& target) {
    target = {};
    if (g_boundDrawFramebuffer != 0) {
        auto* texture = CurrentDrawFramebufferTexture();
        if (!texture || !texture->ready || !texture->rgba || texture->width <= 0 || texture->height <= 0) {
            return false;
        }

        target.rgba = texture->rgba;
        target.width = texture->width;
        target.height = texture->height;
        target.texture = texture;
        target.sharedGui = false;
        return true;
    }

    PrepareDefaultGuiFramebuffer(PackColor(g_clearColor[0], g_clearColor[1], g_clearColor[2], g_clearColor[3]), false);
    if (!g_defaultGuiFramebufferReady ||
        g_defaultGuiFramebufferWidth <= 0 ||
        g_defaultGuiFramebufferHeight <= 0) {
        return false;
    }

    target.rgba = g_defaultGuiFramebufferRgba;
    target.width = g_defaultGuiFramebufferWidth;
    target.height = g_defaultGuiFramebufferHeight;
    target.texture = nullptr;
    target.sharedGui = true;
    return true;
}

static void MapVertexToFramebuffer(
    const DrawVertex& in,
    int fbWidth,
    int fbHeight,
    float& x,
    float& y) {
    const int viewportWidth = g_viewport[2] > 0 ? g_viewport[2] : fbWidth;
    const int viewportHeight = g_viewport[3] > 0 ? g_viewport[3] : fbHeight;
    if (in.x >= -1.5f && in.x <= 1.5f && in.y >= -1.5f && in.y <= 1.5f) {
        x = (in.x * 0.5f + 0.5f) * static_cast<float>(fbWidth);
        y = (0.5f - in.y * 0.5f) * static_cast<float>(fbHeight);
        return;
    }

    x = in.x * static_cast<float>(fbWidth) / static_cast<float>(viewportWidth);
    y = in.y * static_cast<float>(fbHeight) / static_cast<float>(viewportHeight);
}

static void BlendGuiPixel(uint8_t* dst, uint8_t srcR, uint8_t srcG, uint8_t srcB, uint8_t srcA) {
    if (srcA == 0) {
        return;
    }
    if (srcA == 255) {
        dst[0] = srcR;
        dst[1] = srcG;
        dst[2] = srcB;
        dst[3] = 255;
        return;
    }

    const unsigned invA = 255u - srcA;
    dst[0] = static_cast<uint8_t>((srcR * srcA + dst[0] * invA) / 255u);
    dst[1] = static_cast<uint8_t>((srcG * srcA + dst[1] * invA) / 255u);
    dst[2] = static_cast<uint8_t>((srcB * srcA + dst[2] * invA) / 255u);
    dst[3] = 255;
}

enum class GuiTriangleResult {
    None,
    Textured,
    Color
};

static GuiTriangleResult RasterizeGuiTriangle(
    TextureRecord* texture,
    const DrawVertex& a,
    const DrawVertex& b,
    const DrawVertex& c) {
    RasterTarget target = {};
    const bool useTexture = IsTextureUsable(texture) && a.hasUv && b.hasUv && c.hasUv;
    const bool useVertexColor = a.hasColor || b.hasColor || c.hasColor;
    if (!useTexture && !useVertexColor) {
        return GuiTriangleResult::None;
    }
    if (!ResolveCurrentRasterTarget(target)) {
        return GuiTriangleResult::None;
    }

    const int fbWidth = target.width;
    const int fbHeight = target.height;
    float ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
    MapVertexToFramebuffer(a, fbWidth, fbHeight, ax, ay);
    MapVertexToFramebuffer(b, fbWidth, fbHeight, bx, by);
    MapVertexToFramebuffer(c, fbWidth, fbHeight, cx, cy);

    const float area = Edge(ax, ay, bx, by, cx, cy);
    if (area > -0.0001f && area < 0.0001f) {
        return GuiTriangleResult::None;
    }

    const int minX = ClampInt(static_cast<int>(Min3(ax, bx, cx)), 0, fbWidth - 1);
    const int maxX = ClampInt(static_cast<int>(Max3(ax, bx, cx) + 1.0f), 0, fbWidth - 1);
    const int minY = ClampInt(static_cast<int>(Min3(ay, by, cy)), 0, fbHeight - 1);
    const int maxY = ClampInt(static_cast<int>(Max3(ay, by, cy) + 1.0f), 0, fbHeight - 1);
    if (maxX < minX || maxY < minY) {
        return GuiTriangleResult::None;
    }

    bool wrotePixel = false;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const float w0 = Edge(bx, by, cx, cy, px, py) / area;
            const float w1 = Edge(cx, cy, ax, ay, px, py) / area;
            const float w2 = Edge(ax, ay, bx, by, px, py) / area;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                continue;
            }

            const uint8_t vertR = static_cast<uint8_t>(a.r * w0 + b.r * w1 + c.r * w2);
            const uint8_t vertG = static_cast<uint8_t>(a.g * w0 + b.g * w1 + c.g * w2);
            const uint8_t vertB = static_cast<uint8_t>(a.b * w0 + b.b * w1 + c.b * w2);
            const uint8_t vertA = static_cast<uint8_t>(a.a * w0 + b.a * w1 + c.a * w2);
            uint8_t srcR = vertR;
            uint8_t srcG = vertG;
            uint8_t srcB = vertB;
            uint8_t srcA = vertA;
            if (useTexture) {
                float u = a.u * w0 + b.u * w1 + c.u * w2;
                float v = a.v * w0 + b.v * w1 + c.v * w2;
                if (u > 2.0f || v > 2.0f) {
                    u /= static_cast<float>(texture->width);
                    v /= static_cast<float>(texture->height);
                }
                const int tx = ClampInt(static_cast<int>(u * static_cast<float>(texture->width - 1) + 0.5f), 0, texture->width - 1);
                const int ty = ClampInt(static_cast<int>(v * static_cast<float>(texture->height - 1) + 0.5f), 0, texture->height - 1);
                const uint8_t* texel = texture->rgba + (static_cast<size_t>(ty) * texture->width + tx) * 4;
                srcR = static_cast<uint8_t>((static_cast<unsigned>(texel[0]) * vertR) / 255u);
                srcG = static_cast<uint8_t>((static_cast<unsigned>(texel[1]) * vertG) / 255u);
                srcB = static_cast<uint8_t>((static_cast<unsigned>(texel[2]) * vertB) / 255u);
                srcA = static_cast<uint8_t>((static_cast<unsigned>(texel[3]) * vertA) / 255u);
            }
            uint8_t* dst = target.rgba + (static_cast<size_t>(y) * fbWidth + x) * 4;
            BlendGuiPixel(dst, srcR, srcG, srcB, srcA);
            wrotePixel = true;
        }
    }

    if (!wrotePixel) {
        return GuiTriangleResult::None;
    }

    if (target.texture) {
        target.texture->hasPixels = true;
        target.texture->lastUploadSerial = ++g_textureWriteSerial;
    }

    return useTexture ? GuiTriangleResult::Textured : GuiTriangleResult::Color;
}

static bool ReadElementIndex(GLenum type, const void* indices, int drawIndex, GLuint& outIndex) {
    const uint8_t* direct = static_cast<const uint8_t*>(indices);
    auto* elementBuffer = FindBuffer(g_boundElementArrayBuffer, false);
    if (elementBuffer && elementBuffer->data) {
        const size_t offset = reinterpret_cast<uintptr_t>(indices);
        if (offset >= elementBuffer->size) {
            return false;
        }
        direct = elementBuffer->data + offset;
    } else if (reinterpret_cast<uintptr_t>(indices) < 0x10000) {
        return false;
    }

    if (type == GL_UNSIGNED_INT) {
        outIndex = reinterpret_cast<const uint32_t*>(direct)[drawIndex];
        return true;
    }
    if (type == GL_UNSIGNED_SHORT) {
        outIndex = reinterpret_cast<const uint16_t*>(direct)[drawIndex];
        return true;
    }
    if (type == GL_UNSIGNED_BYTE) {
        outIndex = direct[drawIndex];
        return true;
    }
    return false;
}

static bool ResolveElementVertexIndex(GLuint index, GLint baseVertex, int& outIndex) {
    const long long resolved = static_cast<long long>(index) + static_cast<long long>(baseVertex);
    if (resolved < 0 || resolved > 0x7fffffffll) {
        return false;
    }

    outIndex = static_cast<int>(resolved);
    return true;
}

static void PublishGuiRasterStats(
    GLenum mode,
    GLsizei count,
    TextureRecord* lastTexture,
    LONG64 texturedTriangles,
    LONG64 colorTriangles,
    LONG64 skippedVertices) {
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    state->guiLastDrawMode = mode;
    state->guiLastDrawCount = count;
    state->guiLastTextureName = lastTexture ? lastTexture->name : 0;
    state->guiLastTextureWidth = lastTexture ? lastTexture->width : 0;
    state->guiLastTextureHeight = lastTexture ? lastTexture->height : 0;
    if (texturedTriangles > 0) {
        InterlockedAdd64(&state->guiTexturedTriangleSerial, texturedTriangles);
    }
    if (colorTriangles > 0) {
        InterlockedAdd64(&state->guiColorTriangleSerial, colorTriangles);
    }
    if (skippedVertices > 0) {
        InterlockedAdd64(&state->guiSkippedVertexSerial, skippedVertices);
    }

    if (texturedTriangles > 0 || colorTriangles > 0) {
        if (g_boundDrawFramebuffer == 0) {
            PublishDefaultGuiFramebuffer();
        } else {
            auto* targetTexture = CurrentDrawFramebufferTexture();
            if (targetTexture) {
                PublishTextureInventory(targetTexture);
                PublishFramebufferState(false, false, false, 0, 0, 0, 0);
                TryPublishFullSizeRenderTargetCandidate(targetTexture);
            }
        }
    }
}

static void TrackGuiTriangleResult(
    GuiTriangleResult result,
    TextureRecord* texture,
    TextureRecord*& lastTexture,
    LONG64& texturedTriangles,
    LONG64& colorTriangles) {
    if (result == GuiTriangleResult::Textured) {
        ++texturedTriangles;
        lastTexture = texture;
    } else if (result == GuiTriangleResult::Color) {
        ++colorTriangles;
    }
}

static void PublishGuiDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (!IsCommandBridgeEnabled()) {
        return;
    }

    if (count <= 0 || first < 0) {
        return;
    }

    LONG64 texturedTriangles = 0;
    LONG64 colorTriangles = 0;
    LONG64 skippedVertices = 0;
    TextureRecord* lastTexture = nullptr;
    DrawVertex v0 = {}, v1 = {}, v2 = {};
    if (mode == GL_TRIANGLES) {
        const GLsizei limited = count > 3072 ? 3072 : count;
        for (GLsizei i = 0; i + 2 < limited; i += 3) {
            if (ExtractDrawVertex(first + i, v0) &&
                ExtractDrawVertex(first + i + 1, v1) &&
                ExtractDrawVertex(first + i + 2, v2)) {
                auto* texture = SelectTextureForTriangle(v0, v1, v2);
                TrackGuiTriangleResult(
                    RasterizeGuiTriangle(texture, v0, v1, v2),
                    texture,
                    lastTexture,
                    texturedTriangles,
                    colorTriangles);
            } else {
                ++skippedVertices;
            }
        }
    } else if (mode == GL_TRIANGLE_STRIP) {
        const GLsizei limited = count > 3072 ? 3072 : count;
        for (GLsizei i = 0; i + 2 < limited; ++i) {
            if (ExtractDrawVertex(first + i, v0) &&
                ExtractDrawVertex(first + i + 1, v1) &&
                ExtractDrawVertex(first + i + 2, v2)) {
                auto* texture = SelectTextureForTriangle(v0, v1, v2);
                if (i & 1) {
                    TrackGuiTriangleResult(
                        RasterizeGuiTriangle(texture, v1, v0, v2),
                        texture,
                        lastTexture,
                        texturedTriangles,
                        colorTriangles);
                } else {
                    TrackGuiTriangleResult(
                        RasterizeGuiTriangle(texture, v0, v1, v2),
                        texture,
                        lastTexture,
                        texturedTriangles,
                        colorTriangles);
                }
            } else {
                ++skippedVertices;
            }
        }
    } else if (mode == GL_TRIANGLE_FAN) {
        const GLsizei limited = count > 3072 ? 3072 : count;
        DrawVertex root = {};
        if (!ExtractDrawVertex(first, root)) {
            ++skippedVertices;
            PublishGuiRasterStats(mode, count, lastTexture, texturedTriangles, colorTriangles, skippedVertices);
            return;
        }
        for (GLsizei i = 1; i + 1 < limited; ++i) {
            if (ExtractDrawVertex(first + i, v1) &&
                ExtractDrawVertex(first + i + 1, v2)) {
                auto* texture = SelectTextureForTriangle(root, v1, v2);
                TrackGuiTriangleResult(
                    RasterizeGuiTriangle(texture, root, v1, v2),
                    texture,
                    lastTexture,
                    texturedTriangles,
                    colorTriangles);
            } else {
                ++skippedVertices;
            }
        }
    }

    PublishGuiRasterStats(mode, count, lastTexture, texturedTriangles, colorTriangles, skippedVertices);
}

static void PublishGuiDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint baseVertex = 0) {
    if (!IsCommandBridgeEnabled()) {
        return;
    }

    if (count <= 0) {
        return;
    }

    LONG64 texturedTriangles = 0;
    LONG64 colorTriangles = 0;
    LONG64 skippedVertices = 0;
    TextureRecord* lastTexture = nullptr;
    GLuint i0 = 0, i1 = 0, i2 = 0;
    int vIndex0 = 0, vIndex1 = 0, vIndex2 = 0;
    DrawVertex v0 = {}, v1 = {}, v2 = {};
    if (mode == GL_TRIANGLES) {
        const GLsizei limited = count > 3072 ? 3072 : count;
        for (GLsizei i = 0; i + 2 < limited; i += 3) {
            if (ReadElementIndex(type, indices, i, i0) &&
                ReadElementIndex(type, indices, i + 1, i1) &&
                ReadElementIndex(type, indices, i + 2, i2) &&
                ResolveElementVertexIndex(i0, baseVertex, vIndex0) &&
                ResolveElementVertexIndex(i1, baseVertex, vIndex1) &&
                ResolveElementVertexIndex(i2, baseVertex, vIndex2) &&
                ExtractDrawVertex(vIndex0, v0) &&
                ExtractDrawVertex(vIndex1, v1) &&
                ExtractDrawVertex(vIndex2, v2)) {
                auto* texture = SelectTextureForTriangle(v0, v1, v2);
                TrackGuiTriangleResult(
                    RasterizeGuiTriangle(texture, v0, v1, v2),
                    texture,
                    lastTexture,
                    texturedTriangles,
                    colorTriangles);
            } else {
                ++skippedVertices;
            }
        }
    } else if (mode == GL_TRIANGLE_STRIP) {
        const GLsizei limited = count > 3072 ? 3072 : count;
        for (GLsizei i = 0; i + 2 < limited; ++i) {
            if (ReadElementIndex(type, indices, i, i0) &&
                ReadElementIndex(type, indices, i + 1, i1) &&
                ReadElementIndex(type, indices, i + 2, i2) &&
                ResolveElementVertexIndex(i0, baseVertex, vIndex0) &&
                ResolveElementVertexIndex(i1, baseVertex, vIndex1) &&
                ResolveElementVertexIndex(i2, baseVertex, vIndex2) &&
                ExtractDrawVertex(vIndex0, v0) &&
                ExtractDrawVertex(vIndex1, v1) &&
                ExtractDrawVertex(vIndex2, v2)) {
                auto* texture = SelectTextureForTriangle(v0, v1, v2);
                if (i & 1) {
                    TrackGuiTriangleResult(
                        RasterizeGuiTriangle(texture, v1, v0, v2),
                        texture,
                        lastTexture,
                        texturedTriangles,
                        colorTriangles);
                } else {
                    TrackGuiTriangleResult(
                        RasterizeGuiTriangle(texture, v0, v1, v2),
                        texture,
                        lastTexture,
                        texturedTriangles,
                        colorTriangles);
                }
            } else {
                ++skippedVertices;
            }
        }
    }

    PublishGuiRasterStats(mode, count, lastTexture, texturedTriangles, colorTriangles, skippedVertices);
}

__declspec(dllexport) unsigned long long __stdcall xglGetPresentSignalCount(void) {
    return g_presentSignalCount.load(std::memory_order_relaxed);
}

// -----------------------------------------------------------------------------
// Bespoke GL implementations (must match what's listed in xbox_opengl.def)
// -----------------------------------------------------------------------------

__declspec(dllexport) const GLubyte* __stdcall glGetString(GLenum name) {
    LogGlCallProbe("glGetString");
    if (ForceCompatGlIdentity()) {
        switch (name) {
            case GL_VENDOR:                   return (const GLubyte*)kVendor;
            case GL_RENDERER:                 return (const GLubyte*)kRenderer;
            case GL_VERSION:                  return (const GLubyte*)kVersion;
            case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte*)kGlslVersion;
            case GL_EXTENSIONS:               return (const GLubyte*)kExtensions;
            default:                          return (const GLubyte*)"";
        }
    }

    typedef const GLubyte* (__stdcall* GlGetStringProc)(GLenum);
    auto real = XGL_CACHED_REAL_PROC("glGetString", GlGetStringProc);
    if (real) {
        const GLubyte* value = nullptr;
        __try {
            value = real(name);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            value = nullptr;
        }
        if (value) {
            if (name == GL_VERSION || name == GL_RENDERER || name == GL_VENDOR) {
                static std::atomic<int> logCount{ 0 };
                if (logCount.fetch_add(1, std::memory_order_relaxed) < 12) {
                    char line[512] = {};
                    std::snprintf(line, sizeof(line), "glGetString(0x%04X) Mesa -> %s", name, reinterpret_cast<const char*>(value));
                    DebugLine(line);
                }
            }
            return value;
        }

        if (name == GL_VERSION) {
            g_realGlStringUnavailable.store(1, std::memory_order_release);
        }
        static std::atomic<int> logCount{ 0 };
        if (logCount.fetch_add(1, std::memory_order_relaxed) < 8) {
            DebugLine("Mesa glGetString returned null; using LWJGL bootstrap fallback strings");
        }
    }

    switch (name) {
        case GL_VENDOR:                   return (const GLubyte*)kVendor;
        case GL_RENDERER:                 return (const GLubyte*)kRenderer;
        case GL_VERSION:                  return (const GLubyte*)kVersion;
        case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte*)kGlslVersion;
        case GL_EXTENSIONS:               return (const GLubyte*)kExtensions;
        default:                          return (const GLubyte*)"";
    }
}

// glGetStringi(GL_EXTENSIONS, i) -> ""; never called when NUM_EXTENSIONS == 0
// (exported through wglGetProcAddress dispatch table, not from opengl32 itself)
static const GLubyte* GLAPI_glGetStringi(GLenum name, GLuint index) {
    if (ForceCompatGlIdentity()) {
        (void)name;
        (void)index;
        return (const GLubyte*)"";
    }

    typedef const GLubyte* (__stdcall* GlGetStringiProc)(GLenum, GLuint);
    auto real = XGL_CACHED_REAL_PROC("glGetStringi", GlGetStringiProc);
    if (real && !g_realGlStringUnavailable.load(std::memory_order_acquire)) {
        const GLubyte* value = nullptr;
        __try {
            value = real(name, index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            value = nullptr;
        }
        if (value) {
            return value;
        }
    }
    return (const GLubyte*)"";
}

__declspec(dllexport) GLenum __stdcall glGetError(void) {
    if (ForceCompatGlIdentity()) {
        return GL_NO_ERROR;
    }

    typedef GLenum (__stdcall* GlGetErrorProc)(void);
    auto real = XGL_CACHED_REAL_PROC("glGetError", GlGetErrorProc);
    if (real) {
        __try {
            return real();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return GL_NO_ERROR;
}

static void FillIntegerv(GLenum pname, GLint* params) {
    if (!params) return;
    switch (pname) {
        // Version & profile
        case GL_MAJOR_VERSION:                          *params = 4;     break;
        case GL_MINOR_VERSION:                          *params = 6;     break;
        case GL_NUM_EXTENSIONS:                         *params = 0;     break;
        case GL_CONTEXT_FLAGS:                          *params = 0;     break;
        case GL_CONTEXT_PROFILE_MASK:                   *params = GL_CONTEXT_CORE_PROFILE_BIT; break;

        // Texture sizes
        case GL_MAX_TEXTURE_SIZE:                       *params = 16384; break;
        case GL_MAX_3D_TEXTURE_SIZE:                    *params = 16384; break;
        case GL_MAX_CUBE_MAP_TEXTURE_SIZE:              *params = 16384; break;
        case GL_MAX_ARRAY_TEXTURE_LAYERS:               *params = 2048;  break;
        case GL_MAX_RECTANGLE_TEXTURE_SIZE:             *params = 16384; break;
        case GL_MAX_TEXTURE_BUFFER_SIZE:                *params = 134217728; break;
        case GL_MAX_VIEWPORT_DIMS:                      params[0] = 16384; params[1] = 16384; break;
        case GL_MAX_RENDERBUFFER_SIZE:                  *params = 16384; break;

        // Texture image units (all stages)
        case GL_MAX_TEXTURE_IMAGE_UNITS:                *params = 32;    break;
        case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:         *params = 32;    break;
        case GL_MAX_GEOMETRY_TEXTURE_IMAGE_UNITS:       *params = 32;    break;
        case GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS:        *params = 32;    break;
        case GL_MAX_TESS_CONTROL_TEXTURE_IMAGE_UNITS:   *params = 32;    break;
        case GL_MAX_TESS_EVALUATION_TEXTURE_IMAGE_UNITS:*params = 32;    break;
        case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:       *params = 192;   break;

        // Vertex attribs
        case GL_MAX_VERTEX_ATTRIBS:                     *params = 16;    break;
        case GL_MAX_VERTEX_ATTRIB_BINDINGS:             *params = 16;    break;
        case GL_MAX_VERTEX_ATTRIB_STRIDE:               *params = 2048;  break;
        case GL_MAX_VERTEX_ATTRIB_RELATIVE_OFFSET:      *params = 2047;  break;
        case GL_MAX_VERTEX_OUTPUT_COMPONENTS:           *params = 128;   break;
        case GL_MAX_FRAGMENT_INPUT_COMPONENTS:          *params = 128;   break;
        case GL_MAX_GEOMETRY_INPUT_COMPONENTS:          *params = 64;    break;
        case GL_MAX_GEOMETRY_OUTPUT_COMPONENTS:         *params = 128;   break;

        // Uniform components & blocks
        case GL_MAX_VERTEX_UNIFORM_COMPONENTS:          *params = 4096;  break;
        case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS:        *params = 4096;  break;
        case GL_MAX_VARYING_FLOATS:                     *params = 128;   break;
        case GL_MAX_VERTEX_UNIFORM_BLOCKS:              *params = 14;    break;
        case GL_MAX_FRAGMENT_UNIFORM_BLOCKS:            *params = 14;    break;
        case GL_MAX_GEOMETRY_UNIFORM_BLOCKS:            *params = 14;    break;
        case GL_MAX_COMPUTE_UNIFORM_BLOCKS:             *params = 14;    break;
        case GL_MAX_TESS_CONTROL_UNIFORM_BLOCKS:        *params = 12;    break;
        case GL_MAX_TESS_EVALUATION_UNIFORM_BLOCKS:     *params = 12;    break;
        case GL_MAX_COMBINED_UNIFORM_BLOCKS:            *params = 70;    break;
        case GL_MAX_UNIFORM_BUFFER_BINDINGS:            *params = 84;    break;
        case GL_MAX_UNIFORM_BLOCK_SIZE:                 *params = 65536; break;

        // CRITICAL: Mojang divides by these. 0 -> ArithmeticException.
        case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:        *params = 256;   break;
        case GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT: *params = 256;   break;
        case GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT:        *params = 256;   break;
        case GL_MIN_MAP_BUFFER_ALIGNMENT:               *params = 64;    break;

        // SSBO
        case GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS:     *params = 16;    break;
        case GL_MAX_SHADER_STORAGE_BLOCK_SIZE:          *params = 16777216; break;
        case GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS:     *params = 96;    break;

        // Framebuffer / draw
        case GL_MAX_DRAW_BUFFERS:                       *params = 8;     break;
        case GL_MAX_COLOR_ATTACHMENTS:                  *params = 8;     break;
        case GL_MAX_SAMPLES:                            *params = 8;     break;
        case GL_MAX_FRAMEBUFFER_WIDTH:                  *params = 16384; break;
        case GL_MAX_FRAMEBUFFER_HEIGHT:                 *params = 16384; break;
        case GL_MAX_FRAMEBUFFER_LAYERS:                 *params = 2048;  break;
        case GL_MAX_FRAMEBUFFER_SAMPLES:                *params = 8;     break;

        // Element ranges
        case GL_MAX_ELEMENTS_VERTICES:                  *params = 65535; break;
        case GL_MAX_ELEMENTS_INDICES:                   *params = 65535; break;

        // Format counts (must be 0 so Mojang doesn't iterate)
        case GL_NUM_PROGRAM_BINARY_FORMATS:             *params = 0;     break;
        case GL_NUM_SHADER_BINARY_FORMATS:              *params = 0;     break;
        case GL_NUM_COMPRESSED_TEXTURE_FORMATS:         *params = 0;     break;

        // Compute
        case GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS:     *params = 1024;  break;

        default:                                        *params = 0;     break;
    }
}

__declspec(dllexport) void __stdcall glGetIntegerv(GLenum pname, GLint* params) {
    if (ForceCompatGlIdentity()) {
        FillIntegerv(pname, params);
        return;
    }

    typedef void (__stdcall* GlGetIntegervProc)(GLenum, GLint*);
    auto real = XGL_CACHED_REAL_PROC("glGetIntegerv", GlGetIntegervProc);
    if (real && !g_realGlStringUnavailable.load(std::memory_order_acquire)) {
        __try {
            real(pname, params);
            return;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    FillIntegerv(pname, params);
}

__declspec(dllexport) void __stdcall glGetFloatv(GLenum pname, GLfloat* params) {
    if (!params) return;
    if (ForceCompatGlIdentity()) {
        GLint v[4] = { 0,0,0,0 };
        FillIntegerv(pname, v);
        params[0] = (GLfloat)v[0];
        return;
    }

    typedef void (__stdcall* GlGetFloatvProc)(GLenum, GLfloat*);
    auto real = XGL_CACHED_REAL_PROC("glGetFloatv", GlGetFloatvProc);
    if (real && !g_realGlStringUnavailable.load(std::memory_order_acquire)) {
        __try {
            real(pname, params);
            return;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    GLint v[4] = { 0,0,0,0 };
    FillIntegerv(pname, v);
    params[0] = (GLfloat)v[0];
}

__declspec(dllexport) void __stdcall glGetDoublev(GLenum pname, GLdouble* params) {
    if (!params) return;
    if (ForceCompatGlIdentity()) {
        GLint v[4] = { 0,0,0,0 };
        FillIntegerv(pname, v);
        params[0] = (GLdouble)v[0];
        return;
    }

    typedef void (__stdcall* GlGetDoublevProc)(GLenum, GLdouble*);
    auto real = XGL_CACHED_REAL_PROC("glGetDoublev", GlGetDoublevProc);
    if (real && !g_realGlStringUnavailable.load(std::memory_order_acquire)) {
        __try {
            real(pname, params);
            return;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    GLint v[4] = { 0,0,0,0 };
    FillIntegerv(pname, v);
    params[0] = (GLdouble)v[0];
}

__declspec(dllexport) void __stdcall glGetBooleanv(GLenum pname, GLboolean* params) {
    if (ForceCompatGlIdentity()) {
        if (params) *params = GL_FALSE;
        (void)pname;
        return;
    }

    typedef void (__stdcall* GlGetBooleanvProc)(GLenum, GLboolean*);
    auto real = XGL_CACHED_REAL_PROC("glGetBooleanv", GlGetBooleanvProc);
    if (real && !g_realGlStringUnavailable.load(std::memory_order_acquire)) {
        __try {
            real(pname, params);
            return;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (params) *params = GL_FALSE;
}

__declspec(dllexport) GLboolean __stdcall glIsEnabled(GLenum cap) {
    if (cap == GL_TEXTURE_CUBE_MAP_SEAMLESS || cap == GL_PROGRAM_POINT_SIZE) {
        return GL_FALSE;
    }

    typedef GLboolean (__stdcall* GlIsEnabledProc)(GLenum);
    auto real = XGL_CACHED_REAL_PROC("glIsEnabled", GlIsEnabledProc);
    if (real) {
        __try {
            return real(cap);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return GL_FALSE;
}

static bool IsGlesDesktopOnlyEnableCap(GLenum cap) {
    if (!ForceCompatGlIdentity()) {
        return false;
    }
    return cap == GL_TEXTURE_CUBE_MAP_SEAMLESS || cap == GL_PROGRAM_POINT_SIZE;
}

static void LogFilteredCapability(const char* functionName, GLenum cap) {
    static std::atomic<int> logCount{ 0 };
    int count = logCount.fetch_add(1, std::memory_order_relaxed);
    if (count >= 16) {
        return;
    }

    char line[192] = {};
    std::snprintf(line, sizeof(line), "%s filtered GLES-incompatible capability 0x%04x", functionName, cap);
    DebugLine(line);
}

__declspec(dllexport) void __stdcall glEnable(GLenum cap) {
    if (IsGlesDesktopOnlyEnableCap(cap)) {
        LogFilteredCapability("glEnable", cap);
        return;
    }

    typedef void (__stdcall* GlEnableProc)(GLenum);
    auto real = XGL_CACHED_REAL_PROC("glEnable", GlEnableProc);
    if (real) {
        __try {
            real(cap);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

__declspec(dllexport) void __stdcall glDisable(GLenum cap) {
    if (IsGlesDesktopOnlyEnableCap(cap)) {
        LogFilteredCapability("glDisable", cap);
        return;
    }

    typedef void (__stdcall* GlDisableProc)(GLenum);
    auto real = XGL_CACHED_REAL_PROC("glDisable", GlDisableProc);
    if (real) {
        __try {
            real(cap);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

__declspec(dllexport) GLboolean __stdcall glIsTexture(GLuint texture) {
    typedef GLboolean (__stdcall* GlIsTextureProc)(GLuint);
    auto real = XGL_CACHED_REAL_PROC("glIsTexture", GlIsTextureProc);
    if (real) {
        __try {
            return real(texture);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return texture ? GL_TRUE : GL_FALSE;
}

__declspec(dllexport) void __stdcall glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    LogGlCallProbe("glClearColor");
    typedef void (__stdcall* GlClearColorProc)(GLfloat, GLfloat, GLfloat, GLfloat);
    auto real = XGL_CACHED_REAL_PROC("glClearColor", GlClearColorProc);
    if (real) {
        __try {
            real(red, green, blue, alpha);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_clearColor[0] = Clamp01(red);
    g_clearColor[1] = Clamp01(green);
    g_clearColor[2] = Clamp01(blue);
    g_clearColor[3] = Clamp01(alpha);
}

__declspec(dllexport) void __stdcall glClear(GLbitfield mask) {
    LogGlCallProbe("glClear");
    typedef void (__stdcall* GlClearProc)(GLbitfield);
    auto real = XGL_CACHED_REAL_PROC("glClear", GlClearProc);
    if (real) {
        __try {
            real(mask);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    PublishClear(mask);
    if (mask & GL_COLOR_BUFFER_BIT) {
        SignalPresentEvent();
    }
}

__declspec(dllexport) void __stdcall glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    LogGlCallProbe("glViewport");
    GLint realX = x;
    GLint realY = y;
    GLsizei realWidth = width;
    GLsizei realHeight = height;
    if (ShouldScaleDefaultFramebufferRect(g_boundDrawFramebuffer, x, y, width, height, realX, realY, realWidth, realHeight)) {
        LogUpscaleRectOnce("glViewport", x, y, width, height, realX, realY, realWidth, realHeight);
    }

    typedef void (__stdcall* GlViewportProc)(GLint, GLint, GLsizei, GLsizei);
    auto real = XGL_CACHED_REAL_PROC("glViewport", GlViewportProc);
    if (real) {
        __try {
            real(realX, realY, realWidth, realHeight);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    PublishViewport(realX, realY, realWidth, realHeight);
}

__declspec(dllexport) void __stdcall glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    LogGlCallProbe("glDrawArrays");
    typedef void (__stdcall* GlDrawArraysProc)(GLenum, GLint, GLsizei);
    auto real = XGL_CACHED_REAL_PROC("glDrawArrays", GlDrawArraysProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            real(mode, first, count);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingDraw, timingStartMs);
    }
    PublishDraw(mode, first, count, 0);
    PublishGuiDrawArrays(mode, first, count);
}

__declspec(dllexport) void __stdcall glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    LogGlCallProbe("glDrawElements");
    typedef void (__stdcall* GlDrawElementsProc)(GLenum, GLsizei, GLenum, const void*);
    auto real = XGL_CACHED_REAL_PROC("glDrawElements", GlDrawElementsProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            real(mode, count, type, indices);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingDraw, timingStartMs);
    }
    PublishDraw(mode, 0, count, type);
    PublishGuiDrawElements(mode, count, type, indices);
}

#define XGL_FORWARD_REAL_VOID0(symbolName) \
    __declspec(dllexport) void __stdcall symbolName(void) { \
        LogGlCallProbe(#symbolName); \
        typedef void (__stdcall* Proc)(void); \
        XGL_CALL_REAL_VOID(#symbolName, Proc); \
    }

#define XGL_FORWARD_REAL_VOID1(symbolName, type1) \
    __declspec(dllexport) void __stdcall symbolName(type1 a1) { \
        LogGlCallProbe(#symbolName); \
        typedef void (__stdcall* Proc)(type1); \
        XGL_CALL_REAL_VOID(#symbolName, Proc, a1); \
    }

#define XGL_FORWARD_REAL_VOID2(symbolName, type1, type2) \
    __declspec(dllexport) void __stdcall symbolName(type1 a1, type2 a2) { \
        LogGlCallProbe(#symbolName); \
        typedef void (__stdcall* Proc)(type1, type2); \
        XGL_CALL_REAL_VOID(#symbolName, Proc, a1, a2); \
    }

#define XGL_FORWARD_REAL_VOID3(symbolName, type1, type2, type3) \
    __declspec(dllexport) void __stdcall symbolName(type1 a1, type2 a2, type3 a3) { \
        LogGlCallProbe(#symbolName); \
        typedef void (__stdcall* Proc)(type1, type2, type3); \
        XGL_CALL_REAL_VOID(#symbolName, Proc, a1, a2, a3); \
    }

#define XGL_FORWARD_REAL_VOID4(symbolName, type1, type2, type3, type4) \
    __declspec(dllexport) void __stdcall symbolName(type1 a1, type2 a2, type3 a3, type4 a4) { \
        LogGlCallProbe(#symbolName); \
        typedef void (__stdcall* Proc)(type1, type2, type3, type4); \
        XGL_CALL_REAL_VOID(#symbolName, Proc, a1, a2, a3, a4); \
    }

// Minecraft 1.12.2's font renderer still builds glyphs with OpenGL 1.x
// immediate-mode/display-list calls.  These must reach Mesa or button labels
// and menu text vanish while textured UI quads continue to render.
XGL_FORWARD_REAL_VOID1(glBegin, GLenum)
XGL_FORWARD_REAL_VOID0(glEnd)
XGL_FORWARD_REAL_VOID3(glColor3f, GLfloat, GLfloat, GLfloat)
XGL_FORWARD_REAL_VOID1(glColor3fv, const GLfloat*)
XGL_FORWARD_REAL_VOID4(glColor4f, GLfloat, GLfloat, GLfloat, GLfloat)
XGL_FORWARD_REAL_VOID1(glColor4fv, const GLfloat*)
XGL_FORWARD_REAL_VOID4(glColor4ub, GLubyte, GLubyte, GLubyte, GLubyte)
XGL_FORWARD_REAL_VOID1(glColor4ubv, const GLubyte*)
XGL_FORWARD_REAL_VOID2(glTexCoord2f, GLfloat, GLfloat)
XGL_FORWARD_REAL_VOID1(glTexCoord2fv, const GLfloat*)
XGL_FORWARD_REAL_VOID2(glVertex2f, GLfloat, GLfloat)
XGL_FORWARD_REAL_VOID1(glVertex2fv, const GLfloat*)
XGL_FORWARD_REAL_VOID3(glVertex3f, GLfloat, GLfloat, GLfloat)
XGL_FORWARD_REAL_VOID1(glVertex3fv, const GLfloat*)

__declspec(dllexport) void __stdcall glActiveTexture(GLenum texture) {
    typedef void (__stdcall* GlActiveTextureProc)(GLenum);
    auto real = XGL_CACHED_REAL_PROC("glActiveTexture", GlActiveTextureProc);
    if (real) {
        __try {
            real(texture);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (texture >= GL_TEXTURE0 && texture < GL_TEXTURE0 + 32) {
        g_activeTextureUnit = texture - GL_TEXTURE0;
    }
}

__declspec(dllexport) void __stdcall glBindTexture(GLenum target, GLuint texture) {
    LogGlCallProbe("glBindTexture");
    typedef void (__stdcall* GlBindTextureProc)(GLenum, GLuint);
    auto real = XGL_CACHED_REAL_PROC("glBindTexture", GlBindTextureProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            real(target, texture);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingTextureBind, timingStartMs);
    }
    if (g_activeTextureUnit < 32) {
        if (target == GL_TEXTURE_2D) {
            g_boundTexture2D[g_activeTextureUnit] = texture;
        } else if (target == GL_TEXTURE_2D_ARRAY || target == GL_TEXTURE_3D) {
            g_boundTexture2DArray[g_activeTextureUnit] = texture;
        }
    }
    PublishTextureBind(target, texture);
}

static void GLAPI_glBindTextureUnit(GLuint unit, GLuint texture) {
    typedef void (__stdcall* GlBindTextureUnitProc)(GLuint, GLuint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureBind, "glBindTextureUnit", GlBindTextureUnitProc, unit, texture);
    if (unit < 32) {
        g_boundTexture2D[unit] = texture;
        g_boundTexture2DArray[unit] = texture;
        PublishTextureBindForUnit(GL_TEXTURE_2D, texture, unit);
    }
}

static void GLAPI_glBindTextures(GLuint first, GLsizei count, const GLuint* textures) {
    typedef void (__stdcall* GlBindTexturesProc)(GLuint, GLsizei, const GLuint*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureBind, "glBindTextures", GlBindTexturesProc, first, count, textures);
    if (count <= 0) {
        return;
    }

    for (GLsizei i = 0; i < count; ++i) {
        const GLuint unit = first + static_cast<GLuint>(i);
        if (unit >= 32) {
            break;
        }

        const GLuint texture = textures ? textures[i] : 0;
        g_boundTexture2D[unit] = texture;
        g_boundTexture2DArray[unit] = texture;
        PublishTextureBindForUnit(GL_TEXTURE_2D, texture, unit);
    }
}

__declspec(dllexport) void __stdcall glTexImage2D(
    GLenum target,
    GLint level,
    GLint internalFormat,
    GLsizei width,
    GLsizei height,
    GLint /*border*/,
    GLenum format,
    GLenum type,
    const void* pixels) {
    LogGlCallProbe("glTexImage2D");
    typedef void (__stdcall* GlTexImage2DProc)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    auto real = XGL_CACHED_REAL_PROC("glTexImage2D", GlTexImage2DProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            real(target, level, internalFormat, width, height, 0, format, type, pixels);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingTextureUpload, timingStartMs);
    }
    StoreTextureRegion(target, level, 0, 0, width, height, format, type, pixels, true);
    PublishTextureUpload(false, target, level, internalFormat, width, height, 1, format, type, -1);
    PublishTextureSample(target, level, width, height, format, type, pixels);
}

__declspec(dllexport) void __stdcall glTexSubImage2D(
    GLenum target,
    GLint level,
    GLint xoffset,
    GLint yoffset,
    GLsizei width,
    GLsizei height,
    GLenum format,
    GLenum type,
    const void* pixels) {
    LogGlCallProbe("glTexSubImage2D");
    typedef void (__stdcall* GlTexSubImage2DProc)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
    auto real = XGL_CACHED_REAL_PROC("glTexSubImage2D", GlTexSubImage2DProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            real(target, level, xoffset, yoffset, width, height, format, type, pixels);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingTextureUpload, timingStartMs);
    }
    StoreTextureRegion(target, level, xoffset, yoffset, width, height, format, type, pixels, false);
    PublishTextureUpload(true, target, level, 0, width, height, 1, format, type, -1);
    PublishTextureSample(target, level, width, height, format, type, pixels);
}

__declspec(dllexport) void __stdcall glTexParameteri(GLenum target, GLenum pname, GLint param) {
    typedef void (__stdcall* GlTexParameteriProc)(GLenum, GLenum, GLint);
    auto real = XGL_CACHED_REAL_PROC("glTexParameteri", GlTexParameteriProc);
    if (real) {
        __try {
            real(target, pname, param);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    auto* state = EnsureGlCommandState();
    if (!state) return;
    state->lastTextureTarget = target;
    InterlockedIncrement64(&state->textureParameterSerial);
}

__declspec(dllexport) void __stdcall glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    glTexParameteri(target, pname, static_cast<GLint>(param));
}

__declspec(dllexport) void __stdcall glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
    typedef void (__stdcall* GlTexParameterfvProc)(GLenum, GLenum, const GLfloat*);
    XGL_CALL_REAL_VOID("glTexParameterfv", GlTexParameterfvProc, target, pname, params);
}

__declspec(dllexport) void __stdcall glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
    typedef void (__stdcall* GlTexParameterivProc)(GLenum, GLenum, const GLint*);
    XGL_CALL_REAL_VOID("glTexParameteriv", GlTexParameterivProc, target, pname, params);
}

__declspec(dllexport) void __stdcall glAlphaFunc(GLenum func, GLfloat ref) {
    typedef void (__stdcall* GlAlphaFuncProc)(GLenum, GLfloat);
    XGL_CALL_REAL_VOID("glAlphaFunc", GlAlphaFuncProc, func, ref);
}

__declspec(dllexport) void __stdcall glBlendFunc(GLenum sfactor, GLenum dfactor) {
    LogGlCallProbe("glBlendFunc");
    typedef void (__stdcall* GlBlendFuncProc)(GLenum, GLenum);
    XGL_CALL_REAL_VOID("glBlendFunc", GlBlendFuncProc, sfactor, dfactor);
}

__declspec(dllexport) void __stdcall glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
    typedef void (__stdcall* GlColorMaskProc)(GLboolean, GLboolean, GLboolean, GLboolean);
    XGL_CALL_REAL_VOID("glColorMask", GlColorMaskProc, red, green, blue, alpha);
}

__declspec(dllexport) void __stdcall glColorMaterial(GLenum face, GLenum mode) {
    typedef void (__stdcall* GlColorMaterialProc)(GLenum, GLenum);
    XGL_CALL_REAL_VOID("glColorMaterial", GlColorMaterialProc, face, mode);
}

__declspec(dllexport) void __stdcall glFogf(GLenum pname, GLfloat param) {
    typedef void (__stdcall* GlFogfProc)(GLenum, GLfloat);
    XGL_CALL_REAL_VOID("glFogf", GlFogfProc, pname, param);
}

__declspec(dllexport) void __stdcall glFogfv(GLenum pname, const GLfloat* params) {
    typedef void (__stdcall* GlFogfvProc)(GLenum, const GLfloat*);
    XGL_CALL_REAL_VOID("glFogfv", GlFogfvProc, pname, params);
}

__declspec(dllexport) void __stdcall glFogi(GLenum pname, GLint param) {
    typedef void (__stdcall* GlFogiProc)(GLenum, GLint);
    XGL_CALL_REAL_VOID("glFogi", GlFogiProc, pname, param);
}

__declspec(dllexport) void __stdcall glFogiv(GLenum pname, const GLint* params) {
    typedef void (__stdcall* GlFogivProc)(GLenum, const GLint*);
    XGL_CALL_REAL_VOID("glFogiv", GlFogivProc, pname, params);
}

__declspec(dllexport) void __stdcall glGetLightfv(GLenum light, GLenum pname, GLfloat* params) {
    typedef void (__stdcall* GlGetLightfvProc)(GLenum, GLenum, GLfloat*);
    XGL_CALL_REAL_VOID("glGetLightfv", GlGetLightfvProc, light, pname, params);
}

__declspec(dllexport) void __stdcall glGetLightiv(GLenum light, GLenum pname, GLint* params) {
    typedef void (__stdcall* GlGetLightivProc)(GLenum, GLenum, GLint*);
    XGL_CALL_REAL_VOID("glGetLightiv", GlGetLightivProc, light, pname, params);
}

__declspec(dllexport) void __stdcall glGetMaterialfv(GLenum face, GLenum pname, GLfloat* params) {
    typedef void (__stdcall* GlGetMaterialfvProc)(GLenum, GLenum, GLfloat*);
    XGL_CALL_REAL_VOID("glGetMaterialfv", GlGetMaterialfvProc, face, pname, params);
}

__declspec(dllexport) void __stdcall glGetMaterialiv(GLenum face, GLenum pname, GLint* params) {
    typedef void (__stdcall* GlGetMaterialivProc)(GLenum, GLenum, GLint*);
    XGL_CALL_REAL_VOID("glGetMaterialiv", GlGetMaterialivProc, face, pname, params);
}

__declspec(dllexport) void __stdcall glLightModelf(GLenum pname, GLfloat param) {
    typedef void (__stdcall* GlLightModelfProc)(GLenum, GLfloat);
    XGL_CALL_REAL_VOID("glLightModelf", GlLightModelfProc, pname, param);
}

__declspec(dllexport) void __stdcall glLightModelfv(GLenum pname, const GLfloat* params) {
    typedef void (__stdcall* GlLightModelfvProc)(GLenum, const GLfloat*);
    XGL_CALL_REAL_VOID("glLightModelfv", GlLightModelfvProc, pname, params);
}

__declspec(dllexport) void __stdcall glLightModeli(GLenum pname, GLint param) {
    typedef void (__stdcall* GlLightModeliProc)(GLenum, GLint);
    XGL_CALL_REAL_VOID("glLightModeli", GlLightModeliProc, pname, param);
}

__declspec(dllexport) void __stdcall glLightModeliv(GLenum pname, const GLint* params) {
    typedef void (__stdcall* GlLightModelivProc)(GLenum, const GLint*);
    XGL_CALL_REAL_VOID("glLightModeliv", GlLightModelivProc, pname, params);
}

__declspec(dllexport) void __stdcall glLightf(GLenum light, GLenum pname, GLfloat param) {
    typedef void (__stdcall* GlLightfProc)(GLenum, GLenum, GLfloat);
    XGL_CALL_REAL_VOID("glLightf", GlLightfProc, light, pname, param);
}

__declspec(dllexport) void __stdcall glLightfv(GLenum light, GLenum pname, const GLfloat* params) {
    typedef void (__stdcall* GlLightfvProc)(GLenum, GLenum, const GLfloat*);
    XGL_CALL_REAL_VOID("glLightfv", GlLightfvProc, light, pname, params);
}

__declspec(dllexport) void __stdcall glLighti(GLenum light, GLenum pname, GLint param) {
    typedef void (__stdcall* GlLightiProc)(GLenum, GLenum, GLint);
    XGL_CALL_REAL_VOID("glLighti", GlLightiProc, light, pname, param);
}

__declspec(dllexport) void __stdcall glLightiv(GLenum light, GLenum pname, const GLint* params) {
    typedef void (__stdcall* GlLightivProc)(GLenum, GLenum, const GLint*);
    XGL_CALL_REAL_VOID("glLightiv", GlLightivProc, light, pname, params);
}

__declspec(dllexport) void __stdcall glMaterialf(GLenum face, GLenum pname, GLfloat param) {
    typedef void (__stdcall* GlMaterialfProc)(GLenum, GLenum, GLfloat);
    XGL_CALL_REAL_VOID("glMaterialf", GlMaterialfProc, face, pname, param);
}

__declspec(dllexport) void __stdcall glMaterialfv(GLenum face, GLenum pname, const GLfloat* params) {
    typedef void (__stdcall* GlMaterialfvProc)(GLenum, GLenum, const GLfloat*);
    XGL_CALL_REAL_VOID("glMaterialfv", GlMaterialfvProc, face, pname, params);
}

__declspec(dllexport) void __stdcall glMateriali(GLenum face, GLenum pname, GLint param) {
    typedef void (__stdcall* GlMaterialiProc)(GLenum, GLenum, GLint);
    XGL_CALL_REAL_VOID("glMateriali", GlMaterialiProc, face, pname, param);
}

__declspec(dllexport) void __stdcall glMaterialiv(GLenum face, GLenum pname, const GLint* params) {
    typedef void (__stdcall* GlMaterialivProc)(GLenum, GLenum, const GLint*);
    XGL_CALL_REAL_VOID("glMaterialiv", GlMaterialivProc, face, pname, params);
}

__declspec(dllexport) void __stdcall glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz) {
    typedef void (__stdcall* GlNormal3bProc)(GLbyte, GLbyte, GLbyte);
    XGL_CALL_REAL_VOID("glNormal3b", GlNormal3bProc, nx, ny, nz);
}

__declspec(dllexport) void __stdcall glNormal3bv(const GLbyte* v) {
    typedef void (__stdcall* GlNormal3bvProc)(const GLbyte*);
    XGL_CALL_REAL_VOID("glNormal3bv", GlNormal3bvProc, v);
}

__declspec(dllexport) void __stdcall glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz) {
    typedef void (__stdcall* GlNormal3dProc)(GLdouble, GLdouble, GLdouble);
    XGL_CALL_REAL_VOID("glNormal3d", GlNormal3dProc, nx, ny, nz);
}

__declspec(dllexport) void __stdcall glNormal3dv(const GLdouble* v) {
    typedef void (__stdcall* GlNormal3dvProc)(const GLdouble*);
    XGL_CALL_REAL_VOID("glNormal3dv", GlNormal3dvProc, v);
}

__declspec(dllexport) void __stdcall glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) {
    typedef void (__stdcall* GlNormal3fProc)(GLfloat, GLfloat, GLfloat);
    XGL_CALL_REAL_VOID("glNormal3f", GlNormal3fProc, nx, ny, nz);
}

__declspec(dllexport) void __stdcall glNormal3fv(const GLfloat* v) {
    typedef void (__stdcall* GlNormal3fvProc)(const GLfloat*);
    XGL_CALL_REAL_VOID("glNormal3fv", GlNormal3fvProc, v);
}

__declspec(dllexport) void __stdcall glNormal3i(GLint nx, GLint ny, GLint nz) {
    typedef void (__stdcall* GlNormal3iProc)(GLint, GLint, GLint);
    XGL_CALL_REAL_VOID("glNormal3i", GlNormal3iProc, nx, ny, nz);
}

__declspec(dllexport) void __stdcall glNormal3iv(const GLint* v) {
    typedef void (__stdcall* GlNormal3ivProc)(const GLint*);
    XGL_CALL_REAL_VOID("glNormal3iv", GlNormal3ivProc, v);
}

__declspec(dllexport) void __stdcall glNormal3s(GLshort nx, GLshort ny, GLshort nz) {
    typedef void (__stdcall* GlNormal3sProc)(GLshort, GLshort, GLshort);
    XGL_CALL_REAL_VOID("glNormal3s", GlNormal3sProc, nx, ny, nz);
}

__declspec(dllexport) void __stdcall glNormal3sv(const GLshort* v) {
    typedef void (__stdcall* GlNormal3svProc)(const GLshort*);
    XGL_CALL_REAL_VOID("glNormal3sv", GlNormal3svProc, v);
}

__declspec(dllexport) void __stdcall glColorPointer(GLint size, GLenum type, GLsizei stride, const void* pointer) {
    LogGlCallProbe("glColorPointer");
    typedef void (__stdcall* GlColorPointerProc)(GLint, GLenum, GLsizei, const void*);
    XGL_CALL_REAL_VOID("glColorPointer", GlColorPointerProc, size, type, stride, pointer);
}

__declspec(dllexport) void __stdcall glCullFace(GLenum mode) {
    typedef void (__stdcall* GlCullFaceProc)(GLenum);
    XGL_CALL_REAL_VOID("glCullFace", GlCullFaceProc, mode);
}

__declspec(dllexport) void __stdcall glDepthFunc(GLenum func) {
    typedef void (__stdcall* GlDepthFuncProc)(GLenum);
    XGL_CALL_REAL_VOID("glDepthFunc", GlDepthFuncProc, func);
}

__declspec(dllexport) void __stdcall glDepthMask(GLboolean flag) {
    typedef void (__stdcall* GlDepthMaskProc)(GLboolean);
    XGL_CALL_REAL_VOID("glDepthMask", GlDepthMaskProc, flag);
}

__declspec(dllexport) void __stdcall glDepthRange(GLdouble nearValue, GLdouble farValue) {
    typedef void (__stdcall* GlDepthRangeProc)(GLdouble, GLdouble);
    XGL_CALL_REAL_VOID("glDepthRange", GlDepthRangeProc, nearValue, farValue);
}

__declspec(dllexport) void __stdcall glEnableClientState(GLenum array) {
    LogGlCallProbe("glEnableClientState");
    typedef void (__stdcall* GlEnableClientStateProc)(GLenum);
    XGL_CALL_REAL_VOID("glEnableClientState", GlEnableClientStateProc, array);
}

__declspec(dllexport) void __stdcall glDisableClientState(GLenum array) {
    LogGlCallProbe("glDisableClientState");
    typedef void (__stdcall* GlDisableClientStateProc)(GLenum);
    XGL_CALL_REAL_VOID("glDisableClientState", GlDisableClientStateProc, array);
}

__declspec(dllexport) void __stdcall glFrustum(
    GLdouble left,
    GLdouble right,
    GLdouble bottom,
    GLdouble top,
    GLdouble nearValue,
    GLdouble farValue) {
    typedef void (__stdcall* GlFrustumProc)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
    XGL_CALL_REAL_VOID("glFrustum", GlFrustumProc, left, right, bottom, top, nearValue, farValue);
}

__declspec(dllexport) void __stdcall glHint(GLenum target, GLenum mode) {
    typedef void (__stdcall* GlHintProc)(GLenum, GLenum);
    XGL_CALL_REAL_VOID("glHint", GlHintProc, target, mode);
}

__declspec(dllexport) void __stdcall glLineWidth(GLfloat width) {
    typedef void (__stdcall* GlLineWidthProc)(GLfloat);
    XGL_CALL_REAL_VOID("glLineWidth", GlLineWidthProc, width);
}

__declspec(dllexport) void __stdcall glLoadIdentity(void) {
    LogGlCallProbe("glLoadIdentity");
    typedef void (__stdcall* GlLoadIdentityProc)();
    XGL_CALL_REAL_VOID("glLoadIdentity", GlLoadIdentityProc);
}

__declspec(dllexport) void __stdcall glLoadMatrixd(const GLdouble* matrix) {
    typedef void (__stdcall* GlLoadMatrixdProc)(const GLdouble*);
    XGL_CALL_REAL_VOID("glLoadMatrixd", GlLoadMatrixdProc, matrix);
}

__declspec(dllexport) void __stdcall glLoadMatrixf(const GLfloat* matrix) {
    typedef void (__stdcall* GlLoadMatrixfProc)(const GLfloat*);
    XGL_CALL_REAL_VOID("glLoadMatrixf", GlLoadMatrixfProc, matrix);
}

__declspec(dllexport) void __stdcall glMatrixMode(GLenum mode) {
    LogGlCallProbe("glMatrixMode");
    typedef void (__stdcall* GlMatrixModeProc)(GLenum);
    XGL_CALL_REAL_VOID("glMatrixMode", GlMatrixModeProc, mode);
}

__declspec(dllexport) void __stdcall glMultMatrixd(const GLdouble* matrix) {
    typedef void (__stdcall* GlMultMatrixdProc)(const GLdouble*);
    XGL_CALL_REAL_VOID("glMultMatrixd", GlMultMatrixdProc, matrix);
}

__declspec(dllexport) void __stdcall glMultMatrixf(const GLfloat* matrix) {
    typedef void (__stdcall* GlMultMatrixfProc)(const GLfloat*);
    XGL_CALL_REAL_VOID("glMultMatrixf", GlMultMatrixfProc, matrix);
}

__declspec(dllexport) void __stdcall glNormalPointer(GLenum type, GLsizei stride, const void* pointer) {
    typedef void (__stdcall* GlNormalPointerProc)(GLenum, GLsizei, const void*);
    XGL_CALL_REAL_VOID("glNormalPointer", GlNormalPointerProc, type, stride, pointer);
}

__declspec(dllexport) void __stdcall glOrtho(
    GLdouble left,
    GLdouble right,
    GLdouble bottom,
    GLdouble top,
    GLdouble nearValue,
    GLdouble farValue) {
    LogGlCallProbe("glOrtho");
    typedef void (__stdcall* GlOrthoProc)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
    XGL_CALL_REAL_VOID("glOrtho", GlOrthoProc, left, right, bottom, top, nearValue, farValue);
}

__declspec(dllexport) void __stdcall glPointSize(GLfloat size) {
    typedef void (__stdcall* GlPointSizeProc)(GLfloat);
    XGL_CALL_REAL_VOID("glPointSize", GlPointSizeProc, size);
}

__declspec(dllexport) void __stdcall glPolygonOffset(GLfloat factor, GLfloat units) {
    typedef void (__stdcall* GlPolygonOffsetProc)(GLfloat, GLfloat);
    XGL_CALL_REAL_VOID("glPolygonOffset", GlPolygonOffsetProc, factor, units);
}

__declspec(dllexport) void __stdcall glPopAttrib(void) {
    typedef void (__stdcall* GlPopAttribProc)();
    XGL_CALL_REAL_VOID("glPopAttrib", GlPopAttribProc);
}

__declspec(dllexport) void __stdcall glPushAttrib(GLbitfield mask) {
    typedef void (__stdcall* GlPushAttribProc)(GLbitfield);
    XGL_CALL_REAL_VOID("glPushAttrib", GlPushAttribProc, mask);
}

__declspec(dllexport) void __stdcall glPopClientAttrib(void) {
    typedef void (__stdcall* GlPopClientAttribProc)();
    XGL_CALL_REAL_VOID("glPopClientAttrib", GlPopClientAttribProc);
}

__declspec(dllexport) void __stdcall glPushClientAttrib(GLbitfield mask) {
    typedef void (__stdcall* GlPushClientAttribProc)(GLbitfield);
    XGL_CALL_REAL_VOID("glPushClientAttrib", GlPushClientAttribProc, mask);
}

__declspec(dllexport) void __stdcall glPopMatrix(void) {
    LogGlCallProbe("glPopMatrix");
    typedef void (__stdcall* GlPopMatrixProc)();
    XGL_CALL_REAL_VOID("glPopMatrix", GlPopMatrixProc);
}

__declspec(dllexport) void __stdcall glPushMatrix(void) {
    LogGlCallProbe("glPushMatrix");
    typedef void (__stdcall* GlPushMatrixProc)();
    XGL_CALL_REAL_VOID("glPushMatrix", GlPushMatrixProc);
}

__declspec(dllexport) void __stdcall glRotated(
    GLdouble angle,
    GLdouble x,
    GLdouble y,
    GLdouble z) {
    typedef void (__stdcall* GlRotatedProc)(GLdouble, GLdouble, GLdouble, GLdouble);
    XGL_CALL_REAL_VOID("glRotated", GlRotatedProc, angle, x, y, z);
}

__declspec(dllexport) void __stdcall glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    LogGlCallProbe("glRotatef");
    typedef void (__stdcall* GlRotatefProc)(GLfloat, GLfloat, GLfloat, GLfloat);
    XGL_CALL_REAL_VOID("glRotatef", GlRotatefProc, angle, x, y, z);
}

__declspec(dllexport) void __stdcall glScaled(GLdouble x, GLdouble y, GLdouble z) {
    typedef void (__stdcall* GlScaledProc)(GLdouble, GLdouble, GLdouble);
    XGL_CALL_REAL_VOID("glScaled", GlScaledProc, x, y, z);
}

__declspec(dllexport) void __stdcall glScalef(GLfloat x, GLfloat y, GLfloat z) {
    LogGlCallProbe("glScalef");
    typedef void (__stdcall* GlScalefProc)(GLfloat, GLfloat, GLfloat);
    XGL_CALL_REAL_VOID("glScalef", GlScalefProc, x, y, z);
}

__declspec(dllexport) void __stdcall glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    GLint realX = x;
    GLint realY = y;
    GLsizei realWidth = width;
    GLsizei realHeight = height;
    if (ShouldScaleDefaultFramebufferRect(g_boundDrawFramebuffer, x, y, width, height, realX, realY, realWidth, realHeight)) {
        LogUpscaleRectOnce("glScissor", x, y, width, height, realX, realY, realWidth, realHeight);
    }

    typedef void (__stdcall* GlScissorProc)(GLint, GLint, GLsizei, GLsizei);
    XGL_CALL_REAL_VOID("glScissor", GlScissorProc, realX, realY, realWidth, realHeight);
}

__declspec(dllexport) void __stdcall glShadeModel(GLenum mode) {
    typedef void (__stdcall* GlShadeModelProc)(GLenum);
    XGL_CALL_REAL_VOID("glShadeModel", GlShadeModelProc, mode);
}

__declspec(dllexport) void __stdcall glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    typedef void (__stdcall* GlStencilFuncProc)(GLenum, GLint, GLuint);
    XGL_CALL_REAL_VOID("glStencilFunc", GlStencilFuncProc, func, ref, mask);
}

__declspec(dllexport) void __stdcall glStencilMask(GLuint mask) {
    typedef void (__stdcall* GlStencilMaskProc)(GLuint);
    XGL_CALL_REAL_VOID("glStencilMask", GlStencilMaskProc, mask);
}

__declspec(dllexport) void __stdcall glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    typedef void (__stdcall* GlStencilOpProc)(GLenum, GLenum, GLenum);
    XGL_CALL_REAL_VOID("glStencilOp", GlStencilOpProc, fail, zfail, zpass);
}

__declspec(dllexport) void __stdcall glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* pointer) {
    LogGlCallProbe("glTexCoordPointer");
    typedef void (__stdcall* GlTexCoordPointerProc)(GLint, GLenum, GLsizei, const void*);
    XGL_CALL_REAL_VOID("glTexCoordPointer", GlTexCoordPointerProc, size, type, stride, pointer);
}

__declspec(dllexport) void __stdcall glTexEnvf(GLenum target, GLenum pname, GLfloat param) {
    typedef void (__stdcall* GlTexEnvfProc)(GLenum, GLenum, GLfloat);
    XGL_CALL_REAL_VOID("glTexEnvf", GlTexEnvfProc, target, pname, param);
}

__declspec(dllexport) void __stdcall glTexEnvfv(GLenum target, GLenum pname, const GLfloat* params) {
    typedef void (__stdcall* GlTexEnvfvProc)(GLenum, GLenum, const GLfloat*);
    XGL_CALL_REAL_VOID("glTexEnvfv", GlTexEnvfvProc, target, pname, params);
}

__declspec(dllexport) void __stdcall glTexEnvi(GLenum target, GLenum pname, GLint param) {
    typedef void (__stdcall* GlTexEnviProc)(GLenum, GLenum, GLint);
    XGL_CALL_REAL_VOID("glTexEnvi", GlTexEnviProc, target, pname, param);
}

__declspec(dllexport) void __stdcall glTexEnviv(GLenum target, GLenum pname, const GLint* params) {
    typedef void (__stdcall* GlTexEnvivProc)(GLenum, GLenum, const GLint*);
    XGL_CALL_REAL_VOID("glTexEnviv", GlTexEnvivProc, target, pname, params);
}

__declspec(dllexport) void __stdcall glTranslated(GLdouble x, GLdouble y, GLdouble z) {
    typedef void (__stdcall* GlTranslatedProc)(GLdouble, GLdouble, GLdouble);
    XGL_CALL_REAL_VOID("glTranslated", GlTranslatedProc, x, y, z);
}

__declspec(dllexport) void __stdcall glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    LogGlCallProbe("glTranslatef");
    typedef void (__stdcall* GlTranslatefProc)(GLfloat, GLfloat, GLfloat);
    XGL_CALL_REAL_VOID("glTranslatef", GlTranslatefProc, x, y, z);
}

__declspec(dllexport) void __stdcall glVertexPointer(GLint size, GLenum type, GLsizei stride, const void* pointer) {
    LogGlCallProbe("glVertexPointer");
    typedef void (__stdcall* GlVertexPointerProc)(GLint, GLenum, GLsizei, const void*);
    XGL_CALL_REAL_VOID("glVertexPointer", GlVertexPointerProc, size, type, stride, pointer);
}

__declspec(dllexport) void __stdcall glPixelStorei(GLenum pname, GLint param) {
    typedef void (__stdcall* GlPixelStoreiProc)(GLenum, GLint);
    auto real = XGL_CACHED_REAL_PROC("glPixelStorei", GlPixelStoreiProc);
    if (real) {
        __try {
            real(pname, param);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    switch (pname) {
        case GL_UNPACK_ROW_LENGTH:
            g_unpackRowLength = param > 0 ? param : 0;
            break;
        case GL_UNPACK_SKIP_ROWS:
            g_unpackSkipRows = param > 0 ? param : 0;
            break;
        case GL_UNPACK_SKIP_PIXELS:
            g_unpackSkipPixels = param > 0 ? param : 0;
            break;
        case GL_UNPACK_ALIGNMENT:
            g_unpackAlignment = param > 0 ? param : 4;
            break;
        case GL_UNPACK_SWAP_BYTES:
        case GL_UNPACK_LSB_FIRST:
        default:
            break;
    }
}
__declspec(dllexport) void __stdcall glPixelStoref(GLenum pname, GLfloat param) {
    typedef void (__stdcall* GlPixelStorefProc)(GLenum, GLfloat);
    auto real = XGL_CACHED_REAL_PROC("glPixelStoref", GlPixelStorefProc);
    if (real) {
        __try {
            real(pname, param);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    glPixelStorei(pname, static_cast<GLint>(param));
}

static void GLAPI_glTexStorage2D(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height) {
    typedef void (__stdcall* GlTexStorage2DProc)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glTexStorage2D", GlTexStorage2DProc, target, levels, internalFormat, width, height);
    StoreTextureRegion(target, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr, true);
    PublishTextureUpload(false, target, levels, static_cast<GLint>(internalFormat), width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, -1);
}

static void GLAPI_glTexStorage3D(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth) {
    typedef void (__stdcall* GlTexStorage3DProc)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glTexStorage3D", GlTexStorage3DProc, target, levels, internalFormat, width, height, depth);
    StoreTextureRegion(target, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr, true);
    PublishTextureUpload(false, target, levels, static_cast<GLint>(internalFormat), width, height, depth, GL_RGBA, GL_UNSIGNED_BYTE, -1);
}

static void GLAPI_glTextureStorage2D(GLuint texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height) {
    typedef void (__stdcall* GlTextureStorage2DProc)(GLuint, GLsizei, GLenum, GLsizei, GLsizei);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glTextureStorage2D", GlTextureStorage2DProc, texture, levels, internalFormat, width, height);
    StoreTextureRegionForName(texture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr, true);
    PublishTextureUploadForName(false, GL_TEXTURE_2D, texture, levels, static_cast<GLint>(internalFormat), width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, -1);
}

static void GLAPI_glTextureSubImage2D(
    GLuint texture,
    GLint level,
    GLint xoffset,
    GLint yoffset,
    GLsizei width,
    GLsizei height,
    GLenum format,
    GLenum type,
    const void* pixels) {
    typedef void (__stdcall* GlTextureSubImage2DProc)(GLuint, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glTextureSubImage2D", GlTextureSubImage2DProc, texture, level, xoffset, yoffset, width, height, format, type, pixels);
    StoreTextureRegionForName(texture, level, xoffset, yoffset, width, height, format, type, pixels, false);
    PublishTextureUploadForName(true, GL_TEXTURE_2D, texture, level, 0, width, height, 1, format, type, -1);
    PublishTextureSampleForName(GL_TEXTURE_2D, texture, level, width, height, format, type, pixels);
}

static void PublishTextureParameterChange(GLuint texture) {
    auto* state = EnsureGlCommandState();
    if (!state) return;
    state->lastTextureTarget = GL_TEXTURE_2D;
    state->lastTextureName = texture;
    InterlockedIncrement64(&state->textureParameterSerial);
}

static void GLAPI_glTextureParameteri(GLuint texture, GLenum pname, GLint param) {
    typedef void (__stdcall* GlTextureParameteriProc)(GLuint, GLenum, GLint);
    XGL_CALL_REAL_VOID("glTextureParameteri", GlTextureParameteriProc, texture, pname, param);
    PublishTextureParameterChange(texture);
}

static void GLAPI_glTextureParameterf(GLuint texture, GLenum pname, GLfloat param) {
    typedef void (__stdcall* GlTextureParameterfProc)(GLuint, GLenum, GLfloat);
    XGL_CALL_REAL_VOID("glTextureParameterf", GlTextureParameterfProc, texture, pname, param);
    PublishTextureParameterChange(texture);
}

static void GLAPI_glTextureParameteriv(GLuint texture, GLenum pname, const GLint* params) {
    typedef void (__stdcall* GlTextureParameterivProc)(GLuint, GLenum, const GLint*);
    XGL_CALL_REAL_VOID("glTextureParameteriv", GlTextureParameterivProc, texture, pname, params);
    PublishTextureParameterChange(texture);
}

static void GLAPI_glTextureParameterfv(GLuint texture, GLenum pname, const GLfloat* params) {
    typedef void (__stdcall* GlTextureParameterfvProc)(GLuint, GLenum, const GLfloat*);
    XGL_CALL_REAL_VOID("glTextureParameterfv", GlTextureParameterfvProc, texture, pname, params);
    PublishTextureParameterChange(texture);
}

static void GLAPI_glTextureStorage3D(GLuint texture, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height, GLsizei depth) {
    typedef void (__stdcall* GlTextureStorage3DProc)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glTextureStorage3D", GlTextureStorage3DProc, texture, levels, internalFormat, width, height, depth);
    StoreTextureRegionForName(texture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr, true);
    PublishTextureUploadForName(false, GL_TEXTURE_2D, texture, levels, static_cast<GLint>(internalFormat), width, height, depth, GL_RGBA, GL_UNSIGNED_BYTE, -1);
}

static void GLAPI_glTexImage3D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLint, GLenum format, GLenum type, const void* pixels) {
    typedef void (__stdcall* GlTexImage3DProc)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glTexImage3D", GlTexImage3DProc, target, level, internalFormat, width, height, depth, 0, format, type, pixels);
    StoreTextureRegion(target, level, 0, 0, width, height, format, type, pixels, true);
    PublishTextureUpload(false, target, level, internalFormat, width, height, depth, format, type, -1);
    PublishTextureSample(target, level, width, height, format, type, pixels);
}

static void GLAPI_glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void* pixels) {
    typedef void (__stdcall* GlTexSubImage3DProc)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glTexSubImage3D", GlTexSubImage3DProc, target, level, xoffset, yoffset, 0, width, height, depth, format, type, pixels);
    StoreTextureRegion(target, level, xoffset, yoffset, width, height, format, type, pixels, false);
    PublishTextureUpload(true, target, level, 0, width, height, depth, format, type, -1);
    PublishTextureSample(target, level, width, height, format, type, pixels);
}

static void GLAPI_glTextureSubImage3D(
    GLuint texture,
    GLint level,
    GLint xoffset,
    GLint yoffset,
    GLint,
    GLsizei width,
    GLsizei height,
    GLsizei depth,
    GLenum format,
    GLenum type,
    const void* pixels) {
    typedef void (__stdcall* GlTextureSubImage3DProc)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glTextureSubImage3D", GlTextureSubImage3DProc, texture, level, xoffset, yoffset, 0, width, height, depth, format, type, pixels);
    StoreTextureRegionForName(texture, level, xoffset, yoffset, width, height, format, type, pixels, false);
    PublishTextureUploadForName(true, GL_TEXTURE_2D, texture, level, 0, width, height, depth, format, type, -1);
    PublishTextureSampleForName(GL_TEXTURE_2D, texture, level, width, height, format, type, pixels);
}

static void GLAPI_glCompressedTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void* data) {
    typedef void (__stdcall* GlCompressedTexImage2DProc)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glCompressedTexImage2D", GlCompressedTexImage2DProc, target, level, internalFormat, width, height, border, imageSize, data);
    PublishTextureUpload(false, target, level, static_cast<GLint>(internalFormat), width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, imageSize > 0 ? imageSize : 0);
}

static void GLAPI_glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data) {
    typedef void (__stdcall* GlCompressedTexSubImage2DProc)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glCompressedTexSubImage2D", GlCompressedTexSubImage2DProc, target, level, xoffset, yoffset, width, height, format, imageSize, data);
    PublishTextureUpload(true, target, level, 0, width, height, 1, format, GL_UNSIGNED_BYTE, imageSize > 0 ? imageSize : 0);
}

static void GLAPI_glGenerateMipmap(GLenum target) {
    typedef void (__stdcall* GlGenerateMipmapProc)(GLenum);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glGenerateMipmap", GlGenerateMipmapProc, target);
    PublishTextureParameterChange(BoundTextureNameForTarget(target));
}

static void GLAPI_glGenerateTextureMipmap(GLuint texture) {
    typedef void (__stdcall* GlGenerateTextureMipmapProc)(GLuint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glGenerateTextureMipmap", GlGenerateTextureMipmapProc, texture);
    PublishTextureParameterChange(texture);
}

static void GLAPI_glBindFramebuffer(GLenum target, GLuint framebuffer) {
    typedef void (__stdcall* GlBindFramebufferProc)(GLenum, GLuint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingFramebuffer, "glBindFramebuffer", GlBindFramebufferProc, target, framebuffer);
    if (target == GL_FRAMEBUFFER) {
        g_boundDrawFramebuffer = framebuffer;
        g_boundReadFramebuffer = framebuffer;
    } else if (target == GL_DRAW_FRAMEBUFFER) {
        g_boundDrawFramebuffer = framebuffer;
    } else if (target == GL_READ_FRAMEBUFFER) {
        g_boundReadFramebuffer = framebuffer;
    }

    if (framebuffer != 0) {
        FindFramebuffer(framebuffer, true);
    }
    PublishFramebufferState(true, false, false, 0, 0, 0, 0);
}

static void AttachFramebufferTexture(
    GLuint framebuffer,
    GLenum attachment,
    GLenum textarget,
    GLuint texture,
    GLint level) {
    if (framebuffer == 0 ||
        attachment < GL_COLOR_ATTACHMENT0 ||
        attachment > GL_COLOR_ATTACHMENT15) {
        PublishFramebufferState(false, true, false, 0, 0, 0, 0);
        return;
    }

    auto* record = FindFramebuffer(framebuffer, true);
    if (!record) {
        PublishFramebufferState(false, true, false, 0, 0, 0, 0);
        return;
    }

    record->colorTexture = texture;
    record->colorTarget = textarget;
    record->colorLevel = level;
    if (texture != 0) {
        auto* textureRecord = FindTexture(texture, true);
        if (textureRecord) {
            textureRecord->renderTarget = true;
        }
    }
    PublishFramebufferState(false, true, false, 0, 0, 0, 0);
}

static void GLAPI_glFramebufferTexture2D(
    GLenum target,
    GLenum attachment,
    GLenum textarget,
    GLuint texture,
    GLint level) {
    typedef void (__stdcall* GlFramebufferTexture2DProc)(GLenum, GLenum, GLenum, GLuint, GLint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingFramebuffer, "glFramebufferTexture2D", GlFramebufferTexture2DProc, target, attachment, textarget, texture, level);
    AttachFramebufferTexture(FramebufferNameForTarget(target), attachment, textarget, texture, level);
}

static void GLAPI_glFramebufferTexture(
    GLenum target,
    GLenum attachment,
    GLuint texture,
    GLint level) {
    typedef void (__stdcall* GlFramebufferTextureProc)(GLenum, GLenum, GLuint, GLint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingFramebuffer, "glFramebufferTexture", GlFramebufferTextureProc, target, attachment, texture, level);
    AttachFramebufferTexture(FramebufferNameForTarget(target), attachment, GL_TEXTURE_2D, texture, level);
}

static void GLAPI_glNamedFramebufferTexture(
    GLuint framebuffer,
    GLenum attachment,
    GLuint texture,
    GLint level) {
    typedef void (__stdcall* GlNamedFramebufferTextureProc)(GLuint, GLenum, GLuint, GLint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingFramebuffer, "glNamedFramebufferTexture", GlNamedFramebufferTextureProc, framebuffer, attachment, texture, level);
    AttachFramebufferTexture(framebuffer, attachment, GL_TEXTURE_2D, texture, level);
}

static void GLAPI_glNamedFramebufferTextureLayer(
    GLuint framebuffer,
    GLenum attachment,
    GLuint texture,
    GLint level,
    GLint /*layer*/) {
    typedef void (__stdcall* GlNamedFramebufferTextureLayerProc)(GLuint, GLenum, GLuint, GLint, GLint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingFramebuffer, "glNamedFramebufferTextureLayer", GlNamedFramebufferTextureLayerProc, framebuffer, attachment, texture, level, 0);
    AttachFramebufferTexture(framebuffer, attachment, GL_TEXTURE_2D, texture, level);
}

static void GLAPI_glFramebufferTextureLayer(
    GLenum target,
    GLenum attachment,
    GLuint texture,
    GLint level,
    GLint /*layer*/) {
    typedef void (__stdcall* GlFramebufferTextureLayerProc)(GLenum, GLenum, GLuint, GLint, GLint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingFramebuffer, "glFramebufferTextureLayer", GlFramebufferTextureLayerProc, target, attachment, texture, level, 0);
    AttachFramebufferTexture(FramebufferNameForTarget(target), attachment, GL_TEXTURE_2D, texture, level);
}

static void GLAPI_glBlitFramebuffer(
    GLint srcX0,
    GLint srcY0,
    GLint srcX1,
    GLint srcY1,
    GLint dstX0,
    GLint dstY0,
    GLint dstX1,
    GLint dstY1,
    GLbitfield mask,
    GLenum filter) {
    GLint realDstX0 = dstX0;
    GLint realDstY0 = dstY0;
    GLsizei realDstWidth = static_cast<GLsizei>(std::abs(dstX1 - dstX0));
    GLsizei realDstHeight = static_cast<GLsizei>(std::abs(dstY1 - dstY0));
    GLint scaledDstX = dstX0;
    GLint scaledDstY = dstY0;
    GLsizei scaledDstWidth = realDstWidth;
    GLsizei scaledDstHeight = realDstHeight;
    if (ShouldScaleDefaultFramebufferRect(
        g_boundDrawFramebuffer,
        dstX0,
        dstY0,
        realDstWidth,
        realDstHeight,
        scaledDstX,
        scaledDstY,
        scaledDstWidth,
        scaledDstHeight)) {
        LogUpscaleRectOnce("glBlitFramebuffer", dstX0, dstY0, realDstWidth, realDstHeight, scaledDstX, scaledDstY, scaledDstWidth, scaledDstHeight);
        realDstX0 = scaledDstX;
        realDstY0 = scaledDstY;
        dstX1 = realDstX0 + (dstX1 >= dstX0 ? scaledDstWidth : -scaledDstWidth);
        dstY1 = realDstY0 + (dstY1 >= dstY0 ? scaledDstHeight : -scaledDstHeight);
    }

    typedef void (__stdcall* GlBlitFramebufferProc)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingFramebuffer, "glBlitFramebuffer", GlBlitFramebufferProc, srcX0, srcY0, srcX1, srcY1, realDstX0, realDstY0, dstX1, dstY1, mask, filter);
    if ((mask & GL_COLOR_BUFFER_BIT) == 0) {
        return;
    }

    const int width = ClampInt(static_cast<int>(std::abs(dstX1 - realDstX0)), 1, 16384);
    const int height = ClampInt(static_cast<int>(std::abs(dstY1 - realDstY0)), 1, 16384);
    (void)srcX0;
    (void)srcY0;
    (void)srcX1;
    (void)srcY1;
    BlitFramebufferContents(g_boundReadFramebuffer, g_boundDrawFramebuffer, width, height);
}

static void GLAPI_glBlitNamedFramebuffer(
    GLuint readFramebuffer,
    GLuint drawFramebuffer,
    GLint srcX0,
    GLint srcY0,
    GLint srcX1,
    GLint srcY1,
    GLint dstX0,
    GLint dstY0,
    GLint dstX1,
    GLint dstY1,
    GLbitfield mask,
    GLenum filter) {
    GLint realDstX0 = dstX0;
    GLint realDstY0 = dstY0;
    GLsizei realDstWidth = static_cast<GLsizei>(std::abs(dstX1 - dstX0));
    GLsizei realDstHeight = static_cast<GLsizei>(std::abs(dstY1 - dstY0));
    GLint scaledDstX = dstX0;
    GLint scaledDstY = dstY0;
    GLsizei scaledDstWidth = realDstWidth;
    GLsizei scaledDstHeight = realDstHeight;
    if (ShouldScaleDefaultFramebufferRect(
        drawFramebuffer,
        dstX0,
        dstY0,
        realDstWidth,
        realDstHeight,
        scaledDstX,
        scaledDstY,
        scaledDstWidth,
        scaledDstHeight)) {
        LogUpscaleRectOnce("glBlitNamedFramebuffer", dstX0, dstY0, realDstWidth, realDstHeight, scaledDstX, scaledDstY, scaledDstWidth, scaledDstHeight);
        realDstX0 = scaledDstX;
        realDstY0 = scaledDstY;
        dstX1 = realDstX0 + (dstX1 >= dstX0 ? scaledDstWidth : -scaledDstWidth);
        dstY1 = realDstY0 + (dstY1 >= dstY0 ? scaledDstHeight : -scaledDstHeight);
    }

    typedef void (__stdcall* GlBlitNamedFramebufferProc)(GLuint, GLuint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingFramebuffer, "glBlitNamedFramebuffer", GlBlitNamedFramebufferProc, readFramebuffer, drawFramebuffer, srcX0, srcY0, srcX1, srcY1, realDstX0, realDstY0, dstX1, dstY1, mask, filter);
    if ((mask & GL_COLOR_BUFFER_BIT) == 0) {
        return;
    }

    const int width = ClampInt(static_cast<int>(std::abs(dstX1 - realDstX0)), 1, 16384);
    const int height = ClampInt(static_cast<int>(std::abs(dstY1 - realDstY0)), 1, 16384);
    (void)srcX0;
    (void)srcY0;
    (void)srcX1;
    (void)srcY1;
    BlitFramebufferContents(readFramebuffer, drawFramebuffer, width, height);
}

__declspec(dllexport) void __stdcall glCopyTexImage2D(
    GLenum target,
    GLint level,
    GLenum internalFormat,
    GLint x,
    GLint y,
    GLsizei width,
    GLsizei height,
    GLint /*border*/) {
    typedef void (__stdcall* GlCopyTexImage2DProc)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint);
    auto real = XGL_CACHED_REAL_PROC("glCopyTexImage2D", GlCopyTexImage2DProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            real(target, level, internalFormat, x, y, width, height, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingTextureUpload, timingStartMs);
    }

    if (level != 0 || target != GL_TEXTURE_2D || width <= 0 || height <= 0) {
        return;
    }

    auto* dest = FindTexture(BoundTextureNameForTarget(target), true);
    if (!dest || !ResizeTexture(dest, width, height)) {
        return;
    }

    PixelSource source = {};
    if (ResolveReadPixelSource(source)) {
        CopyPixelSourceToTexture(source, dest, 0, 0, width, height);
    }
    PublishTextureUpload(false, target, level, static_cast<GLint>(internalFormat), width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, -1);
    (void)x;
    (void)y;
}

__declspec(dllexport) void __stdcall glCopyTexSubImage2D(
    GLenum target,
    GLint level,
    GLint xoffset,
    GLint yoffset,
    GLint x,
    GLint y,
    GLsizei width,
    GLsizei height) {
    typedef void (__stdcall* GlCopyTexSubImage2DProc)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
    auto real = XGL_CACHED_REAL_PROC("glCopyTexSubImage2D", GlCopyTexSubImage2DProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            real(target, level, xoffset, yoffset, x, y, width, height);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingTextureUpload, timingStartMs);
    }

    if (level != 0 || target != GL_TEXTURE_2D || width <= 0 || height <= 0) {
        return;
    }

    auto* dest = FindTexture(BoundTextureNameForTarget(target), true);
    PixelSource source = {};
    if (dest && ResolveReadPixelSource(source)) {
        CopyPixelSourceToTexture(source, dest, xoffset, yoffset, width, height);
    }
    PublishTextureUpload(true, target, level, 0, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, -1);
    (void)x;
    (void)y;
}

static void GLAPI_glCopyTextureSubImage2D(
    GLuint texture,
    GLint level,
    GLint xoffset,
    GLint yoffset,
    GLint x,
    GLint y,
    GLsizei width,
    GLsizei height) {
    typedef void (__stdcall* GlCopyTextureSubImage2DProc)(GLuint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingTextureUpload, "glCopyTextureSubImage2D", GlCopyTextureSubImage2DProc, texture, level, xoffset, yoffset, x, y, width, height);
    if (level != 0 || width <= 0 || height <= 0) {
        return;
    }

    auto* dest = FindTexture(texture, true);
    PixelSource source = {};
    if (dest && ResolveReadPixelSource(source)) {
        CopyPixelSourceToTexture(source, dest, xoffset, yoffset, width, height);
    }
    PublishTextureUploadForName(true, GL_TEXTURE_2D, texture, level, 0, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, -1);
    (void)x;
    (void)y;
}

__declspec(dllexport) void __stdcall glDrawBuffer(GLenum buf) {
    typedef void (__stdcall* GlDrawBufferProc)(GLenum);
    auto real = XGL_CACHED_REAL_PROC("glDrawBuffer", GlDrawBufferProc);
    if (real) {
        __try {
            real(buf);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    PublishFramebufferState(false, false, false, 0, 0, 0, 0);
}

__declspec(dllexport) void __stdcall glReadBuffer(GLenum src) {
    typedef void (__stdcall* GlReadBufferProc)(GLenum);
    auto real = XGL_CACHED_REAL_PROC("glReadBuffer", GlReadBufferProc);
    if (real) {
        __try {
            real(src);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    PublishFramebufferState(false, false, false, 0, 0, 0, 0);
}

static void GLAPI_glDrawBuffers(GLsizei n, const GLenum* bufs) {
    typedef void (__stdcall* GlDrawBuffersProc)(GLsizei, const GLenum*);
    XGL_CALL_REAL_VOID("glDrawBuffers", GlDrawBuffersProc, n, bufs);
    PublishFramebufferState(false, false, false, 0, 0, 0, 0);
}

static void GLAPI_glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value) {
    typedef void (__stdcall* GlClearBufferfvProc)(GLenum, GLint, const GLfloat*);
    XGL_CALL_REAL_VOID("glClearBufferfv", GlClearBufferfvProc, buffer, drawbuffer, value);
    if (buffer != GL_COLOR || !value) {
        return;
    }

    ClearCurrentDrawTarget(PackColor(value[0], value[1], value[2], value[3]));
}

static void GLAPI_glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint* value) {
    typedef void (__stdcall* GlClearBufferivProc)(GLenum, GLint, const GLint*);
    XGL_CALL_REAL_VOID("glClearBufferiv", GlClearBufferivProc, buffer, drawbuffer, value);
    if (buffer != GL_COLOR || !value) {
        return;
    }

    GLfloat color[4] = {
        value[0] / 255.0f,
        value[1] / 255.0f,
        value[2] / 255.0f,
        value[3] / 255.0f
    };
    (void)drawbuffer;
    ClearCurrentDrawTarget(PackColor(color[0], color[1], color[2], color[3]));
}

static void GLAPI_glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint* value) {
    typedef void (__stdcall* GlClearBufferuivProc)(GLenum, GLint, const GLuint*);
    XGL_CALL_REAL_VOID("glClearBufferuiv", GlClearBufferuivProc, buffer, drawbuffer, value);
    if (buffer != GL_COLOR || !value) {
        return;
    }

    GLfloat color[4] = {
        value[0] / 255.0f,
        value[1] / 255.0f,
        value[2] / 255.0f,
        value[3] / 255.0f
    };
    (void)drawbuffer;
    ClearCurrentDrawTarget(PackColor(color[0], color[1], color[2], color[3]));
}

static void GLAPI_glClearNamedFramebufferfv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat* value) {
    typedef void (__stdcall* GlClearNamedFramebufferfvProc)(GLuint, GLenum, GLint, const GLfloat*);
    XGL_CALL_REAL_VOID("glClearNamedFramebufferfv", GlClearNamedFramebufferfvProc, framebuffer, buffer, drawbuffer, value);
    const GLuint previousDraw = g_boundDrawFramebuffer;
    g_boundDrawFramebuffer = framebuffer;
    if (buffer == GL_COLOR && value) {
        ClearCurrentDrawTarget(PackColor(value[0], value[1], value[2], value[3]));
    }
    g_boundDrawFramebuffer = previousDraw;
}

__declspec(dllexport) void __stdcall glBindBuffer(GLenum target, GLuint buffer) {
    typedef void (__stdcall* GlBindBufferProc)(GLenum, GLuint);
    auto real = XGL_CACHED_REAL_PROC("glBindBuffer", GlBindBufferProc);
    if (real) {
        __try {
            real(target, buffer);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    SetBoundBufferForTarget(target, buffer);
    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        SyncCurrentVertexArrayState();
    }
    PublishBufferBind(target, buffer);
}

__declspec(dllexport) void __stdcall glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    typedef void (__stdcall* GlBufferDataProc)(GLenum, GLsizeiptr, const void*, GLenum);
    auto real = XGL_CACHED_REAL_PROC("glBufferData", GlBufferDataProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            real(target, size, data, usage);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingBufferUpload, timingStartMs);
    }
    const GLuint buffer = BoundBufferForTarget(target);
    StoreBufferBytes(buffer, size, data);
    PublishBufferUpload(false, target, buffer, size, usage);
}

static void GLAPI_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    typedef void (__stdcall* GlBufferSubDataProc)(GLenum, GLintptr, GLsizeiptr, const void*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingBufferUpload, "glBufferSubData", GlBufferSubDataProc, target, offset, size, data);
    const GLuint buffer = BoundBufferForTarget(target);
    StoreBufferSubBytes(buffer, offset, size, data);
    PublishBufferUpload(true, target, buffer, size, 0);
}

static void GLAPI_glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    typedef void (__stdcall* GlBindBufferBaseProc)(GLenum, GLuint, GLuint);
    XGL_CALL_REAL_VOID("glBindBufferBase", GlBindBufferBaseProc, target, index, buffer);
    SetBoundBufferForTarget(target, buffer);
    PublishBufferBind(target, buffer);
}

static void GLAPI_glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    typedef void (__stdcall* GlBindBufferRangeProc)(GLenum, GLuint, GLuint, GLintptr, GLsizeiptr);
    XGL_CALL_REAL_VOID("glBindBufferRange", GlBindBufferRangeProc, target, index, buffer, offset, size);
    SetBoundBufferForTarget(target, buffer);
    PublishBufferBind(target, buffer);
}

__declspec(dllexport) void __stdcall glBindVertexArray(GLuint array) {
    typedef void (__stdcall* GlBindVertexArrayProc)(GLuint);
    auto real = XGL_CACHED_REAL_PROC("glBindVertexArray", GlBindVertexArrayProc);
    if (real) {
        __try {
            real(array);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    SaveCurrentVertexArrayState();
    g_boundVertexArray = array;
    LoadVertexArrayState(array);
    auto* state = EnsureGlCommandState();
    if (!state) return;
    state->lastVertexArray = array;
    InterlockedIncrement64(&state->vertexArrayBindSerial);
}

static void GLAPI_glEnableVertexAttribArray(GLuint index) {
    typedef void (__stdcall* GlEnableVertexAttribArrayProc)(GLuint);
    XGL_CALL_REAL_VOID("glEnableVertexAttribArray", GlEnableVertexAttribArrayProc, index);
    if (index < 16) {
        g_attribs[index].enabled = true;
        SyncCurrentVertexArrayState();
    }
    PublishVertexAttrib(index, 0, 0, GL_FALSE, 0, nullptr);
}

static void GLAPI_glDisableVertexAttribArray(GLuint index) {
    typedef void (__stdcall* GlDisableVertexAttribArrayProc)(GLuint);
    XGL_CALL_REAL_VOID("glDisableVertexAttribArray", GlDisableVertexAttribArrayProc, index);
    if (index < 16) {
        g_attribs[index].enabled = false;
        SyncCurrentVertexArrayState();
    }
    PublishVertexAttrib(index, 0, 0, GL_FALSE, 0, nullptr);
}

static void GLAPI_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer) {
    typedef void (__stdcall* GlVertexAttribPointerProc)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    XGL_CALL_REAL_VOID("glVertexAttribPointer", GlVertexAttribPointerProc, index, size, type, normalized, stride, pointer);
    if (index < 16) {
        g_attribs[index].separateBinding = false;
        g_attribs[index].size = size;
        g_attribs[index].type = type;
        g_attribs[index].normalized = normalized;
        g_attribs[index].stride = stride;
        g_attribs[index].pointer = reinterpret_cast<uintptr_t>(pointer);
        g_attribs[index].relativeOffset = 0;
        g_attribs[index].bindingIndex = index;
        g_attribs[index].buffer = g_boundArrayBuffer;
        SyncCurrentVertexArrayState();
    }
    PublishVertexAttrib(index, size, type, normalized, stride, pointer);
}

static void GLAPI_glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer) {
    typedef void (__stdcall* GlVertexAttribIPointerProc)(GLuint, GLint, GLenum, GLsizei, const void*);
    XGL_CALL_REAL_VOID("glVertexAttribIPointer", GlVertexAttribIPointerProc, index, size, type, stride, pointer);
    if (index < 16) {
        g_attribs[index].separateBinding = false;
        g_attribs[index].size = size;
        g_attribs[index].type = type;
        g_attribs[index].normalized = GL_FALSE;
        g_attribs[index].stride = stride;
        g_attribs[index].pointer = reinterpret_cast<uintptr_t>(pointer);
        g_attribs[index].relativeOffset = 0;
        g_attribs[index].bindingIndex = index;
        g_attribs[index].buffer = g_boundArrayBuffer;
        SyncCurrentVertexArrayState();
    }
    PublishVertexAttrib(index, size, type, GL_FALSE, stride, pointer);
}

static void GLAPI_glVertexAttribFormat(GLuint index, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset) {
    typedef void (__stdcall* GlVertexAttribFormatProc)(GLuint, GLint, GLenum, GLboolean, GLuint);
    XGL_CALL_REAL_VOID("glVertexAttribFormat", GlVertexAttribFormatProc, index, size, type, normalized, relativeoffset);
    if (index < 16) {
        g_attribs[index].separateBinding = true;
        g_attribs[index].size = size;
        g_attribs[index].type = type;
        g_attribs[index].normalized = normalized;
        g_attribs[index].relativeOffset = relativeoffset;
        if (!g_attribs[index].bindingSet) {
            g_attribs[index].bindingIndex = index;
        }
        SyncCurrentVertexArrayState();
    }
    PublishVertexAttrib(index, size, type, normalized, 0, nullptr);
}

static void GLAPI_glVertexAttribIFormat(GLuint index, GLint size, GLenum type, GLuint relativeoffset) {
    typedef void (__stdcall* GlVertexAttribIFormatProc)(GLuint, GLint, GLenum, GLuint);
    XGL_CALL_REAL_VOID("glVertexAttribIFormat", GlVertexAttribIFormatProc, index, size, type, relativeoffset);
    if (index < 16) {
        g_attribs[index].separateBinding = true;
        g_attribs[index].size = size;
        g_attribs[index].type = type;
        g_attribs[index].normalized = GL_FALSE;
        g_attribs[index].relativeOffset = relativeoffset;
        if (!g_attribs[index].bindingSet) {
            g_attribs[index].bindingIndex = index;
        }
        SyncCurrentVertexArrayState();
    }
    PublishVertexAttrib(index, size, type, GL_FALSE, 0, nullptr);
}

static void GLAPI_glVertexAttribBinding(GLuint index, GLuint bindingindex) {
    typedef void (__stdcall* GlVertexAttribBindingProc)(GLuint, GLuint);
    XGL_CALL_REAL_VOID("glVertexAttribBinding", GlVertexAttribBindingProc, index, bindingindex);
    if (index < 16 && bindingindex < 16) {
        g_attribs[index].separateBinding = true;
        g_attribs[index].bindingSet = true;
        g_attribs[index].bindingIndex = bindingindex;
        SyncCurrentVertexArrayState();
    }
    PublishVertexAttrib(index, 0, 0, GL_FALSE, 0, nullptr);
}

static void GLAPI_glBindVertexBuffer(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride) {
    typedef void (__stdcall* GlBindVertexBufferProc)(GLuint, GLuint, GLintptr, GLsizei);
    XGL_CALL_REAL_VOID("glBindVertexBuffer", GlBindVertexBufferProc, bindingindex, buffer, offset, stride);
    if (bindingindex < 16) {
        g_vertexBindings[bindingindex].buffer = buffer;
        g_vertexBindings[bindingindex].offset = offset > 0 ? offset : 0;
        g_vertexBindings[bindingindex].stride = stride;
        SyncCurrentVertexArrayState();
    }
    PublishBufferBind(GL_ARRAY_BUFFER, buffer);
    PublishVertexAttrib(0, 0, 0, GL_FALSE, stride, nullptr);
}

static void GLAPI_glBindVertexBuffers(
    GLuint first,
    GLsizei count,
    const GLuint* buffers,
    const GLintptr* offsets,
    const GLsizei* strides) {
    typedef void (__stdcall* GlBindVertexBuffersProc)(GLuint, GLsizei, const GLuint*, const GLintptr*, const GLsizei*);
    XGL_CALL_REAL_VOID("glBindVertexBuffers", GlBindVertexBuffersProc, first, count, buffers, offsets, strides);
    if (count <= 0 || first >= 16) {
        return;
    }

    const GLuint end = first + static_cast<GLuint>(count);
    for (GLuint binding = first; binding < end && binding < 16; ++binding) {
        const GLuint sourceIndex = binding - first;
        const GLuint buffer = buffers ? buffers[sourceIndex] : 0;
        const GLintptr offset = offsets ? offsets[sourceIndex] : 0;
        const GLsizei stride = strides ? strides[sourceIndex] : 0;
        g_vertexBindings[binding].buffer = buffer;
        g_vertexBindings[binding].offset = offset > 0 ? offset : 0;
        g_vertexBindings[binding].stride = stride;
        SyncCurrentVertexArrayState();
        PublishBufferBind(GL_ARRAY_BUFFER, buffer);
    }
}

static void GLAPI_glEnableVertexArrayAttrib(GLuint vaobj, GLuint index) {
    typedef void (__stdcall* GlEnableVertexArrayAttribProc)(GLuint, GLuint);
    XGL_CALL_REAL_VOID("glEnableVertexArrayAttrib", GlEnableVertexArrayAttribProc, vaobj, index);
    if (index < 16) {
        AttribRecord attrib = {};
        if (vaobj == g_boundVertexArray) {
            attrib = g_attribs[index];
        } else if (auto* record = FindVertexArray(vaobj, true)) {
            attrib = record->attribs[index];
        }
        attrib.enabled = true;
        ModifyVertexArrayAttrib(vaobj, index, attrib);
    }
    PublishVertexAttrib(index, 0, 0, GL_FALSE, 0, nullptr);
}

static void GLAPI_glDisableVertexArrayAttrib(GLuint vaobj, GLuint index) {
    typedef void (__stdcall* GlDisableVertexArrayAttribProc)(GLuint, GLuint);
    XGL_CALL_REAL_VOID("glDisableVertexArrayAttrib", GlDisableVertexArrayAttribProc, vaobj, index);
    if (index < 16) {
        AttribRecord attrib = {};
        if (vaobj == g_boundVertexArray) {
            attrib = g_attribs[index];
        } else if (auto* record = FindVertexArray(vaobj, true)) {
            attrib = record->attribs[index];
        }
        attrib.enabled = false;
        ModifyVertexArrayAttrib(vaobj, index, attrib);
    }
    PublishVertexAttrib(index, 0, 0, GL_FALSE, 0, nullptr);
}

static void GLAPI_glVertexArrayAttribFormat(
    GLuint vaobj,
    GLuint attribindex,
    GLint size,
    GLenum type,
    GLboolean normalized,
    GLuint relativeoffset) {
    typedef void (__stdcall* GlVertexArrayAttribFormatProc)(GLuint, GLuint, GLint, GLenum, GLboolean, GLuint);
    XGL_CALL_REAL_VOID("glVertexArrayAttribFormat", GlVertexArrayAttribFormatProc, vaobj, attribindex, size, type, normalized, relativeoffset);
    if (attribindex < 16) {
        AttribRecord attrib = {};
        if (vaobj == g_boundVertexArray) {
            attrib = g_attribs[attribindex];
        } else if (auto* record = FindVertexArray(vaobj, true)) {
            attrib = record->attribs[attribindex];
        }
        attrib.separateBinding = true;
        attrib.size = size;
        attrib.type = type;
        attrib.normalized = normalized;
        attrib.relativeOffset = relativeoffset;
        if (!attrib.bindingSet) {
            attrib.bindingIndex = attribindex;
        }
        ModifyVertexArrayAttrib(vaobj, attribindex, attrib);
    }
    PublishVertexAttrib(attribindex, size, type, normalized, 0, nullptr);
}

static void GLAPI_glVertexArrayAttribIFormat(
    GLuint vaobj,
    GLuint attribindex,
    GLint size,
    GLenum type,
    GLuint relativeoffset) {
    typedef void (__stdcall* GlVertexArrayAttribIFormatProc)(GLuint, GLuint, GLint, GLenum, GLuint);
    XGL_CALL_REAL_VOID("glVertexArrayAttribIFormat", GlVertexArrayAttribIFormatProc, vaobj, attribindex, size, type, relativeoffset);
    if (attribindex < 16) {
        AttribRecord attrib = {};
        if (vaobj == g_boundVertexArray) {
            attrib = g_attribs[attribindex];
        } else if (auto* record = FindVertexArray(vaobj, true)) {
            attrib = record->attribs[attribindex];
        }
        attrib.separateBinding = true;
        attrib.size = size;
        attrib.type = type;
        attrib.normalized = GL_FALSE;
        attrib.relativeOffset = relativeoffset;
        if (!attrib.bindingSet) {
            attrib.bindingIndex = attribindex;
        }
        ModifyVertexArrayAttrib(vaobj, attribindex, attrib);
    }
    PublishVertexAttrib(attribindex, size, type, GL_FALSE, 0, nullptr);
}

static void GLAPI_glVertexArrayAttribBinding(GLuint vaobj, GLuint attribindex, GLuint bindingindex) {
    typedef void (__stdcall* GlVertexArrayAttribBindingProc)(GLuint, GLuint, GLuint);
    XGL_CALL_REAL_VOID("glVertexArrayAttribBinding", GlVertexArrayAttribBindingProc, vaobj, attribindex, bindingindex);
    if (attribindex < 16 && bindingindex < 16) {
        AttribRecord attrib = {};
        if (vaobj == g_boundVertexArray) {
            attrib = g_attribs[attribindex];
        } else if (auto* record = FindVertexArray(vaobj, true)) {
            attrib = record->attribs[attribindex];
        }
        attrib.separateBinding = true;
        attrib.bindingSet = true;
        attrib.bindingIndex = bindingindex;
        ModifyVertexArrayAttrib(vaobj, attribindex, attrib);
    }
    PublishVertexAttrib(attribindex, 0, 0, GL_FALSE, 0, nullptr);
}

static void GLAPI_glVertexArrayVertexBuffer(
    GLuint vaobj,
    GLuint bindingindex,
    GLuint buffer,
    GLintptr offset,
    GLsizei stride) {
    typedef void (__stdcall* GlVertexArrayVertexBufferProc)(GLuint, GLuint, GLuint, GLintptr, GLsizei);
    XGL_CALL_REAL_VOID("glVertexArrayVertexBuffer", GlVertexArrayVertexBufferProc, vaobj, bindingindex, buffer, offset, stride);
    if (bindingindex < 16) {
        VertexBindingRecord binding = {};
        binding.buffer = buffer;
        binding.offset = offset > 0 ? offset : 0;
        binding.stride = stride;
        ModifyVertexArrayBinding(vaobj, bindingindex, binding);
    }
    PublishBufferBind(GL_ARRAY_BUFFER, buffer);
    PublishVertexAttrib(0, 0, 0, GL_FALSE, stride, nullptr);
}

static void GLAPI_glVertexArrayVertexBuffers(
    GLuint vaobj,
    GLuint first,
    GLsizei count,
    const GLuint* buffers,
    const GLintptr* offsets,
    const GLsizei* strides) {
    typedef void (__stdcall* GlVertexArrayVertexBuffersProc)(GLuint, GLuint, GLsizei, const GLuint*, const GLintptr*, const GLsizei*);
    XGL_CALL_REAL_VOID("glVertexArrayVertexBuffers", GlVertexArrayVertexBuffersProc, vaobj, first, count, buffers, offsets, strides);
    if (count <= 0 || first >= 16) {
        return;
    }

    const GLuint end = first + static_cast<GLuint>(count);
    for (GLuint bindingIndex = first; bindingIndex < end && bindingIndex < 16; ++bindingIndex) {
        const GLuint sourceIndex = bindingIndex - first;
        VertexBindingRecord binding = {};
        binding.buffer = buffers ? buffers[sourceIndex] : 0;
        binding.offset = offsets && offsets[sourceIndex] > 0 ? offsets[sourceIndex] : 0;
        binding.stride = strides ? strides[sourceIndex] : 0;
        ModifyVertexArrayBinding(vaobj, bindingIndex, binding);
        PublishBufferBind(GL_ARRAY_BUFFER, binding.buffer);
    }
}

static void GLAPI_glVertexArrayElementBuffer(GLuint vaobj, GLuint buffer) {
    typedef void (__stdcall* GlVertexArrayElementBufferProc)(GLuint, GLuint);
    XGL_CALL_REAL_VOID("glVertexArrayElementBuffer", GlVertexArrayElementBufferProc, vaobj, buffer);
    if (vaobj == g_boundVertexArray) {
        g_boundElementArrayBuffer = buffer;
        SyncCurrentVertexArrayState();
    } else if (auto* record = FindVertexArray(vaobj, true)) {
        record->elementArrayBuffer = buffer;
    }
    PublishBufferBind(GL_ELEMENT_ARRAY_BUFFER, buffer);
}

static void GLAPI_glBindSampler(GLuint unit, GLuint sampler) {
    typedef void (__stdcall* GlBindSamplerProc)(GLuint, GLuint);
    XGL_CALL_REAL_VOID("glBindSampler", GlBindSamplerProc, unit, sampler);
    auto* state = EnsureGlCommandState();
    if (!state) {
        return;
    }

    if (unit < 32) {
        state->activeTextureUnit = unit;
    }
    InterlockedIncrement64(&state->samplerBindSerial);
}

static void GLAPI_glUseProgram(GLuint program) {
    typedef void (__stdcall* GlUseProgramProc)(GLuint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingProgramUse, "glUseProgram", GlUseProgramProc, program);
    g_currentProgram = program;
    PublishProgramUse(program);
}

static GLint GLAPI_glGetUniformLocation(GLuint program, const char* name) {
    if (!name) {
        return -1;
    }

    const GLuint effectiveProgram = program != 0 ? program : g_currentProgram;
    typedef GLint (__stdcall* GlGetUniformLocationProc)(GLuint, const char*);
    auto real = XGL_CACHED_REAL_PROC("glGetUniformLocation", GlGetUniformLocationProc);
    if (real) {
        __try {
            const GLint realLocation = real(effectiveProgram, name);
            if (realLocation > 0) {
                if (auto* existing = FindUniformByProgramName(effectiveProgram, name)) {
                    existing->location = realLocation;
                } else if (auto* created = CreateUniformRecord(effectiveProgram, name)) {
                    created->location = realLocation;
                }
            }
            return realLocation;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    if (auto* existing = FindUniformByProgramName(effectiveProgram, name)) {
        return existing->location;
    }

    auto* created = CreateUniformRecord(effectiveProgram, name);
    return created ? created->location : 0;
}

static void GLAPI_glUniformTouch(GLint location, ...) {
    PublishUniform(location);
}

static void GLAPI_glUniform1i(GLint location, GLint v0) {
    typedef void (__stdcall* GlUniform1iProc)(GLint, GLint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingUniform, "glUniform1i", GlUniform1iProc, location, v0);
    UpdateSamplerUniform(location, v0);
    PublishUniform(location);
}

static void GLAPI_glUniform1iv(GLint location, GLsizei count, const GLint* value) {
    typedef void (__stdcall* GlUniform1ivProc)(GLint, GLsizei, const GLint*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingUniform, "glUniform1iv", GlUniform1ivProc, location, count, value);
    if (count > 0 && value) {
        UpdateSamplerUniform(location, value[0]);
    }
    PublishUniform(location);
}

static void GLAPI_glProgramUniform1i(GLuint program, GLint location, GLint v0) {
    typedef void (__stdcall* GlProgramUniform1iProc)(GLuint, GLint, GLint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingUniform, "glProgramUniform1i", GlProgramUniform1iProc, program, location, v0);
    UpdateSamplerUniform(location, v0);
    PublishUniform(location);
}

static void GLAPI_glProgramUniform1iv(GLuint program, GLint location, GLsizei count, const GLint* value) {
    typedef void (__stdcall* GlProgramUniform1ivProc)(GLuint, GLint, GLsizei, const GLint*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingUniform, "glProgramUniform1iv", GlProgramUniform1ivProc, program, location, count, value);
    if (count > 0 && value) {
        UpdateSamplerUniform(location, value[0]);
    }
    PublishUniform(location);
}

static void GLAPI_glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei drawcount) {
    typedef void (__stdcall* GlMultiDrawArraysProc)(GLenum, const GLint*, const GLsizei*, GLsizei);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingDraw, "glMultiDrawArrays", GlMultiDrawArraysProc, mode, first, count, drawcount);
    if (!first || !count || drawcount <= 0) {
        return;
    }

    const GLsizei limited = drawcount > 256 ? 256 : drawcount;
    for (GLsizei i = 0; i < limited; ++i) {
        PublishDraw(mode, first[i], count[i], 0);
        PublishGuiDrawArrays(mode, first[i], count[i]);
    }
}

static void GLAPI_glMultiDrawElements(
    GLenum mode,
    const GLsizei* count,
    GLenum type,
    const void* const* indices,
    GLsizei drawcount) {
    typedef void (__stdcall* GlMultiDrawElementsProc)(GLenum, const GLsizei*, GLenum, const void* const*, GLsizei);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingDraw, "glMultiDrawElements", GlMultiDrawElementsProc, mode, count, type, indices, drawcount);
    if (!count || !indices || drawcount <= 0) {
        return;
    }

    const GLsizei limited = drawcount > 256 ? 256 : drawcount;
    for (GLsizei i = 0; i < limited; ++i) {
        PublishDraw(mode, 0, count[i], type);
        PublishGuiDrawElements(mode, count[i], type, indices[i]);
    }
}

static void GLAPI_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount) {
    typedef void (__stdcall* GlDrawArraysInstancedProc)(GLenum, GLint, GLsizei, GLsizei);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingDraw, "glDrawArraysInstanced", GlDrawArraysInstancedProc, mode, first, count, instancecount);
    PublishDraw(mode, first, count, 0);
    PublishGuiDrawArrays(mode, first, count);
}

static void GLAPI_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instancecount) {
    typedef void (__stdcall* GlDrawElementsInstancedProc)(GLenum, GLsizei, GLenum, const void*, GLsizei);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingDraw, "glDrawElementsInstanced", GlDrawElementsInstancedProc, mode, count, type, indices, instancecount);
    PublishDraw(mode, 0, count, type);
    PublishGuiDrawElements(mode, count, type, indices);
}

static void GLAPI_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices) {
    typedef void (__stdcall* GlDrawRangeElementsProc)(GLenum, GLuint, GLuint, GLsizei, GLenum, const void*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingDraw, "glDrawRangeElements", GlDrawRangeElementsProc, mode, start, end, count, type, indices);
    PublishDraw(mode, 0, count, type);
    PublishGuiDrawElements(mode, count, type, indices);
}

static void GLAPI_glDrawElementsBaseVertex(
    GLenum mode,
    GLsizei count,
    GLenum type,
    const void* indices,
    GLint basevertex) {
    typedef void (__stdcall* GlDrawElementsBaseVertexProc)(GLenum, GLsizei, GLenum, const void*, GLint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingDraw, "glDrawElementsBaseVertex", GlDrawElementsBaseVertexProc, mode, count, type, indices, basevertex);
    PublishDraw(mode, basevertex, count, type);
    PublishGuiDrawElements(mode, count, type, indices, basevertex);
}

static void GLAPI_glDrawRangeElementsBaseVertex(
    GLenum mode,
    GLuint start,
    GLuint end,
    GLsizei count,
    GLenum type,
    const void* indices,
    GLint basevertex) {
    typedef void (__stdcall* GlDrawRangeElementsBaseVertexProc)(GLenum, GLuint, GLuint, GLsizei, GLenum, const void*, GLint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingDraw, "glDrawRangeElementsBaseVertex", GlDrawRangeElementsBaseVertexProc, mode, start, end, count, type, indices, basevertex);
    PublishDraw(mode, basevertex, count, type);
    PublishGuiDrawElements(mode, count, type, indices, basevertex);
}

static void GLAPI_glDrawElementsInstancedBaseVertex(
    GLenum mode,
    GLsizei count,
    GLenum type,
    const void* indices,
    GLsizei instancecount,
    GLint basevertex) {
    typedef void (__stdcall* GlDrawElementsInstancedBaseVertexProc)(GLenum, GLsizei, GLenum, const void*, GLsizei, GLint);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingDraw, "glDrawElementsInstancedBaseVertex", GlDrawElementsInstancedBaseVertexProc, mode, count, type, indices, instancecount, basevertex);
    PublishDraw(mode, basevertex, count, type);
    PublishGuiDrawElements(mode, count, type, indices, basevertex);
}

static void GLAPI_glMultiDrawElementsBaseVertex(
    GLenum mode,
    const GLsizei* count,
    GLenum type,
    const void* const* indices,
    GLsizei drawcount,
    const GLint* basevertex) {
    typedef void (__stdcall* GlMultiDrawElementsBaseVertexProc)(GLenum, const GLsizei*, GLenum, const void* const*, GLsizei, const GLint*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingDraw, "glMultiDrawElementsBaseVertex", GlMultiDrawElementsBaseVertexProc, mode, count, type, indices, drawcount, basevertex);
    if (!count || !indices || drawcount <= 0) {
        return;
    }

    const GLsizei limited = drawcount > 256 ? 256 : drawcount;
    for (GLsizei i = 0; i < limited; ++i) {
        const GLint base = basevertex ? basevertex[i] : 0;
        PublishDraw(mode, base, count[i], type);
        PublishGuiDrawElements(mode, count[i], type, indices[i], base);
    }
}

__declspec(dllexport) void __stdcall glFlush(void) {
    typedef void (__stdcall* GlFlushProc)(void);
    auto real = ForceCompatGlIdentity()
        ? nullptr
        : XGL_CACHED_REAL_PROC("glFlush", GlFlushProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            real();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingFlush, timingStartMs);
    }
    auto* state = EnsureGlCommandState();
    if (state) {
        InterlockedIncrement64(&state->flushSerial);
    }
    SignalPresentEvent();
}

__declspec(dllexport) void __stdcall glFinish(void) {
    typedef void (__stdcall* GlFinishProc)(void);
    auto real = ForceCompatGlIdentity()
        ? nullptr
        : XGL_CACHED_REAL_PROC("glFinish", GlFinishProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            real();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingSync, timingStartMs);
    }
    glFlush();
}

// -----------------------------------------------------------------------------
// ID-generating functions (returned via wglGetProcAddress).
//
// We hand out monotonically increasing IDs so Mojang code that uses these as
// keys into HashMaps etc. doesn't collide on 0.
// -----------------------------------------------------------------------------

static std::atomic<GLuint> g_nextId{ 1 };

static GLuint NextId() {
    return g_nextId.fetch_add(1, std::memory_order_relaxed);
}

static GLuint NextIdRange(GLsizei range) {
    if (range <= 0) {
        return 0;
    }
    return g_nextId.fetch_add(static_cast<GLuint>(range), std::memory_order_relaxed);
}

static void GLAPI_glGenAny(GLsizei n, GLuint* ids) {
    if (!ids || n <= 0) return;
    for (GLsizei i = 0; i < n; ++i) {
        ids[i] = NextId();
    }
}

__declspec(dllexport) void __stdcall glGenTextures(GLsizei n, GLuint* textures) {
    typedef void (__stdcall* GlGenTexturesProc)(GLsizei, GLuint*);
    auto real = XGL_CACHED_REAL_PROC("glGenTextures", GlGenTexturesProc);
    if (real) {
        __try {
            real(n, textures);
            if (textures && n > 0 && textures[0] != 0) {
                return;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    GLAPI_glGenAny(n, textures);
}

__declspec(dllexport) GLuint __stdcall glGenLists(GLsizei range) {
    LogGlCallProbe("glGenLists");
    typedef GLuint (__stdcall* GlGenListsProc)(GLsizei);
    auto real = XGL_CACHED_REAL_PROC("glGenLists", GlGenListsProc);
    if (real) {
        __try {
            GLuint first = real(range);
            if (first != 0 || range <= 0) {
                return first;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return NextIdRange(range);
}

__declspec(dllexport) void __stdcall glNewList(GLuint list, GLenum mode) {
    LogGlCallProbe("glNewList");
    typedef void (__stdcall* GlNewListProc)(GLuint, GLenum);
    auto real = XGL_CACHED_REAL_PROC("glNewList", GlNewListProc);
    if (real) {
        __try {
            real(list, mode);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

__declspec(dllexport) void __stdcall glEndList(void) {
    LogGlCallProbe("glEndList");
    typedef void (__stdcall* GlEndListProc)(void);
    auto real = XGL_CACHED_REAL_PROC("glEndList", GlEndListProc);
    if (real) {
        __try {
            real();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

__declspec(dllexport) void __stdcall glCallList(GLuint list) {
    LogGlCallProbe("glCallList");
    typedef void (__stdcall* GlCallListProc)(GLuint);
    auto real = XGL_CACHED_REAL_PROC("glCallList", GlCallListProc);
    if (real) {
        __try {
            real(list);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

__declspec(dllexport) void __stdcall glCallLists(GLsizei n, GLenum type, const void* lists) {
    LogGlCallProbe("glCallLists");
    typedef void (__stdcall* GlCallListsProc)(GLsizei, GLenum, const void*);
    auto real = XGL_CACHED_REAL_PROC("glCallLists", GlCallListsProc);
    if (real) {
        __try {
            real(n, type, lists);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

__declspec(dllexport) void __stdcall glDeleteLists(GLuint list, GLsizei range) {
    LogGlCallProbe("glDeleteLists");
    typedef void (__stdcall* GlDeleteListsProc)(GLuint, GLsizei);
    auto real = XGL_CACHED_REAL_PROC("glDeleteLists", GlDeleteListsProc);
    if (real) {
        __try {
            real(list, range);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

__declspec(dllexport) GLboolean __stdcall glIsList(GLuint list) {
    typedef GLboolean (__stdcall* GlIsListProc)(GLuint);
    auto real = XGL_CACHED_REAL_PROC("glIsList", GlIsListProc);
    if (real) {
        __try {
            GLboolean value = real(list);
            if (value == GL_TRUE) {
                return value;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return list ? GL_TRUE : GL_FALSE;
}

__declspec(dllexport) void __stdcall glListBase(GLuint base) {
    LogGlCallProbe("glListBase");
    typedef void (__stdcall* GlListBaseProc)(GLuint);
    auto real = XGL_CACHED_REAL_PROC("glListBase", GlListBaseProc);
    if (real) {
        __try {
            real(base);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

static void GLAPI_glCreateTextures(GLenum target, GLsizei n, GLuint* textures) {
    GLAPI_glGenAny(n, textures);
    (void)target;
}

static void  GLAPI_glGenBuffers(GLsizei n, GLuint* b)        { GLAPI_glGenAny(n, b); }
static void  GLAPI_glGenVertexArrays(GLsizei n, GLuint* b)   { GLAPI_glGenAny(n, b); }
static void  GLAPI_glGenFramebuffers(GLsizei n, GLuint* b)   { GLAPI_glGenAny(n, b); }
static void  GLAPI_glGenRenderbuffers(GLsizei n, GLuint* b)  { GLAPI_glGenAny(n, b); }
static void  GLAPI_glGenQueries(GLsizei n, GLuint* b)        { GLAPI_glGenAny(n, b); }
static void  GLAPI_glGenSamplers(GLsizei n, GLuint* b)       { GLAPI_glGenAny(n, b); }
static void  GLAPI_glGenProgramPipelines(GLsizei n, GLuint* b){ GLAPI_glGenAny(n, b); }
static void  GLAPI_glGenTransformFeedbacks(GLsizei n, GLuint* b){ GLAPI_glGenAny(n, b); }
static GLuint GLAPI_glCreateShader(GLenum /*type*/)          { return NextId(); }
static GLuint GLAPI_glCreateProgram(void)                    { return NextId(); }
static GLuint GLAPI_glCreateShaderProgramv(GLenum /*type*/, GLsizei /*count*/, const char* const* /*strings*/) { return NextId(); }
static void   GLAPI_glCreateProgramPipelines(GLsizei n, GLuint* b){ GLAPI_glGenAny(n, b); }
static void   GLAPI_glCreateFramebuffers(GLsizei n, GLuint* b){ GLAPI_glGenAny(n, b); }
static void   GLAPI_glCreateVertexArrays(GLsizei n, GLuint* b){ GLAPI_glGenAny(n, b); }

// "Is" queries - return GL_TRUE so Mojang treats fake IDs as live.
static GLboolean GLAPI_glIsBuffer(GLuint name)        { return name ? GL_TRUE : GL_FALSE; }
static GLboolean GLAPI_glIsVertexArray(GLuint name)   { return name ? GL_TRUE : GL_FALSE; }
static GLboolean GLAPI_glIsFramebuffer(GLuint name)   { return name ? GL_TRUE : GL_FALSE; }
static GLboolean GLAPI_glIsRenderbuffer(GLuint name)  { return name ? GL_TRUE : GL_FALSE; }
static GLboolean GLAPI_glIsShader(GLuint name)        { return name ? GL_TRUE : GL_FALSE; }
static GLboolean GLAPI_glIsProgram(GLuint name)       { return name ? GL_TRUE : GL_FALSE; }
static GLboolean GLAPI_glIsEnabledi(GLenum /*cap*/, GLuint /*idx*/) { return GL_FALSE; }
static GLenum    GLAPI_glCheckFramebufferStatus(GLenum /*target*/) { return GL_FRAMEBUFFER_COMPLETE; }
static GLenum    GLAPI_glCheckNamedFramebufferStatus(GLuint /*framebuffer*/, GLenum /*target*/) { return GL_FRAMEBUFFER_COMPLETE; }

// Status-reporting queries for shaders / programs.  We claim everything
// compiled / linked successfully, with 0-length info logs.
static void GLAPI_glGetShaderiv(GLuint /*shader*/, GLenum pname, GLint* params) {
    if (!params) return;
    switch (pname) {
        case GL_COMPILE_STATUS:  *params = GL_TRUE; break;
        case GL_INFO_LOG_LENGTH: *params = 0;       break;
        default:                 *params = 0;       break;
    }
}
static void GLAPI_glGetProgramiv(GLuint /*program*/, GLenum pname, GLint* params) {
    if (!params) return;
    switch (pname) {
        case GL_LINK_STATUS:
        case GL_VALIDATE_STATUS:  *params = GL_TRUE; break;
        case GL_INFO_LOG_LENGTH:
        case GL_ATTACHED_SHADERS:
        case GL_ACTIVE_UNIFORMS:
        case GL_ACTIVE_ATTRIBUTES:
        default:                  *params = 0; break;
    }
}
static void GLAPI_glGetShaderInfoLog(GLuint, GLsizei /*bufSize*/, GLsizei* length, char* infoLog) {
    if (length) *length = 0;
    if (infoLog) infoLog[0] = '\0';
}
static void GLAPI_glGetProgramInfoLog(GLuint, GLsizei /*bufSize*/, GLsizei* length, char* infoLog) {
    if (length) *length = 0;
    if (infoLog) infoLog[0] = '\0';
}
static void GLAPI_glGetIntegeri_v(GLenum pname, GLuint /*index*/, GLint* params) {
    FillIntegerv(pname, params);
}
static void GLAPI_glGetInteger64v(GLenum pname, GLint64* params) {
    if (!params) return;
    GLint v[4] = { 0,0,0,0 };
    FillIntegerv(pname, v);
    *params = v[0];
}
static void GLAPI_glGetInteger64i_v(GLenum pname, GLuint /*index*/, GLint64* params) {
    if (!params) return;
    GLint v[4] = { 0,0,0,0 };
    FillIntegerv(pname, v);
    *params = v[0];
}
static void GLAPI_glGetBooleani_v(GLenum /*pname*/, GLuint /*index*/, GLboolean* data) {
    if (data) *data = GL_FALSE;
}

// Buffer mapping - Mojang's font/resource reload calls glMapBuffer during the
// first render ticks.  Returning nullptr makes Blaze3D throw:
//   IllegalStateException: Can't map buffer, opengl error 0
// We hand back scratch RAM from a fixed pool so CPU-side uploads succeed even
// though nothing ever reaches the GPU.
static uint8_t g_mapPool[64 * 1024 * 1024];
static size_t  g_mapPoolUsed = 0;
static void*   g_lastMappedPointer = nullptr;

static void* AllocateMapRegion(size_t size) {
    if (size == 0) size = 4 * 1024 * 1024;
    size = (size + 63) & ~size_t(63);
    if (size > sizeof(g_mapPool)) {
        return g_mapPool;
    }
    if (g_mapPoolUsed + size > sizeof(g_mapPool)) {
        g_mapPoolUsed = 0;
    }
    void* ptr = g_mapPool + g_mapPoolUsed;
    g_mapPoolUsed += size;
    return ptr;
}

static void MirrorMappedRangeToShadow(GLenum target, GLintptr flushOffset = 0, GLsizeiptr flushLength = 0) {
    if (!IsCommandBridgeEnabled()) {
        return;
    }

    if (!g_lastMappedPointer || g_lastMappedBuffer == 0 || g_lastMappedLength <= 0) {
        return;
    }

    GLintptr relativeOffset = flushOffset > 0 ? flushOffset : 0;
    GLsizeiptr copyLength = flushLength > 0 ? flushLength : g_lastMappedLength;
    if (relativeOffset >= g_lastMappedLength) {
        return;
    }
    if (relativeOffset + copyLength > g_lastMappedLength) {
        copyLength = g_lastMappedLength - relativeOffset;
    }
    if (copyLength <= 0) {
        return;
    }

    auto* record = FindBuffer(g_lastMappedBuffer, true);
    if (!record) {
        return;
    }

    const size_t destinationOffset = static_cast<size_t>(g_lastMappedOffset + relativeOffset);
    const size_t copyBytes = static_cast<size_t>(copyLength);
    if (!EnsureBufferSize(record, destinationOffset + copyBytes)) {
        return;
    }

    const auto* src = static_cast<const uint8_t*>(g_lastMappedPointer) + static_cast<size_t>(relativeOffset);
    const auto* begin = record->data;
    const auto* end = record->data + record->size;
    if (src < begin || src >= end) {
        std::memcpy(record->data + destinationOffset, src, copyBytes);
    }

    PublishBufferUpload(true, target, g_lastMappedBuffer, copyLength, 0);
}

static void* GLAPI_glMapBuffer(GLenum target, GLenum access) {
    typedef void* (__stdcall* GlMapBufferProc)(GLenum, GLenum);
    void* realPointer = nullptr;
    auto real = XGL_CACHED_REAL_PROC("glMapBuffer", GlMapBufferProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            realPointer = real(target, access);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingBufferUpload, timingStartMs);
    }

    const GLuint buffer = BoundBufferForTarget(target);
    auto* record = FindBuffer(buffer, true);
    if (record) {
        if (!EnsureBufferSize(record, record->size > 0 ? record->size : 4 * 1024 * 1024)) {
            return nullptr;
        }
        g_lastMappedBuffer = buffer;
        g_lastMappedOffset = 0;
        g_lastMappedLength = static_cast<GLsizeiptr>(record->size);
        g_lastMappedPointer = realPointer ? realPointer : record->data;
        return g_lastMappedPointer;
    }

    g_lastMappedBuffer = 0;
    g_lastMappedOffset = 0;
    g_lastMappedLength = 0;
    g_lastMappedPointer = realPointer ? realPointer : AllocateMapRegion(4 * 1024 * 1024);
    return g_lastMappedPointer;
}

static void* GLAPI_glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLenum access) {
    typedef void* (__stdcall* GlMapBufferRangeProc)(GLenum, GLintptr, GLsizeiptr, GLenum);
    void* realPointer = nullptr;
    auto real = XGL_CACHED_REAL_PROC("glMapBufferRange", GlMapBufferRangeProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            realPointer = real(target, offset, length, access);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingBufferUpload, timingStartMs);
    }

    if (length <= 0) {
        g_lastMappedPointer = realPointer ? realPointer : AllocateMapRegion(4 * 1024 * 1024);
        return g_lastMappedPointer;
    }

    const GLuint buffer = BoundBufferForTarget(target);
    auto* record = FindBuffer(buffer, true);
    if (record && offset >= 0) {
        const size_t end = static_cast<size_t>(offset) + static_cast<size_t>(length);
        if (!EnsureBufferSize(record, end)) {
            return nullptr;
        }
        g_lastMappedBuffer = buffer;
        g_lastMappedOffset = offset;
        g_lastMappedLength = length;
        g_lastMappedPointer = realPointer ? realPointer : record->data + static_cast<size_t>(offset);
        return g_lastMappedPointer;
    }

    g_lastMappedBuffer = 0;
    g_lastMappedOffset = 0;
    g_lastMappedLength = 0;
    void* base = AllocateMapRegion((size_t)length + (size_t)(offset > 0 ? offset : 0));
    g_lastMappedPointer = realPointer ? realPointer : (uint8_t*)base + (offset > 0 ? (size_t)offset : 0);
    return g_lastMappedPointer;
}

static void* GLAPI_glMapNamedBuffer(GLuint buffer, GLenum access) {
    typedef void* (__stdcall* GlMapNamedBufferProc)(GLuint, GLenum);
    void* realPointer = nullptr;
    auto real = XGL_CACHED_REAL_PROC("glMapNamedBuffer", GlMapNamedBufferProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            realPointer = real(buffer, access);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingBufferUpload, timingStartMs);
    }

    auto* record = FindBuffer(buffer, true);
    if (!record || !EnsureBufferSize(record, record->size > 0 ? record->size : 4 * 1024 * 1024)) {
        return realPointer;
    }
    g_lastMappedBuffer = buffer;
    g_lastMappedOffset = 0;
    g_lastMappedLength = static_cast<GLsizeiptr>(record->size);
    g_lastMappedPointer = realPointer ? realPointer : record->data;
    return g_lastMappedPointer;
}

static void* GLAPI_glMapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length, GLenum access) {
    typedef void* (__stdcall* GlMapNamedBufferRangeProc)(GLuint, GLintptr, GLsizeiptr, GLenum);
    void* realPointer = nullptr;
    auto real = XGL_CACHED_REAL_PROC("glMapNamedBufferRange", GlMapNamedBufferRangeProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            realPointer = real(buffer, offset, length, access);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingBufferUpload, timingStartMs);
    }

    auto* record = FindBuffer(buffer, true);
    if (!record || offset < 0 || length <= 0 ||
        !EnsureBufferSize(record, static_cast<size_t>(offset) + static_cast<size_t>(length))) {
        return realPointer;
    }
    g_lastMappedBuffer = buffer;
    g_lastMappedOffset = offset;
    g_lastMappedLength = length;
    g_lastMappedPointer = realPointer ? realPointer : record->data + static_cast<size_t>(offset);
    return g_lastMappedPointer;
}

static void GLAPI_glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
    MirrorMappedRangeToShadow(target, offset, length);
    typedef void (__stdcall* GlFlushMappedBufferRangeProc)(GLenum, GLintptr, GLsizeiptr);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingBufferUpload, "glFlushMappedBufferRange", GlFlushMappedBufferRangeProc, target, offset, length);
}

static GLboolean GLAPI_glUnmapBuffer(GLenum target) {
    MirrorMappedRangeToShadow(target);
    typedef GLboolean (__stdcall* GlUnmapBufferProc)(GLenum);
    GLboolean result = GL_TRUE;
    auto real = XGL_CACHED_REAL_PROC("glUnmapBuffer", GlUnmapBufferProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            result = real(target);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingBufferUpload, timingStartMs);
    }
    g_lastMappedBuffer = 0;
    g_lastMappedOffset = 0;
    g_lastMappedLength = 0;
    g_lastMappedPointer = nullptr;
    return result;
}

static GLboolean GLAPI_glUnmapNamedBuffer(GLuint buffer) {
    MirrorMappedRangeToShadow(0);
    typedef GLboolean (__stdcall* GlUnmapNamedBufferProc)(GLuint);
    GLboolean result = GL_TRUE;
    auto real = XGL_CACHED_REAL_PROC("glUnmapNamedBuffer", GlUnmapNamedBufferProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            result = real(buffer);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        PublishGlCallTiming(kGlTimingBufferUpload, timingStartMs);
    }
    g_lastMappedBuffer = 0;
    g_lastMappedOffset = 0;
    g_lastMappedLength = 0;
    g_lastMappedPointer = nullptr;
    return result;
}

static void GLAPI_glGetPointerv(GLenum, void** params) {
    if (params) *params = nullptr;
}

static void GLAPI_glGetBufferSubData(GLenum, GLintptr, GLsizeiptr, void*) {
}

static void GLAPI_glGetBufferParameteriv(GLenum target, GLenum pname, GLint* params) {
    if (!params) {
        return;
    }

    *params = 0;
    typedef void (__stdcall* GlGetBufferParameterivProc)(GLenum, GLenum, GLint*);
    auto real = XGL_CACHED_REAL_PROC("glGetBufferParameteriv", GlGetBufferParameterivProc);
    if (real) {
        __try {
            real(target, pname, params);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    const size_t trackedSize = TrackedBufferSizeForTarget(target);
    if (pname == GL_BUFFER_SIZE && trackedSize > static_cast<size_t>(*params > 0 ? *params : 0)) {
        LogBufferSizeFallback(target, static_cast<long long>(*params), trackedSize);
        *params = SizeToGlint(trackedSize);
        return;
    }

    const GLuint buffer = BoundBufferForTarget(target);
    if (buffer != 0 && buffer == g_lastMappedBuffer && g_lastMappedPointer) {
        if (pname == GL_BUFFER_MAPPED) {
            *params = GL_TRUE;
        } else if (pname == GL_BUFFER_MAP_OFFSET) {
            *params = static_cast<GLint>(g_lastMappedOffset);
        } else if (pname == GL_BUFFER_MAP_LENGTH) {
            *params = SizeToGlint(static_cast<size_t>(g_lastMappedLength));
        }
    }
}

static void GLAPI_glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64* params) {
    if (!params) {
        return;
    }

    *params = 0;
    typedef void (__stdcall* GlGetBufferParameteri64vProc)(GLenum, GLenum, GLint64*);
    auto real = XGL_CACHED_REAL_PROC("glGetBufferParameteri64v", GlGetBufferParameteri64vProc);
    if (real) {
        __try {
            real(target, pname, params);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    const size_t trackedSize = TrackedBufferSizeForTarget(target);
    if (pname == GL_BUFFER_SIZE && trackedSize > static_cast<size_t>(*params > 0 ? *params : 0)) {
        LogBufferSizeFallback(target, static_cast<long long>(*params), trackedSize);
        *params = SizeToGlint64(trackedSize);
        return;
    }

    const GLuint buffer = BoundBufferForTarget(target);
    if (buffer != 0 && buffer == g_lastMappedBuffer && g_lastMappedPointer) {
        if (pname == GL_BUFFER_MAPPED) {
            *params = GL_TRUE;
        } else if (pname == GL_BUFFER_MAP_OFFSET) {
            *params = static_cast<GLint64>(g_lastMappedOffset);
        } else if (pname == GL_BUFFER_MAP_LENGTH) {
            *params = SizeToGlint64(static_cast<size_t>(g_lastMappedLength));
        }
    }
}

static void GLAPI_glGetVertexAttribPointerv(GLuint, GLenum, void** pointer) {
    if (pointer) *pointer = nullptr;
}

static void GLAPI_glGetVertexAttribiv(GLuint, GLenum, GLint* params) {
    if (params) *params = 0;
}

static void GLAPI_glGetVertexAttribfv(GLuint, GLenum, GLfloat* params) {
    if (params) *params = 0.0f;
}

static void GLAPI_glGetVertexAttribdv(GLuint, GLenum, GLdouble* params) {
    if (params) *params = 0.0;
}

static void GLAPI_glGetVertexAttribIiv(GLuint, GLenum, GLint* params) {
    if (params) *params = 0;
}

static void GLAPI_glGetVertexAttribIuiv(GLuint, GLenum, GLuint* params) {
    if (params) *params = 0;
}

static void GLAPI_glGetBufferPointerv(GLenum /*target*/, GLenum pname, void** params) {
    if (!params) return;
    if (pname == GL_BUFFER_MAP_POINTER) {
        *params = g_lastMappedPointer;
    } else {
        *params = nullptr;
    }
}

static void GLAPI_glGetNamedBufferPointerv(GLuint /*buffer*/, GLenum pname, void** params) {
    GLAPI_glGetBufferPointerv(0, pname, params);
}

static void GLAPI_glNamedBufferData(GLuint buffer, GLsizeiptr size, const void* data, GLenum usage) {
    typedef void (__stdcall* GlNamedBufferDataProc)(GLuint, GLsizeiptr, const void*, GLenum);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingBufferUpload, "glNamedBufferData", GlNamedBufferDataProc, buffer, size, data, usage);
    StoreBufferBytes(buffer, size, data);
    PublishBufferUpload(false, 0, buffer, size, usage);
}

static void GLAPI_glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data) {
    typedef void (__stdcall* GlNamedBufferSubDataProc)(GLuint, GLintptr, GLsizeiptr, const void*);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingBufferUpload, "glNamedBufferSubData", GlNamedBufferSubDataProc, buffer, offset, size, data);
    StoreBufferSubBytes(buffer, offset, size, data);
    PublishBufferUpload(true, 0, buffer, size, 0);
}

static void GLAPI_glBufferStorage(GLenum target, GLsizeiptr size, const void* data, GLbitfield flags) {
    typedef void (__stdcall* GlBufferStorageProc)(GLenum, GLsizeiptr, const void*, GLbitfield);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingBufferUpload, "glBufferStorage", GlBufferStorageProc, target, size, data, flags);
    const GLuint buffer = BoundBufferForTarget(target);
    StoreBufferBytes(buffer, size, data);
    PublishBufferUpload(false, target, buffer, size, flags);
}
static void GLAPI_glNamedBufferStorage(GLuint buffer, GLsizeiptr size, const void* data, GLbitfield flags) {
    typedef void (__stdcall* GlNamedBufferStorageProc)(GLuint, GLsizeiptr, const void*, GLbitfield);
    XGL_CALL_REAL_VOID_TIMED(kGlTimingBufferUpload, "glNamedBufferStorage", GlNamedBufferStorageProc, buffer, size, data, flags);
    StoreBufferBytes(buffer, size, data);
    PublishBufferUpload(false, 0, buffer, size, flags);
}

// Sync objects.  Prefer Mesa's real fence path in gameplay; the fallback IDs
// keep early bootstrap safe if a Mesa symbol is missing.
static bool IsFallbackSyncObject(void* sync) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(sync);
    return value > 0 && value < 0x1000000u;
}

static void LogRealSyncPath(const char* name, bool usedReal) {
    static std::atomic<int> logCount{ 0 };
    int count = logCount.fetch_add(1, std::memory_order_relaxed);
    if (count < 24) {
        char line[160] = {};
        std::snprintf(
            line,
            sizeof(line),
            "GL sync %s using %s path",
            name ? name : "<unknown>",
            usedReal ? "Mesa" : "fallback");
        DebugLine(line);
    }
}

static constexpr GLenum kGlAlreadySignaled = 0x911A;
static constexpr GLenum kGlTimeoutExpired = 0x911B;
static constexpr GLenum kGlSyncFlushCommandsBit = 0x00000001;

static void LogClientWaitSyncDecision(
    const char* decision,
    GLenum flags,
    GLuint64 timeout,
    GLenum result,
    double elapsedMs) {
    if (!IsVerboseGlLoggingEnabled()) {
        return;
    }

    static std::atomic<int> logCount{ 0 };
    int count = logCount.fetch_add(1, std::memory_order_relaxed);
    if (count < 96 || elapsedMs >= 4.0) {
        char line[220] = {};
        std::snprintf(
            line,
            sizeof(line),
            "GL sync clientWait %s flags=0x%X timeoutNs=%llu result=0x%04X elapsedMs=%.3f",
            decision ? decision : "<unknown>",
            flags,
            static_cast<unsigned long long>(timeout),
            result,
            elapsedMs);
        DebugLine(line);
    }
}

static void* GLAPI_glFenceSync(GLenum condition, GLenum flags) {
    typedef void* (__stdcall* GlFenceSyncProc)(GLenum, GLbitfield);
    auto real = XGL_CACHED_REAL_PROC("glFenceSync", GlFenceSyncProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            void* sync = real(condition, flags);
            PublishGlCallTiming(kGlTimingFenceSync, timingStartMs);
            if (sync) {
                LogRealSyncPath("glFenceSync", true);
                return sync;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            PublishGlCallTiming(kGlTimingFenceSync, timingStartMs);
        }
    }

    LogRealSyncPath("glFenceSync", false);
    return (void*)(uintptr_t)NextId();
}

static void GLAPI_glDeleteSync(void* sync) {
    if (!sync || IsFallbackSyncObject(sync)) {
        return;
    }

    typedef void (__stdcall* GlDeleteSyncProc)(void*);
    auto real = XGL_CACHED_REAL_PROC("glDeleteSync", GlDeleteSyncProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            real(sync);
            PublishGlCallTiming(kGlTimingDeleteSync, timingStartMs);
            LogRealSyncPath("glDeleteSync", true);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            PublishGlCallTiming(kGlTimingDeleteSync, timingStartMs);
        }
    }
}

static GLboolean GLAPI_glIsSync(void* sync) {
    if (!sync) {
        return GL_FALSE;
    }
    if (IsFallbackSyncObject(sync)) {
        return GL_TRUE;
    }

    typedef GLboolean (__stdcall* GlIsSyncProc)(void*);
    auto real = XGL_CACHED_REAL_PROC("glIsSync", GlIsSyncProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            GLboolean result = real(sync);
            PublishGlCallTiming(kGlTimingIsSync, timingStartMs);
            LogRealSyncPath("glIsSync", true);
            return result;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            PublishGlCallTiming(kGlTimingIsSync, timingStartMs);
        }
    }

    return GL_TRUE;
}

static GLenum GLAPI_glClientWaitSync(void* sync, GLenum flags, GLuint64 timeout) {
    if (sync && !IsFallbackSyncObject(sync)) {
        typedef GLenum (__stdcall* GlClientWaitSyncProc)(void*, GLenum, GLuint64);
        auto real = XGL_CACHED_REAL_PROC("glClientWaitSync", GlClientWaitSyncProc);
        if (UseHybridGlClientWaitSync()) {
            if (timeout == 0) {
                if ((flags & kGlSyncFlushCommandsBit) != 0) {
                    glFlush();
                }
                LogClientWaitSyncDecision(
                    "hybrid-zero-timeout",
                    flags,
                    timeout,
                    kGlTimeoutExpired,
                    0.0);
                return kGlTimeoutExpired;
            }
        }

        if (real) {
            double timingStartMs = BeginGlCallTiming();
            __try {
                GLenum result = real(sync, flags, timeout);
                PublishGlCallTiming(kGlTimingClientWaitSync, timingStartMs);
                double elapsedMs = timingStartMs > 0.0 ? FrameTimingNowMs() - timingStartMs : 0.0;
                LogClientWaitSyncDecision("mesa", flags, timeout, result, elapsedMs);
                LogRealSyncPath("glClientWaitSync", true);
                return result;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                PublishGlCallTiming(kGlTimingClientWaitSync, timingStartMs);
                double elapsedMs = timingStartMs > 0.0 ? FrameTimingNowMs() - timingStartMs : 0.0;
                LogClientWaitSyncDecision("mesa-exception", flags, timeout, 0x911D, elapsedMs);
            }
        }
    }

    LogRealSyncPath("glClientWaitSync", false);
    LogClientWaitSyncDecision("fallback", flags, timeout, kGlAlreadySignaled, 0.0);
    return kGlAlreadySignaled;
}

static void GLAPI_glWaitSync(void* sync, GLenum flags, GLuint64 timeout) {
    if (!sync || IsFallbackSyncObject(sync)) {
        (void)flags;
        (void)timeout;
        return;
    }

    typedef void (__stdcall* GlWaitSyncProc)(void*, GLenum, GLuint64);
    auto real = XGL_CACHED_REAL_PROC("glWaitSync", GlWaitSyncProc);
    if (real) {
        double timingStartMs = BeginGlCallTiming();
        __try {
            real(sync, flags, timeout);
            PublishGlCallTiming(kGlTimingWaitSync, timingStartMs);
            LogRealSyncPath("glWaitSync", true);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            PublishGlCallTiming(kGlTimingWaitSync, timingStartMs);
        }
    }
}

static void GLAPI_glGetSynciv(void* sync, GLenum pname, GLsizei bufSize, GLsizei* length, GLint* values) {
    if (sync && !IsFallbackSyncObject(sync)) {
        typedef void (__stdcall* GlGetSyncivProc)(void*, GLenum, GLsizei, GLsizei*, GLint*);
        auto real = XGL_CACHED_REAL_PROC("glGetSynciv", GlGetSyncivProc);
        if (real) {
            double timingStartMs = BeginGlCallTiming();
            __try {
                real(sync, pname, bufSize, length, values);
                PublishGlCallTiming(kGlTimingIsSync, timingStartMs);
                LogRealSyncPath("glGetSynciv", true);
                return;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                PublishGlCallTiming(kGlTimingIsSync, timingStartMs);
            }
        }
    }

    if (length) {
        *length = values && bufSize > 0 ? 1 : 0;
    }
    if (!values || bufSize <= 0) {
        return;
    }

    switch (pname) {
        case 0x9114: // GL_SYNC_STATUS
            values[0] = 0x9119; // GL_SIGNALED
            break;
        case 0x9115: // GL_SYNC_CONDITION
            values[0] = 0x9117; // GL_SYNC_GPU_COMMANDS_COMPLETE
            break;
        case 0x9116: // GL_SYNC_FLAGS
            values[0] = 0;
            break;
        default:
            values[0] = sync ? 1 : 0;
            break;
    }
}

// -----------------------------------------------------------------------------
// WGL implementations.  Mojang never calls these directly - they go through
// GLFW which we already stubbed.  But LWJGL's GL.create() and FunctionProvider
// may inspect wglGetProcAddress, wglGetCurrentContext, wglGetCurrentDC.
// -----------------------------------------------------------------------------

static HGLRC kFakeContext = (HGLRC)(uintptr_t)0x6F70656E674C0001ull;
static HDC   kFakeDC      = (HDC)  (uintptr_t)0x6F70656E674C0002ull;

// Note: wingdi.h (pulled in by windows.h) already declares the canonical wgl*
// functions with WINGDIAPI=__declspec(dllimport).  We sidestep the conflict by
// implementing under xbox_wgl* names and aliasing in xbox_opengl.def.

extern "C" HGLRC WINAPI xbox_wglCreateContext(HDC)                          { return kFakeContext; }
extern "C" HGLRC WINAPI xbox_wglCreateLayerContext(HDC, int)                { return kFakeContext; }
extern "C" BOOL  WINAPI xbox_wglDeleteContext(HGLRC)                        { return TRUE; }
extern "C" BOOL  WINAPI xbox_wglMakeCurrent(HDC, HGLRC)                     {
    static std::atomic<int> logCount{ 0 };
    if (logCount.fetch_add(1, std::memory_order_relaxed) < 8) {
        DebugLine("wglMakeCurrent compat -> TRUE");
    }
    return TRUE;
}
extern "C" HGLRC WINAPI xbox_wglGetCurrentContext(void)                     {
    static std::atomic<int> logCount{ 0 };
    if (logCount.fetch_add(1, std::memory_order_relaxed) < 8) {
        DebugLine("wglGetCurrentContext compat -> sentinel");
    }
    return kFakeContext;
}
extern "C" HDC   WINAPI xbox_wglGetCurrentDC(void)                          {
    static std::atomic<int> logCount{ 0 };
    if (logCount.fetch_add(1, std::memory_order_relaxed) < 8) {
        DebugLine("wglGetCurrentDC compat -> sentinel");
    }
    return kFakeDC;
}
extern "C" BOOL  WINAPI xbox_wglShareLists(HGLRC, HGLRC)                    { return TRUE; }
static void SwapCurrentEglSurfaceForLwjgl2(const char* reason) {
    if (IsCoreWindowPumpOnSwapEnabled()) {
        PumpCoreWindowEventsForCurrentThread("wglSwapBuffers-before");
    }
    TrySwapCurrentEglSurface();
    if (IsCoreWindowPumpOnSwapEnabled()) {
        PumpCoreWindowEventsForCurrentThread(reason);
    }
    SignalPresentEvent();
}
extern "C" BOOL  WINAPI xbox_wglSwapBuffers(HDC)                            { SwapCurrentEglSurfaceForLwjgl2("wglSwapBuffers"); return TRUE; }
extern "C" BOOL  WINAPI xbox_wglSwapLayerBuffers(HDC, UINT)                 { SwapCurrentEglSurfaceForLwjgl2("wglSwapLayerBuffers"); return TRUE; }
extern "C" DWORD WINAPI xbox_wglSwapMultipleBuffers(UINT, const WGLSWAP*)   { SwapCurrentEglSurfaceForLwjgl2("wglSwapMultipleBuffers"); return 1; }
extern "C" int   WINAPI xbox_wglChoosePixelFormat(HDC, const PIXELFORMATDESCRIPTOR*)             { return 1; }
extern "C" int   WINAPI xbox_wglDescribePixelFormat(HDC, int, UINT, PIXELFORMATDESCRIPTOR*)      { return 1; }
extern "C" int   WINAPI xbox_wglGetPixelFormat(HDC)                                              { return 1; }
extern "C" BOOL  WINAPI xbox_wglSetPixelFormat(HDC, int, const PIXELFORMATDESCRIPTOR*)           { return TRUE; }
extern "C" BOOL  WINAPI xbox_wglCopyContext(HGLRC, HGLRC, UINT)                                  { return TRUE; }

// wglGetProcAddress dispatcher: returns a smart stub for specific GL 2.0+
// functions that Mojang relies on, falling back to universal_no_op so every
// other entry point looks "supported".
struct ProcEntry { const char* name; PROC fn; };

extern "C" PROC WINAPI xbox_wglGetProcAddress(LPCSTR name) {
    if (!name) return (PROC)universal_no_op;

    // LWJGL's Windows bootstrap asks these through the OpenGL FunctionProvider.
    // Route them to this shim so an EGL-current context can satisfy the checks.
    if (std::strcmp(name, "wglGetCurrentContext") == 0) return (PROC)xbox_wglGetCurrentContext;
    if (std::strcmp(name, "wglGetCurrentDC") == 0)      return (PROC)xbox_wglGetCurrentDC;
    if (std::strcmp(name, "wglMakeCurrent") == 0)       return (PROC)xbox_wglMakeCurrent;
    if (std::strcmp(name, "wglCreateContext") == 0)     return (PROC)xbox_wglCreateContext;
    if (std::strcmp(name, "wglDeleteContext") == 0)     return (PROC)xbox_wglDeleteContext;

    if (std::strcmp(name, "glGetString") == 0)          return (PROC)glGetString;
    if (std::strcmp(name, "glGetStringi") == 0)         return (PROC)GLAPI_glGetStringi;
    if (std::strcmp(name, "glGetIntegerv") == 0)        return (PROC)glGetIntegerv;
    if (std::strcmp(name, "glGetFloatv") == 0)          return (PROC)glGetFloatv;
    if (std::strcmp(name, "glGetDoublev") == 0)         return (PROC)glGetDoublev;
    if (std::strcmp(name, "glGetBooleanv") == 0)        return (PROC)glGetBooleanv;
    if (std::strcmp(name, "glGetError") == 0)           return (PROC)glGetError;

    // Keep high-value GL calls routed through our compatibility exports.  The
    // exports forward to Mesa first, then update the D3D12 presentation counters
    // and fallback command bridge state.  Unknown/unsupported functions still go
    // straight to Mesa below.
    static const ProcEntry kCompatExports[] = {
        { "glActiveTexture",                   (PROC)glActiveTexture },
        { "glBindBuffer",                     (PROC)glBindBuffer },
        { "glBindBufferBase",                 (PROC)GLAPI_glBindBufferBase },
        { "glBindBufferRange",                (PROC)GLAPI_glBindBufferRange },
        { "glBindFramebuffer",                (PROC)GLAPI_glBindFramebuffer },
        { "glBindFramebufferEXT",             (PROC)GLAPI_glBindFramebuffer },
        { "glBindSampler",                    (PROC)GLAPI_glBindSampler },
        { "glBindTexture",                    (PROC)glBindTexture },
        { "glBindTextures",                   (PROC)GLAPI_glBindTextures },
        { "glBindTextureUnit",                (PROC)GLAPI_glBindTextureUnit },
        { "glBindVertexArray",                (PROC)glBindVertexArray },
        { "glBindVertexBuffer",               (PROC)GLAPI_glBindVertexBuffer },
        { "glBindVertexBuffers",              (PROC)GLAPI_glBindVertexBuffers },
        { "glBlitFramebuffer",                (PROC)GLAPI_glBlitFramebuffer },
        { "glBlitFramebufferEXT",             (PROC)GLAPI_glBlitFramebuffer },
        { "glBlitNamedFramebuffer",           (PROC)GLAPI_glBlitNamedFramebuffer },
        { "glBufferData",                     (PROC)glBufferData },
        { "glBufferStorage",                  (PROC)GLAPI_glBufferStorage },
        { "glBufferSubData",                  (PROC)GLAPI_glBufferSubData },
        { "glClear",                          (PROC)glClear },
        { "glClearBufferfv",                  (PROC)GLAPI_glClearBufferfv },
        { "glClearBufferiv",                  (PROC)GLAPI_glClearBufferiv },
        { "glClearBufferuiv",                 (PROC)GLAPI_glClearBufferuiv },
        { "glClearColor",                     (PROC)glClearColor },
        { "glClearNamedFramebufferfv",        (PROC)GLAPI_glClearNamedFramebufferfv },
        { "glCompressedTexImage2D",           (PROC)GLAPI_glCompressedTexImage2D },
        { "glCompressedTexSubImage2D",        (PROC)GLAPI_glCompressedTexSubImage2D },
        { "glCopyTexImage2D",                 (PROC)glCopyTexImage2D },
        { "glCopyTexSubImage2D",              (PROC)glCopyTexSubImage2D },
        { "glCopyTextureSubImage2D",          (PROC)GLAPI_glCopyTextureSubImage2D },
        { "glDisable",                        (PROC)glDisable },
        { "glDisableVertexArrayAttrib",       (PROC)GLAPI_glDisableVertexArrayAttrib },
        { "glDisableVertexAttribArray",       (PROC)GLAPI_glDisableVertexAttribArray },
        { "glDrawArrays",                     (PROC)glDrawArrays },
        { "glDrawArraysInstanced",            (PROC)GLAPI_glDrawArraysInstanced },
        { "glDrawBuffer",                     (PROC)glDrawBuffer },
        { "glDrawBuffers",                    (PROC)GLAPI_glDrawBuffers },
        { "glDrawElements",                   (PROC)glDrawElements },
        { "glDrawElementsBaseVertex",         (PROC)GLAPI_glDrawElementsBaseVertex },
        { "glDrawElementsInstanced",          (PROC)GLAPI_glDrawElementsInstanced },
        { "glDrawElementsInstancedBaseVertex",(PROC)GLAPI_glDrawElementsInstancedBaseVertex },
        { "glDrawRangeElements",              (PROC)GLAPI_glDrawRangeElements },
        { "glDrawRangeElementsBaseVertex",    (PROC)GLAPI_glDrawRangeElementsBaseVertex },
        { "glEnable",                         (PROC)glEnable },
        { "glEnableVertexArrayAttrib",        (PROC)GLAPI_glEnableVertexArrayAttrib },
        { "glEnableVertexAttribArray",        (PROC)GLAPI_glEnableVertexAttribArray },
        { "glClientWaitSync",                 (PROC)GLAPI_glClientWaitSync },
        { "glDeleteSync",                     (PROC)GLAPI_glDeleteSync },
        { "glFenceSync",                      (PROC)GLAPI_glFenceSync },
        { "glFinish",                         (PROC)glFinish },
        { "glFlush",                          (PROC)glFlush },
        { "glFlushMappedBufferRange",         (PROC)GLAPI_glFlushMappedBufferRange },
        { "glFogf",                           (PROC)glFogf },
        { "glFogfv",                          (PROC)glFogfv },
        { "glFogi",                           (PROC)glFogi },
        { "glFogiv",                          (PROC)glFogiv },
        { "glFramebufferTexture",             (PROC)GLAPI_glFramebufferTexture },
        { "glFramebufferTexture2D",           (PROC)GLAPI_glFramebufferTexture2D },
        { "glFramebufferTexture2DEXT",        (PROC)GLAPI_glFramebufferTexture2D },
        { "glFramebufferTextureEXT",          (PROC)GLAPI_glFramebufferTexture },
        { "glFramebufferTextureLayer",        (PROC)GLAPI_glFramebufferTextureLayer },
        { "glFramebufferTextureLayerEXT",     (PROC)GLAPI_glFramebufferTextureLayer },
        { "glCallList",                       (PROC)glCallList },
        { "glCallLists",                      (PROC)glCallLists },
        { "glDeleteLists",                    (PROC)glDeleteLists },
        { "glEndList",                        (PROC)glEndList },
        { "glGenLists",                       (PROC)glGenLists },
        { "glGenTextures",                    (PROC)glGenTextures },
        { "glIsList",                         (PROC)glIsList },
        { "glListBase",                       (PROC)glListBase },
        { "glNewList",                        (PROC)glNewList },
        { "glGenerateMipmap",                 (PROC)GLAPI_glGenerateMipmap },
        { "glGenerateTextureMipmap",          (PROC)GLAPI_glGenerateTextureMipmap },
        { "glGetBufferParameteriv",           (PROC)GLAPI_glGetBufferParameteriv },
        { "glGetBufferParameteri64v",         (PROC)GLAPI_glGetBufferParameteri64v },
        { "glGetBufferPointerv",              (PROC)GLAPI_glGetBufferPointerv },
        { "glGetBufferSubData",               (PROC)GLAPI_glGetBufferSubData },
        { "glGetLightfv",                     (PROC)glGetLightfv },
        { "glGetLightiv",                     (PROC)glGetLightiv },
        { "glGetMaterialfv",                  (PROC)glGetMaterialfv },
        { "glGetMaterialiv",                  (PROC)glGetMaterialiv },
        { "glGetNamedBufferPointerv",         (PROC)GLAPI_glGetNamedBufferPointerv },
        { "glGetPointerv",                    (PROC)GLAPI_glGetPointerv },
        { "glGetSynciv",                      (PROC)GLAPI_glGetSynciv },
        { "glGetUniformLocation",             (PROC)GLAPI_glGetUniformLocation },
        { "glGetVertexAttribPointerv",        (PROC)GLAPI_glGetVertexAttribPointerv },
        { "glGetVertexAttribiv",              (PROC)GLAPI_glGetVertexAttribiv },
        { "glGetVertexAttribfv",              (PROC)GLAPI_glGetVertexAttribfv },
        { "glGetVertexAttribdv",              (PROC)GLAPI_glGetVertexAttribdv },
        { "glGetVertexAttribIiv",             (PROC)GLAPI_glGetVertexAttribIiv },
        { "glGetVertexAttribIuiv",            (PROC)GLAPI_glGetVertexAttribIuiv },
        { "glIsEnabled",                      (PROC)glIsEnabled },
        { "glIsSync",                         (PROC)GLAPI_glIsSync },
        { "glIsTexture",                      (PROC)glIsTexture },
        { "glLightModelf",                    (PROC)glLightModelf },
        { "glLightModelfv",                   (PROC)glLightModelfv },
        { "glLightModeli",                    (PROC)glLightModeli },
        { "glLightModeliv",                   (PROC)glLightModeliv },
        { "glLightf",                         (PROC)glLightf },
        { "glLightfv",                        (PROC)glLightfv },
        { "glLighti",                         (PROC)glLighti },
        { "glLightiv",                        (PROC)glLightiv },
        { "glMapBuffer",                      (PROC)GLAPI_glMapBuffer },
        { "glMapBufferRange",                 (PROC)GLAPI_glMapBufferRange },
        { "glMapNamedBuffer",                 (PROC)GLAPI_glMapNamedBuffer },
        { "glMapNamedBufferRange",            (PROC)GLAPI_glMapNamedBufferRange },
        { "glMaterialf",                      (PROC)glMaterialf },
        { "glMaterialfv",                     (PROC)glMaterialfv },
        { "glMateriali",                      (PROC)glMateriali },
        { "glMaterialiv",                     (PROC)glMaterialiv },
        { "glMultiDrawArrays",                (PROC)GLAPI_glMultiDrawArrays },
        { "glMultiDrawElements",              (PROC)GLAPI_glMultiDrawElements },
        { "glMultiDrawElementsBaseVertex",    (PROC)GLAPI_glMultiDrawElementsBaseVertex },
        { "glNamedBufferData",                (PROC)GLAPI_glNamedBufferData },
        { "glNamedBufferStorage",             (PROC)GLAPI_glNamedBufferStorage },
        { "glNamedBufferSubData",             (PROC)GLAPI_glNamedBufferSubData },
        { "glNamedFramebufferTexture",        (PROC)GLAPI_glNamedFramebufferTexture },
        { "glNamedFramebufferTextureEXT",     (PROC)GLAPI_glNamedFramebufferTexture },
        { "glNamedFramebufferTextureLayer",   (PROC)GLAPI_glNamedFramebufferTextureLayer },
        { "glNamedFramebufferTextureLayerEXT",(PROC)GLAPI_glNamedFramebufferTextureLayer },
        { "glNormal3b",                       (PROC)glNormal3b },
        { "glNormal3bv",                      (PROC)glNormal3bv },
        { "glNormal3d",                       (PROC)glNormal3d },
        { "glNormal3dv",                      (PROC)glNormal3dv },
        { "glNormal3f",                       (PROC)glNormal3f },
        { "glNormal3fv",                      (PROC)glNormal3fv },
        { "glNormal3i",                       (PROC)glNormal3i },
        { "glNormal3iv",                      (PROC)glNormal3iv },
        { "glNormal3s",                       (PROC)glNormal3s },
        { "glNormal3sv",                      (PROC)glNormal3sv },
        { "glPixelStoref",                    (PROC)glPixelStoref },
        { "glPixelStorei",                    (PROC)glPixelStorei },
        { "glProgramUniform1i",               (PROC)GLAPI_glProgramUniform1i },
        { "glProgramUniform1iv",              (PROC)GLAPI_glProgramUniform1iv },
        { "glReadBuffer",                     (PROC)glReadBuffer },
        { "glTexImage2D",                     (PROC)glTexImage2D },
        { "glTexImage3D",                     (PROC)GLAPI_glTexImage3D },
        { "glTexParameterf",                  (PROC)glTexParameterf },
        { "glTexParameteri",                  (PROC)glTexParameteri },
        { "glTexStorage2D",                   (PROC)GLAPI_glTexStorage2D },
        { "glTexStorage3D",                   (PROC)GLAPI_glTexStorage3D },
        { "glTexSubImage2D",                  (PROC)glTexSubImage2D },
        { "glTexSubImage3D",                  (PROC)GLAPI_glTexSubImage3D },
        { "glTextureParameteri",              (PROC)GLAPI_glTextureParameteri },
        { "glTextureParameterf",              (PROC)GLAPI_glTextureParameterf },
        { "glTextureParameterfv",             (PROC)GLAPI_glTextureParameterfv },
        { "glTextureParameteriv",             (PROC)GLAPI_glTextureParameteriv },
        { "glTextureStorage2D",               (PROC)GLAPI_glTextureStorage2D },
        { "glTextureStorage3D",               (PROC)GLAPI_glTextureStorage3D },
        { "glTextureSubImage2D",              (PROC)GLAPI_glTextureSubImage2D },
        { "glTextureSubImage3D",              (PROC)GLAPI_glTextureSubImage3D },
        { "glUniform1i",                      (PROC)GLAPI_glUniform1i },
        { "glUniform1iv",                     (PROC)GLAPI_glUniform1iv },
        { "glUnmapBuffer",                    (PROC)GLAPI_glUnmapBuffer },
        { "glUnmapNamedBuffer",               (PROC)GLAPI_glUnmapNamedBuffer },
        { "glUseProgram",                     (PROC)GLAPI_glUseProgram },
        { "glVertexArrayAttribBinding",       (PROC)GLAPI_glVertexArrayAttribBinding },
        { "glVertexArrayAttribFormat",        (PROC)GLAPI_glVertexArrayAttribFormat },
        { "glVertexArrayAttribIFormat",       (PROC)GLAPI_glVertexArrayAttribIFormat },
        { "glVertexArrayElementBuffer",       (PROC)GLAPI_glVertexArrayElementBuffer },
        { "glVertexArrayVertexBuffer",        (PROC)GLAPI_glVertexArrayVertexBuffer },
        { "glVertexArrayVertexBuffers",       (PROC)GLAPI_glVertexArrayVertexBuffers },
        { "glVertexAttribBinding",            (PROC)GLAPI_glVertexAttribBinding },
        { "glVertexAttribFormat",             (PROC)GLAPI_glVertexAttribFormat },
        { "glVertexAttribIFormat",            (PROC)GLAPI_glVertexAttribIFormat },
        { "glVertexAttribIPointer",           (PROC)GLAPI_glVertexAttribIPointer },
        { "glVertexAttribPointer",            (PROC)GLAPI_glVertexAttribPointer },
        { "glViewport",                       (PROC)glViewport },
        { "glWaitSync",                       (PROC)GLAPI_glWaitSync },
    };

    for (const auto& e : kCompatExports) {
        if (std::strcmp(e.name, name) == 0) {
            static std::atomic<int> logCount{ 0 };
            int count = logCount.fetch_add(1, std::memory_order_relaxed);
            if (count < 64) {
                char line[320] = {};
                std::snprintf(line, sizeof(line), "wglGetProcAddress compat export %s -> %p", name, e.fn);
                DebugLine(line);
            }
            return e.fn;
        }
    }

    if (ForceCompatGlIdentity()) {
        static const char* kForcedCompatNoMesa[] = {
            "glClearDepth",
            "glClearDepthf",
            "glClearStencil",
            "glColorMask",
            "glColorMaski",
            "glCullFace",
            "glDepthFunc",
            "glDepthMask",
            "glDepthRange",
            "glDepthRangef",
            "glFrontFace",
            "glLineWidth",
            "glLogicOp",
            "glPointParameterf",
            "glPointParameterfv",
            "glPointParameteri",
            "glPointParameteriv",
            "glPointSize",
            "glPolygonMode",
            "glPolygonOffset",
            "glScissor",
            "glStencilFunc",
            "glStencilFuncSeparate",
            "glStencilMask",
            "glStencilMaskSeparate",
            "glStencilOp",
            "glStencilOpSeparate",
            "glGetTexImage",
            "glGetCompressedTexImage",
            "glGetTexLevelParameteriv",
            "glGetTexLevelParameterfv",
            "glGetTexParameteriv",
            "glGetTexParameterfv",
            "glGetTexParameterIiv",
            "glGetTexParameterIuiv",
            "glGetSamplerParameteriv",
            "glGetSamplerParameterfv",
            "glGetSamplerParameterIiv",
            "glGetSamplerParameterIuiv",
            "glTexImage1D",
            "glTexSubImage1D",
            "glCopyTexImage1D",
            "glCopyTexSubImage1D",
            "glCompressedTexImage1D",
            "glCompressedTexSubImage1D",
            "glTexImage2DMultisample",
            "glTexImage3DMultisample",
            "glGetMultisamplefv",
            "glSampleMaski",
            "glSampleCoverage",
        };

        for (const char* compatName : kForcedCompatNoMesa) {
            if (std::strcmp(compatName, name) == 0) {
                static std::atomic<int> logCount{ 0 };
                int count = logCount.fetch_add(1, std::memory_order_relaxed);
                if (count < 64) {
                    char line[320] = {};
                    std::snprintf(line, sizeof(line), "wglGetProcAddress forced compat stub %s", name);
                    DebugLine(line);
                }
                return (PROC)universal_no_op;
            }
        }
    }

    if (std::strncmp(name, "wgl", 3) != 0) {
        PROC real = ResolveRealOpenGlProc(name);
        if (real) {
            LogForwardedProc(name, real);
            return real;
        }
    }

    // Curated table for functions whose default-zero return would crash
    // Mojang (id generators) or whose semantics we must lie about
    // (compile/link status, sync objects, capabilities).
    static const ProcEntry kTable[] = {
        { "glGetStringi",                 (PROC)GLAPI_glGetStringi },
        { "glGetIntegeri_v",              (PROC)GLAPI_glGetIntegeri_v },
        { "glGetInteger64v",              (PROC)GLAPI_glGetInteger64v },
        { "glGetInteger64i_v",            (PROC)GLAPI_glGetInteger64i_v },
        { "glGetBooleani_v",              (PROC)GLAPI_glGetBooleani_v },
        { "glActiveTexture",              (PROC)glActiveTexture },
        { "glBindTexture",                (PROC)glBindTexture },
        { "glBindTextureUnit",            (PROC)GLAPI_glBindTextureUnit },
        { "glBindTextures",               (PROC)GLAPI_glBindTextures },
        { "glTexImage2D",                 (PROC)glTexImage2D },
        { "glTexSubImage2D",              (PROC)glTexSubImage2D },
        { "glTexParameteri",              (PROC)glTexParameteri },
        { "glTexParameterf",              (PROC)glTexParameterf },
        { "glPixelStorei",                (PROC)glPixelStorei },
        { "glPixelStoref",                (PROC)glPixelStoref },
        { "glTexStorage2D",               (PROC)GLAPI_glTexStorage2D },
        { "glTexStorage3D",               (PROC)GLAPI_glTexStorage3D },
        { "glTextureStorage2D",           (PROC)GLAPI_glTextureStorage2D },
        { "glTextureStorage3D",           (PROC)GLAPI_glTextureStorage3D },
        { "glTextureSubImage2D",          (PROC)GLAPI_glTextureSubImage2D },
        { "glTextureSubImage3D",          (PROC)GLAPI_glTextureSubImage3D },
        { "glCopyTexImage2D",             (PROC)glCopyTexImage2D },
        { "glCopyTexSubImage2D",          (PROC)glCopyTexSubImage2D },
        { "glCopyTextureSubImage2D",      (PROC)GLAPI_glCopyTextureSubImage2D },
        { "glTextureParameteri",          (PROC)GLAPI_glTextureParameteri },
        { "glTextureParameterf",          (PROC)GLAPI_glTextureParameterf },
        { "glTextureParameteriv",         (PROC)GLAPI_glTextureParameteriv },
        { "glTextureParameterfv",         (PROC)GLAPI_glTextureParameterfv },
        { "glTexImage3D",                 (PROC)GLAPI_glTexImage3D },
        { "glTexSubImage3D",              (PROC)GLAPI_glTexSubImage3D },
        { "glCompressedTexImage2D",       (PROC)GLAPI_glCompressedTexImage2D },
        { "glCompressedTexSubImage2D",    (PROC)GLAPI_glCompressedTexSubImage2D },
        { "glGenerateMipmap",             (PROC)GLAPI_glGenerateMipmap },
        { "glGenerateTextureMipmap",      (PROC)GLAPI_glGenerateTextureMipmap },
        { "glBindBuffer",                 (PROC)glBindBuffer },
        { "glBufferData",                 (PROC)glBufferData },
        { "glBufferSubData",              (PROC)GLAPI_glBufferSubData },
        { "glBindBufferBase",             (PROC)GLAPI_glBindBufferBase },
        { "glBindBufferRange",            (PROC)GLAPI_glBindBufferRange },
        { "glBindVertexArray",            (PROC)glBindVertexArray },
        { "glEnable",                     (PROC)glEnable },
        { "glDisable",                    (PROC)glDisable },
        { "glEnableVertexAttribArray",    (PROC)GLAPI_glEnableVertexAttribArray },
        { "glDisableVertexAttribArray",   (PROC)GLAPI_glDisableVertexAttribArray },
        { "glVertexAttribPointer",        (PROC)GLAPI_glVertexAttribPointer },
        { "glVertexAttribIPointer",       (PROC)GLAPI_glVertexAttribIPointer },
        { "glVertexAttribFormat",         (PROC)GLAPI_glVertexAttribFormat },
        { "glVertexAttribIFormat",        (PROC)GLAPI_glVertexAttribIFormat },
        { "glVertexAttribBinding",        (PROC)GLAPI_glVertexAttribBinding },
        { "glBindVertexBuffer",           (PROC)GLAPI_glBindVertexBuffer },
        { "glBindVertexBuffers",          (PROC)GLAPI_glBindVertexBuffers },
        { "glEnableVertexArrayAttrib",    (PROC)GLAPI_glEnableVertexArrayAttrib },
        { "glDisableVertexArrayAttrib",   (PROC)GLAPI_glDisableVertexArrayAttrib },
        { "glVertexArrayAttribFormat",    (PROC)GLAPI_glVertexArrayAttribFormat },
        { "glVertexArrayAttribIFormat",   (PROC)GLAPI_glVertexArrayAttribIFormat },
        { "glVertexArrayAttribBinding",   (PROC)GLAPI_glVertexArrayAttribBinding },
        { "glVertexArrayVertexBuffer",    (PROC)GLAPI_glVertexArrayVertexBuffer },
        { "glVertexArrayVertexBuffers",   (PROC)GLAPI_glVertexArrayVertexBuffers },
        { "glVertexArrayElementBuffer",   (PROC)GLAPI_glVertexArrayElementBuffer },
        { "glBindSampler",                (PROC)GLAPI_glBindSampler },
        { "glUseProgram",                 (PROC)GLAPI_glUseProgram },
        { "glGetUniformLocation",         (PROC)GLAPI_glGetUniformLocation },
        { "glGenBuffers",                 (PROC)GLAPI_glGenBuffers },
        { "glGenVertexArrays",            (PROC)GLAPI_glGenVertexArrays },
        { "glGenFramebuffers",            (PROC)GLAPI_glGenFramebuffers },
        { "glGenRenderbuffers",           (PROC)GLAPI_glGenRenderbuffers },
        { "glGenQueries",                 (PROC)GLAPI_glGenQueries },
        { "glGenSamplers",                (PROC)GLAPI_glGenSamplers },
        { "glGenProgramPipelines",        (PROC)GLAPI_glGenProgramPipelines },
        { "glGenTransformFeedbacks",      (PROC)GLAPI_glGenTransformFeedbacks },
        { "glCreateTextures",             (PROC)GLAPI_glCreateTextures },
        { "glCreateFramebuffers",         (PROC)GLAPI_glCreateFramebuffers },
        { "glCreateVertexArrays",         (PROC)GLAPI_glCreateVertexArrays },
        { "glCreateShader",               (PROC)GLAPI_glCreateShader },
        { "glCreateProgram",              (PROC)GLAPI_glCreateProgram },
        { "glCreateShaderProgramv",       (PROC)GLAPI_glCreateShaderProgramv },
        { "glCreateProgramPipelines",     (PROC)GLAPI_glCreateProgramPipelines },
        { "glIsBuffer",                   (PROC)GLAPI_glIsBuffer },
        { "glIsVertexArray",              (PROC)GLAPI_glIsVertexArray },
        { "glIsFramebuffer",              (PROC)GLAPI_glIsFramebuffer },
        { "glIsRenderbuffer",             (PROC)GLAPI_glIsRenderbuffer },
        { "glIsShader",                   (PROC)GLAPI_glIsShader },
        { "glIsProgram",                  (PROC)GLAPI_glIsProgram },
        { "glIsEnabledi",                 (PROC)GLAPI_glIsEnabledi },
        { "glBindFramebuffer",            (PROC)GLAPI_glBindFramebuffer },
        { "glBindFramebufferEXT",         (PROC)GLAPI_glBindFramebuffer },
        { "glFramebufferTexture2D",       (PROC)GLAPI_glFramebufferTexture2D },
        { "glFramebufferTexture2DEXT",    (PROC)GLAPI_glFramebufferTexture2D },
        { "glFramebufferTexture",         (PROC)GLAPI_glFramebufferTexture },
        { "glFramebufferTextureEXT",      (PROC)GLAPI_glFramebufferTexture },
        { "glFramebufferTextureLayer",    (PROC)GLAPI_glFramebufferTextureLayer },
        { "glFramebufferTextureLayerEXT", (PROC)GLAPI_glFramebufferTextureLayer },
        { "glNamedFramebufferTexture",    (PROC)GLAPI_glNamedFramebufferTexture },
        { "glNamedFramebufferTextureEXT", (PROC)GLAPI_glNamedFramebufferTexture },
        { "glNamedFramebufferTextureLayer",(PROC)GLAPI_glNamedFramebufferTextureLayer },
        { "glNamedFramebufferTextureLayerEXT",(PROC)GLAPI_glNamedFramebufferTextureLayer },
        { "glBlitFramebuffer",            (PROC)GLAPI_glBlitFramebuffer },
        { "glBlitFramebufferEXT",         (PROC)GLAPI_glBlitFramebuffer },
        { "glBlitNamedFramebuffer",       (PROC)GLAPI_glBlitNamedFramebuffer },
        { "glReadBuffer",                 (PROC)glReadBuffer },
        { "glDrawBuffer",                 (PROC)glDrawBuffer },
        { "glDrawBuffers",                (PROC)GLAPI_glDrawBuffers },
        { "glClearBufferfv",              (PROC)GLAPI_glClearBufferfv },
        { "glClearBufferiv",              (PROC)GLAPI_glClearBufferiv },
        { "glClearBufferuiv",             (PROC)GLAPI_glClearBufferuiv },
        { "glClearNamedFramebufferfv",    (PROC)GLAPI_glClearNamedFramebufferfv },
        { "glCheckFramebufferStatus",     (PROC)GLAPI_glCheckFramebufferStatus },
        { "glCheckFramebufferStatusEXT",  (PROC)GLAPI_glCheckFramebufferStatus },
        { "glCheckNamedFramebufferStatus", (PROC)GLAPI_glCheckNamedFramebufferStatus },
        { "glGetShaderiv",                (PROC)GLAPI_glGetShaderiv },
        { "glGetProgramiv",               (PROC)GLAPI_glGetProgramiv },
        { "glGetShaderInfoLog",           (PROC)GLAPI_glGetShaderInfoLog },
        { "glGetProgramInfoLog",          (PROC)GLAPI_glGetProgramInfoLog },
        { "glMapBuffer",                  (PROC)GLAPI_glMapBuffer },
        { "glMapBufferRange",             (PROC)GLAPI_glMapBufferRange },
        { "glFlushMappedBufferRange",     (PROC)GLAPI_glFlushMappedBufferRange },
        { "glMapNamedBuffer",             (PROC)GLAPI_glMapNamedBuffer },
        { "glMapNamedBufferRange",        (PROC)GLAPI_glMapNamedBufferRange },
        { "glUnmapBuffer",                (PROC)GLAPI_glUnmapBuffer },
        { "glUnmapNamedBuffer",           (PROC)GLAPI_glUnmapNamedBuffer },
        { "glGetBufferPointerv",          (PROC)GLAPI_glGetBufferPointerv },
        { "glGetNamedBufferPointerv",     (PROC)GLAPI_glGetNamedBufferPointerv },
        { "glNamedBufferData",            (PROC)GLAPI_glNamedBufferData },
        { "glNamedBufferSubData",         (PROC)GLAPI_glNamedBufferSubData },
        { "glBufferStorage",              (PROC)GLAPI_glBufferStorage },
        { "glNamedBufferStorage",         (PROC)GLAPI_glNamedBufferStorage },
        { "glFenceSync",                  (PROC)GLAPI_glFenceSync },
        { "glClientWaitSync",             (PROC)GLAPI_glClientWaitSync },
        { "glDeleteSync",                 (PROC)GLAPI_glDeleteSync },
        { "glIsSync",                     (PROC)GLAPI_glIsSync },
        { "glWaitSync",                   (PROC)GLAPI_glWaitSync },
        { "glGetSynciv",                  (PROC)GLAPI_glGetSynciv },
        // GL 1.x core functions still asked for via wglGetProcAddress
        { "glClearColor",                 (PROC)glClearColor },
        { "glClear",                      (PROC)glClear },
        { "glViewport",                   (PROC)glViewport },
        { "glDrawArrays",                 (PROC)glDrawArrays },
        { "glDrawElements",               (PROC)glDrawElements },
        { "glAlphaFunc",                  (PROC)glAlphaFunc },
        { "glBlendFunc",                  (PROC)glBlendFunc },
        { "glColorMask",                  (PROC)glColorMask },
        { "glColorMaterial",              (PROC)glColorMaterial },
        { "glColorPointer",               (PROC)glColorPointer },
        { "glCullFace",                   (PROC)glCullFace },
        { "glDepthFunc",                  (PROC)glDepthFunc },
        { "glDepthMask",                  (PROC)glDepthMask },
        { "glDepthRange",                 (PROC)glDepthRange },
        { "glEnableClientState",          (PROC)glEnableClientState },
        { "glDisableClientState",         (PROC)glDisableClientState },
        { "glFogf",                       (PROC)glFogf },
        { "glFogfv",                      (PROC)glFogfv },
        { "glFogi",                       (PROC)glFogi },
        { "glFogiv",                      (PROC)glFogiv },
        { "glFrustum",                    (PROC)glFrustum },
        { "glHint",                       (PROC)glHint },
        { "glLineWidth",                  (PROC)glLineWidth },
        { "glLoadIdentity",               (PROC)glLoadIdentity },
        { "glLoadMatrixd",                (PROC)glLoadMatrixd },
        { "glLoadMatrixf",                (PROC)glLoadMatrixf },
        { "glMatrixMode",                 (PROC)glMatrixMode },
        { "glMultMatrixd",                (PROC)glMultMatrixd },
        { "glMultMatrixf",                (PROC)glMultMatrixf },
        { "glLightModelf",                (PROC)glLightModelf },
        { "glLightModelfv",               (PROC)glLightModelfv },
        { "glLightModeli",                (PROC)glLightModeli },
        { "glLightModeliv",               (PROC)glLightModeliv },
        { "glLightf",                     (PROC)glLightf },
        { "glLightfv",                    (PROC)glLightfv },
        { "glLighti",                     (PROC)glLighti },
        { "glLightiv",                    (PROC)glLightiv },
        { "glMaterialf",                  (PROC)glMaterialf },
        { "glMaterialfv",                 (PROC)glMaterialfv },
        { "glMateriali",                  (PROC)glMateriali },
        { "glMaterialiv",                 (PROC)glMaterialiv },
        { "glNormal3b",                   (PROC)glNormal3b },
        { "glNormal3bv",                  (PROC)glNormal3bv },
        { "glNormal3d",                   (PROC)glNormal3d },
        { "glNormal3dv",                  (PROC)glNormal3dv },
        { "glNormal3f",                   (PROC)glNormal3f },
        { "glNormal3fv",                  (PROC)glNormal3fv },
        { "glNormal3i",                   (PROC)glNormal3i },
        { "glNormal3iv",                  (PROC)glNormal3iv },
        { "glNormal3s",                   (PROC)glNormal3s },
        { "glNormal3sv",                  (PROC)glNormal3sv },
        { "glNormalPointer",              (PROC)glNormalPointer },
        { "glOrtho",                      (PROC)glOrtho },
        { "glPointSize",                  (PROC)glPointSize },
        { "glPolygonOffset",              (PROC)glPolygonOffset },
        { "glPopAttrib",                  (PROC)glPopAttrib },
        { "glPushAttrib",                 (PROC)glPushAttrib },
        { "glPopClientAttrib",            (PROC)glPopClientAttrib },
        { "glPushClientAttrib",           (PROC)glPushClientAttrib },
        { "glPopMatrix",                  (PROC)glPopMatrix },
        { "glPushMatrix",                 (PROC)glPushMatrix },
        { "glRotated",                    (PROC)glRotated },
        { "glRotatef",                    (PROC)glRotatef },
        { "glScaled",                     (PROC)glScaled },
        { "glScalef",                     (PROC)glScalef },
        { "glScissor",                    (PROC)glScissor },
        { "glShadeModel",                 (PROC)glShadeModel },
        { "glStencilFunc",                (PROC)glStencilFunc },
        { "glStencilMask",                (PROC)glStencilMask },
        { "glStencilOp",                  (PROC)glStencilOp },
        { "glTexCoordPointer",            (PROC)glTexCoordPointer },
        { "glTexEnvf",                    (PROC)glTexEnvf },
        { "glTexEnvfv",                   (PROC)glTexEnvfv },
        { "glTexEnvi",                    (PROC)glTexEnvi },
        { "glTexEnviv",                   (PROC)glTexEnviv },
        { "glTexParameterfv",             (PROC)glTexParameterfv },
        { "glTexParameteriv",             (PROC)glTexParameteriv },
        { "glTranslated",                 (PROC)glTranslated },
        { "glTranslatef",                 (PROC)glTranslatef },
        { "glVertexPointer",              (PROC)glVertexPointer },
        { "glMultiDrawArrays",            (PROC)GLAPI_glMultiDrawArrays },
        { "glMultiDrawElements",          (PROC)GLAPI_glMultiDrawElements },
        { "glDrawArraysInstanced",        (PROC)GLAPI_glDrawArraysInstanced },
        { "glDrawElementsInstanced",      (PROC)GLAPI_glDrawElementsInstanced },
        { "glDrawRangeElements",          (PROC)GLAPI_glDrawRangeElements },
        { "glDrawElementsBaseVertex",     (PROC)GLAPI_glDrawElementsBaseVertex },
        { "glDrawRangeElementsBaseVertex",(PROC)GLAPI_glDrawRangeElementsBaseVertex },
        { "glDrawElementsInstancedBaseVertex",(PROC)GLAPI_glDrawElementsInstancedBaseVertex },
        { "glMultiDrawElementsBaseVertex",(PROC)GLAPI_glMultiDrawElementsBaseVertex },
        { "glGetString",                  (PROC)glGetString },
        { "glGetIntegerv",                (PROC)glGetIntegerv },
        { "glGetFloatv",                  (PROC)glGetFloatv },
        { "glGetDoublev",                 (PROC)glGetDoublev },
        { "glGetBooleanv",                (PROC)glGetBooleanv },
        { "glGetError",                   (PROC)glGetError },
        { "glGetLightfv",                 (PROC)glGetLightfv },
        { "glGetLightiv",                 (PROC)glGetLightiv },
        { "glGetMaterialfv",              (PROC)glGetMaterialfv },
        { "glGetMaterialiv",              (PROC)glGetMaterialiv },
        { "glFlush",                      (PROC)glFlush },
        { "glFinish",                     (PROC)glFinish },
        { "glIsEnabled",                  (PROC)glIsEnabled },
        { "glIsTexture",                  (PROC)glIsTexture },
        { "glCallList",                   (PROC)glCallList },
        { "glCallLists",                  (PROC)glCallLists },
        { "glDeleteLists",                (PROC)glDeleteLists },
        { "glEndList",                    (PROC)glEndList },
        { "glGenLists",                   (PROC)glGenLists },
        { "glIsList",                     (PROC)glIsList },
        { "glListBase",                   (PROC)glListBase },
        { "glNewList",                    (PROC)glNewList },
        { "glGenTextures",                (PROC)glGenTextures },
        { "glUniform1f",                  (PROC)GLAPI_glUniformTouch },
        { "glUniform2f",                  (PROC)GLAPI_glUniformTouch },
        { "glUniform3f",                  (PROC)GLAPI_glUniformTouch },
        { "glUniform4f",                  (PROC)GLAPI_glUniformTouch },
        { "glUniform1i",                  (PROC)GLAPI_glUniform1i },
        { "glUniform2i",                  (PROC)GLAPI_glUniformTouch },
        { "glUniform3i",                  (PROC)GLAPI_glUniformTouch },
        { "glUniform4i",                  (PROC)GLAPI_glUniformTouch },
        { "glUniform1fv",                 (PROC)GLAPI_glUniformTouch },
        { "glUniform2fv",                 (PROC)GLAPI_glUniformTouch },
        { "glUniform3fv",                 (PROC)GLAPI_glUniformTouch },
        { "glUniform4fv",                 (PROC)GLAPI_glUniformTouch },
        { "glUniform1iv",                 (PROC)GLAPI_glUniform1iv },
        { "glUniform2iv",                 (PROC)GLAPI_glUniformTouch },
        { "glUniform3iv",                 (PROC)GLAPI_glUniformTouch },
        { "glUniform4iv",                 (PROC)GLAPI_glUniformTouch },
        { "glProgramUniform1i",           (PROC)GLAPI_glProgramUniform1i },
        { "glProgramUniform1iv",          (PROC)GLAPI_glProgramUniform1iv },
        { "glUniformMatrix4fv",           (PROC)GLAPI_glUniformTouch },
    };

    for (const auto& e : kTable) {
        if (std::strcmp(e.name, name) == 0) {
            return e.fn;
        }
    }

    // Everything else looks "supported" but does nothing.
    LogFallbackProc(name);
    return (PROC)universal_no_op;
}

extern "C" PROC WINAPI xbox_wglGetDefaultProcAddress(LPCSTR name) {
    return xbox_wglGetProcAddress(name);
}

} // extern "C"
