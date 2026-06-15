// presentation.cpp
//
// Milestone 2: bind a D3D12 swapchain to the UWP XAML surface owned by the
// launcher process and Present() each frame. Java (child process) cannot
// own the Xbox display surface, so the launcher presents on its behalf. A
// named event (Local\MinecraftXboxJavaPresent) lets xbox-glfw.dll signal
// when glfwSwapBuffers is called. A small named shared-memory block carries
// the first OpenGL command translation slice (clear, viewport, draw counters).

#include "graphics_bridge.h"
#include "../xbox-gl-command-state.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.ui.xaml.media.dxinterop.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <string>

using Microsoft::WRL::ComPtr;

namespace
{
    static const wchar_t* kPresentEventName = L"Local\\MinecraftXboxJavaPresent";
    static constexpr UINT kFrameCount = 2;

    std::string g_renderer = "D3D12 bridge not initialized";
    std::string g_version = "OpenGL->D3D12 bridge v0.23";
    std::string g_last_error = "none";

    ComPtr<ID3D12Device> g_device;
    ComPtr<ID3D12CommandQueue> g_queue;
    ComPtr<ID3D12CommandAllocator> g_allocator;
    ComPtr<ID3D12GraphicsCommandList> g_commandList;
    ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
    ComPtr<IDXGISwapChain3> g_swapChain;
    ComPtr<ID3D12Resource> g_renderTargets[kFrameCount];
    ComPtr<ID3D12Resource> g_textureUploadBuffer;
    ComPtr<ID3D12Fence> g_fence;

    UINT g_rtvDescriptorSize = 0;
    UINT g_frameIndex = 0;
    UINT g_width = 0;
    UINT g_height = 0;
    DXGI_FORMAT g_swapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    UINT64 g_textureUploadBufferSize = 0;
    LONG64 g_lastPresentedTextureSampleSerial = 0;
    LONG64 g_lastPresentedGuiFrameSerial = 0;
    LONG64 g_lastAcceptedGuiFrameSerial = 0;
    UINT64 g_lastGuiNonBlackPixels = 0;
    UINT64 g_lastGuiNonTransparentPixels = 0;
    UINT64 g_lastGuiAverageRed = 0;
    UINT64 g_lastGuiAverageGreen = 0;
    UINT64 g_lastGuiAverageBlue = 0;
    UINT64 g_lastGuiCenterRgb = 0;
    UINT64 g_fenceValue = 0;
    HANDLE g_fenceEvent = nullptr;
    HANDLE g_presentEvent = nullptr;
    HANDLE g_glCommandStateMapping = nullptr;
    MinecraftXboxGlCommandState* g_glCommandState = nullptr;
    bool g_presentationReady = false;
    uint64_t g_presentCount = 0;
    uint64_t g_javaSwapSignals = 0;

    void SetError(const std::string& message)
    {
        g_last_error = message;
    }

    void SetHresultError(const char* context, HRESULT hr)
    {
        char buffer[192] = {};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s (HRESULT=0x%08lX)",
            context,
            static_cast<unsigned long>(hr));
        g_last_error = buffer;
    }

    void SetWin32Error(const char* context, DWORD error)
    {
        char buffer[192] = {};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s failed (GetLastError=%lu)",
            context,
            static_cast<unsigned long>(error));
        g_last_error = buffer;
    }

    int SetSehError(const char* context, EXCEPTION_POINTERS* exceptionInfo)
    {
        const auto* record = exceptionInfo ? exceptionInfo->ExceptionRecord : nullptr;
        const auto code = record ? record->ExceptionCode : 0ul;
        const void* address = record ? record->ExceptionAddress : nullptr;

        MEMORY_BASIC_INFORMATION moduleInfo = {};
        char modulePath[MAX_PATH] = {};
        const char* moduleName = "unknown";
        unsigned long long moduleOffset = 0;
        if (address && VirtualQuery(address, &moduleInfo, sizeof(moduleInfo)) == sizeof(moduleInfo))
        {
            const auto moduleBase = reinterpret_cast<const char*>(moduleInfo.AllocationBase);
            moduleOffset = static_cast<unsigned long long>(
                reinterpret_cast<const char*>(address) - moduleBase);
            const DWORD pathLength = GetModuleFileNameA(
                reinterpret_cast<HMODULE>(moduleInfo.AllocationBase),
                modulePath,
                static_cast<DWORD>(sizeof(modulePath)));
            if (pathLength > 0)
            {
                modulePath[sizeof(modulePath) - 1] = '\0';
                const char* slash = std::strrchr(modulePath, '\\');
                const char* forwardSlash = std::strrchr(modulePath, '/');
                if (forwardSlash && (!slash || forwardSlash > slash))
                {
                    slash = forwardSlash;
                }

                moduleName = slash ? slash + 1 : modulePath;
            }
        }

        unsigned long long faultAddress = 0;
        const bool hasFaultAddress =
            record &&
            record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            record->NumberParameters >= 2;
        if (hasFaultAddress)
        {
            faultAddress = static_cast<unsigned long long>(record->ExceptionInformation[1]);
        }

        char buffer[384] = {};
        if (hasFaultAddress)
        {
            std::snprintf(
                buffer,
                sizeof(buffer),
                "%s raised SEH exception 0x%08lX at %s+0x%llX fault=0x%llX",
                context,
                code,
                moduleName,
                moduleOffset,
                faultAddress);
        }
        else
        {
            std::snprintf(
                buffer,
                sizeof(buffer),
                "%s raised SEH exception 0x%08lX at %s+0x%llX",
                context,
                code,
                moduleName,
                moduleOffset);
        }

        g_last_error = buffer;
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void SetSwapChainError(const char* context, UINT width, UINT height, HRESULT hr)
    {
        char buffer[256] = {};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s %ux%u",
            context,
            width,
            height);
        SetHresultError(buffer, hr);
    }

    void WaitForGpu()
    {
        if (!g_fence || !g_queue || !g_fenceEvent)
        {
            return;
        }

        const UINT64 signalValue = g_fenceValue;
        g_queue->Signal(g_fence.Get(), signalValue);
        g_fenceValue++;

        if (g_fence->GetCompletedValue() < signalValue)
        {
            g_fence->SetEventOnCompletion(signalValue, g_fenceEvent);
            WaitForSingleObject(g_fenceEvent, INFINITE);
        }
    }

    void ReleasePresentationResources()
    {
        WaitForGpu();

        for (UINT i = 0; i < kFrameCount; ++i)
        {
            g_renderTargets[i].Reset();
        }

        g_swapChain.Reset();
        g_textureUploadBuffer.Reset();
        g_rtvHeap.Reset();
        g_commandList.Reset();
        g_allocator.Reset();
        g_queue.Reset();
        g_device.Reset();

        if (g_fenceEvent)
        {
            CloseHandle(g_fenceEvent);
            g_fenceEvent = nullptr;
        }
        g_fence.Reset();

        if (g_glCommandState)
        {
            UnmapViewOfFile(g_glCommandState);
            g_glCommandState = nullptr;
        }
        if (g_glCommandStateMapping)
        {
            CloseHandle(g_glCommandStateMapping);
            g_glCommandStateMapping = nullptr;
        }

        g_presentationReady = false;
        g_frameIndex = 0;
        g_width = 0;
        g_height = 0;
        g_swapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        g_textureUploadBufferSize = 0;
        g_lastPresentedTextureSampleSerial = 0;
        g_lastPresentedGuiFrameSerial = 0;
        g_lastAcceptedGuiFrameSerial = 0;
    }

    int ProbeWithDxgiFactory()
    {
        ComPtr<IDXGIFactory6> factory;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            SetError("CreateDXGIFactory1 failed");
            return 0;
        }

        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                adapter.Reset();
                continue;
            }

            ComPtr<ID3D12Device> device;
            hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
            if (SUCCEEDED(hr))
            {
                g_renderer = "D3D12 device probe ok";
                g_last_error = "none";
                return 1;
            }

            adapter.Reset();
        }

        ComPtr<ID3D12Device> fallback;
        hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&fallback));
        if (FAILED(hr))
        {
            SetError("D3D12CreateDevice failed for all adapters");
            return 0;
        }

        g_renderer = "D3D12 default adapter probe ok";
        g_last_error = "none";
        return 1;
    }

    using EGLDisplay = void*;
    using EGLConfig = void*;
    using EGLSurface = void*;
    using EGLContext = void*;
    using EGLBoolean = unsigned int;
    using EGLenum = unsigned int;
    using EGLint = int;

    static constexpr EGLDisplay EGL_NO_DISPLAY_VALUE = nullptr;
    static constexpr EGLSurface EGL_NO_SURFACE_VALUE = nullptr;
    static constexpr EGLContext EGL_NO_CONTEXT_VALUE = nullptr;
    static constexpr EGLint EGL_FALSE_VALUE = 0;
    static constexpr EGLint EGL_NONE_VALUE = 0x3038;
    static constexpr EGLint EGL_WINDOW_BIT_VALUE = 0x0004;
    static constexpr EGLint EGL_PBUFFER_BIT_VALUE = 0x0001;
    static constexpr EGLint EGL_OPENGL_BIT_VALUE = 0x0008;
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
    static constexpr EGLint EGL_PLATFORM_SURFACELESS_MESA_VALUE = 0x31DD;
    static constexpr EGLenum EGL_OPENGL_API_VALUE = 0x30A2;
    static constexpr unsigned int GL_RENDERER_VALUE = 0x1F01;
    static constexpr unsigned int GL_VERSION_VALUE = 0x1F02;

    typedef EGLDisplay(__stdcall* EglGetDisplayProc)(void*);
    typedef EGLDisplay(__stdcall* EglGetPlatformDisplayExtProc)(EGLenum, void*, const EGLint*);
    typedef EGLBoolean(__stdcall* EglInitializeProc)(EGLDisplay, EGLint*, EGLint*);
    typedef EGLBoolean(__stdcall* EglChooseConfigProc)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
    typedef EGLBoolean(__stdcall* EglBindApiProc)(EGLenum);
    typedef EGLSurface(__stdcall* EglCreateWindowSurfaceProc)(EGLDisplay, EGLConfig, void*, const EGLint*);
    typedef EGLSurface(__stdcall* EglCreatePbufferSurfaceProc)(EGLDisplay, EGLConfig, const EGLint*);
    typedef EGLContext(__stdcall* EglCreateContextProc)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
    typedef EGLBoolean(__stdcall* EglMakeCurrentProc)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    typedef EGLBoolean(__stdcall* EglDestroyContextProc)(EGLDisplay, EGLContext);
    typedef EGLBoolean(__stdcall* EglDestroySurfaceProc)(EGLDisplay, EGLSurface);
    typedef EGLBoolean(__stdcall* EglTerminateProc)(EGLDisplay);
    typedef EGLint(__stdcall* EglGetErrorProc)(void);
    typedef void* (__stdcall* EglGetProcAddressProc)(const char*);
    typedef const unsigned char* (__stdcall* GlGetStringProc)(unsigned int);
    typedef int(__stdcall* MesaUwpEnsureWglInitializedProc)(void);
    typedef const char* (__cdecl* MesaUwpGetWglInitStageProc)(void);
    typedef const char* (__cdecl* MesaUwpGetWglInitHistoryProc)(void);
    typedef const char* (__cdecl* MesaUwpGetWglInitDetailProc)(void);
    typedef void* (__cdecl* MesaStwGetDeviceProc)(void);
    typedef bool(__cdecl* MesaStwInitScreenProc)(void*);

    struct MesaEglProcs
    {
        HMODULE egl = nullptr;
        HMODULE opengl = nullptr;
        EglGetDisplayProc getDisplay = nullptr;
        EglGetPlatformDisplayExtProc getPlatformDisplayExt = nullptr;
        EglInitializeProc initialize = nullptr;
        EglChooseConfigProc chooseConfig = nullptr;
        EglBindApiProc bindApi = nullptr;
        EglCreateWindowSurfaceProc createWindowSurface = nullptr;
        EglCreatePbufferSurfaceProc createPbufferSurface = nullptr;
        EglCreateContextProc createContext = nullptr;
        EglMakeCurrentProc makeCurrent = nullptr;
        EglDestroyContextProc destroyContext = nullptr;
        EglDestroySurfaceProc destroySurface = nullptr;
        EglTerminateProc terminate = nullptr;
        EglGetErrorProc getError = nullptr;
        EglGetProcAddressProc getProcAddress = nullptr;
    };

    std::wstring JoinMesaPath(const wchar_t* directory, const wchar_t* fileName)
    {
        std::wstring path = directory ? directory : L"";
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
        {
            path.push_back(L'\\');
        }

        path.append(fileName);
        return path;
    }

    std::wstring JoinMesaSiblingPath(const wchar_t* directory, const wchar_t* fileName)
    {
        std::wstring path = directory ? directory : L"";
        while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
        {
            path.pop_back();
        }

        const auto slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
        {
            path.resize(slash + 1);
        }
        else if (!path.empty())
        {
            path.push_back(L'\\');
        }

        path.append(fileName);
        return path;
    }

    std::string WideToUtf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int needed = WideCharToMultiByte(
            CP_UTF8,
            0,
            value.c_str(),
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (needed <= 1)
        {
            return {};
        }

        std::string result(static_cast<size_t>(needed - 1), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            value.c_str(),
            -1,
            result.data(),
            needed,
            nullptr,
            nullptr);
        return result;
    }

    void SetMesaFmallocEnvironment(const wchar_t* mesaDirectory)
    {
        const auto cachePath = JoinMesaSiblingPath(mesaDirectory, L"mesa-fmalloc-probe.swap");
        const auto cachePathUtf8 = WideToUtf8(cachePath);
        if (!cachePathUtf8.empty())
        {
            _putenv_s("MESA_FMALLOC_CACHE_FILE", cachePathUtf8.c_str());
            SetEnvironmentVariableW(L"MESA_FMALLOC_CACHE_FILE", cachePath.c_str());
        }

        _putenv_s("MESA_FMALLOC_CACHE_MB", "512");
        SetEnvironmentVariableA("MESA_FMALLOC_CACHE_MB", "512");
    }

    HMODULE LoadMesaDll(const wchar_t* directory, const wchar_t* fileName, bool required)
    {
        const auto path = JoinMesaPath(directory, fileName);
        HMODULE module = LoadLibraryW(path.c_str());
        if (!module && required)
        {
            char context[128] = {};
            std::snprintf(context, sizeof(context), "LoadLibraryW(%ls)", fileName);
            SetWin32Error(context, GetLastError());
        }

        return module;
    }

    bool SafeEnsureMesaWglInitialized(MesaUwpEnsureWglInitializedProc proc)
    {
        __try
        {
            return proc() != 0;
        }
        __except (SetSehError("MesaUwpEnsureWglInitialized", GetExceptionInformation()))
        {
            return false;
        }
    }

    bool SafeGetMesaWglDevice(MesaStwGetDeviceProc proc, void** out)
    {
        __try
        {
            *out = proc();
            return true;
        }
        __except (SetSehError("stw_get_device", GetExceptionInformation()))
        {
            return false;
        }
    }

    bool SafeGetMesaWglInitStage(MesaUwpGetWglInitStageProc proc, const char** out)
    {
        __try
        {
            *out = proc ? proc() : nullptr;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *out = nullptr;
            return false;
        }
    }

    void AppendMesaWglInitStage(
        MesaUwpGetWglInitStageProc stageProc,
        MesaUwpGetWglInitDetailProc detailProc,
        MesaUwpGetWglInitHistoryProc historyProc)
    {
        const char* stage = nullptr;
        if (!SafeGetMesaWglInitStage(stageProc, &stage) || !stage || !*stage)
        {
            return;
        }

        if (g_last_error == "none")
        {
            g_last_error = "Mesa WGL startup failed";
        }

        g_last_error += " stage=";
        g_last_error += stage;

        const char* detail = nullptr;
        if (SafeGetMesaWglInitStage(detailProc, &detail) && detail && *detail)
        {
            g_last_error += " detail=\"";
            g_last_error += detail;
            g_last_error += "\"";
        }

        const char* history = nullptr;
        if (SafeGetMesaWglInitStage(historyProc, &history) && history && *history)
        {
            g_last_error += " history=[";
            g_last_error += history;
            g_last_error += "]";
        }
    }

    bool SafeInitMesaWglScreen(MesaStwInitScreenProc proc, void* nativeDisplay)
    {
        __try
        {
            return proc(nativeDisplay);
        }
        __except (SetSehError("stw_init_screen(CoreWindow)", GetExceptionInformation()))
        {
            return false;
        }
    }

    void LoadOptionalMesaDependencyDlls(const wchar_t* mesaDirectory)
    {
        const wchar_t* dlls[] =
        {
            L"vcruntime140_app.dll",
            L"vcruntime140_1_app.dll",
            L"msvcp140_app.dll",
            L"vccorlib140_app.dll",
            L"xbox_fmalloc.dll",
            L"Ole32.dll",
            L"libglapi.dll",
            L"libgallium_wgl.dll",
            L"libGLESv2.dll",
        };

        for (const auto* dll : dlls)
        {
            LoadMesaDll(mesaDirectory, dll, false);
        }
    }

    void* GetRequiredProc(HMODULE module, const char* name)
    {
        void* proc = reinterpret_cast<void*>(GetProcAddress(module, name));
        if (!proc)
        {
            char buffer[160] = {};
            std::snprintf(buffer, sizeof(buffer), "GetProcAddress(%s) failed", name);
            SetError(buffer);
        }

        return proc;
    }

    bool ProbeMesaWglStartup(const wchar_t* mesaDirectory, void* coreWindowUnknown)
    {
        LoadOptionalMesaDependencyDlls(mesaDirectory);
        HMODULE gallium = LoadMesaDll(mesaDirectory, L"libgallium_wgl.dll", true);
        if (!gallium)
        {
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
        if (!ensureWglInitialized)
        {
            SetError("MesaUwpEnsureWglInitialized export missing from libgallium_wgl.dll");
            return false;
        }

        if (!SafeEnsureMesaWglInitialized(ensureWglInitialized))
        {
            if (g_last_error == "none")
            {
                SetError("MesaUwpEnsureWglInitialized failed");
            }

            AppendMesaWglInitStage(getWglInitStage, getWglInitDetail, getWglInitHistory);
            return false;
        }

        auto getDevice = reinterpret_cast<MesaStwGetDeviceProc>(
            GetProcAddress(gallium, "stw_get_device"));
        if (!getDevice)
        {
            SetError("stw_get_device export missing from libgallium_wgl.dll");
            return false;
        }

        void* device = nullptr;
        if (!SafeGetMesaWglDevice(getDevice, &device))
        {
            return false;
        }

        if (!device)
        {
            SetError("MesaUwpEnsureWglInitialized returned but stw_get_device is null");
            return false;
        }

        if (coreWindowUnknown)
        {
            auto initScreen = reinterpret_cast<MesaStwInitScreenProc>(
                GetProcAddress(gallium, "stw_init_screen"));
            if (!initScreen)
            {
                SetError("stw_init_screen export missing from libgallium_wgl.dll");
                return false;
            }

            if (!SafeInitMesaWglScreen(initScreen, coreWindowUnknown))
            {
                if (g_last_error == "none")
                {
                    SetError("stw_init_screen(CoreWindow) returned false");
                }

                AppendMesaWglInitStage(getWglInitStage, getWglInitDetail, getWglInitHistory);
                return false;
            }

            device = nullptr;
            if (!SafeGetMesaWglDevice(getDevice, &device))
            {
                return false;
            }

            if (!device)
            {
                SetError("stw_init_screen(CoreWindow) returned but stw_get_device is null");
                return false;
            }
        }

        return true;
    }

    EGLint MesaEglErrorCode(const MesaEglProcs& egl)
    {
        return egl.getError ? egl.getError() : 0;
    }

    void SetMesaEglCallError(const char* context, const MesaEglProcs& egl)
    {
        char buffer[192] = {};
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s failed (EGL error=0x%04X)",
            context,
            static_cast<unsigned int>(MesaEglErrorCode(egl)));
        SetError(buffer);
    }

    bool LoadMesaEglProcs(const wchar_t* mesaDirectory, MesaEglProcs* egl)
    {
        if (!egl)
        {
            SetError("Mesa EGL proc table missing");
            return false;
        }

        LoadOptionalMesaDependencyDlls(mesaDirectory);
        egl->opengl = LoadMesaDll(mesaDirectory, L"opengl32.dll", false);
        egl->egl = LoadMesaDll(mesaDirectory, L"libEGL.dll", true);
        if (!egl->egl)
        {
            return false;
        }

        egl->getDisplay = reinterpret_cast<EglGetDisplayProc>(GetRequiredProc(egl->egl, "eglGetDisplay"));
        egl->initialize = reinterpret_cast<EglInitializeProc>(GetRequiredProc(egl->egl, "eglInitialize"));
        egl->chooseConfig = reinterpret_cast<EglChooseConfigProc>(GetRequiredProc(egl->egl, "eglChooseConfig"));
        egl->bindApi = reinterpret_cast<EglBindApiProc>(GetRequiredProc(egl->egl, "eglBindAPI"));
        egl->createWindowSurface = reinterpret_cast<EglCreateWindowSurfaceProc>(GetRequiredProc(egl->egl, "eglCreateWindowSurface"));
        egl->createPbufferSurface = reinterpret_cast<EglCreatePbufferSurfaceProc>(GetRequiredProc(egl->egl, "eglCreatePbufferSurface"));
        egl->createContext = reinterpret_cast<EglCreateContextProc>(GetRequiredProc(egl->egl, "eglCreateContext"));
        egl->makeCurrent = reinterpret_cast<EglMakeCurrentProc>(GetRequiredProc(egl->egl, "eglMakeCurrent"));
        egl->destroyContext = reinterpret_cast<EglDestroyContextProc>(GetRequiredProc(egl->egl, "eglDestroyContext"));
        egl->destroySurface = reinterpret_cast<EglDestroySurfaceProc>(GetRequiredProc(egl->egl, "eglDestroySurface"));
        egl->terminate = reinterpret_cast<EglTerminateProc>(GetRequiredProc(egl->egl, "eglTerminate"));
        egl->getError = reinterpret_cast<EglGetErrorProc>(GetRequiredProc(egl->egl, "eglGetError"));
        egl->getProcAddress = reinterpret_cast<EglGetProcAddressProc>(GetRequiredProc(egl->egl, "eglGetProcAddress"));
        egl->getPlatformDisplayExt =
            reinterpret_cast<EglGetPlatformDisplayExtProc>(GetProcAddress(egl->egl, "eglGetPlatformDisplayEXT"));

        if (!egl->getPlatformDisplayExt && egl->getProcAddress)
        {
            egl->getPlatformDisplayExt =
                reinterpret_cast<EglGetPlatformDisplayExtProc>(egl->getProcAddress("eglGetPlatformDisplayEXT"));
        }

        return egl->getDisplay && egl->initialize && egl->chooseConfig && egl->bindApi &&
            egl->createWindowSurface && egl->createPbufferSurface && egl->createContext &&
            egl->makeCurrent && egl->destroyContext && egl->destroySurface && egl->terminate &&
            egl->getError && egl->getProcAddress;
    }

    bool SafeGetDisplay(const char* context, EglGetDisplayProc proc, void* nativeDisplay, EGLDisplay* out)
    {
        __try
        {
            *out = proc(nativeDisplay);
            return true;
        }
        __except (SetSehError(context, GetExceptionInformation()))
        {
            return false;
        }
    }

    bool SafeGetPlatformDisplayExt(EglGetPlatformDisplayExtProc proc, EGLenum platform, void* nativeDisplay, EGLDisplay* out)
    {
        __try
        {
            *out = proc(platform, nativeDisplay, nullptr);
            return true;
        }
        __except (SetSehError("eglGetPlatformDisplayEXT", GetExceptionInformation()))
        {
            return false;
        }
    }

    bool SafeInitialize(const char* context, EglInitializeProc proc, EGLDisplay display, EGLint* major, EGLint* minor, EGLBoolean* out)
    {
        __try
        {
            *out = proc(display, major, minor);
            return true;
        }
        __except (SetSehError(context, GetExceptionInformation()))
        {
            return false;
        }
    }

    bool SafeChooseConfig(EglChooseConfigProc proc, EGLDisplay display, const EGLint* attribs, EGLConfig* config, EGLint* count, EGLBoolean* out)
    {
        __try
        {
            *out = proc(display, attribs, config, 1, count);
            return true;
        }
        __except (SetSehError("eglChooseConfig", GetExceptionInformation()))
        {
            return false;
        }
    }

    bool SafeBindApi(EglBindApiProc proc, EGLenum api, EGLBoolean* out)
    {
        __try
        {
            *out = proc(api);
            return true;
        }
        __except (SetSehError("eglBindAPI", GetExceptionInformation()))
        {
            return false;
        }
    }

    bool SafeCreateWindowSurface(EglCreateWindowSurfaceProc proc, EGLDisplay display, EGLConfig config, void* window, const EGLint* attribs, EGLSurface* out)
    {
        __try
        {
            *out = proc(display, config, window, attribs);
            return true;
        }
        __except (SetSehError("eglCreateWindowSurface", GetExceptionInformation()))
        {
            return false;
        }
    }

    bool SafeCreatePbufferSurface(EglCreatePbufferSurfaceProc proc, EGLDisplay display, EGLConfig config, const EGLint* attribs, EGLSurface* out)
    {
        __try
        {
            *out = proc(display, config, attribs);
            return true;
        }
        __except (SetSehError("eglCreatePbufferSurface", GetExceptionInformation()))
        {
            return false;
        }
    }

    bool SafeCreateContext(EglCreateContextProc proc, EGLDisplay display, EGLConfig config, const EGLint* attribs, EGLContext* out)
    {
        __try
        {
            *out = proc(display, config, EGL_NO_CONTEXT_VALUE, attribs);
            return true;
        }
        __except (SetSehError("eglCreateContext", GetExceptionInformation()))
        {
            return false;
        }
    }

    bool SafeMakeCurrent(EglMakeCurrentProc proc, EGLDisplay display, EGLSurface surface, EGLContext context, EGLBoolean* out)
    {
        __try
        {
            *out = proc(display, surface, surface, context);
            return true;
        }
        __except (SetSehError("eglMakeCurrent", GetExceptionInformation()))
        {
            return false;
        }
    }

    bool SafeGlGetString(GlGetStringProc proc, unsigned int name, const unsigned char** out)
    {
        __try
        {
            *out = proc(name);
            return true;
        }
        __except (SetSehError("glGetString", GetExceptionInformation()))
        {
            return false;
        }
    }

    void SafeCleanupMesaEgl(const MesaEglProcs& egl, EGLDisplay display, EGLSurface surface, EGLContext context)
    {
        if (egl.makeCurrent && display != EGL_NO_DISPLAY_VALUE)
        {
            __try
            {
                egl.makeCurrent(display, EGL_NO_SURFACE_VALUE, EGL_NO_SURFACE_VALUE, EGL_NO_CONTEXT_VALUE);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        if (egl.destroyContext && display != EGL_NO_DISPLAY_VALUE && context != EGL_NO_CONTEXT_VALUE)
        {
            __try
            {
                egl.destroyContext(display, context);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        if (egl.destroySurface && display != EGL_NO_DISPLAY_VALUE && surface != EGL_NO_SURFACE_VALUE)
        {
            __try
            {
                egl.destroySurface(display, surface);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        if (egl.terminate && display != EGL_NO_DISPLAY_VALUE)
        {
            __try
            {
                egl.terminate(display);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
    }

    HRESULT CreateDeviceAndQueue()
    {
        if (g_device && g_queue)
        {
            return S_OK;
        }

        ComPtr<IDXGIFactory4> factory;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            SetHresultError("CreateDXGIFactory1 failed during presentation init", hr);
            return hr;
        }

        ComPtr<IDXGIAdapter1> adapter;
        for (UINT adapterIndex = 0;
             factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND;
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc = {};
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                adapter.Reset();
                continue;
            }

            hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device));
            if (SUCCEEDED(hr))
            {
                break;
            }

            g_device.Reset();
            adapter.Reset();
        }

        if (!g_device)
        {
            hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device));
            if (FAILED(hr))
            {
                SetHresultError("D3D12CreateDevice failed during presentation init", hr);
                return hr;
            }
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr = g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_queue));
        if (FAILED(hr))
        {
            SetHresultError("CreateCommandQueue failed", hr);
            g_device.Reset();
            return hr;
        }

        return S_OK;
    }

    void FillSwapChainDesc(
        DXGI_SWAP_CHAIN_DESC1& desc,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        DXGI_SWAP_EFFECT swapEffect)
    {
        desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = kFrameCount;
        desc.SwapEffect = swapEffect;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    }

    HRESULT QueryDxgiFactoryForSwapChain(ComPtr<IDXGIFactory4>& factory)
    {
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            SetHresultError("CreateDXGIFactory1 failed during swapchain creation", hr);
        }

        return hr;
    }

    HRESULT FinalizeSwapChain(ComPtr<IDXGISwapChain1>& swapChain1)
    {
        HRESULT hr = swapChain1.As(&g_swapChain);
        if (FAILED(hr))
        {
            SetHresultError("QueryInterface IDXGISwapChain3 failed", hr);
            return hr;
        }

        g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
        return S_OK;
    }

    HRESULT CreateSwapChainForXamlPanel(IUnknown* panelUnknown, UINT width, UINT height)
    {
        ComPtr<ISwapChainPanelNative> panelNative;
        HRESULT hr = panelUnknown->QueryInterface(IID_PPV_ARGS(&panelNative));
        if (FAILED(hr))
        {
            SetHresultError("QueryInterface ISwapChainPanelNative failed", hr);
            return hr;
        }

        ComPtr<IDXGIFactory4> factory;
        hr = QueryDxgiFactoryForSwapChain(factory);
        if (FAILED(hr))
        {
            return hr;
        }

        const DXGI_FORMAT formats[] = {
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM,
        };
        const DXGI_SWAP_EFFECT swapEffects[] = {
            DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
            DXGI_SWAP_EFFECT_FLIP_DISCARD,
        };

        ComPtr<IDXGISwapChain1> swapChain1;
        for (DXGI_SWAP_EFFECT swapEffect : swapEffects)
        {
            for (DXGI_FORMAT format : formats)
            {
                DXGI_SWAP_CHAIN_DESC1 desc = {};
                FillSwapChainDesc(desc, width, height, format, swapEffect);

                swapChain1.Reset();
                hr = factory->CreateSwapChainForComposition(
                    g_queue.Get(),
                    &desc,
                    nullptr,
                    &swapChain1);
                if (SUCCEEDED(hr))
                {
                    g_swapChainFormat = format;
                    break;
                }
            }

            if (SUCCEEDED(hr))
            {
                break;
            }
        }

        if (FAILED(hr))
        {
            SetSwapChainError("CreateSwapChainForComposition failed", width, height, hr);
            return hr;
        }

        hr = FinalizeSwapChain(swapChain1);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = panelNative->SetSwapChain(g_swapChain.Get());
        if (FAILED(hr))
        {
            SetHresultError("ISwapChainPanelNative::SetSwapChain failed", hr);
            return hr;
        }

        return S_OK;
    }

    HRESULT CreateSwapChainForCoreWindow(IUnknown* coreWindow, UINT width, UINT height)
    {
        ComPtr<IDXGIFactory4> factory;
        HRESULT hr = QueryDxgiFactoryForSwapChain(factory);
        if (FAILED(hr))
        {
            return hr;
        }

        const DXGI_FORMAT formats[] = {
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM,
        };
        const DXGI_SWAP_EFFECT swapEffects[] = {
            DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
            DXGI_SWAP_EFFECT_FLIP_DISCARD,
        };

        ComPtr<IDXGISwapChain1> swapChain1;
        for (DXGI_SWAP_EFFECT swapEffect : swapEffects)
        {
            for (DXGI_FORMAT format : formats)
            {
                DXGI_SWAP_CHAIN_DESC1 desc = {};
                FillSwapChainDesc(desc, width, height, format, swapEffect);

                swapChain1.Reset();
                hr = factory->CreateSwapChainForCoreWindow(
                    g_queue.Get(),
                    coreWindow,
                    &desc,
                    nullptr,
                    &swapChain1);
                if (SUCCEEDED(hr))
                {
                    g_swapChainFormat = format;
                    break;
                }
            }

            if (SUCCEEDED(hr))
            {
                break;
            }
        }

        if (FAILED(hr))
        {
            SetSwapChainError("CreateSwapChainForCoreWindow failed", width, height, hr);
            return hr;
        }

        return FinalizeSwapChain(swapChain1);
    }

    HRESULT CreateRenderTargets()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = kFrameCount;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        HRESULT hr = g_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_rtvHeap));
        if (FAILED(hr))
        {
            SetError("CreateDescriptorHeap RTV failed");
            return hr;
        }

        g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < kFrameCount; ++i)
        {
            hr = g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i]));
            if (FAILED(hr))
            {
                SetError("SwapChain GetBuffer failed");
                return hr;
            }

            g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += g_rtvDescriptorSize;
        }

        return S_OK;
    }

    HRESULT CreateCommandObjects()
    {
        HRESULT hr = g_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&g_allocator));
        if (FAILED(hr))
        {
            SetError("CreateCommandAllocator failed");
            return hr;
        }

        hr = g_device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            g_allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&g_commandList));
        if (FAILED(hr))
        {
            SetError("CreateCommandList failed");
            return hr;
        }

        g_commandList->Close();

        hr = g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
        if (FAILED(hr))
        {
            SetError("CreateFence failed");
            return hr;
        }

        g_fenceValue = 1;
        g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!g_fenceEvent)
        {
            SetError("CreateEvent fence failed");
            return E_FAIL;
        }

        return S_OK;
    }

    MinecraftXboxGlCommandState* EnsureGlCommandState()
    {
        if (g_glCommandState)
        {
            return g_glCommandState;
        }

        g_glCommandStateMapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            sizeof(MinecraftXboxGlCommandState),
            kMinecraftXboxGlCommandStateName);
        if (!g_glCommandStateMapping)
        {
            return nullptr;
        }

        g_glCommandState = static_cast<MinecraftXboxGlCommandState*>(
            MapViewOfFile(
                g_glCommandStateMapping,
                FILE_MAP_ALL_ACCESS,
                0,
                0,
                sizeof(MinecraftXboxGlCommandState)));
        if (!g_glCommandState)
        {
            CloseHandle(g_glCommandStateMapping);
            g_glCommandStateMapping = nullptr;
            return nullptr;
        }

        MinecraftXboxInitializeGlCommandState(g_glCommandState);
        return g_glCommandState;
    }

    bool TryReadGlClear(float clearColor[4])
    {
        auto* state = EnsureGlCommandState();
        if (!MinecraftXboxIsGlCommandStateReady(state) ||
            state->clearSerial <= 0 ||
            (state->clearMask & kMinecraftXboxGlColorBufferBit) == 0)
        {
            return false;
        }

        clearColor[0] = state->clearColor[0];
        clearColor[1] = state->clearColor[1];
        clearColor[2] = state->clearColor[2];
        clearColor[3] = state->clearColor[3];
        return true;
    }

    UINT AlignTextureRowPitch(UINT bytes)
    {
        return (bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
            ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    }

    UINT ClampUint(UINT value, UINT minValue, UINT maxValue)
    {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    HRESULT EnsureTextureUploadBuffer(UINT64 requiredSize)
    {
        if (g_textureUploadBuffer && g_textureUploadBufferSize >= requiredSize)
        {
            return S_OK;
        }

        g_textureUploadBuffer.Reset();
        g_textureUploadBufferSize = 0;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;
        desc.Width = requiredSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = g_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&g_textureUploadBuffer));
        if (FAILED(hr))
        {
            SetHresultError("Create texture upload buffer failed", hr);
            return hr;
        }

        g_textureUploadBufferSize = requiredSize;
        return S_OK;
    }

    bool TryCopyTextureSampleToBackBuffer()
    {
        auto* state = EnsureGlCommandState();
        if (!MinecraftXboxIsGlCommandStateReady(state) ||
            state->textureSampleReady == 0 ||
            state->textureSampleSerial <= 0 ||
            state->textureSampleWidth <= 0 ||
            state->textureSampleHeight <= 0 ||
            state->textureSampleBytes <= 0)
        {
            return false;
        }

        const LONG64 sampleSerial = state->textureSampleSerial;
        const UINT sampleWidth = static_cast<UINT>(state->textureSampleWidth);
        const UINT sampleHeight = static_cast<UINT>(state->textureSampleHeight);
        const UINT destWidth = ClampUint(g_width, 1, 4096);
        const UINT destHeight = ClampUint(g_height, 1, 4096);
        if (g_lastAcceptedGuiFrameSerial > 0)
        {
            const UINT64 samplePixels = static_cast<UINT64>(sampleWidth) * sampleHeight;
            const UINT64 backBufferPixels = static_cast<UINT64>(destWidth) * destHeight;
            if (samplePixels < (backBufferPixels / 4))
            {
                return false;
            }
        }

        const UINT rowPitch = AlignTextureRowPitch(destWidth * 4);
        const UINT64 requiredSize = static_cast<UINT64>(rowPitch) * destHeight;

        if (FAILED(EnsureTextureUploadBuffer(requiredSize)))
        {
            return false;
        }

        unsigned char* mapped = nullptr;
        D3D12_RANGE readRange = {};
        HRESULT hr = g_textureUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr) || !mapped)
        {
            SetHresultError("Map texture upload buffer failed", hr);
            return false;
        }

        for (UINT y = 0; y < destHeight; ++y)
        {
            const UINT sy = (static_cast<UINT64>(y) * sampleHeight) / destHeight;
            unsigned char* dstRow = mapped + static_cast<size_t>(y) * rowPitch;
            for (UINT x = 0; x < destWidth; ++x)
            {
                const UINT sx = (static_cast<UINT64>(x) * sampleWidth) / destWidth;
                const size_t srcIndex = (static_cast<size_t>(sy) * sampleWidth + sx) * 4;
                const size_t dstIndex = static_cast<size_t>(x) * 4;
                const unsigned char r = state->textureSampleRgba[srcIndex + 0];
                const unsigned char g = state->textureSampleRgba[srcIndex + 1];
                const unsigned char b = state->textureSampleRgba[srcIndex + 2];
                const unsigned char a = 255;

                if (g_swapChainFormat == DXGI_FORMAT_B8G8R8A8_UNORM)
                {
                    dstRow[dstIndex + 0] = b;
                    dstRow[dstIndex + 1] = g;
                    dstRow[dstIndex + 2] = r;
                    dstRow[dstIndex + 3] = a;
                }
                else
                {
                    dstRow[dstIndex + 0] = r;
                    dstRow[dstIndex + 1] = g;
                    dstRow[dstIndex + 2] = b;
                    dstRow[dstIndex + 3] = a;
                }
            }
        }

        g_textureUploadBuffer->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = g_renderTargets[g_frameIndex].Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = g_textureUploadBuffer.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = 0;
        src.PlacedFootprint.Footprint.Format = g_swapChainFormat;
        src.PlacedFootprint.Footprint.Width = destWidth;
        src.PlacedFootprint.Footprint.Height = destHeight;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = rowPitch;

        g_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        g_lastPresentedTextureSampleSerial = sampleSerial;
        return true;
    }

    bool TryCopyGuiFramebufferToBackBuffer()
    {
        auto* state = EnsureGlCommandState();
        if (!MinecraftXboxIsGlCommandStateReady(state) ||
            state->guiFramebufferReady == 0 ||
            state->guiFramebufferSerial <= 0 ||
            state->guiFramebufferWidth <= 0 ||
            state->guiFramebufferHeight <= 0 ||
            state->guiFramebufferBytes <= 0)
        {
            return false;
        }

        const LONG64 frameSerial = state->guiFramebufferSerial;
        const UINT sourceWidth = static_cast<UINT>(state->guiFramebufferWidth);
        const UINT sourceHeight = static_cast<UINT>(state->guiFramebufferHeight);
        const UINT destWidth = ClampUint(g_width, 1, 4096);
        const UINT destHeight = ClampUint(g_height, 1, 4096);
        const UINT rowPitch = AlignTextureRowPitch(destWidth * 4);
        const UINT64 requiredSize = static_cast<UINT64>(rowPitch) * destHeight;

        if (sourceWidth == 0 || sourceHeight == 0 ||
            static_cast<UINT64>(sourceWidth) * sourceHeight * 4u >
                kMinecraftXboxGuiFramebufferMaxBytes ||
            FAILED(EnsureTextureUploadBuffer(requiredSize)))
        {
            return false;
        }

        unsigned char* mapped = nullptr;
        D3D12_RANGE readRange = {};
        HRESULT hr = g_textureUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr) || !mapped)
        {
            SetHresultError("Map GUI framebuffer upload buffer failed", hr);
            return false;
        }

        UINT64 nonBlackPixels = 0;
        UINT64 nonTransparentPixels = 0;
        UINT64 sumRed = 0;
        UINT64 sumGreen = 0;
        UINT64 sumBlue = 0;
        UINT64 centerRgb = 0;
        const UINT centerX = destWidth / 2;
        const UINT centerY = destHeight / 2;

        for (UINT y = 0; y < destHeight; ++y)
        {
            const UINT sy = (static_cast<UINT64>(y) * sourceHeight) / destHeight;
            unsigned char* dstRow = mapped + static_cast<size_t>(y) * rowPitch;
            for (UINT x = 0; x < destWidth; ++x)
            {
                const UINT sx = (static_cast<UINT64>(x) * sourceWidth) / destWidth;
                const size_t srcIndex = (static_cast<size_t>(sy) * sourceWidth + sx) * 4;
                const size_t dstIndex = static_cast<size_t>(x) * 4;
                const unsigned char r = state->guiFramebufferRgba[srcIndex + 0];
                const unsigned char g = state->guiFramebufferRgba[srcIndex + 1];
                const unsigned char b = state->guiFramebufferRgba[srcIndex + 2];
                const unsigned char sourceA = state->guiFramebufferRgba[srcIndex + 3];
                const unsigned char a = 255;

                if (r != 0 || g != 0 || b != 0)
                {
                    ++nonBlackPixels;
                }
                if (sourceA != 0)
                {
                    ++nonTransparentPixels;
                }
                sumRed += r;
                sumGreen += g;
                sumBlue += b;
                if (x == centerX && y == centerY)
                {
                    centerRgb =
                        (static_cast<UINT64>(r) << 16) |
                        (static_cast<UINT64>(g) << 8) |
                        static_cast<UINT64>(b);
                }

                if (g_swapChainFormat == DXGI_FORMAT_B8G8R8A8_UNORM)
                {
                    dstRow[dstIndex + 0] = b;
                    dstRow[dstIndex + 1] = g;
                    dstRow[dstIndex + 2] = r;
                    dstRow[dstIndex + 3] = a;
                }
                else
                {
                    dstRow[dstIndex + 0] = r;
                    dstRow[dstIndex + 1] = g;
                    dstRow[dstIndex + 2] = b;
                    dstRow[dstIndex + 3] = a;
                }
            }
        }

        g_textureUploadBuffer->Unmap(0, nullptr);
        const UINT64 pixelCount = static_cast<UINT64>(destWidth) * destHeight;
        g_lastGuiNonBlackPixels = nonBlackPixels;
        g_lastGuiNonTransparentPixels = nonTransparentPixels;
        g_lastGuiAverageRed = pixelCount > 0 ? sumRed / pixelCount : 0;
        g_lastGuiAverageGreen = pixelCount > 0 ? sumGreen / pixelCount : 0;
        g_lastGuiAverageBlue = pixelCount > 0 ? sumBlue / pixelCount : 0;
        g_lastGuiCenterRgb = centerRgb;

        const UINT64 centerRed = (centerRgb >> 16) & 0xFF;
        const UINT64 centerGreen = (centerRgb >> 8) & 0xFF;
        const UINT64 centerBlue = centerRgb & 0xFF;
        const bool mostlyOpaque =
            pixelCount > 0 &&
            nonTransparentPixels >= ((pixelCount * 95u) / 100u);
        const bool mostlyFilled =
            pixelCount > 0 &&
            nonBlackPixels >= ((pixelCount * 95u) / 100u);
        const bool tooSparse =
            pixelCount > 0 &&
            (nonTransparentPixels <= (pixelCount / 100u) ||
                (nonBlackPixels <= (pixelCount / 100u) &&
                    g_lastGuiAverageRed <= 8 &&
                    g_lastGuiAverageGreen <= 8 &&
                    g_lastGuiAverageBlue <= 8));
        const bool nearlyWhite =
            mostlyOpaque &&
            mostlyFilled &&
            g_lastGuiAverageRed >= 245 &&
            g_lastGuiAverageGreen >= 245 &&
            g_lastGuiAverageBlue >= 245 &&
            centerRed >= 240 &&
            centerGreen >= 240 &&
            centerBlue >= 240;
        const bool nearlyBlack =
            mostlyOpaque &&
            nonBlackPixels <= (pixelCount / 100u) &&
            g_lastGuiAverageRed <= 8 &&
            g_lastGuiAverageGreen <= 8 &&
            g_lastGuiAverageBlue <= 8;
        if (tooSparse || nearlyWhite || nearlyBlack)
        {
            return false;
        }

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = g_renderTargets[g_frameIndex].Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = g_textureUploadBuffer.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = 0;
        src.PlacedFootprint.Footprint.Format = g_swapChainFormat;
        src.PlacedFootprint.Footprint.Width = destWidth;
        src.PlacedFootprint.Footprint.Height = destHeight;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = rowPitch;

        g_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        g_lastPresentedGuiFrameSerial = frameSerial;
        g_lastAcceptedGuiFrameSerial = frameSerial;
        return true;
    }

    bool HasTextureSampleReady()
    {
        auto* state = EnsureGlCommandState();
        return MinecraftXboxIsGlCommandStateReady(state) &&
            state->textureSampleReady != 0 &&
            state->textureSampleSerial > 0 &&
            state->textureSampleWidth > 0 &&
            state->textureSampleHeight > 0 &&
            state->textureSampleBytes > 0;
    }

    bool HasGuiFramebufferReady()
    {
        auto* state = EnsureGlCommandState();
        return MinecraftXboxIsGlCommandStateReady(state) &&
            state->guiFramebufferReady != 0 &&
            state->guiFramebufferSerial > 0 &&
            state->guiFramebufferWidth > 0 &&
            state->guiFramebufferHeight > 0 &&
            state->guiFramebufferBytes > 0;
    }
}

MGB_EXPORT int mgb_create_d3d12_device_probe()
{
    return ProbeWithDxgiFactory();
}

MGB_EXPORT int mgb_probe_mesa_egl_corewindow(const wchar_t* mesaDirectory, void* coreWindowUnknown, unsigned int width, unsigned int height)
{
    if (!mesaDirectory || !mesaDirectory[0])
    {
        SetError("Mesa EGL probe missing runtime directory");
        return 0;
    }

    const bool softwarePbufferProbe = false;
    const bool useCoreWindowDisplay = coreWindowUnknown != nullptr;
    SetEnvironmentVariableA("GALLIUM_DRIVER", softwarePbufferProbe ? "softpipe" : "d3d12");
    SetEnvironmentVariableA("MESA_LOADER_DRIVER_OVERRIDE", softwarePbufferProbe ? "softpipe" : "d3d12");
    SetEnvironmentVariableA("LIBGL_ALWAYS_SOFTWARE", softwarePbufferProbe ? "true" : nullptr);
    SetEnvironmentVariableA("EGL_PLATFORM", useCoreWindowDisplay ? "windows" : "surfaceless");
    SetMesaFmallocEnvironment(mesaDirectory);

    if (!ProbeMesaWglStartup(mesaDirectory, coreWindowUnknown))
    {
        return 0;
    }

    MesaEglProcs egl = {};
    if (!LoadMesaEglProcs(mesaDirectory, &egl))
    {
        return 0;
    }

    EGLDisplay display = EGL_NO_DISPLAY_VALUE;
    const char* displayPath = useCoreWindowDisplay ? "CoreWindow" : "default";
    if (useCoreWindowDisplay)
    {
        if (!SafeGetDisplay("eglGetDisplay(CoreWindow)", egl.getDisplay, coreWindowUnknown, &display))
        {
            return 0;
        }
    }
    else if (egl.getPlatformDisplayExt)
    {
        displayPath = "surfaceless";
        if (!SafeGetPlatformDisplayExt(egl.getPlatformDisplayExt, EGL_PLATFORM_SURFACELESS_MESA_VALUE, nullptr, &display))
        {
            return 0;
        }
    }
    else if (!SafeGetDisplay("eglGetDisplay(default)", egl.getDisplay, nullptr, &display))
    {
        return 0;
    }

    if (display == EGL_NO_DISPLAY_VALUE && !useCoreWindowDisplay && egl.getPlatformDisplayExt)
    {
        displayPath = "surfaceless";
        if (!SafeGetPlatformDisplayExt(egl.getPlatformDisplayExt, EGL_PLATFORM_SURFACELESS_MESA_VALUE, nullptr, &display))
        {
            return 0;
        }
    }

    if (display == EGL_NO_DISPLAY_VALUE)
    {
        SetMesaEglCallError("eglGetDisplay", egl);
        return 0;
    }

    EGLint eglMajor = 0;
    EGLint eglMinor = 0;
    EGLBoolean initialized = EGL_FALSE_VALUE;
    char initializeContext[96] = {};
    std::snprintf(initializeContext, sizeof(initializeContext), "eglInitialize(%s)", displayPath);
    if (!SafeInitialize(initializeContext, egl.initialize, display, &eglMajor, &eglMinor, &initialized))
    {
        return 0;
    }
    if (!initialized)
    {
        SetMesaEglCallError("eglInitialize", egl);
        return 0;
    }

    const EGLint surfaceType = (coreWindowUnknown && !softwarePbufferProbe)
        ? EGL_WINDOW_BIT_VALUE
        : EGL_PBUFFER_BIT_VALUE;
    const EGLint configAttribs[] =
    {
        EGL_SURFACE_TYPE_VALUE, surfaceType,
        EGL_RENDERABLE_TYPE_VALUE, EGL_OPENGL_BIT_VALUE,
        EGL_RED_SIZE_VALUE, 8,
        EGL_GREEN_SIZE_VALUE, 8,
        EGL_BLUE_SIZE_VALUE, 8,
        EGL_ALPHA_SIZE_VALUE, 8,
        EGL_DEPTH_SIZE_VALUE, 24,
        EGL_STENCIL_SIZE_VALUE, 8,
        EGL_NONE_VALUE,
    };

    EGLConfig config = nullptr;
    EGLint configCount = 0;
    EGLBoolean choseConfig = EGL_FALSE_VALUE;
    if (!SafeChooseConfig(egl.chooseConfig, display, configAttribs, &config, &configCount, &choseConfig))
    {
        SafeCleanupMesaEgl(egl, display, EGL_NO_SURFACE_VALUE, EGL_NO_CONTEXT_VALUE);
        return 0;
    }
    if (!choseConfig || configCount <= 0 || !config)
    {
        SetMesaEglCallError("eglChooseConfig", egl);
        SafeCleanupMesaEgl(egl, display, EGL_NO_SURFACE_VALUE, EGL_NO_CONTEXT_VALUE);
        return 0;
    }

    EGLBoolean boundApi = EGL_FALSE_VALUE;
    if (!SafeBindApi(egl.bindApi, EGL_OPENGL_API_VALUE, &boundApi))
    {
        SafeCleanupMesaEgl(egl, display, EGL_NO_SURFACE_VALUE, EGL_NO_CONTEXT_VALUE);
        return 0;
    }
    if (!boundApi)
    {
        SetMesaEglCallError("eglBindAPI(OpenGL)", egl);
        SafeCleanupMesaEgl(egl, display, EGL_NO_SURFACE_VALUE, EGL_NO_CONTEXT_VALUE);
        return 0;
    }

    EGLSurface surface = EGL_NO_SURFACE_VALUE;
    if (coreWindowUnknown && !softwarePbufferProbe)
    {
        if (!SafeCreateWindowSurface(egl.createWindowSurface, display, config, coreWindowUnknown, nullptr, &surface))
        {
            SafeCleanupMesaEgl(egl, display, EGL_NO_SURFACE_VALUE, EGL_NO_CONTEXT_VALUE);
            return 0;
        }
    }
    else
    {
        const EGLint surfaceAttribs[] =
        {
            EGL_WIDTH_VALUE, static_cast<EGLint>(width > 0 ? width : 64),
            EGL_HEIGHT_VALUE, static_cast<EGLint>(height > 0 ? height : 64),
            EGL_NONE_VALUE,
        };
        if (!SafeCreatePbufferSurface(egl.createPbufferSurface, display, config, surfaceAttribs, &surface))
        {
            SafeCleanupMesaEgl(egl, display, EGL_NO_SURFACE_VALUE, EGL_NO_CONTEXT_VALUE);
            return 0;
        }
    }
    if (surface == EGL_NO_SURFACE_VALUE)
    {
        SetMesaEglCallError(
            (coreWindowUnknown && !softwarePbufferProbe)
                ? "eglCreateWindowSurface(CoreWindow)"
                : "eglCreatePbufferSurface",
            egl);
        SafeCleanupMesaEgl(egl, display, EGL_NO_SURFACE_VALUE, EGL_NO_CONTEXT_VALUE);
        return 0;
    }

    const EGLint contextAttribsCore[] =
    {
        EGL_CONTEXT_MAJOR_VERSION_VALUE, 3,
        EGL_CONTEXT_MINOR_VERSION_VALUE, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_VALUE, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_VALUE,
        EGL_NONE_VALUE,
    };
    const EGLint contextAttribsVersion[] =
    {
        EGL_CONTEXT_MAJOR_VERSION_VALUE, 3,
        EGL_CONTEXT_MINOR_VERSION_VALUE, 3,
        EGL_NONE_VALUE,
    };

    EGLContext context = EGL_NO_CONTEXT_VALUE;
    if (!SafeCreateContext(egl.createContext, display, config, contextAttribsCore, &context))
    {
        SafeCleanupMesaEgl(egl, display, surface, EGL_NO_CONTEXT_VALUE);
        return 0;
    }
    if (context == EGL_NO_CONTEXT_VALUE)
    {
        if (!SafeCreateContext(egl.createContext, display, config, contextAttribsVersion, &context))
        {
            SafeCleanupMesaEgl(egl, display, surface, EGL_NO_CONTEXT_VALUE);
            return 0;
        }
    }
    if (context == EGL_NO_CONTEXT_VALUE)
    {
        if (!SafeCreateContext(egl.createContext, display, config, nullptr, &context))
        {
            SafeCleanupMesaEgl(egl, display, surface, EGL_NO_CONTEXT_VALUE);
            return 0;
        }
    }
    if (context == EGL_NO_CONTEXT_VALUE)
    {
        SetMesaEglCallError("eglCreateContext", egl);
        SafeCleanupMesaEgl(egl, display, surface, EGL_NO_CONTEXT_VALUE);
        return 0;
    }

    EGLBoolean madeCurrent = EGL_FALSE_VALUE;
    if (!SafeMakeCurrent(egl.makeCurrent, display, surface, context, &madeCurrent))
    {
        SafeCleanupMesaEgl(egl, display, surface, context);
        return 0;
    }
    if (!madeCurrent)
    {
        SetMesaEglCallError("eglMakeCurrent", egl);
        SafeCleanupMesaEgl(egl, display, surface, context);
        return 0;
    }

    GlGetStringProc glGetString =
        reinterpret_cast<GlGetStringProc>(egl.getProcAddress("glGetString"));
    if (!glGetString && egl.opengl)
    {
        glGetString = reinterpret_cast<GlGetStringProc>(GetProcAddress(egl.opengl, "glGetString"));
    }

    const unsigned char* glRenderer = nullptr;
    const unsigned char* glVersion = nullptr;
    if (glGetString)
    {
        SafeGlGetString(glGetString, GL_RENDERER_VALUE, &glRenderer);
        SafeGlGetString(glGetString, GL_VERSION_VALUE, &glVersion);
    }

    const char* probeName = useCoreWindowDisplay
        ? "Mesa EGL/CoreWindow"
        : "Mesa EGL/surfaceless pbuffer";

    char renderer[256] = {};
    std::snprintf(
        renderer,
        sizeof(renderer),
        "%s display=%s%s%s",
        probeName,
        displayPath,
        glRenderer ? " renderer=" : "",
        glRenderer ? reinterpret_cast<const char*>(glRenderer) : "");
    g_renderer = renderer;

    char version[256] = {};
    std::snprintf(
        version,
        sizeof(version),
        "EGL %d.%d%s%s",
        eglMajor,
        eglMinor,
        glVersion ? " OpenGL " : "",
        glVersion ? reinterpret_cast<const char*>(glVersion) : "");
    g_version = version;
    char success[160] = {};
    std::snprintf(
        success,
        sizeof(success),
        "%s probe succeeded using %s display",
        probeName,
        displayPath);
    g_last_error = success;

    SafeCleanupMesaEgl(egl, display, surface, context);
    return 1;
}

MGB_EXPORT const char* mgb_get_renderer_string()
{
    return g_renderer.c_str();
}

MGB_EXPORT const char* mgb_get_version_string()
{
    return g_version.c_str();
}

MGB_EXPORT const char* mgb_get_last_error_string()
{
    return g_last_error.c_str();
}

MGB_EXPORT int mgb_presentation_ensure_swap_event()
{
    if (!g_presentEvent)
    {
        g_presentEvent = CreateEventW(nullptr, TRUE, FALSE, kPresentEventName);
    }

    if (!g_presentEvent)
    {
        SetError("CreateEventW for Java present signal failed");
        return 0;
    }

    g_last_error = "none";
    return 1;
}

MGB_EXPORT int mgb_presentation_consume_swap_signal()
{
    if (!g_presentEvent)
    {
        return 0;
    }

    const DWORD waitResult = WaitForSingleObject(g_presentEvent, 0);
    if (waitResult == WAIT_OBJECT_0)
    {
        ResetEvent(g_presentEvent);
        ++g_javaSwapSignals;
        return 1;
    }

    return 0;
}

MGB_EXPORT int mgb_presentation_init(void* surfaceUnknown, unsigned int width, unsigned int height)
{
    if (!surfaceUnknown || width == 0 || height == 0)
    {
        SetError("presentation_init invalid arguments");
        return 0;
    }

    mgb_presentation_shutdown();

    if (!mgb_presentation_ensure_swap_event())
    {
        return 0;
    }

    auto* surface = static_cast<IUnknown*>(surfaceUnknown);
    HRESULT hr = CreateDeviceAndQueue();
    if (FAILED(hr))
    {
        return 0;
    }

    hr = CreateSwapChainForXamlPanel(surface, width, height);
    if (FAILED(hr))
    {
        const std::string xamlError = g_last_error;
        hr = CreateSwapChainForCoreWindow(surface, width, height);
        if (FAILED(hr))
        {
            g_last_error = xamlError + "; CoreWindow fallback: " + g_last_error;
            ReleasePresentationResources();
            return 0;
        }
    }

    hr = CreateRenderTargets();
    if (FAILED(hr))
    {
        ReleasePresentationResources();
        return 0;
    }

    hr = CreateCommandObjects();
    if (FAILED(hr))
    {
        ReleasePresentationResources();
        return 0;
    }

    g_width = width;
    g_height = height;
    EnsureGlCommandState();
    g_presentationReady = true;
    g_renderer = "D3D12 XAML presentation ready with GL command bridge";
    g_last_error = "none";
    return 1;
}

MGB_EXPORT int mgb_presentation_present(unsigned int rgba)
{
    if (!g_presentationReady || !g_device || !g_swapChain || !g_commandList)
    {
        SetError("presentation_present called before init");
        return 0;
    }

    HRESULT hr = g_allocator->Reset();
    if (FAILED(hr))
    {
        SetError("CommandAllocator Reset failed");
        return 0;
    }

    hr = g_commandList->Reset(g_allocator.Get(), nullptr);
    if (FAILED(hr))
    {
        SetError("CommandList Reset failed");
        return 0;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = g_renderTargets[g_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_commandList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(g_frameIndex) * g_rtvDescriptorSize;
    g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    float clearColor[4] = {};
    if (!TryReadGlClear(clearColor))
    {
        clearColor[0] = ((rgba >> 16) & 0xFF) / 255.0f;
        clearColor[1] = ((rgba >> 8) & 0xFF) / 255.0f;
        clearColor[2] = (rgba & 0xFF) / 255.0f;
        clearColor[3] = ((rgba >> 24) & 0xFF) / 255.0f;
    }
    g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    const bool guiReady = HasGuiFramebufferReady();
    const bool textureReady = HasTextureSampleReady();
    const bool wantsCopy = guiReady || textureReady;
    bool copiedPixels = false;

    if (wantsCopy)
    {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        g_commandList->ResourceBarrier(1, &barrier);

        if (guiReady)
        {
            copiedPixels = TryCopyGuiFramebufferToBackBuffer();
        }
        if (!copiedPixels && textureReady)
        {
            copiedPixels = TryCopyTextureSampleToBackBuffer();
        }
    }

    barrier.Transition.StateBefore = wantsCopy
        ? D3D12_RESOURCE_STATE_COPY_DEST
        : D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_commandList->ResourceBarrier(1, &barrier);

    hr = g_commandList->Close();
    if (FAILED(hr))
    {
        SetError("CommandList Close failed");
        return 0;
    }

    ID3D12CommandList* lists[] = { g_commandList.Get() };
    g_queue->ExecuteCommandLists(1, lists);

    hr = g_swapChain->Present(1, 0);
    if (FAILED(hr))
    {
        SetError("SwapChain Present failed");
        return 0;
    }

    WaitForGpu();
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
    ++g_presentCount;
    g_last_error = "none";
    return 1;
}

MGB_EXPORT unsigned long long mgb_presentation_get_present_count()
{
    return g_presentCount;
}

MGB_EXPORT unsigned long long mgb_presentation_get_java_swap_count()
{
    return g_javaSwapSignals;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_clear_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->clearSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_viewport_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->viewportSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_draw_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->drawSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_upload_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureUploadSerial + state->textureSubUploadSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_upload_bytes()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureUploadBytes)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_sample_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureSampleSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_sample_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureSampleWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_sample_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureSampleHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_present_count()
{
    return static_cast<unsigned long long>(g_lastPresentedTextureSampleSerial);
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_frame_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiFramebufferSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_frame_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiFramebufferWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_frame_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiFramebufferHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_frame_present_count()
{
    return static_cast<unsigned long long>(g_lastPresentedGuiFrameSerial);
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_non_black_pixels()
{
    return static_cast<unsigned long long>(g_lastGuiNonBlackPixels);
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_non_transparent_pixels()
{
    return static_cast<unsigned long long>(g_lastGuiNonTransparentPixels);
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_average_red()
{
    return static_cast<unsigned long long>(g_lastGuiAverageRed);
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_average_green()
{
    return static_cast<unsigned long long>(g_lastGuiAverageGreen);
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_average_blue()
{
    return static_cast<unsigned long long>(g_lastGuiAverageBlue);
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_center_rgb()
{
    return static_cast<unsigned long long>(g_lastGuiCenterRgb);
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_textured_triangle_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiTexturedTriangleSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_color_triangle_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiColorTriangleSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_skipped_vertex_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiSkippedVertexSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_last_texture_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiLastTextureName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_last_texture_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiLastTextureWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_last_texture_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiLastTextureHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_accepted_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateAcceptedSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_rejected_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateRejectedSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_source_texture_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateLastSourceTextureName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_source_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateLastSourceWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_source_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateLastSourceHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateLastWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateLastHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_pixels()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateLastPixels)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_non_black_pixels()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateLastNonBlackPixels)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_non_transparent_pixels()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateLastNonTransparentPixels)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_average_red()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateLastAverageRed)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_average_green()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateLastAverageGreen)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_average_blue()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateLastAverageBlue)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_reject_reason()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateLastRejectReason)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_accepted_source_texture_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateAcceptedSourceTextureName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_accepted_source_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateAcceptedSourceWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_gui_candidate_accepted_source_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->guiCandidateAcceptedSourceHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_stored_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastStoredName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_stored_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastStoredWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_stored_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastStoredHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_largest_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLargestName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_largest_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLargestWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_largest_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLargestHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_best_gui_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureBestGuiName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_best_gui_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureBestGuiWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_best_gui_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureBestGuiHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_record_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureRecordCount)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_table_full_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureTableFullSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_exact_gui_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureExactGuiName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_exact_gui_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureExactGuiWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_exact_gui_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureExactGuiHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_exact_gui_has_pixels()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureExactGuiHasPixels)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_best_allocated_gui_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureBestAllocatedGuiName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_best_allocated_gui_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureBestAllocatedGuiWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_best_allocated_gui_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureBestAllocatedGuiHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_allocation_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAllocationName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_allocation_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAllocationWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_allocation_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAllocationHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_shrink_preserved_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureShrinkPreservedSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_attempt_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAttemptName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_attempt_level()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAttemptLevel)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_attempt_x()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAttemptX)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_attempt_y()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAttemptY)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_attempt_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAttemptWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_attempt_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAttemptHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_attempt_format()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAttemptFormat)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_attempt_type()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAttemptType)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_attempt_reason()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAttemptReason)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_attempt_pbo()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAttemptPbo)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_last_attempt_unit()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureLastAttemptUnit)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_exact_gui_upload_attempt_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureExactGuiUploadAttemptSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_exact_gui_upload_accepted_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureExactGuiUploadAcceptedSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_exact_gui_upload_rejected_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureExactGuiUploadRejectedSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_texture_exact_gui_last_reject_reason()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->textureExactGuiLastRejectReason)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_framebuffer_bind_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->framebufferBindSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_framebuffer_attach_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->framebufferAttachSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_framebuffer_blit_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->framebufferBlitSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_framebuffer_draw_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->framebufferDrawName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_framebuffer_read_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->framebufferReadName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_framebuffer_color_texture_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->framebufferColorTextureName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_framebuffer_color_texture_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->framebufferColorTextureWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_framebuffer_color_texture_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->framebufferColorTextureHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_framebuffer_last_blit_source_texture_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->framebufferLastBlitSourceTextureName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_framebuffer_last_blit_dest_texture_name()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->framebufferLastBlitDestTextureName)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_framebuffer_last_blit_width()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->framebufferLastBlitWidth)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_framebuffer_last_blit_height()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->framebufferLastBlitHeight)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_buffer_upload_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->bufferUploadSerial + state->bufferSubUploadSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_buffer_upload_bytes()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->bufferUploadBytes)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_vertex_attrib_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->vertexAttribSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_program_use_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->programUseSerial)
        : 0;
}

MGB_EXPORT unsigned long long mgb_presentation_get_gl_uniform_count()
{
    auto* state = EnsureGlCommandState();
    return MinecraftXboxIsGlCommandStateReady(state)
        ? static_cast<unsigned long long>(state->uniformSerial)
        : 0;
}

MGB_EXPORT void mgb_presentation_shutdown()
{
    ReleasePresentationResources();
    g_renderer = "D3D12 presentation shut down";
}
