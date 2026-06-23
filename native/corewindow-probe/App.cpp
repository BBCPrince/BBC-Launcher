#include "pch.h"

using namespace winrt;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Data::Json;
using namespace Windows::Devices::Input;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Graphics::Display;
using namespace Windows::Security::Authentication::Web;
using namespace Windows::Security::Cryptography;
using namespace Windows::Security::Cryptography::DataProtection;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace Windows::UI::Core;
using namespace Windows::UI::Input;
using namespace Windows::UI::Popups;
using namespace Windows::UI::ViewManagement;
using namespace Windows::Web::Http;

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
static constexpr EGLint EGL_CONTEXT_CLIENT_VERSION_VALUE = 0x3098;
static constexpr EGLint EGL_VENDOR_VALUE = 0x3053;
static constexpr EGLint EGL_VERSION_VALUE = 0x3054;
static constexpr EGLint EGL_EXTENSIONS_VALUE = 0x3055;
static constexpr EGLint EGL_CLIENT_APIS_VALUE = 0x308D;
static constexpr EGLenum EGL_OPENGL_API_VALUE = 0x30A2;
static constexpr EGLenum EGL_OPENGL_ES_API_VALUE = 0x30A0;
static constexpr unsigned int GL_COLOR_BUFFER_BIT_VALUE = 0x00004000;
static constexpr unsigned int GL_SCISSOR_TEST_VALUE = 0x0C11;

using EglGetDisplayProc = EGLDisplay(__stdcall*)(void*);
using EglInitializeProc = EGLBoolean(__stdcall*)(EGLDisplay, EGLint*, EGLint*);
using EglGetConfigsProc = EGLBoolean(__stdcall*)(EGLDisplay, EGLConfig*, EGLint, EGLint*);
using EglChooseConfigProc = EGLBoolean(__stdcall*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
using EglBindApiProc = EGLBoolean(__stdcall*)(EGLenum);
using EglCreateWindowSurfaceProc = EGLSurface(__stdcall*)(EGLDisplay, EGLConfig, void*, const EGLint*);
using EglCreatePlatformWindowSurfaceProc = EGLSurface(__stdcall*)(EGLDisplay, EGLConfig, void*, const intptr_t*);
using EglCreatePlatformWindowSurfaceExtProc = EGLSurface(__stdcall*)(EGLDisplay, EGLConfig, void*, const EGLint*);
using EglCreateContextProc = EGLContext(__stdcall*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
using EglMakeCurrentProc = EGLBoolean(__stdcall*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
using EglSwapBuffersProc = EGLBoolean(__stdcall*)(EGLDisplay, EGLSurface);
using EglDestroySurfaceProc = EGLBoolean(__stdcall*)(EGLDisplay, EGLSurface);
using EglDestroyContextProc = EGLBoolean(__stdcall*)(EGLDisplay, EGLContext);
using EglTerminateProc = EGLBoolean(__stdcall*)(EGLDisplay);
using EglGetErrorProc = EGLint(__stdcall*)(void);
using EglGetProcAddressProc = void*(__stdcall*)(const char*);
using EglQueryStringProc = const char*(__stdcall*)(EGLDisplay, EGLint);
using GlClearColorProc = void(__stdcall*)(float, float, float, float);
using GlClearProc = void(__stdcall*)(unsigned int);
using GlViewportProc = void(__stdcall*)(int, int, int, int);
using GlEnableProc = void(__stdcall*)(unsigned int);
using GlDisableProc = void(__stdcall*)(unsigned int);
using GlScissorProc = void(__stdcall*)(int, int, int, int);

struct EglState
{
    HMODULE openGl = nullptr;
    HMODULE egl = nullptr;
    HMODULE gles = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY_VALUE;
    EGLConfig config = nullptr;
    EGLSurface surface = EGL_NO_SURFACE_VALUE;
    EGLContext context = EGL_NO_CONTEXT_VALUE;
    EglSwapBuffersProc swapBuffers = nullptr;
    GlClearColorProc clearColor = nullptr;
    GlClearProc clear = nullptr;
    GlViewportProc viewport = nullptr;
    GlEnableProc enable = nullptr;
    GlDisableProc disable = nullptr;
    GlScissorProc scissor = nullptr;
    int width = 0;
    int height = 0;
    int major = 0;
    int minor = 0;
    bool ready = false;
};

static bool WriteTextFileUtf8(std::wstring const& path, std::string const& text);
static IUICommand ShowDialogAndPump(CoreWindow const& window, MessageDialog& dialog);
static std::wstring FileNameOnly(std::wstring const& path);
static std::wstring ToLongWin32Path(std::wstring const& path);
static std::wstring TrimWhitespace(std::wstring value);

static std::wstring g_logPath;
static EglState g_eglState;
static std::atomic<unsigned long long> g_downloadUiCompleted{ 0 };
static std::atomic<unsigned long long> g_downloadUiTotal{ 0 };
static std::atomic<unsigned long long> g_downloadUiPhase{ 0 };
static std::atomic<bool> g_downloadWorkPerformed{ false };
static std::atomic<bool> g_nativeLanAdvertiserStarted{ false };
static std::atomic<bool> g_nativeLanDiscoveryBridgeStarted{ false };
static int g_launchWidth = 1920;
static int g_launchHeight = 1080;
static int g_surfaceWidth = 1920;
static int g_surfaceHeight = 1080;
static std::wstring g_launchResolutionLabel = L"1080p";

static void WriteLogW(std::wstring const& text);
static void WriteLogF(const wchar_t* format, ...);
static bool ProbeMesaEgl(CoreWindow const& window, bool preloadOpenGl, bool useDesktopOpenGl);
static void CleanupProbeEgl();
static void RenderDownloadStatusFrame(uint32_t frame);
static bool RunMousePointerTest(CoreWindow const& window, bool& closed);

static void TryDisableXboxLayoutScaling()
{
    try
    {
        bool disabled = ApplicationViewScaling::TrySetDisableLayoutScaling(true);
        WriteLogF(L"ApplicationViewScaling.TrySetDisableLayoutScaling(true) result=%d", disabled ? 1 : 0);
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"ApplicationViewScaling.TrySetDisableLayoutScaling failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
    }
    catch (...)
    {
        WriteLogW(L"ApplicationViewScaling.TrySetDisableLayoutScaling failed with unknown exception");
    }
}

static std::string WideToUtf8(std::wstring const& text)
{
    if (text.empty())
    {
        return std::string();
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<size_t>(size), '\0');
    if (size > 0)
    {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), bytes.data(), size, nullptr, nullptr);
    }
    return bytes;
}

static std::wstring Utf8ToWide(const char* text)
{
    if (!text || !text[0])
    {
        return std::wstring();
    }

    int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(size > 0 ? size - 1 : 0), L'\0');
    if (size > 1)
    {
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), size);
    }
    return wide;
}

static void EnsureDirectory(std::wstring const& path)
{
    if (path.empty())
    {
        return;
    }

    std::wstring current;
    size_t index = 0;
    if (path.size() >= 2 && path[1] == L':')
    {
        current = path.substr(0, 2);
        index = 2;
    }

    while (index < path.size())
    {
        size_t next = path.find_first_of(L"\\/", index);
        std::wstring part = path.substr(index, next == std::wstring::npos ? std::wstring::npos : next - index);
        if (!part.empty())
        {
            if (!current.empty() && current.back() != L'\\')
            {
                current.push_back(L'\\');
            }
            current += part;
            std::wstring nativeCurrent = ToLongWin32Path(current);
            CreateDirectoryW(nativeCurrent.c_str(), nullptr);
        }
        if (next == std::wstring::npos)
        {
            break;
        }
        index = next + 1;
    }
}

static std::wstring ParentDirectory(std::wstring const& path)
{
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

static void InitializeLog()
{
    if (!g_logPath.empty())
    {
        return;
    }

    try
    {
        std::wstring local = ApplicationData::Current().LocalFolder().Path().c_str();
        std::wstring logDir = local + L"\\logs";
        EnsureDirectory(logDir);
        g_logPath = logDir + L"\\native-corewindow-probe.log";
        DeleteFileW(g_logPath.c_str());
    }
    catch (...)
    {
        g_logPath.clear();
    }
}

static void WriteLogW(std::wstring const& text)
{
    OutputDebugStringW(L"[NativeCoreWindowProbe] ");
    OutputDebugStringW(text.c_str());
    OutputDebugStringW(L"\n");

    if (g_logPath.empty())
    {
        return;
    }

    std::string line = WideToUtf8(L"[NativeCoreWindowProbe] " + text + L"\r\n");
    HANDLE file = CreateFile2(
        g_logPath.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        OPEN_ALWAYS,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
}

static void WriteLogA(const char* text)
{
    OutputDebugStringA("[NativeCoreWindowProbe] ");
    OutputDebugStringA(text ? text : "<null>");
    OutputDebugStringA("\n");
    WriteLogW(Utf8ToWide(text ? text : "<null>"));
}

static void WriteLogF(const wchar_t* format, ...)
{
    wchar_t buffer[1024] = {};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _TRUNCATE, format, args);
    va_end(args);
    WriteLogW(buffer);
}

static std::wstring NormalizeWin32Path(std::wstring const& path)
{
    std::wstring normalized = path;
    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed > 0 && needed < 32768)
    {
        std::wstring buffer(needed, L'\0');
        DWORD written = GetFullPathNameW(path.c_str(), needed, buffer.data(), nullptr);
        if (written > 0 && written < needed)
        {
            buffer.resize(written);
            normalized = buffer;
        }
    }
    return normalized;
}

static std::wstring ToLongWin32Path(std::wstring const& path)
{
    if (path.size() >= 4 && path[0] == L'\\' && path[1] == L'\\' && path[2] == L'?' && path[3] == L'\\')
    {
        return path;
    }

    std::wstring normalized = NormalizeWin32Path(path);
    if (normalized.size() >= 3 && normalized[1] == L':' && (normalized[2] == L'\\' || normalized[2] == L'/'))
    {
        return L"\\\\?\\" + normalized;
    }
    if (normalized.size() >= 2 && normalized[0] == L'\\' && normalized[1] == L'\\')
    {
        return L"\\\\?\\UNC\\" + normalized.substr(2);
    }

    return normalized;
}

static bool FileExists(std::wstring const& path)
{
    std::wstring nativePath = ToLongWin32Path(path);
    DWORD attrs = GetFileAttributesW(nativePath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool DirectoryExists(std::wstring const& path)
{
    std::wstring nativePath = ToLongWin32Path(path);
    DWORD attrs = GetFileAttributesW(nativePath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool ReadSmallTextFileUtf8(std::wstring const& path, std::string& text)
{
    text.clear();
    std::wstring nativePath = ToLongWin32Path(path);
    HANDLE file = CreateFile2(
        nativePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        OPEN_EXISTING,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 64 * 1024)
    {
        CloseHandle(file);
        return false;
    }

    text.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = text.empty() ||
        ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != text.size())
    {
        text.clear();
        return false;
    }

    return true;
}

static bool ReadTextFileUtf8(std::wstring const& path, std::string& text, unsigned long long maxBytes)
{
    text.clear();
    std::wstring nativePath = ToLongWin32Path(path);
    HANDLE file = CreateFile2(
        nativePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        OPEN_EXISTING,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) > maxBytes)
    {
        CloseHandle(file);
        return false;
    }

    text.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = text.empty() ||
        ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != text.size())
    {
        text.clear();
        return false;
    }

    return true;
}

static std::wstring GetEnvironmentVariableString(const wchar_t* name)
{
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0)
    {
        return std::wstring();
    }

    std::wstring value(static_cast<size_t>(needed), L'\0');
    DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    if (written == 0)
    {
        return std::wstring();
    }

    value.resize(static_cast<size_t>(written));
    return value;
}

static void PrependEnvironmentPath(std::vector<std::wstring> const& entries)
{
    std::wstring path;
    for (auto const& entry : entries)
    {
        if (!entry.empty())
        {
            if (!path.empty())
            {
                path.push_back(L';');
            }
            path += entry;
        }
    }

    std::wstring existing = GetEnvironmentVariableString(L"PATH");
    if (!existing.empty())
    {
        if (!path.empty())
        {
            path.push_back(L';');
        }
        path += existing;
    }

    SetEnvironmentVariableW(L"PATH", path.c_str());
    WriteLogF(L"PATH prepended with %zu entries", entries.size());
}

static std::wstring ResolvePackagedOrLocalFile(
    std::wstring const& localRoot,
    std::wstring const& packageRoot,
    const wchar_t* relativePath)
{
    std::wstring localPath = localRoot + L"\\" + relativePath;
    if (FileExists(localPath))
    {
        return localPath;
    }

    std::wstring packagePath = packageRoot + L"\\" + relativePath;
    if (FileExists(packagePath))
    {
        return packagePath;
    }

    return std::wstring();
}

static std::wstring ResolvePackageOrLocalFile(
    std::wstring const& localRoot,
    std::wstring const& packageRoot,
    const wchar_t* relativePath)
{
    std::wstring packagePath = packageRoot + L"\\" + relativePath;
    if (FileExists(packagePath))
    {
        return packagePath;
    }

    std::wstring localPath = localRoot + L"\\" + relativePath;
    if (FileExists(localPath))
    {
        return localPath;
    }

    return std::wstring();
}

static std::wstring ToLowerInvariant(std::wstring value)
{
    for (auto& ch : value)
    {
        if (ch >= L'A' && ch <= L'Z')
        {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
    }
    return value;
}

static bool EndsWithInsensitive(std::wstring const& value, const wchar_t* suffix)
{
    std::wstring suffixText(suffix ? suffix : L"");
    if (suffixText.size() > value.size())
    {
        return false;
    }

    std::wstring tail = value.substr(value.size() - suffixText.size());
    return ToLowerInvariant(tail) == ToLowerInvariant(suffixText);
}

static bool ContainsInsensitive(std::wstring const& value, const wchar_t* needle)
{
    std::wstring needleText(needle ? needle : L"");
    if (needleText.empty())
    {
        return true;
    }

    return ToLowerInvariant(value).find(ToLowerInvariant(needleText)) != std::wstring::npos;
}

static bool IsTruthyEnvironment(std::wstring const& name, bool defaultValue)
{
    std::wstring value = ToLowerInvariant(GetEnvironmentVariableString(name.c_str()));
    if (value.empty())
    {
        return defaultValue;
    }

    return value == L"1" ||
        value == L"true" ||
        value == L"yes" ||
        value == L"on" ||
        value == L"enabled";
}

static int GetEnvironmentInt(std::wstring const& name, int defaultValue, int minValue, int maxValue)
{
    std::wstring value = GetEnvironmentVariableString(name.c_str());
    if (value.empty())
    {
        return defaultValue;
    }

    wchar_t* end = nullptr;
    long parsed = std::wcstol(value.c_str(), &end, 10);
    if (end == value.c_str())
    {
        return defaultValue;
    }

    if (parsed < minValue)
    {
        return minValue;
    }
    if (parsed > maxValue)
    {
        return maxValue;
    }
    return static_cast<int>(parsed);
}

static std::wstring JoinPaths(std::vector<std::wstring> const& paths, const wchar_t* separator)
{
    std::wstring joined;
    for (auto const& path : paths)
    {
        if (!joined.empty())
        {
            joined += separator;
        }
        joined += path;
    }
    return joined;
}

static bool WriteTextFileUtf8(std::wstring const& path, std::string const& text)
{
    EnsureDirectory(ParentDirectory(path));
    std::wstring nativePath = ToLongWin32Path(path);
    HANDLE file = CreateFile2(
        nativePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        CREATE_ALWAYS,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        WriteLogF(L"WriteTextFileUtf8 failed for %s GetLastError=%lu", path.c_str(), GetLastError());
        return false;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == static_cast<DWORD>(text.size());
}

static bool CopyFileContentsFresh(std::wstring const& sourcePath, std::wstring const& destinationPath)
{
    EnsureDirectory(ParentDirectory(destinationPath));

    std::wstring nativeSourcePath = ToLongWin32Path(sourcePath);
    std::wstring nativeDestinationPath = ToLongWin32Path(destinationPath);

    HANDLE source = CreateFile2(
        nativeSourcePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        OPEN_EXISTING,
        nullptr);
    if (source == INVALID_HANDLE_VALUE)
    {
        WriteLogF(L"Payload copy open source failed %s GetLastError=%lu", sourcePath.c_str(), GetLastError());
        return false;
    }

    SetFileAttributesW(nativeDestinationPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    HANDLE destination = CreateFile2(
        nativeDestinationPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        CREATE_ALWAYS,
        nullptr);
    if (destination == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        CloseHandle(source);
        WriteLogF(L"Payload copy open destination failed %s GetLastError=%lu", destinationPath.c_str(), error);
        return false;
    }

    std::vector<unsigned char> buffer(1024 * 1024);
    bool ok = true;
    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(source, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
        {
            WriteLogF(L"Payload copy read failed %s GetLastError=%lu", sourcePath.c_str(), GetLastError());
            ok = false;
            break;
        }
        if (read == 0)
        {
            break;
        }

        DWORD offset = 0;
        while (offset < read)
        {
            DWORD written = 0;
            if (!WriteFile(destination, buffer.data() + offset, read - offset, &written, nullptr) || written == 0)
            {
                WriteLogF(L"Payload copy write failed %s GetLastError=%lu", destinationPath.c_str(), GetLastError());
                ok = false;
                break;
            }
            offset += written;
        }
        if (!ok)
        {
            break;
        }
    }

    if (!FlushFileBuffers(destination))
    {
        WriteLogF(L"Payload copy flush failed %s GetLastError=%lu", destinationPath.c_str(), GetLastError());
        ok = false;
    }

    CloseHandle(destination);
    CloseHandle(source);

    if (!ok)
    {
        DeleteFileW(nativeDestinationPath.c_str());
        return false;
    }

    SetFileAttributesW(nativeDestinationPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    return true;
}

static bool CopyDirectoryContentsRecursive(
    std::wstring const& source,
    std::wstring const& destination,
    int& filesCopied,
    int& directoriesCreated,
    int& failures)
{
    EnsureDirectory(destination);

    std::wstring pattern = source + L"\\*";
    std::wstring nativePattern = ToLongWin32Path(pattern);
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileExW(
        nativePattern.c_str(),
        FindExInfoBasic,
        &data,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
    {
        WriteLogF(L"Payload copy could not enumerate %s GetLastError=%lu", source.c_str(), GetLastError());
        ++failures;
        return false;
    }

    bool ok = true;
    do
    {
        if (std::wcscmp(data.cFileName, L".") == 0 || std::wcscmp(data.cFileName, L"..") == 0)
        {
            continue;
        }

        std::wstring sourcePath = source + L"\\" + data.cFileName;
        std::wstring destinationPath = destination + L"\\" + data.cFileName;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                continue;
            }

            EnsureDirectory(destinationPath);
            ++directoriesCreated;
            if (!CopyDirectoryContentsRecursive(sourcePath, destinationPath, filesCopied, directoriesCreated, failures))
            {
                ok = false;
            }
            continue;
        }

        std::wstring nativeDestinationPath = ToLongWin32Path(destinationPath);
        SetFileAttributesW(nativeDestinationPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!CopyFileContentsFresh(sourcePath, destinationPath))
        {
            WriteLogF(
                L"Payload copy failed %s -> %s GetLastError=%lu",
                sourcePath.c_str(),
                destinationPath.c_str(),
                GetLastError());
            ++failures;
            ok = false;
            continue;
        }

        SetFileAttributesW(nativeDestinationPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        ++filesCopied;
        if ((filesCopied % 500) == 0)
        {
            WriteLogF(L"Payload copy progress files=%d dirs=%d", filesCopied, directoriesCreated);
        }
    }
    while (FindNextFileW(find, &data));

    DWORD lastError = GetLastError();
    FindClose(find);
    if (lastError != ERROR_NO_MORE_FILES)
    {
        WriteLogF(L"Payload copy enumeration stopped early for %s GetLastError=%lu", source.c_str(), lastError);
        ++failures;
        ok = false;
    }

    return ok;
}

static bool DeleteDirectoryContentsRecursive(std::wstring const& directory);

static bool CriticalPackagedPayloadPresent(
    std::wstring const& localRoot,
    std::wstring const& payloadRoot,
    bool packagedMultiProfile)
{
    if (!packagedMultiProfile)
    {
        return FileExists(localRoot + L"\\client.jar");
    }

    if (!DirectoryExists(localRoot + L"\\profiles"))
    {
        WriteLogW(L"Packaged payload marker ignored: LocalState profiles folder is missing");
        return false;
    }

    std::wstring packagedSharedJvm = payloadRoot + L"\\shared\\runtimes\\runtime-1\\jre\\bin\\server\\jvm.dll";
    if (FileExists(packagedSharedJvm))
    {
        std::wstring localSharedJvm = localRoot + L"\\shared\\runtimes\\runtime-1\\jre\\bin\\server\\jvm.dll";
        if (!FileExists(localSharedJvm))
        {
            WriteLogF(L"Packaged payload marker ignored: shared Java runtime is missing: %s", localSharedJvm.c_str());
            return false;
        }
    }

    return true;
}

static bool CopyPackagedLocalStatePayloadIfPresent(
    std::wstring const& localRoot,
    std::wstring const& packageRoot)
{
    std::wstring payloadRoot = packageRoot + L"\\LocalStatePayload";
    if (!DirectoryExists(payloadRoot))
    {
        WriteLogW(L"Packaged LocalState payload not present; using existing LocalState files");
        return true;
    }

    bool packagedMultiProfile = DirectoryExists(payloadRoot + L"\\profiles");
    std::string packagedSummary;
    if (packagedMultiProfile)
    {
        ReadSmallTextFileUtf8(payloadRoot + L"\\multi-staging-summary.json", packagedSummary);
    }
    if (packagedSummary.empty())
    {
        ReadSmallTextFileUtf8(payloadRoot + L"\\staging-summary.json", packagedSummary);
    }
    std::string marker = WideToUtf8(packageRoot) + "\n";
    marker += packagedSummary.empty()
        ? "LocalStatePayload"
        : packagedSummary;
    std::wstring markerPath = localRoot + L"\\.native-packaged-payload-version";
    std::string existingMarker;
    bool localPayloadPresent = CriticalPackagedPayloadPresent(localRoot, payloadRoot, packagedMultiProfile);
    if (localPayloadPresent &&
        ReadSmallTextFileUtf8(markerPath, existingMarker) &&
        existingMarker == marker)
    {
        WriteTextFileUtf8(markerPath, marker);
        WriteLogW(L"Packaged LocalState payload already copied");
        return true;
    }

    WriteLogF(L"Copying packaged LocalState payload from %s", payloadRoot.c_str());
    if (packagedMultiProfile && DirectoryExists(localRoot + L"\\profiles"))
    {
        std::wstring localProfilesRoot = localRoot + L"\\profiles";
        WriteLogF(L"Refreshing LocalState profiles before payload copy: %s", localProfilesRoot.c_str());
        std::wstring nativeLocalProfilesRoot = ToLongWin32Path(localProfilesRoot);
        if (!DeleteDirectoryContentsRecursive(localProfilesRoot) || !RemoveDirectoryW(nativeLocalProfilesRoot.c_str()))
        {
            WriteLogF(L"WARN: failed to remove stale LocalState profiles GetLastError=%lu", GetLastError());
        }
    }

    int filesCopied = 0;
    int directoriesCreated = 0;
    int failures = 0;
    bool copied = CopyDirectoryContentsRecursive(
        payloadRoot,
        localRoot,
        filesCopied,
        directoriesCreated,
        failures);

    WriteLogF(
        L"Packaged LocalState payload copy complete files=%d dirs=%d failures=%d",
        filesCopied,
        directoriesCreated,
        failures);

    if (copied && failures == 0)
    {
        if (CriticalPackagedPayloadPresent(localRoot, payloadRoot, packagedMultiProfile))
        {
            WriteTextFileUtf8(markerPath, marker);
        }
        return true;
    }

    if (filesCopied > 0 && CriticalPackagedPayloadPresent(localRoot, payloadRoot, packagedMultiProfile))
    {
        WriteTextFileUtf8(markerPath, marker);
        WriteLogW(L"WARN: Packaged LocalState payload copy had failures; marker saved so selected profile downloads can repair missing files without a restart loop");
        return true;
    }

    WriteLogW(L"WARN: Packaged LocalState payload copy had failures; continuing with whatever files are present");
    return false;
}

struct DownloadManifestEntry
{
    std::wstring remoteUrl;
    std::wstring localRelativePath;
    unsigned long long expectedSize = 0;
    std::string sha256;
    bool requiredBeforeLaunch = true;
};

static std::string ToLowerAscii(std::string value)
{
    for (char& ch : value)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

static bool TryGetJsonString(JsonObject const& object, wchar_t const* key, std::wstring& value)
{
    value.clear();
    if (!object.HasKey(key))
    {
        return false;
    }

    IJsonValue jsonValue = object.GetNamedValue(key);
    if (jsonValue.ValueType() != JsonValueType::String)
    {
        return false;
    }

    value = jsonValue.GetString().c_str();
    return true;
}

static bool TryGetJsonBool(JsonObject const& object, wchar_t const* key, bool& value)
{
    if (!object.HasKey(key))
    {
        return false;
    }

    IJsonValue jsonValue = object.GetNamedValue(key);
    if (jsonValue.ValueType() != JsonValueType::Boolean)
    {
        return false;
    }

    value = jsonValue.GetBoolean();
    return true;
}

static bool TryGetJsonUInt64(JsonObject const& object, wchar_t const* key, unsigned long long& value)
{
    value = 0;
    if (!object.HasKey(key))
    {
        return false;
    }

    IJsonValue jsonValue = object.GetNamedValue(key);
    if (jsonValue.ValueType() != JsonValueType::Number)
    {
        return false;
    }

    double number = jsonValue.GetNumber();
    if (number <= 0.0)
    {
        return false;
    }

    value = static_cast<unsigned long long>(number + 0.5);
    return true;
}

static std::string JsonEscape(std::string const& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value)
    {
        switch (ch)
        {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\r': escaped += "\\r"; break;
        case '\n': escaped += "\\n"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

static std::string FormUrlEncode(std::wstring const& value)
{
    static const char* hex = "0123456789ABCDEF";
    std::string utf8 = WideToUtf8(value);
    std::string encoded;
    encoded.reserve(utf8.size() * 3);
    for (unsigned char ch : utf8)
    {
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~')
        {
            encoded.push_back(static_cast<char>(ch));
        }
        else if (ch == ' ')
        {
            encoded.push_back('+');
        }
        else
        {
            encoded.push_back('%');
            encoded.push_back(hex[(ch >> 4) & 0x0F]);
            encoded.push_back(hex[ch & 0x0F]);
        }
    }
    return encoded;
}

static int HexDigitValue(wchar_t ch)
{
    if (ch >= L'0' && ch <= L'9')
    {
        return ch - L'0';
    }
    if (ch >= L'a' && ch <= L'f')
    {
        return 10 + ch - L'a';
    }
    if (ch >= L'A' && ch <= L'F')
    {
        return 10 + ch - L'A';
    }
    return -1;
}

static std::wstring FormUrlDecode(std::wstring const& value)
{
    std::string bytes;
    bytes.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        wchar_t ch = value[i];
        if (ch == L'+' )
        {
            bytes.push_back(' ');
            continue;
        }
        if (ch == L'%' && i + 2 < value.size())
        {
            int hi = HexDigitValue(value[i + 1]);
            int lo = HexDigitValue(value[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                bytes.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (ch <= 0x7F)
        {
            bytes.push_back(static_cast<char>(ch));
        }
        else
        {
            bytes += WideToUtf8(std::wstring(1, ch));
        }
    }
    return Utf8ToWide(bytes.c_str());
}

static bool TryReadQueryParameter(std::wstring const& url, std::wstring const& key, std::wstring& value)
{
    value.clear();
    size_t queryStart = url.find(L'?');
    if (queryStart == std::wstring::npos)
    {
        return false;
    }

    size_t pos = queryStart + 1;
    while (pos <= url.size())
    {
        size_t end = url.find(L'&', pos);
        std::wstring part = url.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
        size_t equals = part.find(L'=');
        std::wstring partKey = equals == std::wstring::npos ? part : part.substr(0, equals);
        if (partKey == key)
        {
            value = FormUrlDecode(equals == std::wstring::npos ? L"" : part.substr(equals + 1));
            return true;
        }
        if (end == std::wstring::npos)
        {
            break;
        }
        pos = end + 1;
    }

    return false;
}

static bool TryParseJsonObject(std::wstring const& text, JsonObject& object)
{
    try
    {
        object = JsonObject::Parse(hstring(text.c_str()));
        return true;
    }
    catch (...)
    {
        object = nullptr;
        return false;
    }
}

static bool HttpReadJsonResponse(HttpResponseMessage const& response, JsonObject& json, std::wstring& text)
{
    text = response.Content().ReadAsStringAsync().get().c_str();
    return TryParseJsonObject(text, json);
}

static bool HttpPostJson(
    std::wstring const& url,
    std::string const& body,
    JsonObject& json,
    std::wstring& error)
{
    try
    {
        HttpClient client;
        std::wstring wideBody = Utf8ToWide(body.c_str());
        HttpStringContent content(hstring(wideBody.c_str()), UnicodeEncoding::Utf8, L"application/json");
        HttpResponseMessage response = client.PostAsync(Uri(hstring(url.c_str())), content).get();
        std::wstring responseText;
        if (!HttpReadJsonResponse(response, json, responseText))
        {
            error = L"HTTP response was not JSON";
            return false;
        }
        if (static_cast<int>(response.StatusCode()) < 200 || static_cast<int>(response.StatusCode()) >= 300)
        {
            std::wstring message;
            TryGetJsonString(json, L"errorMessage", message);
            if (message.empty())
            {
                TryGetJsonString(json, L"error", message);
            }
            error = message.empty() ? (L"HTTP status " + std::to_wstring(static_cast<int>(response.StatusCode()))) : message;
            return false;
        }
        return true;
    }
    catch (hresult_error const& ex)
    {
        error = L"HTTP POST failed hr=0x" + std::to_wstring(static_cast<unsigned int>(ex.code()));
        return false;
    }
    catch (...)
    {
        error = L"HTTP POST failed";
        return false;
    }
}

static bool HttpPostForm(
    std::wstring const& url,
    std::string const& body,
    JsonObject& json,
    std::wstring& error,
    int& statusCode)
{
    statusCode = 0;
    try
    {
        HttpClient client;
        std::wstring wideBody = Utf8ToWide(body.c_str());
        HttpStringContent content(hstring(wideBody.c_str()), UnicodeEncoding::Utf8, L"application/x-www-form-urlencoded");
        HttpResponseMessage response = client.PostAsync(Uri(hstring(url.c_str())), content).get();
        statusCode = static_cast<int>(response.StatusCode());
        std::wstring responseText;
        if (!HttpReadJsonResponse(response, json, responseText))
        {
            error = L"HTTP response was not JSON";
            return false;
        }
        if (statusCode < 200 || statusCode >= 300)
        {
            TryGetJsonString(json, L"error", error);
            if (error.empty())
            {
                error = L"HTTP status " + std::to_wstring(statusCode);
            }
            return false;
        }
        return true;
    }
    catch (hresult_error const& ex)
    {
        error = L"HTTP form POST failed hr=0x" + std::to_wstring(static_cast<unsigned int>(ex.code()));
        return false;
    }
    catch (...)
    {
        error = L"HTTP form POST failed";
        return false;
    }
}

static bool HttpGetJson(
    std::wstring const& url,
    JsonObject& json,
    std::wstring& error)
{
    try
    {
        HttpClient client;
        HttpResponseMessage response = client.GetAsync(Uri(hstring(url.c_str()))).get();
        std::wstring responseText;
        if (!HttpReadJsonResponse(response, json, responseText))
        {
            error = L"HTTP response was not JSON";
            return false;
        }
        int statusCode = static_cast<int>(response.StatusCode());
        if (statusCode < 200 || statusCode >= 300)
        {
            TryGetJsonString(json, L"error_description", error);
            if (error.empty())
            {
                TryGetJsonString(json, L"error", error);
            }
            if (error.empty())
            {
                error = L"HTTP status " + std::to_wstring(statusCode);
            }
            return false;
        }
        return true;
    }
    catch (hresult_error const& ex)
    {
        error = L"HTTP GET failed hr=0x" + std::to_wstring(static_cast<unsigned int>(ex.code()));
        return false;
    }
    catch (...)
    {
        error = L"HTTP GET failed";
        return false;
    }
}

static bool HttpGetJsonWithBearer(
    std::wstring const& url,
    std::wstring const& bearerToken,
    JsonObject& json,
    std::wstring& error)
{
    try
    {
        HttpClient client;
        HttpRequestMessage request(HttpMethod::Get(), Uri(hstring(url.c_str())));
        std::wstring auth = L"Bearer " + bearerToken;
        request.Headers().Insert(L"Authorization", hstring(auth.c_str()));
        HttpResponseMessage response = client.SendRequestAsync(request).get();
        std::wstring responseText;
        if (!HttpReadJsonResponse(response, json, responseText))
        {
            error = L"HTTP response was not JSON";
            return false;
        }
        if (static_cast<int>(response.StatusCode()) < 200 || static_cast<int>(response.StatusCode()) >= 300)
        {
            TryGetJsonString(json, L"errorMessage", error);
            if (error.empty())
            {
                error = L"HTTP status " + std::to_wstring(static_cast<int>(response.StatusCode()));
            }
            return false;
        }
        return true;
    }
    catch (hresult_error const& ex)
    {
        error = L"HTTP GET failed hr=0x" + std::to_wstring(static_cast<unsigned int>(ex.code()));
        return false;
    }
    catch (...)
    {
        error = L"HTTP GET failed";
        return false;
    }
}

static bool ResolveDownloadRelativePath(
    std::wstring const& root,
    std::wstring const& relative,
    std::wstring& fullPath)
{
    fullPath.clear();
    if (relative.empty() ||
        relative.find(L':') != std::wstring::npos ||
        relative[0] == L'\\' ||
        relative[0] == L'/')
    {
        return false;
    }

    std::wstring normalized = relative;
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    size_t pos = 0;
    while (pos <= normalized.size())
    {
        size_t end = normalized.find(L'\\', pos);
        std::wstring segment = normalized.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
        if (segment.empty() || segment == L"." || segment == L"..")
        {
            return false;
        }
        if (end == std::wstring::npos)
        {
            break;
        }
        pos = end + 1;
    }

    fullPath = root + L"\\" + normalized;
    return true;
}

static bool FileSizeMatches(std::wstring const& path, unsigned long long expectedSize)
{
    if (!FileExists(path))
    {
        return false;
    }
    if (expectedSize == 0)
    {
        return true;
    }

    WIN32_FILE_ATTRIBUTE_DATA data = {};
    std::wstring nativePath = ToLongWin32Path(path);
    if (!GetFileAttributesExW(nativePath.c_str(), GetFileExInfoStandard, &data))
    {
        return false;
    }

    unsigned long long actual =
        (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) |
        static_cast<unsigned long long>(data.nFileSizeLow);
    return actual == expectedSize;
}

static std::string HexFromBytes(std::vector<unsigned char> const& bytes)
{
    static const char* digits = "0123456789abcdef";
    std::string hex;
    hex.resize(bytes.size() * 2);
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        hex[i * 2] = digits[(bytes[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = digits[bytes[i] & 0x0F];
    }
    return hex;
}

static bool ComputeHashHex(std::vector<unsigned char> const& bytes, LPCWSTR algorithmName, std::string& hex)
{
    hex.clear();
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, algorithmName, nullptr, 0);
    if (status < 0)
    {
        return false;
    }

    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD cbData = 0;
    status = BCryptGetProperty(
        algorithm,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength),
        sizeof(objectLength),
        &cbData,
        0);
    if (status >= 0)
    {
        status = BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashLength),
            sizeof(hashLength),
            &cbData,
            0);
    }

    std::vector<unsigned char> objectBuffer(objectLength);
    std::vector<unsigned char> hashBuffer(hashLength);
    if (status >= 0)
    {
        status = BCryptCreateHash(
            algorithm,
            &hash,
            objectBuffer.data(),
            static_cast<ULONG>(objectBuffer.size()),
            nullptr,
            0,
            0);
    }
    if (status >= 0 && !bytes.empty())
    {
        status = BCryptHashData(
            hash,
            const_cast<PUCHAR>(bytes.data()),
            static_cast<ULONG>(bytes.size()),
            0);
    }
    if (status >= 0)
    {
        status = BCryptFinishHash(
            hash,
            hashBuffer.data(),
            static_cast<ULONG>(hashBuffer.size()),
            0);
    }

    if (hash)
    {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0)
    {
        return false;
    }

    hex = HexFromBytes(hashBuffer);
    return true;
}

static bool ComputeSha256Hex(std::vector<unsigned char> const& bytes, std::string& hex)
{
    return ComputeHashHex(bytes, BCRYPT_SHA256_ALGORITHM, hex);
}

static bool ComputeSha1Hex(std::vector<unsigned char> const& bytes, std::string& hex)
{
    return ComputeHashHex(bytes, BCRYPT_SHA1_ALGORITHM, hex);
}

static bool ReadBinaryFile(std::wstring const& path, std::vector<unsigned char>& bytes, unsigned long long maxBytes)
{
    bytes.clear();
    std::wstring nativePath = ToLongWin32Path(path);
    HANDLE file = CreateFile2(
        nativePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        OPEN_EXISTING,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) > maxBytes)
    {
        CloseHandle(file);
        return false;
    }

    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = bytes.empty() ||
        ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != bytes.size())
    {
        bytes.clear();
        return false;
    }

    return true;
}

static bool WriteBinaryFile(std::wstring const& path, std::vector<unsigned char> const& bytes)
{
    EnsureDirectory(ParentDirectory(path));
    std::wstring nativePath = ToLongWin32Path(path);
    HANDLE file = CreateFile2(
        nativePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        CREATE_ALWAYS,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        WriteLogF(L"Download write failed for %s GetLastError=%lu", path.c_str(), GetLastError());
        return false;
    }

    DWORD written = 0;
    BOOL ok = bytes.empty() ||
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == static_cast<DWORD>(bytes.size());
}

static bool ExistingDownloadTargetMatches(
    std::wstring const& targetPath,
    DownloadManifestEntry const& entry)
{
    if (!FileSizeMatches(targetPath, entry.expectedSize))
    {
        return false;
    }

    if (entry.sha256.empty())
    {
        return true;
    }

    std::vector<unsigned char> bytes;
    unsigned long long maxBytes = entry.expectedSize > 0
        ? entry.expectedSize
        : 512ull * 1024ull * 1024ull;
    if (!ReadBinaryFile(targetPath, bytes, maxBytes))
    {
        WriteLogF(L"DOWNLOAD existing file unreadable; will redownload path=%s", entry.localRelativePath.c_str());
        return false;
    }

    std::string actualSha256;
    if (!ComputeSha256Hex(bytes, actualSha256) || actualSha256 != entry.sha256)
    {
        WriteLogF(L"DOWNLOAD existing sha256 mismatch; will redownload path=%s", entry.localRelativePath.c_str());
        return false;
    }

    return true;
}

static bool ReadDownloadManifestEntries(
    std::wstring const& profileRoot,
    std::vector<DownloadManifestEntry>& entries)
{
    entries.clear();
    std::wstring manifestPath = profileRoot + L"\\download-manifest.json";
    std::string jsonBytes;
    if (!ReadTextFileUtf8(manifestPath, jsonBytes, 32ull * 1024ull * 1024ull))
    {
        WriteLogF(L"Download manifest missing or unreadable: %s", manifestPath.c_str());
        return false;
    }
    if (jsonBytes.size() >= 3 &&
        static_cast<unsigned char>(jsonBytes[0]) == 0xEF &&
        static_cast<unsigned char>(jsonBytes[1]) == 0xBB &&
        static_cast<unsigned char>(jsonBytes[2]) == 0xBF)
    {
        jsonBytes.erase(0, 3);
    }

    try
    {
        std::wstring jsonWide = Utf8ToWide(jsonBytes.c_str());
        JsonObject root = JsonObject::Parse(hstring(jsonWide.c_str()));
        if (!root.HasKey(L"Entries"))
        {
            WriteLogF(L"Download manifest has no Entries array: %s", manifestPath.c_str());
            return false;
        }

        JsonArray array = root.GetNamedArray(L"Entries");
        for (uint32_t i = 0; i < array.Size(); ++i)
        {
            IJsonValue value = array.GetAt(i);
            if (value.ValueType() != JsonValueType::Object)
            {
                continue;
            }

            JsonObject item = value.GetObject();
            DownloadManifestEntry entry = {};
            if (!TryGetJsonString(item, L"RemoteUrl", entry.remoteUrl) ||
                !TryGetJsonString(item, L"LocalRelativePath", entry.localRelativePath))
            {
                continue;
            }

            TryGetJsonUInt64(item, L"ExpectedSizeBytes", entry.expectedSize);
            std::wstring sha256;
            if (TryGetJsonString(item, L"Sha256", sha256))
            {
                entry.sha256 = ToLowerAscii(WideToUtf8(sha256));
            }
            TryGetJsonBool(item, L"RequiredBeforeLaunch", entry.requiredBeforeLaunch);
            entries.push_back(entry);
        }

        WriteLogF(L"Download manifest entries loaded count=%zu path=%s", entries.size(), manifestPath.c_str());
        return true;
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"Download manifest parse failed hr=0x%08X path=%s", static_cast<unsigned int>(ex.code()), manifestPath.c_str());
    }
    catch (...)
    {
        WriteLogF(L"Download manifest parse failed with unknown exception path=%s", manifestPath.c_str());
    }

    return false;
}

static void SetDownloadUiProgress(size_t completed, size_t total, unsigned long long phase)
{
    g_downloadUiCompleted.store(static_cast<unsigned long long>(completed), std::memory_order_release);
    g_downloadUiTotal.store(static_cast<unsigned long long>(total), std::memory_order_release);
    g_downloadUiPhase.store(phase, std::memory_order_release);
}

static bool DownloadUrlToFile(
    HttpClient& client,
    std::wstring const& remoteUrl,
    std::wstring const& targetPath,
    unsigned long long expectedSize,
    std::string const& expectedSha256,
    std::string const& expectedSha1,
    std::wstring const& label,
    size_t index,
    size_t total)
{
    std::wstring lowerUrl = ToLowerInvariant(remoteUrl);
    if (lowerUrl.find(L"https://") != 0)
    {
        WriteLogF(L"DOWNLOAD FAIL refusing non-HTTPS URL for %s", label.c_str());
        return false;
    }

    if (total > 0)
    {
        SetDownloadUiProgress(index > 0 ? index - 1 : 0, total, g_downloadUiPhase.load(std::memory_order_acquire));
    }

    WriteLogF(
        L"DOWNLOAD [%zu/%zu] %s -> %s",
        index,
        total,
        remoteUrl.c_str(),
        targetPath.c_str());

    try
    {
        Uri uri(remoteUrl);
        HttpResponseMessage response = client.GetAsync(uri).get();
        if (!response.IsSuccessStatusCode())
        {
            WriteLogF(
                L"DOWNLOAD FAIL HTTP status=%d path=%s",
                static_cast<int>(response.StatusCode()),
                label.c_str());
            return false;
        }

        IBuffer buffer = response.Content().ReadAsBufferAsync().get();
        std::vector<unsigned char> bytes(buffer.Length());
        if (!bytes.empty())
        {
            DataReader reader = DataReader::FromBuffer(buffer);
            reader.ReadBytes(bytes);
        }

        if (expectedSize > 0 && bytes.size() != expectedSize)
        {
            WriteLogF(
                L"DOWNLOAD FAIL size mismatch path=%s expected=%llu actual=%zu",
                label.c_str(),
                expectedSize,
                bytes.size());
            return false;
        }

        if (!expectedSha256.empty())
        {
            std::string actualSha256;
            if (!ComputeSha256Hex(bytes, actualSha256) || actualSha256 != expectedSha256)
            {
                WriteLogF(L"DOWNLOAD FAIL sha256 mismatch path=%s", label.c_str());
                return false;
            }
        }
        if (!expectedSha1.empty())
        {
            std::string actualSha1;
            if (!ComputeSha1Hex(bytes, actualSha1) || actualSha1 != expectedSha1)
            {
                WriteLogF(L"DOWNLOAD FAIL sha1 mismatch path=%s", label.c_str());
                return false;
            }
        }

        std::wstring tempPath = targetPath + L".download";
        std::wstring nativeTempPath = ToLongWin32Path(tempPath);
        std::wstring nativeTargetPath = ToLongWin32Path(targetPath);
        SetFileAttributesW(nativeTempPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(nativeTempPath.c_str());
        if (!WriteBinaryFile(tempPath, bytes))
        {
            DeleteFileW(nativeTempPath.c_str());
            return false;
        }

        SetFileAttributesW(nativeTargetPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!MoveFileExW(nativeTempPath.c_str(), nativeTargetPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            WriteLogF(L"DOWNLOAD FAIL move %s -> %s GetLastError=%lu", tempPath.c_str(), targetPath.c_str(), GetLastError());
            DeleteFileW(nativeTempPath.c_str());
            return false;
        }

        WriteLogF(L"DOWNLOAD OK path=%s bytes=%zu", label.c_str(), bytes.size());
        if (total > 0)
        {
            SetDownloadUiProgress(index, total, g_downloadUiPhase.load(std::memory_order_acquire));
        }
        return true;
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(
            L"DOWNLOAD FAIL hr=0x%08X path=%s",
            static_cast<unsigned int>(ex.code()),
            label.c_str());
    }
    catch (...)
    {
        WriteLogF(L"DOWNLOAD FAIL unknown exception path=%s", label.c_str());
    }

    return false;
}

static bool DownloadManifestEntryToFile(
    HttpClient& client,
    std::wstring const& profileRoot,
    DownloadManifestEntry const& entry,
    size_t index,
    size_t total)
{
    std::wstring targetPath;
    if (!ResolveDownloadRelativePath(profileRoot, entry.localRelativePath, targetPath))
    {
        WriteLogF(L"DOWNLOAD FAIL invalid relative path: %s", entry.localRelativePath.c_str());
        return false;
    }

    if (ExistingDownloadTargetMatches(targetPath, entry))
    {
        SetDownloadUiProgress(index, total, 1);
        return true;
    }

    if (FileExists(targetPath))
    {
        std::wstring nativeTargetPath = ToLongWin32Path(targetPath);
        SetFileAttributesW(nativeTargetPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(nativeTargetPath.c_str()))
        {
            WriteLogF(L"DOWNLOAD WARN could not delete stale target before replace path=%s GetLastError=%lu", targetPath.c_str(), GetLastError());
        }
    }

    return DownloadUrlToFile(
        client,
        entry.remoteUrl,
        targetPath,
        entry.expectedSize,
        entry.sha256,
        std::string(),
        entry.localRelativePath,
        index,
        total);
}

struct AssetObjectDownloadEntry
{
    std::wstring remoteUrl;
    std::wstring targetPath;
    std::wstring label;
    std::string sha1;
    unsigned long long expectedSize = 0;
};

static bool TryReadProfileAssetIndexId(std::wstring const& profileRoot, std::wstring& assetIndexId)
{
    assetIndexId.clear();
    std::string jsonBytes;
    if (!ReadSmallTextFileUtf8(profileRoot + L"\\staging-summary.json", jsonBytes))
    {
        return false;
    }

    try
    {
        std::wstring jsonWide = Utf8ToWide(jsonBytes.c_str());
        JsonObject root = JsonObject::Parse(hstring(jsonWide.c_str()));
        return TryGetJsonString(root, L"AssetIndex", assetIndexId) && !assetIndexId.empty();
    }
    catch (...)
    {
        assetIndexId.clear();
        return false;
    }
}

static bool ResolveProfileAssetIndexPath(
    std::wstring const& profileRoot,
    std::wstring const& localRoot,
    std::wstring& assetIndexPath,
    std::wstring& assetsRoot)
{
    assetIndexPath.clear();
    assetsRoot.clear();

    std::vector<std::wstring> candidateAssetRoots;
    if (!localRoot.empty())
    {
        candidateAssetRoots.push_back(localRoot + L"\\assets");
    }
    candidateAssetRoots.push_back(profileRoot + L"\\assets");

    std::wstring assetIndexId;
    if (TryReadProfileAssetIndexId(profileRoot, assetIndexId))
    {
        for (auto const& root : candidateAssetRoots)
        {
            std::wstring candidate = root + L"\\indexes\\" + assetIndexId + L".json";
            if (FileExists(candidate))
            {
                assetIndexPath = candidate;
                assetsRoot = root;
                return true;
            }
        }
    }

    for (auto const& root : candidateAssetRoots)
    {
        std::wstring indexesRoot = root + L"\\indexes";
        std::wstring pattern = indexesRoot + L"\\*.json";
        WIN32_FIND_DATAW data = {};
        HANDLE find = FindFirstFileExW(
            pattern.c_str(),
            FindExInfoBasic,
            &data,
            FindExSearchNameMatch,
            nullptr,
            0);
        if (find == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        do
        {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                assetIndexPath = indexesRoot + L"\\" + data.cFileName;
                assetsRoot = root;
                FindClose(find);
                return true;
            }
        }
        while (FindNextFileW(find, &data));

        FindClose(find);
    }

    return false;
}

static bool ReadAssetObjectDownloadEntries(
    std::wstring const& profileRoot,
    std::wstring const& localRoot,
    std::vector<AssetObjectDownloadEntry>& entries,
    std::wstring* resolvedAssetsRoot = nullptr)
{
    entries.clear();
    if (resolvedAssetsRoot)
    {
        resolvedAssetsRoot->clear();
    }

    std::wstring assetIndexPath;
    std::wstring assetsRoot;
    if (!ResolveProfileAssetIndexPath(profileRoot, localRoot, assetIndexPath, assetsRoot))
    {
        WriteLogF(L"DOWNLOAD asset index not present for profile root=%s", profileRoot.c_str());
        return true;
    }
    if (resolvedAssetsRoot)
    {
        *resolvedAssetsRoot = assetsRoot;
    }

    std::string jsonBytes;
    if (!ReadTextFileUtf8(assetIndexPath, jsonBytes, 32ull * 1024ull * 1024ull))
    {
        WriteLogF(L"DOWNLOAD asset index unreadable: %s", assetIndexPath.c_str());
        return false;
    }
    if (jsonBytes.size() >= 3 &&
        static_cast<unsigned char>(jsonBytes[0]) == 0xEF &&
        static_cast<unsigned char>(jsonBytes[1]) == 0xBB &&
        static_cast<unsigned char>(jsonBytes[2]) == 0xBF)
    {
        jsonBytes.erase(0, 3);
    }

    try
    {
        std::wstring jsonWide = Utf8ToWide(jsonBytes.c_str());
        JsonObject root = JsonObject::Parse(hstring(jsonWide.c_str()));
        if (!root.HasKey(L"objects"))
        {
            WriteLogF(L"DOWNLOAD asset index has no objects: %s", assetIndexPath.c_str());
            return false;
        }

        JsonObject objects = root.GetNamedObject(L"objects");
        for (auto const& pair : objects)
        {
            IJsonValue value = pair.Value();
            if (value.ValueType() != JsonValueType::Object)
            {
                continue;
            }

            JsonObject object = value.GetObject();
            std::wstring hash;
            unsigned long long size = 0;
            if (!TryGetJsonString(object, L"hash", hash) ||
                hash.size() < 2 ||
                !TryGetJsonUInt64(object, L"size", size))
            {
                continue;
            }

            std::wstring prefix = hash.substr(0, 2);
            AssetObjectDownloadEntry entry = {};
            entry.remoteUrl = L"https://resources.download.minecraft.net/" + prefix + L"/" + hash;
            entry.targetPath = assetsRoot + L"\\objects\\" + prefix + L"\\" + hash;
            entry.label = L"assets\\objects\\" + prefix + L"\\" + hash;
            entry.sha1 = ToLowerAscii(WideToUtf8(hash));
            entry.expectedSize = size;
            entries.push_back(entry);
        }

        WriteLogF(
            L"DOWNLOAD asset objects listed count=%zu index=%s assetsRoot=%s",
            entries.size(),
            assetIndexPath.c_str(),
            assetsRoot.c_str());
        return true;
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"DOWNLOAD asset index parse failed hr=0x%08X path=%s", static_cast<unsigned int>(ex.code()), assetIndexPath.c_str());
    }
    catch (...)
    {
        WriteLogF(L"DOWNLOAD asset index parse failed with unknown exception path=%s", assetIndexPath.c_str());
    }

    return false;
}

static bool EnsureProfileAssetObjectsWorker(
    HttpClient& client,
    std::wstring const& profileRoot,
    std::wstring const& localRoot)
{
    std::vector<AssetObjectDownloadEntry> entries;
    std::wstring assetsRoot;
    if (!ReadAssetObjectDownloadEntries(profileRoot, localRoot, entries, &assetsRoot))
    {
        return false;
    }
    if (entries.empty())
    {
        return true;
    }

    size_t missingCount = 0;
    unsigned long long missingBytes = 0;
    for (auto const& entry : entries)
    {
        if (!FileSizeMatches(entry.targetPath, entry.expectedSize))
        {
            ++missingCount;
            missingBytes += entry.expectedSize;
        }
    }

    if (missingCount == 0)
    {
        WriteLogF(L"DOWNLOAD asset objects already hydrated count=%zu root=%s", entries.size(), assetsRoot.c_str());
        SetDownloadUiProgress(entries.size(), entries.size(), 2);
        return true;
    }

    g_downloadWorkPerformed.store(true, std::memory_order_release);
    SetDownloadUiProgress(0, missingCount, 2);
    WriteLogF(
        L"DOWNLOAD asset hydration starting missing=%zu total=%zu approxMB=%llu root=%s",
        missingCount,
        entries.size(),
        (missingBytes + (1024ull * 1024ull - 1ull)) / (1024ull * 1024ull),
        assetsRoot.c_str());

    size_t index = 0;
    for (auto const& entry : entries)
    {
        if (FileSizeMatches(entry.targetPath, entry.expectedSize))
        {
            continue;
        }

        ++index;
        if (!DownloadUrlToFile(
            client,
            entry.remoteUrl,
            entry.targetPath,
            entry.expectedSize,
            std::string(),
            entry.sha1,
            entry.label,
            index,
            missingCount))
        {
            WriteLogF(L"DOWNLOAD asset hydration failed at missing object %zu/%zu", index, missingCount);
            return false;
        }
    }

    SetDownloadUiProgress(missingCount, missingCount, 2);
    WriteLogF(L"DOWNLOAD asset hydration complete downloaded=%zu total=%zu root=%s", missingCount, entries.size(), assetsRoot.c_str());
    return true;
}

static bool EnsureProfileDownloadsWorker(
    std::wstring const& profileRoot,
    std::wstring const& localRoot,
    std::vector<DownloadManifestEntry> const& entries)
{
    init_apartment(apartment_type::multi_threaded);
    HttpClient client;
    size_t requiredCount = 0;
    size_t missingCount = 0;
    unsigned long long missingBytes = 0;

    for (auto const& entry : entries)
    {
        if (!entry.requiredBeforeLaunch)
        {
            continue;
        }
        ++requiredCount;
        std::wstring targetPath;
        if (!ResolveDownloadRelativePath(profileRoot, entry.localRelativePath, targetPath) ||
            !ExistingDownloadTargetMatches(targetPath, entry))
        {
            ++missingCount;
            missingBytes += entry.expectedSize;
        }
    }

    if (missingCount == 0)
    {
        WriteLogF(L"DOWNLOAD profile already hydrated required=%zu root=%s", requiredCount, profileRoot.c_str());
        return EnsureProfileAssetObjectsWorker(client, profileRoot, localRoot);
    }

    g_downloadWorkPerformed.store(true, std::memory_order_release);
    SetDownloadUiProgress(0, requiredCount, 1);
    WriteLogF(
        L"DOWNLOAD first-launch hydration starting missing=%zu required=%zu approxMB=%llu root=%s",
        missingCount,
        requiredCount,
        (missingBytes + (1024ull * 1024ull - 1ull)) / (1024ull * 1024ull),
        profileRoot.c_str());

    size_t index = 0;
    for (auto const& entry : entries)
    {
        if (!entry.requiredBeforeLaunch)
        {
            continue;
        }
        ++index;
        if (!DownloadManifestEntryToFile(client, profileRoot, entry, index, requiredCount))
        {
            WriteLogF(L"DOWNLOAD first-launch hydration failed at required entry %zu/%zu", index, requiredCount);
            return false;
        }
    }

    SetDownloadUiProgress(requiredCount, requiredCount, 1);
    WriteLogF(L"DOWNLOAD first-launch hydration complete required=%zu root=%s", requiredCount, profileRoot.c_str());
    return EnsureProfileAssetObjectsWorker(client, profileRoot, localRoot);
}

static bool RunWithDispatcherPump(
    CoreWindow const& window,
    std::function<bool()> work,
    std::wstring const& statusMessage = std::wstring())
{
    std::atomic<int> done{ 0 };
    bool result = false;
    std::thread worker([&]()
    {
        try
        {
            result = work();
        }
        catch (hresult_error const& ex)
        {
            WriteLogF(L"Background work threw hresult_error hr=0x%08X", static_cast<unsigned int>(ex.code()));
            result = false;
        }
        catch (...)
        {
            WriteLogW(L"Background work threw unknown exception");
            result = false;
        }
        done.store(1, std::memory_order_release);
    });

    auto dispatcher = window ? window.Dispatcher() : CoreWindow::GetForCurrentThread().Dispatcher();
    bool statusSurfaceReady = false;
    uint32_t statusFrame = 0;
    if (!statusMessage.empty())
    {
        if (IsTruthyEnvironment(L"MINECRAFT_XBOX_DOWNLOAD_EGL_STATUS_UI", true))
        {
            try
            {
                WriteLogF(L"DOWNLOAD EGL status UI requested: %s", statusMessage.c_str());
                statusSurfaceReady = ProbeMesaEgl(window, false, false);
                WriteLogW(statusSurfaceReady
                    ? L"DOWNLOAD status EGL surface ready"
                    : L"DOWNLOAD status EGL surface unavailable; dispatcher pump only");
                if (statusSurfaceReady)
                {
                    RenderDownloadStatusFrame(statusFrame++);
                }
            }
            catch (hresult_error const& ex)
            {
                WriteLogF(L"DOWNLOAD status EGL surface failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
            }
            catch (...)
            {
                WriteLogW(L"DOWNLOAD status EGL surface failed with unknown exception");
            }
        }
        else
        {
            WriteLogF(L"DOWNLOAD simple status dialog requested: %s", statusMessage.c_str());
            try
            {
                MessageDialog dialog(hstring(statusMessage.c_str()));
                ShowDialogAndPump(window, dialog);
            }
            catch (hresult_error const& ex)
            {
                WriteLogF(L"DOWNLOAD status dialog failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
            }
            catch (...)
            {
                WriteLogW(L"DOWNLOAD status dialog failed with unknown exception");
            }
        }
    }

    while (done.load(std::memory_order_acquire) == 0)
    {
        dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        if (statusSurfaceReady)
        {
            RenderDownloadStatusFrame(statusFrame++);
        }
        Sleep(16);
    }

    if (statusSurfaceReady)
    {
        for (int i = 0; i < 12; ++i)
        {
            dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            RenderDownloadStatusFrame(statusFrame++);
            Sleep(16);
        }
        CleanupProbeEgl();
    }

    worker.join();
    return result;
}

static std::wstring BuildDownloadStatusMessage(
    std::wstring const& profileRoot,
    std::wstring const& localRoot,
    std::vector<DownloadManifestEntry> const& entries)
{
    size_t requiredCount = 0;
    size_t missingCount = 0;
    unsigned long long missingBytes = 0;

    for (auto const& entry : entries)
    {
        if (!entry.requiredBeforeLaunch)
        {
            continue;
        }
        ++requiredCount;

        std::wstring targetPath;
        if (!ResolveDownloadRelativePath(profileRoot, entry.localRelativePath, targetPath) ||
            !ExistingDownloadTargetMatches(targetPath, entry))
        {
            ++missingCount;
            missingBytes += entry.expectedSize;
        }
    }

    if (missingCount == 0)
    {
        std::vector<AssetObjectDownloadEntry> assetEntries;
        std::wstring assetsRoot;
        if (ReadAssetObjectDownloadEntries(profileRoot, localRoot, assetEntries, &assetsRoot))
        {
            size_t missingAssetCount = 0;
            unsigned long long missingAssetBytes = 0;
            for (auto const& entry : assetEntries)
            {
                if (!FileSizeMatches(entry.targetPath, entry.expectedSize))
                {
                    ++missingAssetCount;
                    missingAssetBytes += entry.expectedSize;
                }
            }
            if (missingAssetCount > 0)
            {
                unsigned long long approxAssetMb = (missingAssetBytes + (1024ull * 1024ull - 1ull)) / (1024ull * 1024ull);
                return L"Downloading Minecraft assets...\r\n\r\n" +
                    std::to_wstring(missingAssetCount) +
                    L" asset files missing, about " +
                    std::to_wstring(approxAssetMb) +
                    L" MB.\r\n\r\nWhen this finishes, relaunch once so Minecraft starts with a clean graphics window.";
            }
        }
        return std::wstring();
    }

    unsigned long long approxMb = (missingBytes + (1024ull * 1024ull - 1ull)) / (1024ull * 1024ull);
    return L"Downloading Minecraft game files...\r\n\r\n" +
        std::to_wstring(missingCount) +
        L" files missing, about " +
        std::to_wstring(approxMb) +
        L" MB.\r\n\r\nThis can take a while on first launch. When it finishes, relaunch once so Minecraft starts clean.";
}

static bool EnsureProfileDownloadsBeforeLaunch(
    CoreWindow const& window,
    std::wstring const& profileRoot,
    std::wstring const& localRoot)
{
    std::vector<DownloadManifestEntry> entries;
    if (!ReadDownloadManifestEntries(profileRoot, entries))
    {
        WriteLogW(L"DOWNLOAD no usable manifest; assuming profile files were packaged");
        return true;
    }

    std::wstring statusMessage = BuildDownloadStatusMessage(profileRoot, localRoot, entries);
    return RunWithDispatcherPump(window, [&]()
    {
        return EnsureProfileDownloadsWorker(profileRoot, localRoot, entries);
    }, statusMessage);
}

static void CollectJarFilesRecursive(std::wstring const& directory, std::vector<std::wstring>& jars)
{
    std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &data,
        FindExSearchNameMatch,
        nullptr,
        0);
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L"..")
        {
            continue;
        }

        std::wstring path = directory + L"\\" + name;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            CollectJarFilesRecursive(path, jars);
        }
        else if (EndsWithInsensitive(name, L".jar") && !ContainsInsensitive(name, L"-natives-"))
        {
            jars.push_back(path);
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);
}

static unsigned long long FileSizeForLog(std::wstring const& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
    {
        return 0;
    }

    return (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) |
        static_cast<unsigned long long>(data.nFileSizeLow);
}

static void LogModJarList(const wchar_t* label, std::vector<std::wstring> const& jars)
{
    WriteLogF(L"MODLOG %s count=%zu", label, jars.size());
    for (size_t i = 0; i < jars.size(); ++i)
    {
        std::wstring name = FileNameOnly(jars[i]);
        WriteLogF(
            L"MODLOG %s[%zu] name=%s size=%llu path=%s",
            label,
            i + 1,
            name.c_str(),
            FileSizeForLog(jars[i]),
            jars[i].c_str());
    }
}

static void LogModDirectorySnapshot(const wchar_t* label, std::wstring const& directory)
{
    if (!DirectoryExists(directory))
    {
        WriteLogF(L"MODLOG %s directory missing: %s", label, directory.c_str());
        return;
    }

    std::vector<std::wstring> jars;
    CollectJarFilesRecursive(directory, jars);
    std::sort(jars.begin(), jars.end());
    LogModJarList(label, jars);
}

static void CollectJarFilesDirect(std::wstring const& directory, std::vector<std::wstring>& jars)
{
    std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &data,
        FindExSearchNameMatch,
        nullptr,
        0);
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L"..")
        {
            continue;
        }

        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
            EndsWithInsensitive(name, L".jar") &&
            !ContainsInsensitive(name, L"-natives-"))
        {
            jars.push_back(directory + L"\\" + name);
        }
    }
    while (FindNextFileW(find, &data));

    FindClose(find);
}

struct ModFolderProfile
{
    std::wstring name;
    std::vector<std::wstring> directories;
};

static bool SamePathInsensitive(std::wstring const& a, std::wstring const& b)
{
    return ToLowerInvariant(a) == ToLowerInvariant(b);
}

static void AddUniquePath(std::vector<std::wstring>& paths, std::wstring const& path)
{
    if (path.empty())
    {
        return;
    }

    auto found = std::find_if(paths.begin(), paths.end(), [&path](std::wstring const& existing)
    {
        return SamePathInsensitive(existing, path);
    });
    if (found == paths.end())
    {
        paths.push_back(path);
    }
}

static void BuildModLibraryRoots(
    std::wstring const& localRoot,
    std::wstring const& profileRoot,
    std::wstring const& minecraftVersion,
    std::wstring const& baseMinecraftVersion,
    bool includeBaseVersionModLibrary,
    std::vector<std::wstring>& roots)
{
    roots.clear();
    AddUniquePath(roots, localRoot + L"\\mods-library\\common");
    AddUniquePath(roots, localRoot + L"\\mods-library\\" + minecraftVersion);
    if (includeBaseVersionModLibrary && !baseMinecraftVersion.empty() && baseMinecraftVersion != minecraftVersion)
    {
        AddUniquePath(roots, localRoot + L"\\mods-library\\" + baseMinecraftVersion);
    }
    AddUniquePath(roots, profileRoot + L"\\mods-library");
}

static bool DirectoryHasJarRecursive(std::wstring const& directory)
{
    std::vector<std::wstring> jars;
    CollectJarFilesRecursive(directory, jars);
    return !jars.empty();
}

static void AddReservedModFolderName(std::vector<std::wstring>& reserved, std::wstring const& name)
{
    if (name.empty())
    {
        return;
    }
    AddUniquePath(reserved, ToLowerInvariant(name));
}

static void CollectReservedTopLevelModFolderNames(
    std::wstring const& localRoot,
    std::wstring const& minecraftVersion,
    std::wstring const& baseMinecraftVersion,
    std::vector<std::wstring>& reserved)
{
    reserved.clear();
    AddReservedModFolderName(reserved, L"common");
    AddReservedModFolderName(reserved, minecraftVersion);
    AddReservedModFolderName(reserved, baseMinecraftVersion);

    std::wstring profilesRoot = localRoot + L"\\profiles";
    std::wstring pattern = profilesRoot + L"\\*";
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &data,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L"..")
        {
            continue;
        }
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            AddReservedModFolderName(reserved, name);
        }
    }
    while (FindNextFileW(find, &data));

    FindClose(find);
}

static bool IsReservedModFolderName(std::vector<std::wstring> const& reserved, std::wstring const& name)
{
    std::wstring lower = ToLowerInvariant(name);
    return std::find(reserved.begin(), reserved.end(), lower) != reserved.end();
}

static void AddModFolderProfileDirectory(
    std::vector<ModFolderProfile>& profiles,
    std::wstring const& profileName,
    std::wstring const& directory)
{
    if (profileName.empty() || !DirectoryHasJarRecursive(directory))
    {
        return;
    }

    std::wstring lower = ToLowerInvariant(profileName);
    for (auto& profile : profiles)
    {
        if (ToLowerInvariant(profile.name) == lower)
        {
            AddUniquePath(profile.directories, directory);
            return;
        }
    }

    ModFolderProfile profile = {};
    profile.name = profileName;
    profile.directories.push_back(directory);
    profiles.push_back(profile);
}

static void CollectImmediateModFolderProfiles(
    std::wstring const& root,
    std::vector<std::wstring> const& reservedNames,
    std::vector<ModFolderProfile>& profiles)
{
    if (!DirectoryExists(root))
    {
        return;
    }

    std::wstring pattern = root + L"\\*";
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &data,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L"..")
        {
            continue;
        }
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            continue;
        }
        if (IsReservedModFolderName(reservedNames, name))
        {
            continue;
        }

        AddModFolderProfileDirectory(profiles, name, root + L"\\" + name);
    }
    while (FindNextFileW(find, &data));

    FindClose(find);
}

static void CollectModFolderProfiles(
    std::wstring const& localRoot,
    std::wstring const& profileRoot,
    std::wstring const& minecraftVersion,
    std::wstring const& baseMinecraftVersion,
    bool includeBaseVersionModLibrary,
    std::vector<ModFolderProfile>& profiles)
{
    profiles.clear();

    std::vector<std::wstring> reservedTopLevel;
    CollectReservedTopLevelModFolderNames(localRoot, minecraftVersion, baseMinecraftVersion, reservedTopLevel);
    CollectImmediateModFolderProfiles(localRoot + L"\\mods-library", reservedTopLevel, profiles);

    std::vector<std::wstring> roots;
    BuildModLibraryRoots(localRoot, profileRoot, minecraftVersion, baseMinecraftVersion, includeBaseVersionModLibrary, roots);
    std::vector<std::wstring> noReservedNames;
    for (auto const& root : roots)
    {
        CollectImmediateModFolderProfiles(root, noReservedNames, profiles);
    }

    std::sort(profiles.begin(), profiles.end(), [](ModFolderProfile const& a, ModFolderProfile const& b)
    {
        return ToLowerInvariant(a.name) < ToLowerInvariant(b.name);
    });

    WriteLogF(L"MODLOG mod folder profiles discovered count=%zu", profiles.size());
    for (size_t i = 0; i < profiles.size(); ++i)
    {
        WriteLogF(L"MODLOG mod folder profile[%zu] name=%s roots=%zu", i + 1, profiles[i].name.c_str(), profiles[i].directories.size());
        for (auto const& directory : profiles[i].directories)
        {
            WriteLogF(L"MODLOG mod folder profile root name=%s path=%s", profiles[i].name.c_str(), directory.c_str());
        }
    }
}

static const ModFolderProfile* FindModFolderProfile(
    std::vector<ModFolderProfile> const& profiles,
    std::wstring const& name)
{
    std::wstring lower = ToLowerInvariant(name);
    for (auto const& profile : profiles)
    {
        if (ToLowerInvariant(profile.name) == lower)
        {
            return &profile;
        }
    }
    return nullptr;
}

static std::wstring SelectedModFolderProfilePath(std::wstring const& localRoot)
{
    return localRoot + L"\\selected-mod-folder-profile.txt";
}

static std::wstring ReadSavedModFolderProfile(
    std::wstring const& localRoot,
    std::vector<ModFolderProfile> const& profiles)
{
    std::string savedBytes;
    if (!ReadSmallTextFileUtf8(SelectedModFolderProfilePath(localRoot), savedBytes))
    {
        return std::wstring();
    }

    std::wstring saved = Utf8ToWide(savedBytes.c_str());
    while (!saved.empty() && (saved.back() == L'\r' || saved.back() == L'\n'))
    {
        saved.pop_back();
    }

    return FindModFolderProfile(profiles, saved) ? saved : std::wstring();
}

static std::wstring PickModFolderProfileInteractive(
    CoreWindow const& window,
    std::wstring const& localRoot,
    std::vector<ModFolderProfile> const& profiles)
{
    if (profiles.empty())
    {
        WriteTextFileUtf8(SelectedModFolderProfilePath(localRoot), "");
        WriteLogW(L"MODLOG no mod folder profiles available; using loose/global mods");
        return std::wstring();
    }

    std::wstring saved = ReadSavedModFolderProfile(localRoot, profiles);
    try
    {
        if (profiles.size() <= 2)
        {
            MessageDialog dialog(L"Choose a mod folder profile.");
            dialog.Commands().Append(UICommand(L"Loose mods"));
            for (auto const& profile : profiles)
            {
                dialog.Commands().Append(UICommand(profile.name));
            }

            unsigned int defaultIndex = 0;
            for (size_t i = 0; i < profiles.size(); ++i)
            {
                if (!saved.empty() && ToLowerInvariant(profiles[i].name) == ToLowerInvariant(saved))
                {
                    defaultIndex = static_cast<unsigned int>(i + 1);
                    break;
                }
            }
            dialog.DefaultCommandIndex(defaultIndex);

            IUICommand selection = ShowDialogAndPump(window, dialog);
            std::wstring label = selection ? selection.Label().c_str() : std::wstring();
            if (FindModFolderProfile(profiles, label))
            {
                WriteTextFileUtf8(SelectedModFolderProfilePath(localRoot), WideToUtf8(label));
                WriteLogF(L"MODLOG selected mod folder profile=%s", label.c_str());
                return label;
            }

            WriteTextFileUtf8(SelectedModFolderProfilePath(localRoot), "");
            WriteLogW(L"MODLOG selected loose/global mods with no mod folder profile");
            return std::wstring();
        }

        if (!saved.empty())
        {
            std::wstring prompt = L"Use saved mod folder profile?\r\n" + saved;
            MessageDialog dialog{ hstring(prompt) };
            dialog.Commands().Append(UICommand(L"Yes"));
            dialog.Commands().Append(UICommand(L"No"));
            dialog.DefaultCommandIndex(0);
            IUICommand selection = ShowDialogAndPump(window, dialog);
            std::wstring label = selection ? selection.Label().c_str() : std::wstring();
            if (label == L"Yes")
            {
                WriteLogF(L"MODLOG selected saved mod folder profile=%s", saved.c_str());
                return saved;
            }
        }

        for (auto const& profile : profiles)
        {
            std::wstring prompt = L"Use this mod folder profile?\r\n" + profile.name;
            MessageDialog dialog{ hstring(prompt) };
            dialog.Commands().Append(UICommand(L"Yes"));
            dialog.Commands().Append(UICommand(L"No"));
            dialog.DefaultCommandIndex(1);
            IUICommand selection = ShowDialogAndPump(window, dialog);
            std::wstring label = selection ? selection.Label().c_str() : std::wstring();
            if (label == L"Yes")
            {
                WriteTextFileUtf8(SelectedModFolderProfilePath(localRoot), WideToUtf8(profile.name));
                WriteLogF(L"MODLOG selected mod folder profile=%s", profile.name.c_str());
                return profile.name;
            }
        }
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"Mod folder profile picker failed hr=0x%08X; using saved/loose fallback", static_cast<unsigned int>(ex.code()));
    }
    catch (...)
    {
        WriteLogW(L"Mod folder profile picker failed with unknown exception; using saved/loose fallback");
    }

    if (!saved.empty())
    {
        WriteLogF(L"MODLOG fallback mod folder profile=%s", saved.c_str());
        return saved;
    }

    WriteTextFileUtf8(SelectedModFolderProfilePath(localRoot), "");
    WriteLogW(L"MODLOG fallback to loose/global mods with no mod folder profile");
    return std::wstring();
}

static bool DeleteDirectoryContentsRecursive(std::wstring const& directory)
{
    if (!DirectoryExists(directory))
    {
        return true;
    }

    std::wstring pattern = directory + L"\\*";
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &data,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    bool ok = true;
    do
    {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L"..")
        {
            continue;
        }

        std::wstring path = directory + L"\\" + name;
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            if (!DeleteDirectoryContentsRecursive(path) || !RemoveDirectoryW(path.c_str()))
            {
                WriteLogF(L"WARN: failed to remove mod staging directory %s GetLastError=%lu", path.c_str(), GetLastError());
                ok = false;
            }
        }
        else if (!DeleteFileW(path.c_str()))
        {
            WriteLogF(L"WARN: failed to remove mod staging file %s GetLastError=%lu", path.c_str(), GetLastError());
            ok = false;
        }
    }
    while (FindNextFileW(find, &data));

    FindClose(find);
    return ok;
}

static void CollectAvailableMods(
    std::wstring const& localRoot,
    std::wstring const& profileRoot,
    std::wstring const& minecraftVersion,
    std::wstring const& baseMinecraftVersion,
    bool includeBaseVersionModLibrary,
    std::vector<ModFolderProfile> const& modFolderProfiles,
    std::wstring const& selectedModFolderProfile,
    std::vector<std::wstring>& mods)
{
    mods.clear();
    std::vector<std::wstring> roots;
    BuildModLibraryRoots(localRoot, profileRoot, minecraftVersion, baseMinecraftVersion, includeBaseVersionModLibrary, roots);

    WriteLogF(
        L"MODLOG collecting available mods version=%s baseVersion=%s includeBaseVersionLibrary=%d selectedFolderProfile=%s localRoot=%s profileRoot=%s",
        minecraftVersion.c_str(),
        baseMinecraftVersion.empty() ? L"<none>" : baseMinecraftVersion.c_str(),
        includeBaseVersionModLibrary ? 1 : 0,
        selectedModFolderProfile.empty() ? L"<loose>" : selectedModFolderProfile.c_str(),
        localRoot.c_str(),
        profileRoot.c_str());

    for (auto const& root : roots)
    {
        if (DirectoryExists(root))
        {
            std::vector<std::wstring> rootMods;
            CollectJarFilesDirect(root, rootMods);
            std::sort(rootMods.begin(), rootMods.end());
            WriteLogF(L"MODLOG mod library root present: %s", root.c_str());
            LogModJarList(L"available-root-loose", rootMods);
            mods.insert(mods.end(), rootMods.begin(), rootMods.end());
        }
        else
        {
            WriteLogF(L"MODLOG mod library root missing: %s", root.c_str());
        }
    }

    if (!selectedModFolderProfile.empty())
    {
        if (auto profile = FindModFolderProfile(modFolderProfiles, selectedModFolderProfile))
        {
            for (auto const& directory : profile->directories)
            {
                std::vector<std::wstring> profileMods;
                CollectJarFilesRecursive(directory, profileMods);
                std::sort(profileMods.begin(), profileMods.end());
                WriteLogF(L"MODLOG selected mod folder profile root present name=%s path=%s", profile->name.c_str(), directory.c_str());
                LogModJarList(L"available-folder-profile", profileMods);
                mods.insert(mods.end(), profileMods.begin(), profileMods.end());
            }
        }
        else
        {
            WriteLogF(L"MODLOG selected mod folder profile missing name=%s; using loose/global mods only", selectedModFolderProfile.c_str());
        }
    }
    else
    {
        WriteLogW(L"MODLOG no mod folder profile selected; using loose/global mods only");
    }

    std::sort(mods.begin(), mods.end());
    mods.erase(std::unique(mods.begin(), mods.end()), mods.end());
    LogModJarList(L"available-combined", mods);
}

static bool IsPathUnderDirectory(std::wstring const& path, std::wstring const& directory)
{
    if (path.empty() || directory.empty())
    {
        return false;
    }

    std::wstring normalizedPath = ToLowerInvariant(path);
    std::wstring normalizedDirectory = ToLowerInvariant(directory);
    for (auto& ch : normalizedPath)
    {
        if (ch == L'/') ch = L'\\';
    }
    for (auto& ch : normalizedDirectory)
    {
        if (ch == L'/') ch = L'\\';
    }
    while (!normalizedDirectory.empty() && normalizedDirectory.back() == L'\\')
    {
        normalizedDirectory.pop_back();
    }

    if (normalizedPath.size() <= normalizedDirectory.size())
    {
        return false;
    }
    if (normalizedPath.compare(0, normalizedDirectory.size(), normalizedDirectory) != 0)
    {
        return false;
    }

    return normalizedPath[normalizedDirectory.size()] == L'\\';
}

static bool IsPathUnderAnyDirectory(std::wstring const& path, std::vector<std::wstring> const& directories)
{
    return std::any_of(directories.begin(), directories.end(), [&path](std::wstring const& directory)
    {
        return IsPathUnderDirectory(path, directory);
    });
}

static bool CopySelectedMod(std::wstring const& modPath, std::wstring const& modsDir)
{
    EnsureDirectory(modsDir);
    std::wstring destination = modsDir + L"\\" + FileNameOnly(modPath);
    SetFileAttributesW(destination.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!CopyFileW(modPath.c_str(), destination.c_str(), FALSE))
    {
        WriteLogF(L"WARN: failed to stage mod %s -> %s GetLastError=%lu", modPath.c_str(), destination.c_str(), GetLastError());
        return false;
    }

    WriteLogF(L"Staged mod %s", destination.c_str());
    return true;
}

static bool SelectionContainsMod(std::vector<std::wstring> const& selected, const wchar_t* nameNeedle)
{
    return std::any_of(selected.begin(), selected.end(), [nameNeedle](std::wstring const& mod)
    {
        return ContainsInsensitive(FileNameOnly(mod), nameNeedle);
    });
}

static bool IsMainLegacy4JModName(std::wstring const& fileName)
{
    std::wstring lower = ToLowerInvariant(fileName);
    return lower.rfind(L"legacy4j-", 0) == 0;
}

static bool IsFabricLanDiscoveryModName(std::wstring const& fileName)
{
    std::wstring lower = ToLowerInvariant(fileName);
    return lower.rfind(L"xbox-lan-discovery-fabric", 0) == 0;
}

static bool IsCoreFabricSupportModName(std::wstring const& fileName)
{
    std::wstring lower = ToLowerInvariant(fileName);
    return lower.rfind(L"fabric-api-", 0) == 0 ||
        lower.rfind(L"framework-fabric-", 0) == 0 ||
        IsFabricLanDiscoveryModName(fileName);
}

static bool AddSelectedModUnique(std::vector<std::wstring>& selected, std::wstring const& mod)
{
    if (std::find(selected.begin(), selected.end(), mod) != selected.end())
    {
        return false;
    }

    selected.push_back(mod);
    return true;
}

static void AutoStageCoreFabricSupportMods(
    std::vector<std::wstring> const& available,
    std::vector<std::wstring>& selected,
    bool includeLanDiscovery)
{
    for (auto const& mod : available)
    {
        std::wstring name = FileNameOnly(mod);
        if (!IsCoreFabricSupportModName(name))
        {
            continue;
        }

        if (IsFabricLanDiscoveryModName(name) && !includeLanDiscovery)
        {
            WriteLogF(
                L"MODLOG skipped core Fabric support name=%s path=%s reason=requires Minecraft 1.21+",
                name.c_str(),
                mod.c_str());
            continue;
        }

        if (AddSelectedModUnique(selected, mod))
        {
            WriteLogF(
                L"MODLOG auto-staged core Fabric support name=%s size=%llu path=%s",
                name.c_str(),
                FileSizeForLog(mod),
                mod.c_str());
        }
    }
}

static bool SelectionContainsMainLegacy4J(std::vector<std::wstring> const& selected)
{
    return std::any_of(selected.begin(), selected.end(), [](std::wstring const& mod)
    {
        return IsMainLegacy4JModName(FileNameOnly(mod));
    });
}

static bool SelectionContainsAnyMod(std::vector<std::wstring> const& selected, const wchar_t* const* nameNeedles, size_t nameNeedleCount)
{
    for (size_t i = 0; i < nameNeedleCount; ++i)
    {
        if (SelectionContainsMod(selected, nameNeedles[i]))
        {
            return true;
        }
    }

    return false;
}

static bool AutoStageRequiredMod(
    std::vector<std::wstring> const& available,
    std::vector<std::wstring>& selected,
    const wchar_t* ownerName,
    const wchar_t* dependencyName,
    const wchar_t* const* nameNeedles,
    size_t nameNeedleCount)
{
    if (SelectionContainsAnyMod(selected, nameNeedles, nameNeedleCount))
    {
        WriteLogF(L"MODLOG %s dependency already selected: %s", ownerName, dependencyName);
        return true;
    }

    for (auto const& mod : available)
    {
        if (std::find(selected.begin(), selected.end(), mod) != selected.end())
        {
            continue;
        }

        if (SelectionContainsAnyMod(std::vector<std::wstring>{ mod }, nameNeedles, nameNeedleCount))
        {
            std::wstring modName = FileNameOnly(mod);
            if (AddSelectedModUnique(selected, mod))
            {
                WriteLogF(
                    L"MODLOG %s detected; auto-staged required %s dependency name=%s size=%llu path=%s",
                    ownerName,
                    dependencyName,
                    modName.c_str(),
                    FileSizeForLog(mod),
                    mod.c_str());
            }
            return true;
        }
    }

    WriteLogF(L"MODLOG WARN: %s requires %s, but no matching jar was found in the mod library", ownerName, dependencyName);
    return false;
}

static void ForceControllableGlfwInputLibrary(std::wstring const& gameDir)
{
    std::wstring configPath = gameDir + L"\\config\\controllable-client.toml";
    std::string config;
    ReadSmallTextFileUtf8(configPath, config);

    constexpr const char* desiredLine = "inputLibrary = \"GLFW\"";
    size_t lineStart = 0;
    bool replaced = false;
    while (lineStart <= config.size())
    {
        size_t lineEnd = config.find_first_of("\r\n", lineStart);
        size_t physicalEnd = lineEnd == std::string::npos ? config.size() : lineEnd;
        std::string line = config.substr(lineStart, physicalEnd - lineStart);
        size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 12, "inputLibrary") == 0)
        {
            config.replace(lineStart, physicalEnd - lineStart, desiredLine);
            replaced = true;
            break;
        }

        if (lineEnd == std::string::npos)
        {
            break;
        }

        lineStart = lineEnd + 1;
        if (lineStart < config.size() && config[lineEnd] == '\r' && config[lineStart] == '\n')
        {
            ++lineStart;
        }
    }

    if (!replaced)
    {
        if (!config.empty() && config.back() != '\n')
        {
            config += "\r\n";
        }

        config += desiredLine;
        config += "\r\n";
    }

    if (WriteTextFileUtf8(configPath, config))
    {
        WriteLogF(L"Controllable detected; forced GLFW input library in %s", configPath.c_str());
    }
}

static void ForceForgeSplashDisabled(std::wstring const& gameDir)
{
    std::wstring configPath = gameDir + L"\\config\\splash.properties";
    std::string config =
        "# Written by BBC Launcher.\r\n"
        "# Forge 1.12.2 splash uses a second GL thread/FBO path that is not stable on UWP CoreWindow EGL.\r\n"
        "enabled=false\r\n"
        "rotate=false\r\n"
        "showMemory=false\r\n";

    if (WriteTextFileUtf8(configPath, config))
    {
        WriteLogF(L"Forge profile detected; disabled Forge splash screen in %s", configPath.c_str());
    }
}

static void UpsertTomlScalar(std::string& config, const char* key, const char* value)
{
    std::string desiredLine = std::string(key) + " = " + value;
    size_t pos = 0;
    while (pos < config.size())
    {
        size_t lineEnd = config.find_first_of("\r\n", pos);
        size_t lineLimit = lineEnd == std::string::npos ? config.size() : lineEnd;
        size_t lineStart = pos;
        while (lineStart < lineLimit && (config[lineStart] == ' ' || config[lineStart] == '\t'))
        {
            ++lineStart;
        }

        if (config.compare(lineStart, std::strlen(key), key) == 0)
        {
            size_t afterKey = lineStart + std::strlen(key);
            while (afterKey < lineLimit && (config[afterKey] == ' ' || config[afterKey] == '\t'))
            {
                ++afterKey;
            }
            if (afterKey < lineLimit && config[afterKey] == '=')
            {
                config.replace(pos, lineLimit - pos, desiredLine);
                return;
            }
        }

        if (lineEnd == std::string::npos)
        {
            break;
        }
        pos = lineEnd + 1;
        while (pos < config.size() && (config[pos] == '\r' || config[pos] == '\n'))
        {
            ++pos;
        }
    }

    if (!config.empty() && config.back() != '\n')
    {
        config += "\r\n";
    }
    config += desiredLine;
    config += "\r\n";
}

static void ForceModLauncherEarlyWindowDisabled(std::wstring const& gameDir, std::wstring const& loaderName)
{
    std::wstring configPath = gameDir + L"\\config\\fml.toml";
    std::string config;
    ReadSmallTextFileUtf8(configPath, config);
    if (config.empty())
    {
        config =
            "# Written by BBC Launcher.\r\n"
            "# Forge/NeoForge's early loading window creates a second GL bootstrap path that is unstable on UWP CoreWindow EGL.\r\n";
    }

    UpsertTomlScalar(config, "earlyWindowControl", "false");
    UpsertTomlScalar(config, "earlyWindowProvider", "\"dummyprovider\"");

    if (WriteTextFileUtf8(configPath, config))
    {
        WriteLogF(L"%s profile detected; disabled ModLauncher early window in %s", loaderName.c_str(), configPath.c_str());
    }
}

static bool UpsertJsonNumberProperty(std::string& json, const char* key, const char* value)
{
    std::string property = "\"";
    property += key;
    property += "\"";

    size_t keyPos = json.find(property);
    if (keyPos != std::string::npos)
    {
        size_t colon = json.find(':', keyPos + property.size());
        if (colon == std::string::npos)
        {
            return false;
        }

        size_t valueStart = json.find_first_not_of(" \t\r\n", colon + 1);
        if (valueStart == std::string::npos)
        {
            return false;
        }

        size_t valueEnd = valueStart;
        while (valueEnd < json.size() && json[valueEnd] != ',' && json[valueEnd] != '}')
        {
            ++valueEnd;
        }

        json.replace(valueStart, valueEnd - valueStart, value);
        return true;
    }

    size_t closeBrace = json.find_last_of('}');
    if (closeBrace == std::string::npos)
    {
        json = "{\r\n  ";
        json += property;
        json += ": ";
        json += value;
        json += "\r\n}\r\n";
        return true;
    }

    size_t previous = closeBrace;
    while (previous > 0 && (json[previous - 1] == ' ' || json[previous - 1] == '\t' || json[previous - 1] == '\r' || json[previous - 1] == '\n'))
    {
        --previous;
    }

    bool needsComma = previous > 0 && json[previous - 1] != '{';
    std::string insert = needsComma ? ",\r\n  " : "\r\n  ";
    insert += property;
    insert += ": ";
    insert += value;
    insert += "\r\n";
    json.insert(closeBrace, insert);
    return true;
}

static void ForceLegacy4JGlfwControllerHandler(std::wstring const& gameDir)
{
    std::wstring configDir = gameDir + L"\\config\\legacy";
    EnsureDirectory(configDir);

    std::wstring configPath = configDir + L"\\client_options.json";
    std::string config;
    ReadSmallTextFileUtf8(configPath, config);

    // Legacy4J stores controller handlers by list index: none=0, glfw=1, sdl3=2.
    if (!UpsertJsonNumberProperty(config, "selectedControllerHandler", "1"))
    {
        config = "{\r\n  \"selectedControllerHandler\": 1\r\n}\r\n";
    }

    if (WriteTextFileUtf8(configPath, config))
    {
        WriteLogF(L"Legacy4J detected; forced GLFW controller handler in %s", configPath.c_str());
    }
}

static bool IsCreativeSearchMesaRiskVersion(std::wstring const& version)
{
    return version == L"1.20.1" || version == L"1.21.1";
}

static void ForceSodiumStableFpsConfig(std::wstring const& gameDir, bool mesaSafetyMode)
{
    std::wstring configPath = gameDir + L"\\config\\sodium-options.json";
    const char* noErrorGlContext = mesaSafetyMode ? "false" : "true";
    const char* advancedStagingBuffers = mesaSafetyMode ? "false" : "true";
    const char* cpuRenderAheadLimit = mesaSafetyMode ? "1" : "3";
    const char* terrainSortingEnabled = mesaSafetyMode ? "false" : "true";

    std::string config =
        "{\r\n"
        "  \"quality\": {\r\n"
        "    \"hidden_fluid_culling\": true,\r\n"
        "    \"improved_fluid_shaping\": false,\r\n"
        "    \"use_closest_point_entity_sort\": false,\r\n"
        "    \"pixel_filtering_mode\": \"NEAREST\"\r\n"
        "  },\r\n"
        "  \"performance\": {\r\n"
        "    \"chunk_builder_threads\": 2,\r\n"
        "    \"chunk_build_defer_mode\": \"ALWAYS\",\r\n"
        "    \"animate_only_visible_textures\": true,\r\n"
        "    \"use_entity_culling\": true,\r\n"
        "    \"use_fog_occlusion\": true,\r\n"
        "    \"use_block_face_culling\": true,\r\n"
        "    \"use_no_error_gl_context\": " + std::string(noErrorGlContext) + ",\r\n"
        "    \"quad_splitting_mode\": \"SAFE\"\r\n"
        "  },\r\n"
        "  \"advanced\": {\r\n"
        "    \"enable_memory_tracing\": false,\r\n"
        "    \"use_advanced_staging_buffers\": " + std::string(advancedStagingBuffers) + ",\r\n"
        "    \"cpu_render_ahead_limit\": " + std::string(cpuRenderAheadLimit) + "\r\n"
        "  },\r\n"
        "  \"debug\": {\r\n"
        "    \"terrain_sorting_enabled\": " + std::string(terrainSortingEnabled) + "\r\n"
        "  },\r\n"
        "  \"notifications\": {\r\n"
        "    \"has_cleared_donation_button\": true,\r\n"
        "    \"has_seen_donation_prompt\": true\r\n"
        "  }\r\n"
        "}\r\n";

    if (WriteTextFileUtf8(configPath, config))
    {
        WriteLogF(
            L"Sodium detected; wrote stable FPS config in %s mesaSafetyMode=%d",
            configPath.c_str(),
            mesaSafetyMode ? 1 : 0);
    }
}

static std::string ControlifyGlfwOnlyConfig()
{
    return
        "{\r\n"
        "  \"current_controller\": null,\r\n"
        "  \"controllers\": {},\r\n"
        "  \"global\": {\r\n"
        "    \"virtual_mouse_screens\": [],\r\n"
        "    \"always_keyboard_movement\": false,\r\n"
        "    \"keyboard_movement_whitelist\": [],\r\n"
        "    \"out_of_focus_input\": false,\r\n"
        "    \"load_vibration_natives\": false,\r\n"
        "    \"custom_vibration_natives_path\": \"\",\r\n"
        "    \"vibration_onboarded\": true,\r\n"
        "    \"reach_around\": \"OFF\",\r\n"
        "    \"allow_server_rumble\": false,\r\n"
        "    \"ui_sounds\": false,\r\n"
        "    \"notify_low_battery\": true,\r\n"
        "    \"quiet_mode\": false,\r\n"
        "    \"ingame_button_guide_scale\": 1.0,\r\n"
        "    \"use_enhanced_steam_deck_driver\": false,\r\n"
        "    \"seen_servers\": []\r\n"
        "  }\r\n"
        "}\r\n";
}

static void ForceControlifyGlfwOnlyConfig(std::wstring const& gameDir)
{
    std::wstring configDir = gameDir + L"\\config";
    EnsureDirectory(configDir);

    std::wstring configPath = configDir + L"\\controlify.json";
    std::string config;
    ReadSmallTextFileUtf8(configPath, config);
    if (config.find("\"global\"") == std::string::npos)
    {
        config = ControlifyGlfwOnlyConfig();
    }
    else
    {
        UpsertJsonNumberProperty(config, "load_vibration_natives", "false");
        UpsertJsonNumberProperty(config, "vibration_onboarded", "true");
        UpsertJsonNumberProperty(config, "custom_vibration_natives_path", "\"\"");
        UpsertJsonNumberProperty(config, "allow_server_rumble", "false");
        UpsertJsonNumberProperty(config, "use_enhanced_steam_deck_driver", "false");
    }

    EnsureDirectory(gameDir + L"\\controlify-natives");
    if (WriteTextFileUtf8(configPath, config))
    {
        WriteLogF(L"Controlify detected; forced GLFW-only config and disabled SDL vibration natives in %s", configPath.c_str());
    }
}

static bool HasStagedMod(std::wstring const& gameDir, const wchar_t* nameNeedle)
{
    std::wstring pattern = gameDir + L"\\mods\\*.jar";
    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &data,
        FindExSearchNameMatch,
        nullptr,
        0);
    if (find == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    bool found = false;
    do
    {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            ContainsInsensitive(data.cFileName, nameNeedle))
        {
            found = true;
            break;
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);
    return found;
}

static std::wstring InferBaseMinecraftVersion(std::wstring const& version);
static int ParseMinecraftMinorVersion(std::wstring const& version);

static void PickAndStageModsInteractive(
    CoreWindow const& window,
    std::wstring const& localRoot,
    std::wstring const& profileRoot,
    std::wstring const& gameDir,
    std::wstring const& minecraftVersion,
    std::wstring const& baseMinecraftVersion,
    bool includeBaseVersionModLibrary,
    bool profileLooksModded,
    bool fabricProfile,
    std::wstring const& builtInFabricLanMod)
{
    std::vector<ModFolderProfile> modFolderProfiles;
    CollectModFolderProfiles(localRoot, profileRoot, minecraftVersion, baseMinecraftVersion, includeBaseVersionModLibrary, modFolderProfiles);
    std::wstring selectedModFolderProfile = PickModFolderProfileInteractive(window, localRoot, modFolderProfiles);
    std::vector<std::wstring> selectedModFolderDirectories;
    if (!selectedModFolderProfile.empty())
    {
        if (auto profile = FindModFolderProfile(modFolderProfiles, selectedModFolderProfile))
        {
            selectedModFolderDirectories = profile->directories;
            WriteLogF(
                L"MODLOG selected mod folder profile will isolate picker mods name=%s roots=%zu",
                profile->name.c_str(),
                selectedModFolderDirectories.size());
        }
    }

    std::vector<std::wstring> mods;
    CollectAvailableMods(
        localRoot,
        profileRoot,
        minecraftVersion,
        baseMinecraftVersion,
        includeBaseVersionModLibrary,
        modFolderProfiles,
        selectedModFolderProfile,
        mods);

    std::wstring modsDir = gameDir + L"\\mods";
    EnsureDirectory(modsDir);
    WriteLogF(
        L"MODLOG staging start version=%s includeBaseVersionLibrary=%d profileLooksModded=%d modsDir=%s",
        minecraftVersion.c_str(),
        includeBaseVersionModLibrary ? 1 : 0,
        profileLooksModded ? 1 : 0,
        modsDir.c_str());
    LogModDirectorySnapshot(L"staged-before-clear", modsDir);
    bool clearedModsDir = DeleteDirectoryContentsRecursive(modsDir);
    WriteLogF(L"MODLOG cleared staged mods dir=%s result=%d", modsDir.c_str(), clearedModsDir ? 1 : 0);
    LogModDirectorySnapshot(L"staged-after-clear", modsDir);

    std::vector<std::wstring> builtInFabricSupportMods;
    std::wstring builtInFabricLanCompatVersion = baseMinecraftVersion.empty()
        ? InferBaseMinecraftVersion(minecraftVersion)
        : baseMinecraftVersion;
    bool builtInFabricLanCompatible =
        fabricProfile && ParseMinecraftMinorVersion(builtInFabricLanCompatVersion) >= 21;
    if (builtInFabricLanCompatible && FileExists(builtInFabricLanMod))
    {
        builtInFabricSupportMods.push_back(builtInFabricLanMod);
        WriteLogF(
            L"MODLOG built-in Fabric support available name=%s path=%s",
            FileNameOnly(builtInFabricLanMod).c_str(),
            builtInFabricLanMod.c_str());
    }
    else if (fabricProfile && !builtInFabricLanCompatible)
    {
        WriteLogF(
            L"MODLOG built-in Fabric LAN discovery skipped for Minecraft %s; bundled jar requires Minecraft 1.21+",
            builtInFabricLanCompatVersion.empty() ? L"<unknown>" : builtInFabricLanCompatVersion.c_str());
    }
    else if (fabricProfile)
    {
        WriteLogF(
            L"MODLOG built-in Fabric LAN discovery mod missing path=%s",
            builtInFabricLanMod.empty() ? L"<empty>" : builtInFabricLanMod.c_str());
    }

    if (mods.empty() && builtInFabricSupportMods.empty())
    {
        WriteLogF(
            L"MODLOG no mods available for profile %s. Put loose jars in LocalState\\mods-library\\common or LocalState\\mods-library\\%s, or create a mod folder profile under LocalState\\mods-library.",
            minecraftVersion.c_str(),
            minecraftVersion.c_str());
        return;
    }
    if (mods.empty())
    {
        WriteLogF(
            L"MODLOG no user mods available for profile %s; staging built-in support only",
            minecraftVersion.c_str());
    }

    if (!profileLooksModded)
    {
        WriteLogW(L"Mods are available, but the selected profile appears to be vanilla. A Fabric/Forge/Quilt profile is still required for mods to load.");
    }

    std::vector<std::wstring> pickerMods;
    for (auto const& mod : mods)
    {
        std::wstring name = FileNameOnly(mod);
        if (fabricProfile && IsCoreFabricSupportModName(name))
        {
            WriteLogF(L"MODLOG hidden from picker core Fabric support name=%s path=%s", name.c_str(), mod.c_str());
            continue;
        }
        if (!selectedModFolderDirectories.empty() && !IsPathUnderAnyDirectory(mod, selectedModFolderDirectories))
        {
            WriteLogF(L"MODLOG hidden from picker outside selected mod folder profile name=%s path=%s", name.c_str(), mod.c_str());
            continue;
        }

        pickerMods.push_back(mod);
    }
    LogModJarList(L"picker-visible", pickerMods);

    enum class ModChoice
    {
        None,
        Pick,
        All,
    };

    std::vector<std::wstring> staged;
    for (auto const& mod : builtInFabricSupportMods)
    {
        if (AddSelectedModUnique(staged, mod))
        {
            WriteLogF(
                L"MODLOG auto-staged built-in Fabric support name=%s size=%llu path=%s",
                FileNameOnly(mod).c_str(),
                FileSizeForLog(mod),
                mod.c_str());
        }
    }
    if (fabricProfile)
    {
        AutoStageCoreFabricSupportMods(mods, staged, builtInFabricLanCompatible);
    }

    ModChoice choice = ModChoice::None;
    std::wstring choiceLabel = pickerMods.empty() ? L"Only core Fabric support" : L"No mods";
    if (pickerMods.empty())
    {
        WriteLogW(L"MODLOG no user-selectable mods; staging only core Fabric support mods if present");
    }
    else
    {
        try
        {
            MessageDialog dialog(L"Choose mods to stage before launch.");
            dialog.Commands().Append(UICommand(L"No mods"));
            dialog.Commands().Append(UICommand(L"Pick mods"));
            dialog.Commands().Append(UICommand(L"All mods"));
            dialog.DefaultCommandIndex(0);

            IUICommand selection = ShowDialogAndPump(window, dialog);
            std::wstring label = selection ? selection.Label().c_str() : std::wstring();
            choiceLabel = label.empty() ? L"<empty>" : label;
            if (label == L"All mods")
            {
                choice = ModChoice::All;
            }
            else if (label == L"Pick mods")
            {
                choice = ModChoice::Pick;
            }
        }
        catch (hresult_error const& ex)
        {
            WriteLogF(L"Mod picker failed hr=0x%08X; launching with core support mods only", static_cast<unsigned int>(ex.code()));
        }
        catch (...)
        {
            WriteLogW(L"Mod picker failed with unknown exception; launching with core support mods only");
        }
    }

    WriteLogF(
        L"MODLOG picker choice=%s available=%zu visible=%zu coreSupportPreStaged=%zu profileLooksModded=%d fabricProfile=%d",
        choiceLabel.c_str(),
        mods.size(),
        pickerMods.size(),
        staged.size(),
        profileLooksModded ? 1 : 0,
        fabricProfile ? 1 : 0);

    if (choice == ModChoice::All)
    {
        for (auto const& mod : pickerMods)
        {
            AddSelectedModUnique(staged, mod);
        }
        WriteLogF(L"MODLOG picker selected all visible mods visibleCount=%zu totalStaged=%zu", pickerMods.size(), staged.size());
    }
    else if (choice == ModChoice::Pick)
    {
        for (auto const& mod : pickerMods)
        {
            try
            {
                std::wstring prompt = L"Enable this mod?\r\n" + FileNameOnly(mod);
                MessageDialog dialog{ hstring(prompt) };
                dialog.Commands().Append(UICommand(L"Yes"));
                dialog.Commands().Append(UICommand(L"No"));
                dialog.DefaultCommandIndex(1);
                IUICommand selection = ShowDialogAndPump(window, dialog);
                std::wstring label = selection ? selection.Label().c_str() : std::wstring();
                if (label == L"Yes")
                {
                    AddSelectedModUnique(staged, mod);
                    WriteLogF(
                        L"MODLOG picker selected mod name=%s size=%llu path=%s",
                        FileNameOnly(mod).c_str(),
                        FileSizeForLog(mod),
                        mod.c_str());
                }
                else
                {
                    WriteLogF(
                        L"MODLOG picker skipped mod name=%s response=%s path=%s",
                        FileNameOnly(mod).c_str(),
                        label.empty() ? L"<empty>" : label.c_str(),
                        mod.c_str());
                }
            }
            catch (...)
            {
                WriteLogW(L"Per-mod picker failed; keeping the selections made so far");
                break;
            }
        }
    }

    LogModJarList(L"selected-before-dependencies", staged);

    if (SelectionContainsMainLegacy4J(staged))
    {
        const wchar_t* const fabricApiNeedles[] = { L"fabric-api" };
        const wchar_t* const factoryApiNeedles[] = { L"factoryapi", L"factory_api", L"factory-api" };
        AutoStageRequiredMod(mods, staged, L"Legacy4J", L"Fabric API", fabricApiNeedles, _countof(fabricApiNeedles));
        AutoStageRequiredMod(mods, staged, L"Legacy4J", L"Factory API", factoryApiNeedles, _countof(factoryApiNeedles));
    }

    LogModJarList(L"selected-final-before-copy", staged);

    for (auto const& mod : staged)
    {
        CopySelectedMod(mod, modsDir);
    }

    LogModDirectorySnapshot(L"staged-final", modsDir);

    bool controllableStaged = std::any_of(staged.begin(), staged.end(), [](std::wstring const& mod)
    {
        return ContainsInsensitive(FileNameOnly(mod), L"controllable");
    });
    if (controllableStaged)
    {
        ForceControllableGlfwInputLibrary(gameDir);
    }
    if (SelectionContainsMainLegacy4J(staged))
    {
        ForceLegacy4JGlfwControllerHandler(gameDir);
    }
    bool sodiumStaged = std::any_of(staged.begin(), staged.end(), [](std::wstring const& mod)
    {
        return ContainsInsensitive(FileNameOnly(mod), L"sodium");
    });
    if (sodiumStaged)
    {
        ForceSodiumStableFpsConfig(gameDir, IsCreativeSearchMesaRiskVersion(baseMinecraftVersion));
    }

    std::string selected;
    for (auto const& mod : staged)
    {
        selected += WideToUtf8(FileNameOnly(mod));
        selected += "\r\n";
    }
    std::wstring selectedModsPath = gameDir + L"\\selected-mods.txt";
    WriteTextFileUtf8(selectedModsPath, selected);
    WriteLogF(L"MODLOG selected mods manifest=%s bytes=%zu", selectedModsPath.c_str(), selected.size());
    WriteLogF(L"MODLOG mods staged for launch count=%zu", staged.size());
}

static std::vector<std::wstring> BuildMinecraftClasspath(std::wstring const& localRoot)
{
    std::vector<std::wstring> entries;
    std::string classpathText;
    if (ReadSmallTextFileUtf8(localRoot + L"\\minecraft-classpath.txt", classpathText))
    {
        size_t pos = 0;
        while (pos < classpathText.size())
        {
            size_t end = classpathText.find_first_of("\r\n", pos);
            std::string line = classpathText.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
            {
                line.erase(0, 3);
            }
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
            {
                line.pop_back();
            }

            size_t first = line.find_first_not_of(" \t");
            if (first != std::string::npos)
            {
                std::wstring path = Utf8ToWide(line.substr(first).c_str());
                if (!path.empty() && path[0] == static_cast<wchar_t>(0xFEFF))
                {
                    path.erase(path.begin());
                }
                bool rooted = path.size() >= 2 && path[1] == L':';
                std::wstring fullPath = rooted ? path : localRoot + L"\\" + path;
                if (FileExists(fullPath))
                {
                    entries.push_back(fullPath);
                }
                else
                {
                    WriteLogF(L"WARN: classpath entry missing: %s", fullPath.c_str());
                }
            }

            if (end == std::string::npos)
            {
                break;
            }
            pos = end + 1;
            while (pos < classpathText.size() && (classpathText[pos] == '\r' || classpathText[pos] == '\n'))
            {
                ++pos;
            }
        }

        WriteLogF(L"Minecraft exact classpath entries from minecraft-classpath.txt=%zu", entries.size());
        return entries;
    }

    std::wstring clientJar = localRoot + L"\\client.jar";
    if (FileExists(clientJar))
    {
        entries.push_back(clientJar);
    }

    std::wstring librariesDir = localRoot + L"\\libraries";
    if (DirectoryExists(librariesDir))
    {
        CollectJarFilesRecursive(librariesDir, entries);
    }

    return entries;
}

static std::wstring TrimWhitespace(std::wstring value)
{
    while (!value.empty() && (value.back() == L' ' || value.back() == L'\t' || value.back() == L'\r' || value.back() == L'\n'))
    {
        value.pop_back();
    }
    size_t first = 0;
    while (first < value.size() && (value[first] == L' ' || value[first] == L'\t' || value[first] == L'\r' || value[first] == L'\n'))
    {
        ++first;
    }
    if (first > 0)
    {
        value.erase(0, first);
    }
    return value;
}

static std::wstring ReadGamepadMouseSettingFile(
    std::wstring const& localRoot,
    const wchar_t* fileName,
    const wchar_t* fallback)
{
    std::wstring path = localRoot + L"\\" + fileName;
    std::string bytes;
    if (ReadSmallTextFileUtf8(path, bytes))
    {
        std::wstring value = TrimWhitespace(Utf8ToWide(bytes.c_str()));
        if (!value.empty())
        {
            return value;
        }
    }

    WriteTextFileUtf8(path, WideToUtf8(fallback) + "\r\n");
    return fallback;
}

static void ApplyGamepadMouseSettings(std::wstring const& localRoot)
{
    std::wstring speed = ReadGamepadMouseSettingFile(localRoot, L"gamepad-mouse-speed.txt", L"0.38");
    std::wstring deadzone = ReadGamepadMouseSettingFile(localRoot, L"gamepad-mouse-deadzone.txt", L"0.18");
    std::wstring rightStick = ReadGamepadMouseSettingFile(localRoot, L"gamepad-mouse-right-stick.txt", L"0.45");
    std::wstring stickDeadzone = ReadGamepadMouseSettingFile(localRoot, L"gamepad-stick-deadzone.txt", L"0.22");

    SetEnvironmentVariableW(L"MINECRAFT_XBOX_GAMEPAD_MOUSE_SPEED", speed.c_str());
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_GAMEPAD_MOUSE_DEADZONE", deadzone.c_str());
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_GAMEPAD_MOUSE_RIGHT_STICK", rightStick.c_str());
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_GAMEPAD_STICK_DEADZONE", stickDeadzone.c_str());
    WriteLogF(
        L"Gamepad settings mouseSpeed=%s mouseDeadzone=%s rightStick=%s stickDeadzone=%s",
        speed.c_str(),
        deadzone.c_str(),
        rightStick.c_str(),
        stickDeadzone.c_str());
}

static bool IsRootedWindowsPath(std::wstring const& path)
{
    return (path.size() >= 2 && path[1] == L':') ||
        (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\');
}

static std::wstring ResolveProfileRelativePath(std::wstring const& profileRoot, std::wstring const& path)
{
    if (IsRootedWindowsPath(path))
    {
        return NormalizeWin32Path(path);
    }

    return NormalizeWin32Path(profileRoot + L"\\" + path);
}

static std::wstring ResolveProfileJavaRuntimeRoot(std::wstring const& profileRoot)
{
    std::wstring profileRuntime = profileRoot + L"\\runtime\\jre";
    if (DirectoryExists(profileRuntime))
    {
        return profileRuntime;
    }

    std::string sharedRuntimeBytes;
    if (!ReadSmallTextFileUtf8(profileRoot + L"\\shared-runtime.txt", sharedRuntimeBytes))
    {
        return profileRuntime;
    }

    std::wstring sharedRuntime = TrimWhitespace(Utf8ToWide(sharedRuntimeBytes.c_str()));
    if (sharedRuntime.empty())
    {
        return profileRuntime;
    }

    std::wstring resolved = ResolveProfileRelativePath(profileRoot, sharedRuntime);
    if (DirectoryExists(resolved))
    {
        WriteLogF(L"Using shared Java runtime for profile: %s", resolved.c_str());
        return resolved;
    }

    WriteLogF(L"WARN: shared Java runtime marker exists but target is missing: %s", resolved.c_str());
    return profileRuntime;
}

static void ReplaceAllInPlace(std::string& value, std::string const& from, std::string const& to)
{
    if (from.empty())
    {
        return;
    }

    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos)
    {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::vector<std::string> ReadArgumentLines(std::wstring const& path)
{
    std::vector<std::string> args;
    std::string text;
    if (!ReadSmallTextFileUtf8(path, text))
    {
        return args;
    }

    size_t pos = 0;
    while (pos < text.size())
    {
        size_t end = text.find_first_of("\r\n", pos);
        std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF)
        {
            line.erase(0, 3);
        }
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
        {
            line.pop_back();
        }

        size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos)
        {
            args.push_back(line.substr(first));
        }

        if (end == std::string::npos)
        {
            break;
        }
        pos = end + 1;
        while (pos < text.size() && (text[pos] == '\r' || text[pos] == '\n'))
        {
            ++pos;
        }
    }

    return args;
}

static bool TryReadJsonString(std::string const& json, char const* key, std::wstring& value)
{
    std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos)
    {
        return false;
    }

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos)
    {
        return false;
    }

    pos = json.find('"', pos + 1);
    if (pos == std::string::npos)
    {
        return false;
    }

    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos)
    {
        return false;
    }

    value = Utf8ToWide(json.substr(pos + 1, end - pos - 1).c_str());
    return !value.empty();
}

static bool TryReadJsonInt(std::string const& json, char const* key, int& value)
{
    std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos)
    {
        return false;
    }

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos)
    {
        return false;
    }

    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n'))
    {
        ++pos;
    }

    size_t end = pos;
    while (end < json.size() && json[end] >= '0' && json[end] <= '9')
    {
        ++end;
    }

    if (end == pos)
    {
        return false;
    }

    value = std::atoi(json.substr(pos, end - pos).c_str());
    return true;
}

static IUICommand ShowDialogAndPump(CoreWindow const& window, MessageDialog& dialog)
{
    auto dispatcher = window.Dispatcher();
    IAsyncOperation<IUICommand> operation = dialog.ShowAsync();
    while (operation.Status() == AsyncStatus::Started)
    {
        dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        Sleep(16);
    }

    return operation.get();
}

struct MinecraftAuthSession
{
    std::wstring username = L"TestPlayer";
    std::wstring uuid = L"00000000000000000000000000000000";
    std::wstring accessToken = L"0";
    std::wstring xuid = L"0";
    std::wstring clientId;
    bool online = false;
};

struct XboxAuthToken
{
    std::wstring token;
    std::wstring userHash;
    std::wstring xuid;
};

static void SaveAuthSession(std::wstring const& localRoot, MinecraftAuthSession const& session);

static std::wstring NormalizeMinecraftUuid(std::wstring uuid)
{
    uuid.erase(std::remove(uuid.begin(), uuid.end(), L'-'), uuid.end());
    return uuid;
}

static constexpr wchar_t kMinecraftLauncherTestRedirectUri[] = L"https://login.live.com/oauth20_desktop.srf";

static std::wstring ResolveMicrosoftClientId(std::wstring const& localRoot = L"")
{
    std::wstring fromEnv = GetEnvironmentVariableString(L"MINECRAFT_XBOX_MICROSOFT_CLIENT_ID");
    if (!fromEnv.empty())
    {
        return TrimWhitespace(fromEnv);
    }

    if (!localRoot.empty())
    {
        std::string text;
        if (ReadSmallTextFileUtf8(localRoot + L"\\microsoft-client-id.txt", text))
        {
            return TrimWhitespace(Utf8ToWide(text.c_str()));
        }
    }

    return L"";
}

static std::wstring AuthSessionPath(std::wstring const& localRoot)
{
    return localRoot + L"\\minecraft-auth-session.json";
}

static std::wstring ProtectedAuthSessionPath(std::wstring const& localRoot)
{
    return localRoot + L"\\minecraft-auth-session.protected";
}

static bool ProtectAuthSessionJson(std::string const& json, std::string& protectedText)
{
    try
    {
        IBuffer plainBuffer = CryptographicBuffer::ConvertStringToBinary(
            hstring(Utf8ToWide(json.c_str())),
            BinaryStringEncoding::Utf8);
        DataProtectionProvider provider(L"LOCAL=user");
        IBuffer protectedBuffer = provider.ProtectAsync(plainBuffer).get();
        std::wstring encoded = CryptographicBuffer::EncodeToBase64String(protectedBuffer).c_str();
        protectedText = "BBC-LAUNCHER-AUTH-V1\r\n" + WideToUtf8(encoded);
        return true;
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"AUTH token protection failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
    }
    catch (...)
    {
        WriteLogW(L"AUTH token protection failed with unknown exception");
    }

    protectedText.clear();
    return false;
}

static bool UnprotectAuthSessionJson(std::string const& protectedText, std::string& json)
{
    json.clear();
    try
    {
        std::wstring encoded = TrimWhitespace(Utf8ToWide(protectedText.c_str()));
        std::wstring const prefix = L"BBC-LAUNCHER-AUTH-V1";
        if (encoded.rfind(prefix, 0) == 0)
        {
            encoded.erase(0, prefix.size());
            encoded = TrimWhitespace(encoded);
        }
        if (encoded.empty())
        {
            return false;
        }

        IBuffer protectedBuffer = CryptographicBuffer::DecodeFromBase64String(hstring(encoded));
        DataProtectionProvider provider;
        IBuffer plainBuffer = provider.UnprotectAsync(protectedBuffer).get();
        std::wstring plain = CryptographicBuffer::ConvertBinaryToString(
            BinaryStringEncoding::Utf8,
            plainBuffer).c_str();
        json = WideToUtf8(plain);
        return !json.empty();
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"AUTH token unprotect failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
    }
    catch (...)
    {
        WriteLogW(L"AUTH token unprotect failed with unknown exception");
    }

    return false;
}

static bool TryParseAuthSessionJson(std::string const& json, MinecraftAuthSession& session)
{
    std::wstring username;
    std::wstring uuid;
    std::wstring accessToken;
    if (!TryReadJsonString(json, "username", username) ||
        !TryReadJsonString(json, "uuid", uuid) ||
        !TryReadJsonString(json, "accessToken", accessToken) ||
        username.empty() ||
        uuid.empty() ||
        accessToken.empty())
    {
        return false;
    }

    session.username = username;
    session.uuid = NormalizeMinecraftUuid(uuid);
    session.accessToken = accessToken;
    TryReadJsonString(json, "xuid", session.xuid);
    TryReadJsonString(json, "clientId", session.clientId);
    if (session.xuid.empty())
    {
        session.xuid = L"0";
    }
    if (session.clientId.empty())
    {
        session.clientId = L"";
    }
    session.online = true;
    return true;
}

static bool TryLoadCachedAuthSession(std::wstring const& localRoot, MinecraftAuthSession& session)
{
    if (FileExists(localRoot + L"\\force-account-signin"))
    {
        WriteLogW(L"AUTH force-account-signin present; cached session ignored");
        DeleteFileW((localRoot + L"\\force-account-signin").c_str());
        DeleteFileW(AuthSessionPath(localRoot).c_str());
        DeleteFileW(ProtectedAuthSessionPath(localRoot).c_str());
        return false;
    }

    std::string protectedText;
    std::string json;
    if (ReadSmallTextFileUtf8(ProtectedAuthSessionPath(localRoot), protectedText) &&
        UnprotectAuthSessionJson(protectedText, json) &&
        TryParseAuthSessionJson(json, session))
    {
        WriteLogF(L"AUTH using protected cached Minecraft session username=%s uuid=%s", session.username.c_str(), session.uuid.c_str());
        if (session.clientId.empty())
        {
            session.clientId = ResolveMicrosoftClientId(localRoot);
        }
        return true;
    }

    if (!ReadSmallTextFileUtf8(AuthSessionPath(localRoot), json))
    {
        return false;
    }
    if (!TryParseAuthSessionJson(json, session))
    {
        WriteLogW(L"AUTH cached session file is incomplete; sign-in required");
        return false;
    }

    WriteLogF(L"AUTH migrated legacy plaintext Minecraft session username=%s uuid=%s", session.username.c_str(), session.uuid.c_str());
    if (session.clientId.empty())
    {
        session.clientId = ResolveMicrosoftClientId(localRoot);
    }
    SaveAuthSession(localRoot, session);
    return true;
}

static void SaveAuthSession(std::wstring const& localRoot, MinecraftAuthSession const& session)
{
    std::string json =
        "{\r\n"
        "  \"username\":\"" + JsonEscape(WideToUtf8(session.username)) + "\",\r\n"
        "  \"uuid\":\"" + JsonEscape(WideToUtf8(session.uuid)) + "\",\r\n"
        "  \"accessToken\":\"" + JsonEscape(WideToUtf8(session.accessToken)) + "\",\r\n"
        "  \"xuid\":\"" + JsonEscape(WideToUtf8(session.xuid)) + "\",\r\n"
        "  \"clientId\":\"" + JsonEscape(WideToUtf8(session.clientId)) + "\"\r\n"
        "}\r\n";
    std::string protectedText;
    if (!ProtectAuthSessionJson(json, protectedText))
    {
        WriteLogW(L"AUTH protected session cache not saved; refusing plaintext token cache");
        return;
    }

    if (WriteTextFileUtf8(ProtectedAuthSessionPath(localRoot), protectedText + "\r\n"))
    {
        DeleteFileW(AuthSessionPath(localRoot).c_str());
        WriteLogW(L"AUTH protected Minecraft session saved");
    }
    else
    {
        WriteLogW(L"AUTH protected session cache write failed; refusing plaintext token cache");
    }
}

static bool TryReadXboxToken(JsonObject const& json, XboxAuthToken& token)
{
    TryGetJsonString(json, L"Token", token.token);
    if (token.token.empty())
    {
        return false;
    }

    try
    {
        JsonObject displayClaims = json.GetNamedObject(L"DisplayClaims");
        JsonArray xui = displayClaims.GetNamedArray(L"xui");
        if (xui.Size() > 0)
        {
            JsonObject first = xui.GetObjectAt(0);
            TryGetJsonString(first, L"uhs", token.userHash);
            TryGetJsonString(first, L"xid", token.xuid);
        }
    }
    catch (...)
    {
    }

    return true;
}

static bool AuthenticateXboxLive(
    std::wstring const& microsoftAccessToken,
    bool prefixRpsTicket,
    XboxAuthToken& token,
    std::wstring& error)
{
    std::wstring rpsTicket = prefixRpsTicket ? (L"d=" + microsoftAccessToken) : microsoftAccessToken;
    std::string body =
        "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\",\"RpsTicket\":\"" +
        JsonEscape(WideToUtf8(rpsTicket)) +
        "\"},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}";

    JsonObject json;
    if (!HttpPostJson(L"https://user.auth.xboxlive.com/user/authenticate", body, json, error))
    {
        return false;
    }
    return TryReadXboxToken(json, token);
}

static bool AuthorizeXsts(std::wstring const& xboxToken, XboxAuthToken& token, std::wstring& error)
{
    std::string body =
        "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"" +
        JsonEscape(WideToUtf8(xboxToken)) +
        "\"]},\"RelyingParty\":\"rp://api.minecraftservices.com/\",\"TokenType\":\"JWT\"}";

    JsonObject json;
    if (!HttpPostJson(L"https://xsts.auth.xboxlive.com/xsts/authorize", body, json, error))
    {
        return false;
    }
    return TryReadXboxToken(json, token);
}

static bool LoginWithMinecraftServices(
    std::wstring const& userHash,
    std::wstring const& xstsToken,
    std::wstring& minecraftAccessToken,
    std::wstring& error)
{
    std::wstring identityToken = L"XBL3.0 x=" + userHash + L";" + xstsToken;
    std::string body = "{\"identityToken\":\"" + JsonEscape(WideToUtf8(identityToken)) + "\"}";
    JsonObject json;
    if (!HttpPostJson(L"https://api.minecraftservices.com/authentication/login_with_xbox", body, json, error))
    {
        return false;
    }
    return TryGetJsonString(json, L"access_token", minecraftAccessToken);
}

static bool VerifyMinecraftEntitlement(std::wstring const& minecraftAccessToken, std::wstring& error)
{
    JsonObject json;
    if (!HttpGetJsonWithBearer(L"https://api.minecraftservices.com/entitlements/mcstore", minecraftAccessToken, json, error))
    {
        return false;
    }

    try
    {
        JsonArray items = json.GetNamedArray(L"items");
        for (uint32_t i = 0; i < items.Size(); ++i)
        {
            JsonObject item = items.GetObjectAt(i);
            std::wstring name;
            if (TryGetJsonString(item, L"name", name))
            {
                std::string lower = ToLowerAscii(WideToUtf8(name));
                if (lower == "game_minecraft" || lower == "product_minecraft")
                {
                    return true;
                }
            }
        }
    }
    catch (...)
    {
    }

    error = L"Minecraft Java ownership was not found on this account";
    return false;
}

static bool ReadMinecraftProfile(
    std::wstring const& minecraftAccessToken,
    std::wstring& username,
    std::wstring& uuid,
    std::wstring& error)
{
    JsonObject json;
    if (!HttpGetJsonWithBearer(L"https://api.minecraftservices.com/minecraft/profile", minecraftAccessToken, json, error))
    {
        return false;
    }

    return TryGetJsonString(json, L"name", username) &&
        TryGetJsonString(json, L"id", uuid) &&
        !username.empty() &&
        !uuid.empty();
}

static bool AcquireMicrosoftDeviceCodeToken(
    CoreWindow const& window,
    std::wstring const& clientId,
    std::wstring& microsoftAccessToken,
    std::wstring& error)
{
    std::string requestBody =
        "client_id=" + FormUrlEncode(clientId) +
        "&scope=" + FormUrlEncode(L"XboxLive.signin offline_access");

    JsonObject deviceJson;
    int statusCode = 0;
    if (!HttpPostForm(
        L"https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode",
        requestBody,
        deviceJson,
        error,
        statusCode))
    {
        return false;
    }

    std::wstring deviceCode;
    std::wstring userCode;
    std::wstring verificationUri;
    std::wstring message;
    TryGetJsonString(deviceJson, L"device_code", deviceCode);
    TryGetJsonString(deviceJson, L"user_code", userCode);
    TryGetJsonString(deviceJson, L"verification_uri", verificationUri);
    TryGetJsonString(deviceJson, L"message", message);

    unsigned long long interval = 5;
    unsigned long long expiresIn = 900;
    TryGetJsonUInt64(deviceJson, L"interval", interval);
    TryGetJsonUInt64(deviceJson, L"expires_in", expiresIn);
    if (deviceCode.empty())
    {
        error = L"Microsoft device-code response did not include a device_code";
        return false;
    }

    std::wstring prompt = L"Sign in to Microsoft for Minecraft Java.\r\n\r\n";
    if (!message.empty())
    {
        prompt += message;
    }
    else
    {
        prompt += L"Go to " + verificationUri + L" and enter code " + userCode + L".";
    }
    prompt += L"\r\n\r\nPress OK after you finish sign-in.";
    MessageDialog dialog(hstring(prompt.c_str()));
    ShowDialogAndPump(window, dialog);

    std::string tokenBody =
        "grant_type=" + FormUrlEncode(L"urn:ietf:params:oauth:grant-type:device_code") +
        "&client_id=" + FormUrlEncode(clientId) +
        "&device_code=" + FormUrlEncode(deviceCode);

    unsigned long long waited = 0;
    while (waited <= expiresIn)
    {
        JsonObject tokenJson;
        std::wstring tokenError;
        int tokenStatus = 0;
        if (HttpPostForm(
            L"https://login.microsoftonline.com/consumers/oauth2/v2.0/token",
            tokenBody,
            tokenJson,
            tokenError,
            tokenStatus))
        {
            if (TryGetJsonString(tokenJson, L"access_token", microsoftAccessToken) && !microsoftAccessToken.empty())
            {
                return true;
            }
            error = L"Microsoft token response did not include access_token";
            return false;
        }

        if (tokenError == L"authorization_pending")
        {
            Sleep(static_cast<DWORD>(interval * 1000ull));
            waited += interval;
            continue;
        }
        if (tokenError == L"slow_down")
        {
            interval += 5;
            Sleep(static_cast<DWORD>(interval * 1000ull));
            waited += interval;
            continue;
        }

        error = tokenError.empty() ? L"Microsoft token polling failed" : tokenError;
        return false;
    }

    error = L"Microsoft device-code sign-in expired";
    return false;
}

static std::string HtmlEscape(std::string const& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 32);
    for (char ch : value)
    {
        switch (ch)
        {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

static bool ExchangeMicrosoftLiveOAuthCode(
    std::wstring const& clientId,
    std::wstring const& code,
    std::wstring& microsoftAccessToken,
    std::wstring& error)
{
    std::string tokenUrl =
        "https://login.live.com/oauth20_token.srf"
        "?client_id=" + FormUrlEncode(clientId) +
        "&code=" + FormUrlEncode(code) +
        "&redirect_uri=" + FormUrlEncode(kMinecraftLauncherTestRedirectUri) +
        "&grant_type=authorization_code"
        "&scope=" + FormUrlEncode(L"service::user.auth.xboxlive.com::MBI_SSL");

    JsonObject tokenJson;
    if (!HttpGetJson(Utf8ToWide(tokenUrl.c_str()), tokenJson, error))
    {
        return false;
    }

    if (!TryGetJsonString(tokenJson, L"access_token", microsoftAccessToken) || microsoftAccessToken.empty())
    {
        error = L"Microsoft token response did not include access_token";
        return false;
    }

    return true;
}

static std::wstring GetAuthBridgeIpv4Address()
{
    ULONG bufferSize = 16 * 1024;
    std::vector<unsigned char> buffer(bufferSize);
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        auto adapterAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, adapterAddresses, &bufferSize);
        if (result == ERROR_BUFFER_OVERFLOW)
        {
            buffer.resize(bufferSize);
            continue;
        }
        if (result != NO_ERROR)
        {
            break;
        }

        for (auto adapter = adapterAddresses; adapter != nullptr; adapter = adapter->Next)
        {
            if (adapter->OperStatus != IfOperStatusUp)
            {
                continue;
            }

            for (auto unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next)
            {
                if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET)
                {
                    continue;
                }

                auto sockaddr = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
                uint32_t host = ntohl(sockaddr->sin_addr.s_addr);
                if (host == 0 || (host >> 24) == 127)
                {
                    continue;
                }

                wchar_t text[32]{};
                swprintf_s(
                    text,
                    L"%u.%u.%u.%u",
                    static_cast<unsigned int>((host >> 24) & 0xFF),
                    static_cast<unsigned int>((host >> 16) & 0xFF),
                    static_cast<unsigned int>((host >> 8) & 0xFF),
                    static_cast<unsigned int>(host & 0xFF));
                return text;
            }
        }
        break;
    }

    return L"127.0.0.1";
}

static bool ExtractSubmittedOAuthCode(std::wstring const& submitted, std::wstring& code)
{
    code.clear();
    std::wstring value = submitted;
    while (!value.empty() && (value.back() == L'\r' || value.back() == L'\n' || value.back() == L' '))
    {
        value.pop_back();
    }
    size_t first = value.find_first_not_of(L" \t\r\n");
    if (first != std::wstring::npos && first > 0)
    {
        value.erase(0, first);
    }

    std::wstring nestedCode;
    if (TryReadQueryParameter(value, L"code", nestedCode) && !nestedCode.empty())
    {
        code = nestedCode;
        return true;
    }

    if (value.find(L"://") == std::wstring::npos &&
        value.find(L" ") == std::wstring::npos &&
        value.size() > 16)
    {
        code = value;
        return true;
    }

    return false;
}

static bool TryReadOAuthCodeFromHttpRequest(std::string const& request, std::wstring& code)
{
    code.clear();
    size_t firstSpace = request.find(' ');
    if (firstSpace == std::string::npos)
    {
        return false;
    }
    size_t secondSpace = request.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos)
    {
        return false;
    }

    std::string path = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    std::wstring widePath = Utf8ToWide(path.c_str());
    std::wstring submitted;
    if (!TryReadQueryParameter(widePath, L"code", submitted) || submitted.empty())
    {
        return false;
    }

    return ExtractSubmittedOAuthCode(submitted, code);
}

static void SendAuthBridgeResponse(SOCKET client, std::string const& body)
{
    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n" +
        body;
    send(client, response.c_str(), static_cast<int>(response.size()), 0);
}

static bool AcquireMicrosoftLiveOAuthTokenManual(
    CoreWindow const& window,
    std::wstring const& clientId,
    std::wstring const& authorizeUrl,
    std::wstring& microsoftAccessToken,
    std::wstring& error)
{
    WSADATA data = {};
    int startup = WSAStartup(MAKEWORD(2, 2), &data);
    if (startup != 0)
    {
        error = L"Auth bridge WSAStartup failed " + std::to_wstring(startup);
        return false;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET)
    {
        error = L"Auth bridge socket failed";
        WSACleanup();
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in bindAddress{};
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddress.sin_port = 0;
    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&bindAddress), sizeof(bindAddress)) == SOCKET_ERROR)
    {
        error = L"Auth bridge bind failed WSA=" + std::to_wstring(WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return false;
    }

    if (listen(listenSocket, 4) == SOCKET_ERROR)
    {
        error = L"Auth bridge listen failed WSA=" + std::to_wstring(WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return false;
    }

    sockaddr_in actualAddress{};
    int actualLength = sizeof(actualAddress);
    if (getsockname(listenSocket, reinterpret_cast<sockaddr*>(&actualAddress), &actualLength) == SOCKET_ERROR)
    {
        error = L"Auth bridge getsockname failed WSA=" + std::to_wstring(WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return false;
    }

    u_long nonblocking = 1;
    ioctlsocket(listenSocket, FIONBIO, &nonblocking);

    unsigned short port = ntohs(actualAddress.sin_port);
    std::wstring host = GetAuthBridgeIpv4Address();
    std::wstring bridgeUrl = L"http://" + host + L":" + std::to_wstring(port) + L"/";
    std::string bridgeUrlUtf8 = WideToUtf8(bridgeUrl);
    std::string authorizeUrlUtf8 = WideToUtf8(authorizeUrl);
    std::atomic<bool> stop{ false };
    std::atomic<bool> received{ false };
    std::wstring submittedCode;

    std::thread server([&]()
    {
        while (!stop.load(std::memory_order_acquire) && !received.load(std::memory_order_acquire))
        {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(listenSocket, &readSet);
            timeval timeout{};
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            int ready = select(0, &readSet, nullptr, nullptr, &timeout);
            if (ready <= 0)
            {
                continue;
            }

            SOCKET client = accept(listenSocket, nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                continue;
            }

            char buffer[8192]{};
            int bytes = recv(client, buffer, static_cast<int>(sizeof(buffer) - 1), 0);
            std::string request = bytes > 0 ? std::string(buffer, buffer + bytes) : std::string();
            std::wstring code;
            if (TryReadOAuthCodeFromHttpRequest(request, code))
            {
                submittedCode = code;
                received.store(true, std::memory_order_release);
                SendAuthBridgeResponse(
                    client,
                    "<!doctype html><html><body style=\"font-family:sans-serif\">"
                    "<h1>BBC Launcher sign-in received</h1>"
                    "<p>You can return to the Xbox now and press OK.</p>"
                    "</body></html>");
            }
            else
            {
                std::string body =
                    "<!doctype html><html><body style=\"font-family:sans-serif;max-width:760px;margin:32px auto\">"
                    "<h1>BBC Launcher Microsoft sign-in</h1>"
                    "<p>Open this Microsoft sign-in link, finish login, then copy the final redirect URL or just its code value.</p>"
                    "<p><a href=\"" + HtmlEscape(authorizeUrlUtf8) + "\">Open Microsoft sign-in</a></p>"
                    "<form action=\"/submit\" method=\"get\">"
                    "<p><textarea name=\"code\" rows=\"6\" style=\"width:100%\" placeholder=\"Paste final redirect URL or code here\"></textarea></p>"
                    "<p><button type=\"submit\">Send to Xbox</button></p>"
                    "</form>"
                    "</body></html>";
                SendAuthBridgeResponse(client, body);
            }
            closesocket(client);
        }
    });

    WriteLogF(L"AUTH manual OAuth bridge listening url=%s authUrlLength=%zu", bridgeUrl.c_str(), authorizeUrlUtf8.size());
    std::wstring prompt =
        L"Microsoft sign-in needs another device.\r\n\r\n"
        L"1. On your phone or PC, open:\r\n" + bridgeUrl +
        L"\r\n\r\n2. Use the Microsoft link on that page.\r\n"
        L"3. After login, paste the final redirect URL or code into the page.\r\n\r\n"
        L"Press OK here after the page says sign-in received.";
    try
    {
        MessageDialog dialog(hstring(prompt.c_str()));
        ShowDialogAndPump(window, dialog);
    }
    catch (...)
    {
        WriteLogW(L"AUTH manual OAuth bridge dialog failed; waiting for submitted code anyway");
    }

    for (int i = 0; i < 300 && !received.load(std::memory_order_acquire); ++i)
    {
        Sleep(1000);
    }

    stop.store(true, std::memory_order_release);
    if (server.joinable())
    {
        server.join();
    }
    closesocket(listenSocket);
    WSACleanup();

    if (!received.load(std::memory_order_acquire) || submittedCode.empty())
    {
        error = L"Manual Microsoft sign-in did not receive a code";
        return false;
    }

    WriteLogW(L"AUTH manual OAuth bridge received code; exchanging for Microsoft token");
    return ExchangeMicrosoftLiveOAuthCode(
        clientId,
        submittedCode,
        microsoftAccessToken,
        error);
}

static bool AcquireMicrosoftLiveOAuthToken(
    CoreWindow const& window,
    std::wstring const& clientId,
    std::wstring& microsoftAccessToken,
    std::wstring& error)
{
    std::string authorizeUrl =
        "https://login.live.com/oauth20_authorize.srf"
        "?prompt=select_account"
        "&client_id=" + FormUrlEncode(clientId) +
        "&response_type=code"
        "&scope=" + FormUrlEncode(L"service::user.auth.xboxlive.com::MBI_SSL") +
        "&redirect_uri=" + FormUrlEncode(kMinecraftLauncherTestRedirectUri) +
        "&lw=1"
        "&fl=" + FormUrlEncode(L"dob,easi2") +
        "&xsup=1"
        "&nopa=2";

    std::wstring authorizeUrlWide = Utf8ToWide(authorizeUrl.c_str());
    if (!IsTruthyEnvironment(L"MINECRAFT_XBOX_ENABLE_EMBEDDED_WEB_SIGNIN", false))
    {
        return AcquireMicrosoftLiveOAuthTokenManual(window, clientId, authorizeUrlWide, microsoftAccessToken, error);
    }

    WebAuthenticationResult result = WebAuthenticationBroker::AuthenticateAsync(
        WebAuthenticationOptions::None,
        Uri(hstring(authorizeUrlWide.c_str())),
        Uri(hstring(kMinecraftLauncherTestRedirectUri))).get();

    if (result.ResponseStatus() == WebAuthenticationStatus::UserCancel)
    {
        error = L"Microsoft sign-in was cancelled";
        return false;
    }
    if (result.ResponseStatus() != WebAuthenticationStatus::Success)
    {
        error = L"Microsoft web sign-in failed with status " + std::to_wstring(static_cast<int>(result.ResponseStatus()));
        return false;
    }

    std::wstring responseUrl = result.ResponseData().c_str();
    std::wstring oauthError;
    if (TryReadQueryParameter(responseUrl, L"error", oauthError) && !oauthError.empty())
    {
        std::wstring description;
        TryReadQueryParameter(responseUrl, L"error_description", description);
        error = description.empty() ? oauthError : description;
        return false;
    }

    std::wstring code;
    if (!TryReadQueryParameter(responseUrl, L"code", code) || code.empty())
    {
        error = L"Microsoft web sign-in did not return an authorization code";
        return false;
    }

    return ExchangeMicrosoftLiveOAuthCode(clientId, code, microsoftAccessToken, error);
}

static bool SignInMinecraftAccount(
    CoreWindow const& window,
    std::wstring const& localRoot,
    MinecraftAuthSession& session,
    std::wstring& error)
{
    session.clientId = ResolveMicrosoftClientId(localRoot);
    WriteLogF(L"AUTH sign-in starting clientIdConfigured=%d", session.clientId.empty() ? 0 : 1);
    if (session.clientId.empty())
    {
        error = L"Microsoft client ID is not configured. Set MINECRAFT_XBOX_MICROSOFT_CLIENT_ID or create LocalState\\microsoft-client-id.txt.";
        return false;
    }

    std::wstring microsoftAccessToken;
    bool microsoftTokenAcquired = false;
    bool forceDeviceCode =
        IsTruthyEnvironment(L"MINECRAFT_XBOX_FORCE_DEVICE_CODE_SIGNIN", false) ||
        FileExists(localRoot + L"\\force-device-code-signin");
    bool useWebSignIn =
        !forceDeviceCode &&
        (IsTruthyEnvironment(L"MINECRAFT_XBOX_ENABLE_WEB_ACCOUNT_SIGNIN", false) ||
            FileExists(localRoot + L"\\enable-web-account-signin"));
    if (useWebSignIn)
    {
        WriteLogW(L"AUTH web sign-in requested; attempting Live OAuth broker flow");
        microsoftTokenAcquired = AcquireMicrosoftLiveOAuthToken(window, session.clientId, microsoftAccessToken, error);
        if (!microsoftTokenAcquired)
        {
            WriteLogF(L"AUTH Microsoft web sign-in failed: %s", error.c_str());
            WriteLogW(L"AUTH trying device-code fallback for non-default client id");
        }
    }

    if (!microsoftTokenAcquired &&
        !AcquireMicrosoftDeviceCodeToken(window, session.clientId, microsoftAccessToken, error))
    {
        return false;
    }
    WriteLogW(L"AUTH Microsoft access token acquired; authenticating Xbox Live");

    XboxAuthToken xbox;
    std::wstring xboxError;
    if (!AuthenticateXboxLive(microsoftAccessToken, true, xbox, xboxError) &&
        !AuthenticateXboxLive(microsoftAccessToken, false, xbox, xboxError))
    {
        error = L"Xbox Live authentication failed: " + xboxError;
        return false;
    }

    XboxAuthToken xsts;
    if (!AuthorizeXsts(xbox.token, xsts, error))
    {
        error = L"Xbox XSTS authorization failed: " + error;
        return false;
    }

    std::wstring userHash = xsts.userHash.empty() ? xbox.userHash : xsts.userHash;
    if (userHash.empty())
    {
        error = L"Xbox authentication did not return a user hash";
        return false;
    }

    std::wstring minecraftAccessToken;
    if (!LoginWithMinecraftServices(userHash, xsts.token, minecraftAccessToken, error))
    {
        error = L"Minecraft services login failed: " + error;
        return false;
    }

    if (!VerifyMinecraftEntitlement(minecraftAccessToken, error))
    {
        return false;
    }

    std::wstring username;
    std::wstring uuid;
    if (!ReadMinecraftProfile(minecraftAccessToken, username, uuid, error))
    {
        error = L"Minecraft profile lookup failed: " + error;
        return false;
    }

    session.username = username;
    session.uuid = NormalizeMinecraftUuid(uuid);
    session.accessToken = minecraftAccessToken;
    session.xuid = !xsts.xuid.empty() ? xsts.xuid : (!xbox.xuid.empty() ? xbox.xuid : L"0");
    session.online = true;
    SaveAuthSession(localRoot, session);
    WriteLogF(L"AUTH signed in Minecraft profile username=%s uuid=%s xuid=%s", session.username.c_str(), session.uuid.c_str(), session.xuid.c_str());
    return true;
}

static MinecraftAuthSession ResolveMinecraftAuthSession(CoreWindow const& window, std::wstring const& localRoot)
{
    MinecraftAuthSession session = {};
    session.clientId = ResolveMicrosoftClientId(localRoot);
#if defined(MINECRAFT_XBOX_DISABLE_ACCOUNT_SIGNIN_BUILD)
    WriteLogW(L"AUTH disabled by build flag; using offline placeholders");
    return session;
#endif
    bool signInEnabled =
        IsTruthyEnvironment(L"MINECRAFT_XBOX_ENABLE_ACCOUNT_SIGNIN", true) ||
        FileExists(localRoot + L"\\enable-account-signin");
    if (FileExists(localRoot + L"\\disable-account-signin"))
    {
        signInEnabled = false;
    }
    WriteLogF(
        L"AUTH resolve enabled=%d marker=%d disableMarker=%d protectedCachedSession=%d legacyCachedSession=%d clientIdConfigured=%d",
        signInEnabled ? 1 : 0,
        FileExists(localRoot + L"\\enable-account-signin") ? 1 : 0,
        FileExists(localRoot + L"\\disable-account-signin") ? 1 : 0,
        FileExists(ProtectedAuthSessionPath(localRoot)) ? 1 : 0,
        FileExists(AuthSessionPath(localRoot)) ? 1 : 0,
        session.clientId.empty() ? 0 : 1);
    if (!signInEnabled)
    {
        WriteLogW(L"AUTH disabled for this build; using offline placeholders");
        return session;
    }

    if (TryLoadCachedAuthSession(localRoot, session))
    {
        return session;
    }

    std::wstring error;
    if (SignInMinecraftAccount(window, localRoot, session, error))
    {
        return session;
    }

    WriteLogF(L"AUTH sign-in failed; using offline placeholders: %s", error.c_str());
    std::wstring prompt =
        L"Microsoft/Minecraft sign-in failed.\r\n\r\n" +
        error +
        L"\r\n\r\nMinecraft will launch offline for this test.";
    try
    {
        MessageDialog dialog(hstring(prompt.c_str()));
        ShowDialogAndPump(window, dialog);
    }
    catch (...)
    {
    }
    return session;
}

static std::wstring FileNameOnly(std::wstring const& path)
{
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

static std::wstring SanitizePathSegment(std::wstring value)
{
    for (auto& ch : value)
    {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' ||
            ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
        {
            ch = L'_';
        }
    }
    return value.empty() ? L"default" : value;
}

static std::wstring NormalizeJavaClassName(std::wstring value)
{
    for (auto& ch : value)
    {
        if (ch == L'.')
        {
            ch = L'/';
        }
    }
    return value.empty() ? L"net/minecraft/client/main/Main" : value;
}

struct MinecraftLaunchProfile
{
    std::wstring id;
    std::wstring root;
    std::wstring label;
    std::wstring baseVersion;
    std::wstring modLoader;
};

static std::wstring InferBaseMinecraftVersion(std::wstring const& version);

static std::wstring ReadProfileVersion(std::wstring const& profileRoot, std::wstring const& fallback)
{
    std::string json;
    std::wstring version;
    if (ReadSmallTextFileUtf8(profileRoot + L"\\staging-summary.json", json) &&
        TryReadJsonString(json, "Version", version))
    {
        return version;
    }

    return fallback;
}

static std::wstring ReadProfileBaseMinecraftVersion(std::wstring const& profileRoot, std::wstring const& profileId)
{
    std::string json;
    std::wstring baseVersion;
    if (ReadSmallTextFileUtf8(profileRoot + L"\\staging-summary.json", json) &&
        TryReadJsonString(json, "BaseMinecraftVersion", baseVersion) &&
        !baseVersion.empty())
    {
        return baseVersion;
    }

    json.clear();
    if (ReadSmallTextFileUtf8(profileRoot + L"\\download-manifest.json", json) &&
        TryReadJsonString(json, "MinecraftVersion", baseVersion) &&
        !baseVersion.empty())
    {
        return baseVersion;
    }

    return InferBaseMinecraftVersion(profileId);
}

static std::wstring ResolveStagedMainClass(std::wstring const& profileRoot, std::wstring const& minecraftVersion)
{
    std::string json;
    std::wstring mainClass;
    if (ReadSmallTextFileUtf8(profileRoot + L"\\staging-summary.json", json) &&
        TryReadJsonString(json, "MainClass", mainClass))
    {
        return NormalizeJavaClassName(mainClass);
    }

    json.clear();
    std::wstring versionJson = profileRoot + L"\\versions\\" + minecraftVersion + L"\\" + minecraftVersion + L".json";
    if (ReadSmallTextFileUtf8(versionJson, json) &&
        TryReadJsonString(json, "mainClass", mainClass))
    {
        return NormalizeJavaClassName(mainClass);
    }

    return L"net/minecraft/client/main/Main";
}

static std::wstring ResolveStagedFabricLoaderVersion(std::wstring const& profileRoot)
{
    std::string json;
    std::wstring loaderVersion;
    if (ReadSmallTextFileUtf8(profileRoot + L"\\staging-summary.json", json) &&
        TryReadJsonString(json, "FabricLoaderVersion", loaderVersion))
    {
        return loaderVersion;
    }

    return std::wstring();
}

static std::wstring ResolveStagedLauncherVersionName(std::wstring const& profileRoot, std::wstring const& fallback)
{
    std::string json;
    std::wstring launcherVersionName;
    if (ReadSmallTextFileUtf8(profileRoot + L"\\staging-summary.json", json) &&
        TryReadJsonString(json, "LauncherVersionName", launcherVersionName))
    {
        return launcherVersionName;
    }

    return fallback;
}

static std::wstring ResolveStagedArgumentFile(
    std::wstring const& profileRoot,
    char const* summaryKey,
    wchar_t const* fallbackName)
{
    std::string json;
    std::wstring relative;
    if (ReadSmallTextFileUtf8(profileRoot + L"\\staging-summary.json", json) &&
        TryReadJsonString(json, summaryKey, relative) &&
        !relative.empty())
    {
        return profileRoot + L"\\" + relative;
    }

    return profileRoot + L"\\" + fallbackName;
}

static std::vector<std::string> ResolveProfileArguments(
    std::wstring const& profileRoot,
    char const* summaryKey,
    wchar_t const* fallbackName,
    std::wstring const& launcherVersionName)
{
    std::wstring path = ResolveStagedArgumentFile(profileRoot, summaryKey, fallbackName);
    std::vector<std::string> args = ReadArgumentLines(path);
    std::string libraryDir = WideToUtf8(profileRoot + L"\\libraries");
    std::string versionName = WideToUtf8(launcherVersionName);
    for (auto& arg : args)
    {
        ReplaceAllInPlace(arg, "${library_directory}", libraryDir);
        ReplaceAllInPlace(arg, "${classpath_separator}", ";");
        ReplaceAllInPlace(arg, "${version_name}", versionName);
    }

    return args;
}

static bool TryNormalizeJvmOptionValuePair(
    std::string const& option,
    std::string const& value,
    std::string& normalized)
{
    if (option == "-p" || option == "--module-path")
    {
        normalized = "--module-path=" + value;
        return true;
    }
    if (option == "--upgrade-module-path")
    {
        normalized = "--upgrade-module-path=" + value;
        return true;
    }
    if (option == "--add-modules")
    {
        normalized = "--add-modules=" + value;
        return true;
    }
    if (option == "--add-opens")
    {
        normalized = "--add-opens=" + value;
        return true;
    }
    if (option == "--add-exports")
    {
        normalized = "--add-exports=" + value;
        return true;
    }
    if (option == "--add-reads")
    {
        normalized = "--add-reads=" + value;
        return true;
    }
    if (option == "--patch-module")
    {
        normalized = "--patch-module=" + value;
        return true;
    }
    if (option == "--limit-modules")
    {
        normalized = "--limit-modules=" + value;
        return true;
    }
    if (option == "--enable-native-access")
    {
        normalized = "--enable-native-access=" + value;
        return true;
    }

    return false;
}

static std::vector<std::string> NormalizeJvmArgumentsForInvocation(
    std::vector<std::string> const& args,
    size_t* normalizedPairs)
{
    if (normalizedPairs)
    {
        *normalizedPairs = 0;
    }

    std::vector<std::string> normalizedArgs;
    normalizedArgs.reserve(args.size());
    for (size_t i = 0; i < args.size(); ++i)
    {
        std::string normalized;
        if (i + 1 < args.size() && TryNormalizeJvmOptionValuePair(args[i], args[i + 1], normalized))
        {
            normalizedArgs.push_back(normalized);
            ++i;
            if (normalizedPairs)
            {
                ++(*normalizedPairs);
            }
            continue;
        }

        normalizedArgs.push_back(args[i]);
    }

    return normalizedArgs;
}

static bool IsChunkLoadMitigationEnabled()
{
    return IsTruthyEnvironment(L"MINECRAFT_XBOX_CHUNK_LOAD_MITIGATION", false);
}

static std::vector<std::string> SplitOptionsLines(std::string const& text)
{
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t end = text.find('\n', pos);
        std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (!line.empty())
        {
            lines.push_back(line);
        }
        if (end == std::string::npos)
        {
            break;
        }
        pos = end + 1;
    }

    return lines;
}

static bool TryParseOptionInt(std::string const& line, std::string const& key, int& value)
{
    std::string prefix = key + ":";
    if (line.rfind(prefix, 0) != 0)
    {
        return false;
    }

    char* end = nullptr;
    long parsed = std::strtol(line.c_str() + prefix.size(), &end, 10);
    if (end == line.c_str() + prefix.size())
    {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

static bool UpsertCappedIntOption(std::vector<std::string>& lines, std::string const& key, int cap)
{
    std::string prefix = key + ":";
    for (auto& line : lines)
    {
        if (line.rfind(prefix, 0) != 0)
        {
            continue;
        }

        int existing = 0;
        if (!TryParseOptionInt(line, key, existing) || existing > cap)
        {
            line = prefix + std::to_string(cap);
            return true;
        }

        return false;
    }

    lines.push_back(prefix + std::to_string(cap));
    return true;
}

static std::string JoinOptionsLines(std::vector<std::string> const& lines)
{
    std::string text;
    for (auto const& line : lines)
    {
        text += line;
        text += "\r\n";
    }
    return text;
}

static void ApplyChunkLoadMitigationOptions(
    std::wstring const& gameDir,
    std::wstring const& minecraftVersion,
    std::wstring const& baseMinecraftVersion,
    std::wstring const& profileLabel,
    bool legacyMinecraftArgs)
{
    if (!IsChunkLoadMitigationEnabled())
    {
        WriteLogW(L"Chunk-load mitigation disabled by default; set MINECRAFT_XBOX_CHUNK_LOAD_MITIGATION=1 to enable");
        return;
    }

    std::wstring presetVersion = baseMinecraftVersion.empty() ? minecraftVersion : baseMinecraftVersion;
    std::wstring markerPath = gameDir + L"\\xbox-chunk-load-preset-" +
        SanitizePathSegment(presetVersion) + L"-applied.txt";
    if (FileExists(markerPath))
    {
        WriteLogF(
            L"Chunk-load mitigation options already applied for profile=%s version=%s base=%s marker=%s",
            profileLabel.c_str(),
            minecraftVersion.c_str(),
            baseMinecraftVersion.empty() ? L"<none>" : baseMinecraftVersion.c_str(),
            markerPath.c_str());
        return;
    }

    std::wstring optionsPath = gameDir + L"\\options.txt";
    std::string optionsText;
    bool hadOptions = ReadTextFileUtf8(optionsPath, optionsText, 512ull * 1024ull);
    std::vector<std::string> lines = hadOptions ? SplitOptionsLines(optionsText) : std::vector<std::string>();

    bool changed = false;
    changed |= UpsertCappedIntOption(lines, "renderDistance", 8);
    if (!legacyMinecraftArgs)
    {
        changed |= UpsertCappedIntOption(lines, "simulationDistance", 4);
    }

    if (changed)
    {
        if (WriteTextFileUtf8(optionsPath, JoinOptionsLines(lines)))
        {
            WriteLogF(
                L"Chunk-load mitigation wrote options for profile=%s version=%s base=%s path=%s hadOptions=%d renderDistance<=8 simulationDistance<=4",
                profileLabel.c_str(),
                minecraftVersion.c_str(),
                baseMinecraftVersion.empty() ? L"<none>" : baseMinecraftVersion.c_str(),
                optionsPath.c_str(),
                hadOptions ? 1 : 0);
        }
    }
    else
    {
        WriteLogF(
            L"Chunk-load mitigation left existing options unchanged for profile=%s version=%s base=%s",
            profileLabel.c_str(),
            minecraftVersion.c_str(),
            baseMinecraftVersion.empty() ? L"<none>" : baseMinecraftVersion.c_str());
    }

    WriteTextFileUtf8(markerPath, "chunk-load mitigation preset applied\r\n");
}

static void AppendChunkLoadMitigationJvmOptions(
    std::vector<std::string>& optionText,
    bool legacyMinecraftArgs,
    int javaRuntimeMajor)
{
    if (!IsChunkLoadMitigationEnabled())
    {
        return;
    }

    optionText.emplace_back(legacyMinecraftArgs ? "-Xms512M" : "-Xms1024M");
    optionText.emplace_back("-XX:+UseG1GC");
    optionText.emplace_back("-XX:MaxGCPauseMillis=60");
    optionText.emplace_back("-XX:G1ReservePercent=20");
    optionText.emplace_back("-XX:ParallelGCThreads=4");
    optionText.emplace_back("-XX:ConcGCThreads=2");
    if (!legacyMinecraftArgs)
    {
        optionText.emplace_back("-XX:ActiveProcessorCount=6");
        optionText.emplace_back("-Dmax.bg.threads=4");
        optionText.emplace_back("-Dmax.render.threads=2");
    }

    WriteLogF(
        L"Chunk-load mitigation JVM options enabled legacy=%d javaMajor=%d",
        legacyMinecraftArgs ? 1 : 0,
        javaRuntimeMajor);
}

static void AppendLanPlayJvmOptions(
    std::vector<std::string>& optionText,
    std::wstring const& minecraftLog,
    std::wstring const& profileLabel)
{
    (void)minecraftLog;
    (void)profileLabel;
    optionText.emplace_back("-Djava.net.preferIPv4Stack=true");
    optionText.emplace_back("-Djava.net.preferIPv6Addresses=false");
    WriteLogW(L"LAN play JVM options enabled: IPv4 preferred; native LAN advertiser handles discovery");
}

static void EnsureManualLanServerTemplate(std::wstring const& localRoot)
{
    std::wstring manualLanServers = localRoot + L"\\xbox-lan-servers.txt";
    if (FileExists(manualLanServers))
    {
        return;
    }

    std::string text =
        "# Manual Minecraft LAN servers for Xbox fallback.\r\n"
        "# Use this when the Xbox cannot see PC LAN discovery packets.\r\n"
        "# Format:\r\n"
        "#   My PC World=192.168.0.25:51234\r\n"
        "# Put the port shown on the PC after Open to LAN.\r\n";
    if (WriteTextFileUtf8(manualLanServers, text))
    {
        WriteLogF(L"LAN manual server template created path=%s", manualLanServers.c_str());
    }
}

static bool ReadTailTextFileUtf8(std::wstring const& path, std::string& text, unsigned long long maxBytes)
{
    text.clear();
    HANDLE file = CreateFile2(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        OPEN_EXISTING,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0)
    {
        CloseHandle(file);
        return false;
    }

    unsigned long long length = static_cast<unsigned long long>(size.QuadPart);
    unsigned long long bytesToRead = std::min(length, maxBytes);
    LARGE_INTEGER offset = {};
    offset.QuadPart = static_cast<LONGLONG>(length - bytesToRead);
    if (!SetFilePointerEx(file, offset, nullptr, FILE_BEGIN))
    {
        CloseHandle(file);
        return false;
    }

    text.resize(static_cast<size_t>(bytesToRead));
    DWORD read = 0;
    BOOL ok = text.empty() ||
        ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != text.size())
    {
        text.clear();
        return false;
    }

    return true;
}

static int ParseLanPortFromLogTail(std::string const& text)
{
    int port = 0;
    size_t lineStart = 0;
    while (lineStart < text.size())
    {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos)
        {
            lineEnd = text.size();
        }

        std::string line = text.substr(lineStart, lineEnd - lineStart);
        std::string lower = ToLowerAscii(line);
        bool looksLikeLanLine =
            lower.find("started serving") != std::string::npos ||
            lower.find("opened to lan") != std::string::npos ||
            lower.find("open to lan") != std::string::npos ||
            lower.find("local game") != std::string::npos ||
            lower.find("lan server") != std::string::npos;
        bool looksLikeServerStop =
            lower.find("stopping singleplayer server") != std::string::npos ||
            lower.find("stopping server") != std::string::npos ||
            lower.find("server stopped") != std::string::npos;

        if (looksLikeServerStop)
        {
            port = 0;
        }

        if (looksLikeLanLine)
        {
            for (size_t i = 0; i < line.size();)
            {
                if (line[i] < '0' || line[i] > '9')
                {
                    ++i;
                    continue;
                }

                int value = 0;
                while (i < line.size() && line[i] >= '0' && line[i] <= '9')
                {
                    value = value * 10 + (line[i] - '0');
                    ++i;
                }

                if (value >= 1024 && value <= 65535)
                {
                    port = value;
                }
            }
        }

        lineStart = lineEnd + 1;
    }

    return port;
}

struct NativeLanAddress
{
    in_addr local{};
    in_addr broadcast{};
    bool hasBroadcast = false;
};

static std::vector<NativeLanAddress> GetNativeLanIpv4Addresses()
{
    std::vector<NativeLanAddress> addresses;
    ULONG bufferSize = 16 * 1024;
    std::vector<unsigned char> buffer(bufferSize);
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        auto adapterAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, adapterAddresses, &bufferSize);
        if (result == ERROR_BUFFER_OVERFLOW)
        {
            buffer.resize(bufferSize);
            continue;
        }
        if (result != NO_ERROR)
        {
            WriteLogF(L"LAN advertiser GetAdaptersAddresses failed result=%lu", result);
            return addresses;
        }

        for (auto adapter = adapterAddresses; adapter != nullptr; adapter = adapter->Next)
        {
            if (adapter->OperStatus != IfOperStatusUp)
            {
                continue;
            }

            for (auto unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next)
            {
                if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET)
                {
                    continue;
                }

                auto sockaddr = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
                uint32_t host = ntohl(sockaddr->sin_addr.s_addr);
                if (host == 0 || (host >> 24) == 127)
                {
                    continue;
                }

                NativeLanAddress entry{};
                entry.local = sockaddr->sin_addr;

                ULONG prefixLength = unicast->OnLinkPrefixLength;
                if (prefixLength <= 30)
                {
                    uint32_t mask = prefixLength == 0 ? 0 : (0xFFFFFFFFu << (32 - prefixLength));
                    uint32_t broadcast = (host & mask) | ~mask;
                    entry.broadcast.s_addr = htonl(broadcast);
                    entry.hasBroadcast = true;
                }

                addresses.push_back(entry);
            }
        }
        break;
    }

    return addresses;
}

static std::wstring NativeIpv4AddressToString(in_addr address)
{
    uint32_t host = ntohl(address.s_addr);
    wchar_t buffer[32]{};
    swprintf_s(
        buffer,
        L"%u.%u.%u.%u",
        static_cast<unsigned int>((host >> 24) & 0xFF),
        static_cast<unsigned int>((host >> 16) & 0xFF),
        static_cast<unsigned int>((host >> 8) & 0xFF),
        static_cast<unsigned int>(host & 0xFF));
    return buffer;
}

static std::string NativeIpv4AddressToStringA(in_addr address)
{
    return WideToUtf8(NativeIpv4AddressToString(address));
}

static bool IsSameNativeIpv4Address(in_addr left, in_addr right)
{
    return left.s_addr == right.s_addr;
}

static bool IsLocalNativeLanAddress(in_addr address, std::vector<NativeLanAddress> const& localAddresses)
{
    uint32_t host = ntohl(address.s_addr);
    if (host == 0 || (host >> 24) == 127)
    {
        return true;
    }

    for (auto const& localAddress : localAddresses)
    {
        if (IsSameNativeIpv4Address(address, localAddress.local))
        {
            return true;
        }
    }

    return false;
}

static int ParseLanAdvertPort(std::string const& message)
{
    size_t motdStart = message.find("[MOTD]");
    size_t motdEnd = message.find("[/MOTD]");
    size_t adStart = message.find("[AD]", motdEnd == std::string::npos ? 0 : motdEnd + 7);
    size_t adEnd = message.find("[/AD]", adStart == std::string::npos ? 0 : adStart + 4);
    if (motdStart == std::string::npos ||
        motdEnd == std::string::npos ||
        adStart == std::string::npos ||
        adEnd == std::string::npos ||
        motdStart > motdEnd ||
        adStart > adEnd)
    {
        return 0;
    }

    int port = 0;
    for (size_t i = adStart + 4; i < adEnd; ++i)
    {
        char c = message[i];
        if (c < '0' || c > '9')
        {
            return 0;
        }

        port = port * 10 + (c - '0');
        if (port > 65535)
        {
            return 0;
        }
    }

    return port >= 1024 ? port : 0;
}

static std::wstring GetPreferredNativeLanIpv4Address()
{
    std::vector<NativeLanAddress> addresses = GetNativeLanIpv4Addresses();
    if (addresses.empty())
    {
        return std::wstring();
    }

    return NativeIpv4AddressToString(addresses.front().local);
}

static void LogNativeLanSocketFailure(const wchar_t* action, int error)
{
    static int failureLogCount = 0;
    if (failureLogCount < 24)
    {
        ++failureLogCount;
        WriteLogF(L"LAN advertiser %s failed WSA=%d", action, error);
    }
}

static void SendNativeLanPacket(
    sockaddr_in const& target,
    char const* data,
    int dataLength,
    in_addr const* localAddress,
    bool broadcast,
    bool multicast)
{
    SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == INVALID_SOCKET)
    {
        LogNativeLanSocketFailure(L"socket", WSAGetLastError());
        return;
    }

    if (broadcast)
    {
        BOOL enabled = TRUE;
        if (setsockopt(socketHandle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == SOCKET_ERROR)
        {
            LogNativeLanSocketFailure(L"setsockopt(SO_BROADCAST)", WSAGetLastError());
        }
    }

    if (multicast)
    {
        DWORD ttl = 1;
        setsockopt(socketHandle, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl));
        if (localAddress)
        {
            if (setsockopt(socketHandle, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(localAddress), sizeof(*localAddress)) == SOCKET_ERROR)
            {
                LogNativeLanSocketFailure(L"setsockopt(IP_MULTICAST_IF)", WSAGetLastError());
            }
        }
    }

    if (broadcast && localAddress)
    {
        sockaddr_in bindAddress{};
        bindAddress.sin_family = AF_INET;
        bindAddress.sin_addr = *localAddress;
        bindAddress.sin_port = 0;
        bind(socketHandle, reinterpret_cast<sockaddr*>(&bindAddress), sizeof(bindAddress));
    }

    if (sendto(socketHandle, data, dataLength, 0, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == SOCKET_ERROR)
    {
        LogNativeLanSocketFailure(
            multicast ? L"sendto(multicast)" : (broadcast ? L"sendto(broadcast)" : L"sendto(unicast)"),
            WSAGetLastError());
    }

    closesocket(socketHandle);
}

static void AdvertiseNativeLanPort(int port, std::string const& motd)
{
    std::string message = "[MOTD]" + motd + "[/MOTD][AD]" + std::to_string(port) + "[/AD]";
    std::vector<NativeLanAddress> addresses = GetNativeLanIpv4Addresses();

    sockaddr_in multicastTarget{};
    multicastTarget.sin_family = AF_INET;
    multicastTarget.sin_port = htons(4445);
    InetPtonA(AF_INET, "224.0.2.60", &multicastTarget.sin_addr);

    if (addresses.empty())
    {
        SendNativeLanPacket(multicastTarget, message.data(), static_cast<int>(message.size()), nullptr, false, true);
    }
    else
    {
        for (auto const& address : addresses)
        {
            SendNativeLanPacket(multicastTarget, message.data(), static_cast<int>(message.size()), &address.local, false, true);
            if (address.hasBroadcast)
            {
                sockaddr_in subnetTarget{};
                subnetTarget.sin_family = AF_INET;
                subnetTarget.sin_port = htons(4445);
                subnetTarget.sin_addr = address.broadcast;
                SendNativeLanPacket(subnetTarget, message.data(), static_cast<int>(message.size()), &address.local, true, false);
            }
        }
    }

    sockaddr_in globalTarget{};
    globalTarget.sin_family = AF_INET;
    globalTarget.sin_port = htons(4445);
    globalTarget.sin_addr.s_addr = INADDR_BROADCAST;
    SendNativeLanPacket(globalTarget, message.data(), static_cast<int>(message.size()), nullptr, true, false);
}

static void ForwardNativeLanAdvertToMinecraft(
    std::string const& message,
    std::string const& senderIp,
    std::vector<NativeLanAddress> const& addresses)
{
    std::string forwarded = message + "[XBOXHOST]" + senderIp + "[/XBOXHOST]";
    if (forwarded.size() > 1400)
    {
        return;
    }

    sockaddr_in loopbackTarget{};
    loopbackTarget.sin_family = AF_INET;
    loopbackTarget.sin_port = htons(4445);
    InetPtonA(AF_INET, "127.0.0.1", &loopbackTarget.sin_addr);
    SendNativeLanPacket(loopbackTarget, forwarded.data(), static_cast<int>(forwarded.size()), nullptr, false, false);

    sockaddr_in multicastTarget{};
    multicastTarget.sin_family = AF_INET;
    multicastTarget.sin_port = htons(4445);
    InetPtonA(AF_INET, "224.0.2.60", &multicastTarget.sin_addr);

    for (auto const& address : addresses)
    {
        sockaddr_in localTarget{};
        localTarget.sin_family = AF_INET;
        localTarget.sin_port = htons(4445);
        localTarget.sin_addr = address.local;
        SendNativeLanPacket(localTarget, forwarded.data(), static_cast<int>(forwarded.size()), nullptr, false, false);
        SendNativeLanPacket(multicastTarget, forwarded.data(), static_cast<int>(forwarded.size()), &address.local, false, true);
    }
}

static void StartNativeLanDiscoveryBridge()
{
    bool expected = false;
    if (!g_nativeLanDiscoveryBridgeStarted.compare_exchange_strong(expected, true))
    {
        return;
    }

    std::thread([]()
    {
        WSADATA data{};
        int startup = WSAStartup(MAKEWORD(2, 2), &data);
        if (startup != 0)
        {
            WriteLogF(L"Native LAN discovery bridge WSAStartup failed result=%d", startup);
            return;
        }

        SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socketHandle == INVALID_SOCKET)
        {
            LogNativeLanSocketFailure(L"discovery bridge socket", WSAGetLastError());
            return;
        }

        BOOL enabled = TRUE;
        if (setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == SOCKET_ERROR)
        {
            LogNativeLanSocketFailure(L"discovery bridge setsockopt(SO_REUSEADDR)", WSAGetLastError());
        }
        if (setsockopt(socketHandle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == SOCKET_ERROR)
        {
            LogNativeLanSocketFailure(L"discovery bridge setsockopt(SO_BROADCAST)", WSAGetLastError());
        }

        sockaddr_in bindAddress{};
        bindAddress.sin_family = AF_INET;
        bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
        bindAddress.sin_port = htons(4445);
        if (bind(socketHandle, reinterpret_cast<sockaddr*>(&bindAddress), sizeof(bindAddress)) == SOCKET_ERROR)
        {
            LogNativeLanSocketFailure(L"discovery bridge bind(4445)", WSAGetLastError());
            closesocket(socketHandle);
            return;
        }

        std::vector<NativeLanAddress> addresses = GetNativeLanIpv4Addresses();
        in_addr multicastGroup{};
        InetPtonA(AF_INET, "224.0.2.60", &multicastGroup);
        for (auto const& address : addresses)
        {
            ip_mreq request{};
            request.imr_multiaddr = multicastGroup;
            request.imr_interface = address.local;
            if (setsockopt(socketHandle, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&request), sizeof(request)) == SOCKET_ERROR)
            {
                WriteLogF(
                    L"Native LAN discovery bridge join 224.0.2.60 on %s failed WSA=%d",
                    NativeIpv4AddressToString(address.local).c_str(),
                    WSAGetLastError());
            }
            else
            {
                WriteLogF(
                    L"Native LAN discovery bridge joined 224.0.2.60 on %s",
                    NativeIpv4AddressToString(address.local).c_str());
            }
        }

        WriteLogF(L"Native LAN discovery bridge listening on UDP 4445 localAddresses=%u", static_cast<unsigned int>(addresses.size()));

        int forwardedLogCount = 0;
        unsigned long long lastForwardLogTick = 0;
        char buffer[2048]{};
        while (true)
        {
            sockaddr_in from{};
            int fromLength = sizeof(from);
            int received = recvfrom(
                socketHandle,
                buffer,
                static_cast<int>(sizeof(buffer)),
                0,
                reinterpret_cast<sockaddr*>(&from),
                &fromLength);
            if (received == SOCKET_ERROR)
            {
                int error = WSAGetLastError();
                if (error != WSAEINTR)
                {
                    LogNativeLanSocketFailure(L"discovery bridge recvfrom", error);
                    Sleep(250);
                }
                continue;
            }
            if (received <= 0 || from.sin_family != AF_INET)
            {
                continue;
            }

            std::string message(buffer, buffer + received);
            if (message.find("[XBOXHOST]") != std::string::npos ||
                IsLocalNativeLanAddress(from.sin_addr, addresses))
            {
                continue;
            }

            int gamePort = ParseLanAdvertPort(message);
            if (gamePort <= 0)
            {
                continue;
            }

            std::string senderIp = NativeIpv4AddressToStringA(from.sin_addr);
            ForwardNativeLanAdvertToMinecraft(message, senderIp, addresses);

            unsigned long long now = GetTickCount64();
            if (forwardedLogCount < 12 || now - lastForwardLogTick > 30000ull)
            {
                ++forwardedLogCount;
                lastForwardLogTick = now;
                WriteLogF(
                    L"Native LAN discovery bridge forwarded PC LAN advert from %s gamePort=%d bytes=%d",
                    NativeIpv4AddressToString(from.sin_addr).c_str(),
                    gamePort,
                    received);
            }
        }
    }).detach();
}

static void StartNativeLanAdvertiser(std::wstring const& minecraftLog, std::wstring const& profileLabel)
{
    bool expected = false;
    if (!g_nativeLanAdvertiserStarted.compare_exchange_strong(expected, true))
    {
        return;
    }

    std::string motd = WideToUtf8(profileLabel + L" on Xbox");
    std::thread([minecraftLog, motd]()
    {
        WSADATA data{};
        int startup = WSAStartup(MAKEWORD(2, 2), &data);
        if (startup != 0)
        {
            WriteLogF(L"LAN advertiser WSAStartup failed result=%d", startup);
            return;
        }

        WriteLogW(L"Native LAN advertiser started");
        int lastPort = 0;
        unsigned long long lastLogTick = 0;
        while (true)
        {
            std::string tail;
            if (ReadTailTextFileUtf8(minecraftLog, tail, 64ull * 1024ull))
            {
                int port = ParseLanPortFromLogTail(tail);
                if (port > 0)
                {
                    unsigned long long now = GetTickCount64();
                    if (port != lastPort || now - lastLogTick > 30000ull)
                    {
                        lastPort = port;
                        lastLogTick = now;
                        WriteLogF(L"Native LAN advertiser using detected port %d", port);
                    }
                    AdvertiseNativeLanPort(port, motd);
                }
            }

            Sleep(1500);
        }
    }).detach();
}

static std::wstring ReadProfileModLoader(std::wstring const& profileRoot, std::wstring const& profileId)
{
    std::string json;
    std::wstring loader;
    if (ReadSmallTextFileUtf8(profileRoot + L"\\staging-summary.json", json) &&
        TryReadJsonString(json, "ModLoader", loader))
    {
        return loader;
    }

    std::wstring lower = ToLowerInvariant(profileId);
    if (lower.find(L"neoforge") != std::wstring::npos) return L"NeoForge";
    if (lower.find(L"fabric") != std::wstring::npos) return L"Fabric";
    if (lower.find(L"forge") != std::wstring::npos) return L"Forge";
    if (lower.find(L"quilt") != std::wstring::npos) return L"Quilt";
    return std::wstring();
}

static std::wstring ReadProfileDisplayName(
    std::wstring const& profileRoot,
    std::wstring const& profileId,
    std::wstring const& modLoader)
{
    std::string json;
    std::wstring displayName;
    if (ReadSmallTextFileUtf8(profileRoot + L"\\staging-summary.json", json))
    {
        if (TryReadJsonString(json, "DisplayName", displayName))
        {
            return displayName;
        }

        std::wstring baseVersion;
        if (!modLoader.empty() && TryReadJsonString(json, "BaseMinecraftVersion", baseVersion))
        {
            return baseVersion + L" " + modLoader;
        }
    }

    return profileId + (modLoader.empty() ? L"" : L" " + modLoader);
}

static void CollectMinecraftLaunchProfiles(std::wstring const& localRoot, std::vector<MinecraftLaunchProfile>& profiles)
{
    profiles.clear();
    std::wstring profilesRoot = localRoot + L"\\profiles";
    if (DirectoryExists(profilesRoot))
    {
        std::wstring pattern = profilesRoot + L"\\*";
        WIN32_FIND_DATAW data = {};
        HANDLE find = FindFirstFileExW(
            pattern.c_str(),
            FindExInfoBasic,
            &data,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH);
        if (find != INVALID_HANDLE_VALUE)
        {
            do
            {
                std::wstring name = data.cFileName;
                if (name == L"." || name == L"..")
                {
                    continue;
                }
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                    continue;
                }

                std::wstring root = profilesRoot + L"\\" + name;
                if (!FileExists(root + L"\\client.jar") &&
                    !FileExists(root + L"\\download-manifest.json") &&
                    !FileExists(root + L"\\staging-summary.json"))
                {
                    continue;
                }

                MinecraftLaunchProfile profile = {};
                profile.root = root;
                profile.id = ReadProfileVersion(root, name);
                profile.baseVersion = ReadProfileBaseMinecraftVersion(root, profile.id);
                profile.modLoader = ReadProfileModLoader(root, profile.id);
                profile.label = ReadProfileDisplayName(root, profile.id, profile.modLoader);
                profiles.push_back(profile);
            }
            while (FindNextFileW(find, &data));
            FindClose(find);
        }
    }

    if (profiles.empty() &&
        (FileExists(localRoot + L"\\client.jar") ||
            FileExists(localRoot + L"\\download-manifest.json") ||
            FileExists(localRoot + L"\\staging-summary.json")))
    {
        MinecraftLaunchProfile profile = {};
        profile.root = localRoot;
        profile.id = ReadProfileVersion(localRoot, L"1.21.1");
        profile.baseVersion = ReadProfileBaseMinecraftVersion(localRoot, profile.id);
        profile.modLoader = ReadProfileModLoader(localRoot, profile.id);
        profile.label = ReadProfileDisplayName(localRoot, profile.id, profile.modLoader);
        profiles.push_back(profile);
    }

    std::sort(profiles.begin(), profiles.end(), [](MinecraftLaunchProfile const& a, MinecraftLaunchProfile const& b)
    {
        return a.id < b.id;
    });
}

static int DefaultProfileCommandIndex(std::vector<MinecraftLaunchProfile> const& profiles, std::wstring const& savedId)
{
    for (size_t i = 0; i < profiles.size(); ++i)
    {
        if (profiles[i].id == savedId)
        {
            return static_cast<int>(i);
        }
    }
    return 0;
}

static MinecraftLaunchProfile FallbackMinecraftLaunchProfile(std::vector<MinecraftLaunchProfile> const& profiles, std::wstring const& savedId)
{
    for (auto const& profile : profiles)
    {
        if (profile.id == savedId)
        {
            return profile;
        }
    }

    for (auto const& profile : profiles)
    {
        if (!profile.modLoader.empty())
        {
            return profile;
        }
    }

    return profiles[0];
}

static MinecraftLaunchProfile PickMinecraftLaunchProfileInteractive(CoreWindow const& window, std::wstring const& localRoot)
{
    std::vector<MinecraftLaunchProfile> profiles;
    CollectMinecraftLaunchProfiles(localRoot, profiles);
    if (profiles.empty())
    {
        WriteLogW(L"FAIL: no Minecraft launch profiles found");
        return {};
    }

    if (profiles.size() == 1)
    {
        WriteLogF(L"Only one Minecraft launch profile available: %s", profiles[0].label.c_str());
        WriteTextFileUtf8(localRoot + L"\\selected-minecraft-profile.txt", WideToUtf8(profiles[0].id));
        return profiles[0];
    }

    std::string savedBytes;
    std::wstring savedId;
    if (ReadSmallTextFileUtf8(localRoot + L"\\selected-minecraft-profile.txt", savedBytes))
    {
        savedId = Utf8ToWide(savedBytes.c_str());
        while (!savedId.empty() && (savedId.back() == L'\r' || savedId.back() == L'\n'))
        {
            savedId.pop_back();
        }
    }

    auto showPagedChoice =
        [&](std::wstring const& prompt, std::vector<std::wstring> const& labels, int preferredIndex) -> int
    {
        if (labels.empty())
        {
            return -1;
        }

        constexpr size_t kChoicesPerPage = 2;
        size_t page = preferredIndex > 0 ? static_cast<size_t>(preferredIndex) / kChoicesPerPage : 0;
        while (true)
        {
            size_t start = page * kChoicesPerPage;
            if (start >= labels.size())
            {
                page = 0;
                start = 0;
            }

            size_t count = std::min(kChoicesPerPage, labels.size() - start);
            bool hasNext = start + count < labels.size();
            bool hasBack = page > 0;
            std::wstring nextLabel = L"More...";
            std::wstring backLabel = L"Back";

            MessageDialog dialog{ hstring(prompt) };
            int defaultIndex = 0;
            for (size_t i = 0; i < count; ++i)
            {
                dialog.Commands().Append(UICommand(labels[start + i]));
                if (preferredIndex >= 0 && static_cast<size_t>(preferredIndex) == start + i)
                {
                    defaultIndex = static_cast<int>(i);
                }
            }
            if (hasNext)
            {
                dialog.Commands().Append(UICommand(nextLabel));
            }
            else if (hasBack)
            {
                dialog.Commands().Append(UICommand(backLabel));
            }
            dialog.DefaultCommandIndex(defaultIndex);

            IUICommand selection = ShowDialogAndPump(window, dialog);
            std::wstring label = selection ? selection.Label().c_str() : std::wstring();
            if (label == nextLabel && hasNext)
            {
                ++page;
                continue;
            }
            if (label == backLabel && hasBack)
            {
                --page;
                continue;
            }

            for (size_t i = 0; i < count; ++i)
            {
                if (label == labels[start + i])
                {
                    return static_cast<int>(start + i);
                }
            }

            return -1;
        }
    };

    try
    {
        std::vector<std::wstring> baseVersions;
        for (auto const& profile : profiles)
        {
            std::wstring baseVersion = profile.baseVersion.empty() ? profile.id : profile.baseVersion;
            if (std::find(baseVersions.begin(), baseVersions.end(), baseVersion) == baseVersions.end())
            {
                baseVersions.push_back(baseVersion);
            }
        }

        std::sort(baseVersions.begin(), baseVersions.end());

        int preferredBaseIndex = 0;
        for (size_t i = 0; i < profiles.size(); ++i)
        {
            if (profiles[i].id == savedId)
            {
                std::wstring baseVersion = profiles[i].baseVersion.empty() ? profiles[i].id : profiles[i].baseVersion;
                for (size_t j = 0; j < baseVersions.size(); ++j)
                {
                    if (baseVersions[j] == baseVersion)
                    {
                        preferredBaseIndex = static_cast<int>(j);
                        break;
                    }
                }
                break;
            }
        }

        int baseIndex = showPagedChoice(L"Choose the Minecraft version to launch.", baseVersions, preferredBaseIndex);
        if (baseIndex < 0 || static_cast<size_t>(baseIndex) >= baseVersions.size())
        {
            throw hresult_error(E_FAIL);
        }

        std::wstring selectedBaseVersion = baseVersions[static_cast<size_t>(baseIndex)];
        std::vector<size_t> matchingProfiles;
        for (size_t i = 0; i < profiles.size(); ++i)
        {
            std::wstring baseVersion = profiles[i].baseVersion.empty() ? profiles[i].id : profiles[i].baseVersion;
            if (baseVersion == selectedBaseVersion)
            {
                matchingProfiles.push_back(i);
            }
        }

        if (matchingProfiles.empty())
        {
            throw hresult_error(E_FAIL);
        }

        std::sort(matchingProfiles.begin(), matchingProfiles.end(), [&](size_t a, size_t b)
        {
            return profiles[a].label < profiles[b].label;
        });

        size_t selectedProfileIndex = matchingProfiles[0];
        if (matchingProfiles.size() > 1)
        {
            std::vector<std::wstring> loaderLabels;
            int preferredLoaderIndex = 0;
            for (size_t i = 0; i < matchingProfiles.size(); ++i)
            {
                MinecraftLaunchProfile const& profile = profiles[matchingProfiles[i]];
                loaderLabels.push_back(profile.modLoader.empty() ? profile.label : profile.modLoader);
                if (profile.id == savedId)
                {
                    preferredLoaderIndex = static_cast<int>(i);
                }
            }

            int loaderIndex = showPagedChoice(
                L"Choose mod loader for Minecraft " + selectedBaseVersion + L".",
                loaderLabels,
                preferredLoaderIndex);
            if (loaderIndex < 0 || static_cast<size_t>(loaderIndex) >= matchingProfiles.size())
            {
                throw hresult_error(E_FAIL);
            }
            selectedProfileIndex = matchingProfiles[static_cast<size_t>(loaderIndex)];
        }

        MinecraftLaunchProfile const& profile = profiles[selectedProfileIndex];
        WriteLogF(L"Selected Minecraft launch profile %s root=%s", profile.label.c_str(), profile.root.c_str());
        WriteTextFileUtf8(localRoot + L"\\selected-minecraft-profile.txt", WideToUtf8(profile.id));
        return profile;
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"Version picker failed hr=0x%08X; using fallback profile", static_cast<unsigned int>(ex.code()));
    }
    catch (...)
    {
        WriteLogW(L"Version picker failed with unknown exception; using fallback profile");
    }

    MinecraftLaunchProfile fallback = FallbackMinecraftLaunchProfile(profiles, savedId);
    WriteLogF(L"Fallback Minecraft launch profile %s root=%s", fallback.label.c_str(), fallback.root.c_str());
    WriteTextFileUtf8(localRoot + L"\\selected-minecraft-profile.txt", WideToUtf8(fallback.id));
    return fallback;
}

static std::wstring ResolveStagedMinecraftVersion(std::wstring const& localRoot)
{
    std::string json;
    std::wstring version;
    if (ReadSmallTextFileUtf8(localRoot + L"\\staging-summary.json", json) &&
        TryReadJsonString(json, "Version", version))
    {
        return version;
    }

    json.clear();
    if (ReadSmallTextFileUtf8(localRoot + L"\\download-manifest.json", json) &&
        TryReadJsonString(json, "MinecraftVersion", version))
    {
        return version;
    }

    return L"1.21.1";
}

static std::wstring InferBaseMinecraftVersion(std::wstring const& version)
{
    std::wstring lower = ToLowerInvariant(version);
    size_t forgeMarker = lower.find(L"-forge-");
    if (forgeMarker != std::wstring::npos)
    {
        return version.substr(0, forgeMarker);
    }

    size_t fabricMarker = lower.find(L"-fabric-");
    if (fabricMarker != std::wstring::npos)
    {
        return version.substr(0, fabricMarker);
    }

    size_t neoForgeMarker = lower.find(L"-neoforge-");
    if (neoForgeMarker != std::wstring::npos)
    {
        return version.substr(0, neoForgeMarker);
    }

    return version;
}

static std::wstring ResolveStagedBaseMinecraftVersion(std::wstring const& localRoot, std::wstring const& fallback)
{
    std::string json;
    std::wstring version;
    if (ReadSmallTextFileUtf8(localRoot + L"\\staging-summary.json", json))
    {
        if (TryReadJsonString(json, "BaseMinecraftVersion", version) ||
            TryReadJsonString(json, "MinecraftVersion", version))
        {
            return version;
        }
    }

    json.clear();
    if (ReadSmallTextFileUtf8(localRoot + L"\\download-manifest.json", json) &&
        TryReadJsonString(json, "MinecraftVersion", version))
    {
        return version;
    }

    return InferBaseMinecraftVersion(fallback);
}

static std::wstring ResolveStagedAssetIndex(std::wstring const& localRoot, std::wstring const& fallback)
{
    std::string json;
    std::wstring assetIndex;
    if (ReadSmallTextFileUtf8(localRoot + L"\\staging-summary.json", json) &&
        TryReadJsonString(json, "AssetIndex", assetIndex))
    {
        return assetIndex;
    }

    return fallback;
}

static int ParseMinecraftMinorVersion(std::wstring const& version)
{
    if (version.size() < 3 || version[0] != L'1' || version[1] != L'.')
    {
        return -1;
    }

    int minor = 0;
    size_t pos = 2;
    bool any = false;
    while (pos < version.size() && version[pos] >= L'0' && version[pos] <= L'9')
    {
        any = true;
        minor = (minor * 10) + static_cast<int>(version[pos] - L'0');
        ++pos;
    }

    return any ? minor : -1;
}

static bool UsesLegacyMinecraftArguments(std::wstring const& version)
{
    int minor = ParseMinecraftMinorVersion(version);
    return minor > 0 && minor <= 12;
}

static int ResolveStagedJavaRuntimeMajor(std::wstring const& localRoot, std::wstring const& minecraftVersion)
{
    std::string json;
    int javaMajor = 0;
    if (ReadSmallTextFileUtf8(localRoot + L"\\staging-summary.json", json) &&
        TryReadJsonInt(json, "JavaRuntimeMajor", javaMajor) &&
        javaMajor > 0)
    {
        return javaMajor;
    }

    int minor = ParseMinecraftMinorVersion(minecraftVersion);
    if (minor > 0 && minor <= 16)
    {
        return 8;
    }
    if (minor > 0 && minor <= 20)
    {
        return 17;
    }

    return 21;
}

static HMODULE LoadDllFromDirectory(std::wstring const& directory, const wchar_t* fileName, bool required)
{
    std::wstring path = directory + L"\\" + fileName;
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module)
    {
        WriteLogF(L"LoadLibraryExW(%s, LOAD_WITH_ALTERED_SEARCH_PATH) succeeded", path.c_str());
        return module;
    }

    WriteLogF(L"LoadLibraryExW(%s, LOAD_WITH_ALTERED_SEARCH_PATH) failed GetLastError=%lu", path.c_str(), GetLastError());
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
    module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module)
    {
        WriteLogF(L"LoadLibraryExW(%s, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR) succeeded", path.c_str());
        return module;
    }

    WriteLogF(L"LoadLibraryExW(%s, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR) failed GetLastError=%lu", path.c_str(), GetLastError());
    module = LoadLibraryW(path.c_str());
    if (module)
    {
        WriteLogF(L"LoadLibraryW(%s) succeeded", path.c_str());
        return module;
    }

    WriteLogF(L"LoadLibraryW(%s) failed GetLastError=%lu", path.c_str(), GetLastError());
    module = LoadPackagedLibrary(fileName, 0);
    if (module)
    {
        WriteLogF(L"LoadPackagedLibrary(%s) succeeded", fileName);
        return module;
    }

    if (!module && required)
    {
        WriteLogF(L"LoadPackagedLibrary(%s) fallback failed GetLastError=%lu", fileName, GetLastError());
    }
    return module;
}

static void PreloadMesaDependencies(std::wstring const& mesaDir)
{
    const wchar_t* dependencies[] =
    {
        L"vcruntime140_app.dll",
        L"vcruntime140_1_app.dll",
        L"msvcp140_app.dll",
        L"vccorlib140_app.dll",
        L"vcruntime140.dll",
        L"vcruntime140_1.dll",
        L"msvcp140.dll",
        L"libglapi.dll",
        L"xbox_fmalloc.dll",
        L"z-1.dll",
        L"dxil.dll",
        L"glu32.dll",
#ifndef MINECRAFT_JAVA_TEST_FIXES
        L"libgallium_wgl.dll",
#endif
        L"libEGL.dll",
        L"libGLESv2.dll",
        L"libGLESv1_CM.dll",
    };

    WriteLogW(L"Preloading Mesa dependency DLLs");
    for (auto name : dependencies)
    {
        if (FileExists(mesaDir + L"\\" + name))
        {
            LoadDllFromDirectory(mesaDir, name, false);
        }
        else if (_wcsicmp(name, L"dxil.dll") == 0)
        {
            WriteLogF(L"WARN: Mesa dependency missing: %ls", name);
        }
    }
}

static int LogSehA(const char* call, unsigned long code)
{
    char line[256] = {};
    std::snprintf(line, sizeof(line), "%s raised SEH exception 0x%08lx", call, code);
    WriteLogA(line);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void TryCallMesaProcInit(HMODULE openGlModule)
{
    if (!openGlModule)
    {
        WriteLogW(L"WARN: Mesa proc_init skipped; opengl32 module is null");
        return;
    }

    using ProcInitFn = void(*)();
    auto procInit = reinterpret_cast<ProcInitFn>(GetProcAddress(openGlModule, "proc_init"));
    if (!procInit)
    {
        WriteLogW(L"WARN: Mesa opengl32!proc_init export missing");
        return;
    }

    WriteLogW(L"Calling Mesa opengl32!proc_init");
    procInit();
    WriteLogW(L"Mesa opengl32!proc_init returned");
}

static EGLDisplay SafeGetDisplay(EglGetDisplayProc proc, void* nativeDisplay)
{
    EGLDisplay display = EGL_NO_DISPLAY_VALUE;
    __try
    {
        display = proc ? proc(nativeDisplay) : EGL_NO_DISPLAY_VALUE;
    }
    __except (LogSehA("eglGetDisplay", GetExceptionCode()))
    {
        display = EGL_NO_DISPLAY_VALUE;
    }
    return display;
}

static EGLBoolean SafeInitialize(EglInitializeProc proc, EGLDisplay display, EGLint* major, EGLint* minor)
{
    EGLBoolean result = EGL_FALSE_VALUE;
    __try
    {
        result = proc ? proc(display, major, minor) : EGL_FALSE_VALUE;
    }
    __except (LogSehA("eglInitialize", GetExceptionCode()))
    {
        result = EGL_FALSE_VALUE;
    }
    return result;
}

static EGLSurface SafeCreateWindowSurface(
    const char* name,
    EglCreateWindowSurfaceProc createWindowSurface,
    EglCreatePlatformWindowSurfaceExtProc createPlatformWindowSurfaceEXT,
    EglCreatePlatformWindowSurfaceProc createPlatformWindowSurface,
    EGLDisplay display,
    EGLConfig config,
    void* nativeWindow,
    int method)
{
    EGLSurface surface = EGL_NO_SURFACE_VALUE;
    __try
    {
        if (method == 0 && createWindowSurface)
        {
            surface = createWindowSurface(display, config, nativeWindow, nullptr);
        }
        else if (method == 1 && createPlatformWindowSurfaceEXT)
        {
            surface = createPlatformWindowSurfaceEXT(display, config, nativeWindow, nullptr);
        }
        else if (method == 2 && createPlatformWindowSurface)
        {
            surface = createPlatformWindowSurface(display, config, nativeWindow, nullptr);
        }
    }
    __except (LogSehA(name, GetExceptionCode()))
    {
        surface = EGL_NO_SURFACE_VALUE;
    }
    return surface;
}

static EGLContext SafeCreateContext(
    const char* name,
    EglCreateContextProc proc,
    EGLDisplay display,
    EGLConfig config,
    const EGLint* attribs)
{
    EGLContext context = EGL_NO_CONTEXT_VALUE;
    __try
    {
        context = proc ? proc(display, config, EGL_NO_CONTEXT_VALUE, attribs) : EGL_NO_CONTEXT_VALUE;
    }
    __except (LogSehA(name, GetExceptionCode()))
    {
        context = EGL_NO_CONTEXT_VALUE;
    }
    return context;
}

static EGLBoolean SafeMakeCurrent(
    EglMakeCurrentProc proc,
    EGLDisplay display,
    EGLSurface surface,
    EGLContext context)
{
    EGLBoolean result = EGL_FALSE_VALUE;
    __try
    {
        result = proc ? proc(display, surface, surface, context) : EGL_FALSE_VALUE;
    }
    __except (LogSehA("eglMakeCurrent", GetExceptionCode()))
    {
        result = EGL_FALSE_VALUE;
    }
    return result;
}

static EGLBoolean SafeSwapBuffers(EglSwapBuffersProc proc, EGLDisplay display, EGLSurface surface)
{
    EGLBoolean result = EGL_FALSE_VALUE;
    __try
    {
        result = proc ? proc(display, surface) : EGL_FALSE_VALUE;
    }
    __except (LogSehA("eglSwapBuffers", GetExceptionCode()))
    {
        result = EGL_FALSE_VALUE;
    }
    return result;
}

static EGLint EglError(EglGetErrorProc getError)
{
    return getError ? getError() : 0;
}

static bool ResolveMesaDirectory(std::wstring& directory)
{
    std::vector<std::wstring> candidates;
    try
    {
        candidates.push_back(std::wstring(Package::Current().InstalledLocation().Path().c_str()) + L"\\native\\mesa-uwp");
    }
    catch (...)
    {
    }
    try
    {
        candidates.push_back(std::wstring(ApplicationData::Current().LocalFolder().Path().c_str()) + L"\\native\\mesa-uwp");
    }
    catch (...)
    {
    }

    for (auto const& candidate : candidates)
    {
        if (FileExists(candidate + L"\\opengl32.dll") &&
            FileExists(candidate + L"\\libEGL.dll") &&
            FileExists(candidate + L"\\libGLESv2.dll"))
        {
            directory = candidate;
            return true;
        }
        WriteLogF(L"Mesa candidate missing required files: %s", candidate.c_str());
    }

    return false;
}

static void* LoadEglProc(HMODULE module, EglGetProcAddressProc getProcAddress, const char* name)
{
    void* proc = module ? reinterpret_cast<void*>(GetProcAddress(module, name)) : nullptr;
    if (!proc && getProcAddress)
    {
        __try
        {
            proc = getProcAddress(name);
        }
        __except (LogSehA("eglGetProcAddress", GetExceptionCode()))
        {
            proc = nullptr;
        }
    }
    return proc;
}

using jint = int;
using jsize = int;
using jboolean = unsigned char;
using jobject = void*;
using jclass = void*;
using jmethodID = void*;
using jstring = void*;
using jobjectArray = void*;

struct JavaVM_;
struct JNIEnv_;
using JavaVM = JavaVM_;
using JNIEnv = JNIEnv_;

union jvalue
{
    jboolean z;
    signed char b;
    wchar_t c;
    short s;
    jint i;
    long long j;
    float f;
    double d;
    jobject l;
};

struct JavaVMOption
{
    char* optionString;
    void* extraInfo;
};

struct JavaVMInitArgs
{
    jint version;
    jint nOptions;
    JavaVMOption* options;
    jboolean ignoreUnrecognized;
};

static constexpr jint JNI_VERSION_1_8_VALUE = 0x00010008;
static constexpr jint JNI_OK_VALUE = 0;

using JNI_CreateJavaVMProc = jint(__stdcall*)(JavaVM**, void**, void*);
using FindClassProc = jclass(__stdcall*)(JNIEnv*, const char*);
using GetObjectClassProc = jclass(__stdcall*)(JNIEnv*, jobject);
using GetMethodIdProc = jmethodID(__stdcall*)(JNIEnv*, jclass, const char*, const char*);
using GetStaticMethodIdProc = jmethodID(__stdcall*)(JNIEnv*, jclass, const char*, const char*);
using NewObjectAProc = jobject(__stdcall*)(JNIEnv*, jclass, jmethodID, jvalue*);
using CallObjectMethodAProc = jobject(__stdcall*)(JNIEnv*, jobject, jmethodID, jvalue*);
using CallVoidMethodAProc = void(__stdcall*)(JNIEnv*, jobject, jmethodID, jvalue*);
using CallStaticObjectMethodAProc = jobject(__stdcall*)(JNIEnv*, jclass, jmethodID, jvalue*);
using CallStaticVoidMethodAProc = void(__stdcall*)(JNIEnv*, jclass, jmethodID, jvalue*);
using ExceptionOccurredProc = jobject(__stdcall*)(JNIEnv*);
using ExceptionDescribeProc = void(__stdcall*)(JNIEnv*);
using ExceptionClearProc = void(__stdcall*)(JNIEnv*);
using NewStringUtfProc = jstring(__stdcall*)(JNIEnv*, const char*);
using GetStringUtfCharsProc = const char*(__stdcall*)(JNIEnv*, jstring, jboolean*);
using ReleaseStringUtfCharsProc = void(__stdcall*)(JNIEnv*, jstring, const char*);
using NewObjectArrayProc = jobjectArray(__stdcall*)(JNIEnv*, jsize, jclass, jobject);
using SetObjectArrayElementProc = void(__stdcall*)(JNIEnv*, jobjectArray, jsize, jobject);

struct JniApi
{
    FindClassProc findClass = nullptr;
    GetObjectClassProc getObjectClass = nullptr;
    GetMethodIdProc getMethodId = nullptr;
    GetStaticMethodIdProc getStaticMethodId = nullptr;
    NewObjectAProc newObjectA = nullptr;
    CallObjectMethodAProc callObjectMethodA = nullptr;
    CallVoidMethodAProc callVoidMethodA = nullptr;
    CallStaticObjectMethodAProc callStaticObjectMethodA = nullptr;
    CallStaticVoidMethodAProc callStaticVoidMethodA = nullptr;
    ExceptionOccurredProc exceptionOccurred = nullptr;
    ExceptionDescribeProc exceptionDescribe = nullptr;
    ExceptionClearProc exceptionClear = nullptr;
    NewStringUtfProc newStringUtf = nullptr;
    GetStringUtfCharsProc getStringUtfChars = nullptr;
    ReleaseStringUtfCharsProc releaseStringUtfChars = nullptr;
    NewObjectArrayProc newObjectArray = nullptr;
    SetObjectArrayElementProc setObjectArrayElement = nullptr;
};

template <typename TProc>
static TProc GetJniFunction(JNIEnv* env, size_t index)
{
    if (!env)
    {
        return nullptr;
    }

    void** table = *reinterpret_cast<void***>(env);
    if (!table)
    {
        return nullptr;
    }

    return reinterpret_cast<TProc>(table[index]);
}

static bool InitializeJniApi(JNIEnv* env, JniApi& api)
{
    api.findClass = GetJniFunction<FindClassProc>(env, 6);
    api.exceptionOccurred = GetJniFunction<ExceptionOccurredProc>(env, 15);
    api.exceptionDescribe = GetJniFunction<ExceptionDescribeProc>(env, 16);
    api.exceptionClear = GetJniFunction<ExceptionClearProc>(env, 17);
    api.newObjectA = GetJniFunction<NewObjectAProc>(env, 30);
    api.getObjectClass = GetJniFunction<GetObjectClassProc>(env, 31);
    api.getMethodId = GetJniFunction<GetMethodIdProc>(env, 33);
    api.callObjectMethodA = GetJniFunction<CallObjectMethodAProc>(env, 36);
    api.callVoidMethodA = GetJniFunction<CallVoidMethodAProc>(env, 63);
    api.getStaticMethodId = GetJniFunction<GetStaticMethodIdProc>(env, 113);
    api.callStaticObjectMethodA = GetJniFunction<CallStaticObjectMethodAProc>(env, 116);
    api.callStaticVoidMethodA = GetJniFunction<CallStaticVoidMethodAProc>(env, 143);
    api.newStringUtf = GetJniFunction<NewStringUtfProc>(env, 167);
    api.getStringUtfChars = GetJniFunction<GetStringUtfCharsProc>(env, 169);
    api.releaseStringUtfChars = GetJniFunction<ReleaseStringUtfCharsProc>(env, 170);
    api.newObjectArray = GetJniFunction<NewObjectArrayProc>(env, 172);
    api.setObjectArrayElement = GetJniFunction<SetObjectArrayElementProc>(env, 174);

    bool ok =
        api.findClass &&
        api.getObjectClass &&
        api.getMethodId &&
        api.exceptionOccurred &&
        api.exceptionDescribe &&
        api.exceptionClear &&
        api.newObjectA &&
        api.callObjectMethodA &&
        api.callVoidMethodA &&
        api.getStaticMethodId &&
        api.callStaticObjectMethodA &&
        api.callStaticVoidMethodA &&
        api.newStringUtf &&
        api.getStringUtfChars &&
        api.releaseStringUtfChars &&
        api.newObjectArray &&
        api.setObjectArrayElement;
    WriteLogW(ok ? L"JNI function table resolved" : L"FAIL: JNI function table missing required entries");
    return ok;
}

static void ClearJavaExceptionQuietly(JNIEnv* env, JniApi const& api)
{
    if (api.exceptionOccurred && api.exceptionOccurred(env) && api.exceptionClear)
    {
        api.exceptionClear(env);
    }
}

static const char* SafeGetStringUtfChars(GetStringUtfCharsProc proc, JNIEnv* env, jstring value)
{
    const char* chars = nullptr;
    __try
    {
        chars = proc ? proc(env, value, nullptr) : nullptr;
    }
    __except (LogSehA("GetStringUTFChars", GetExceptionCode()))
    {
        chars = nullptr;
    }
    return chars;
}

static void SafeReleaseStringUtfChars(ReleaseStringUtfCharsProc proc, JNIEnv* env, jstring value, const char* chars)
{
    __try
    {
        if (proc && chars)
        {
            proc(env, value, chars);
        }
    }
    __except (LogSehA("ReleaseStringUTFChars", GetExceptionCode()))
    {
    }
}

static std::string JniStringToUtf8(JNIEnv* env, JniApi const& api, jstring value)
{
    if (!value || !api.getStringUtfChars || !api.releaseStringUtfChars)
    {
        return std::string();
    }

    const char* chars = SafeGetStringUtfChars(api.getStringUtfChars, env, value);
    if (!chars)
    {
        ClearJavaExceptionQuietly(env, api);
        return std::string();
    }

    std::string text(chars);
    SafeReleaseStringUtfChars(api.releaseStringUtfChars, env, value, chars);
    ClearJavaExceptionQuietly(env, api);
    return text;
}

static void LogJavaThrowable(JNIEnv* env, JniApi const& api, jobject exception)
{
    if (!exception || !api.getObjectClass || !api.getMethodId || !api.callObjectMethodA)
    {
        return;
    }

    jclass throwableClass = api.getObjectClass(env, exception);
    ClearJavaExceptionQuietly(env, api);
    if (!throwableClass)
    {
        WriteLogW(L"Java exception detail unavailable: GetObjectClass failed");
        return;
    }

    jmethodID toStringMethod = api.getMethodId(env, throwableClass, "toString", "()Ljava/lang/String;");
    ClearJavaExceptionQuietly(env, api);
    if (toStringMethod)
    {
        jstring text = reinterpret_cast<jstring>(api.callObjectMethodA(env, exception, toStringMethod, nullptr));
        ClearJavaExceptionQuietly(env, api);
        std::string message = JniStringToUtf8(env, api, text);
        if (!message.empty())
        {
            WriteLogA(("Java exception: " + message).c_str());
        }
    }

    if (!api.findClass || !api.newObjectA || !api.callVoidMethodA)
    {
        return;
    }

    jclass stringWriterClass = api.findClass(env, "java/io/StringWriter");
    ClearJavaExceptionQuietly(env, api);
    jclass printWriterClass = api.findClass(env, "java/io/PrintWriter");
    ClearJavaExceptionQuietly(env, api);
    if (!stringWriterClass || !printWriterClass)
    {
        return;
    }

    jmethodID stringWriterCtor = api.getMethodId(env, stringWriterClass, "<init>", "()V");
    ClearJavaExceptionQuietly(env, api);
    jmethodID printWriterCtor = api.getMethodId(env, printWriterClass, "<init>", "(Ljava/io/Writer;)V");
    ClearJavaExceptionQuietly(env, api);
    jmethodID printStackTraceMethod = api.getMethodId(env, throwableClass, "printStackTrace", "(Ljava/io/PrintWriter;)V");
    ClearJavaExceptionQuietly(env, api);
    jmethodID stringWriterToString = api.getMethodId(env, stringWriterClass, "toString", "()Ljava/lang/String;");
    ClearJavaExceptionQuietly(env, api);
    if (!stringWriterCtor || !printWriterCtor || !printStackTraceMethod || !stringWriterToString)
    {
        return;
    }

    jobject stringWriter = api.newObjectA(env, stringWriterClass, stringWriterCtor, nullptr);
    ClearJavaExceptionQuietly(env, api);
    if (!stringWriter)
    {
        return;
    }

    jvalue printWriterArgs[1] = {};
    printWriterArgs[0].l = stringWriter;
    jobject printWriter = api.newObjectA(env, printWriterClass, printWriterCtor, printWriterArgs);
    ClearJavaExceptionQuietly(env, api);
    if (!printWriter)
    {
        return;
    }

    jvalue printStackArgs[1] = {};
    printStackArgs[0].l = printWriter;
    api.callVoidMethodA(env, exception, printStackTraceMethod, printStackArgs);
    ClearJavaExceptionQuietly(env, api);

    jstring stackText = reinterpret_cast<jstring>(api.callObjectMethodA(env, stringWriter, stringWriterToString, nullptr));
    ClearJavaExceptionQuietly(env, api);
    std::string stack = JniStringToUtf8(env, api, stackText);
    if (!stack.empty())
    {
        WriteLogA(("Java exception stack:\n" + stack).c_str());
    }
}

static bool ClearPendingJavaException(JNIEnv* env, JniApi const& api, const char* stage)
{
    if (!api.exceptionOccurred)
    {
        return false;
    }

    jobject exception = api.exceptionOccurred(env);
    if (!exception)
    {
        return false;
    }

    WriteLogA(stage);
    if (api.exceptionDescribe)
    {
        api.exceptionDescribe(env);
    }
    if (api.exceptionClear)
    {
        api.exceptionClear(env);
    }
    LogJavaThrowable(env, api, exception);
    return true;
}

static jint SafeCreateJavaVm(JNI_CreateJavaVMProc proc, JavaVM** javaVm, JNIEnv** env, JavaVMInitArgs* args)
{
    jint result = -1;
    void* rawEnv = nullptr;
    __try
    {
        result = proc(javaVm, &rawEnv, args);
    }
    __except (LogSehA("JNI_CreateJavaVM", GetExceptionCode()))
    {
        result = -1;
    }

    *env = reinterpret_cast<JNIEnv*>(rawEnv);
    return result;
}

static jclass SafeFindClass(FindClassProc proc, JNIEnv* env, const char* name, const char* label)
{
    jclass result = nullptr;
    __try
    {
        result = proc(env, name);
    }
    __except (LogSehA(label, GetExceptionCode()))
    {
        result = nullptr;
    }
    return result;
}

static bool SafeCallStaticVoidMethodA(
    CallStaticVoidMethodAProc proc,
    JNIEnv* env,
    jclass clazz,
    jmethodID method,
    jvalue* args,
    const char* label)
{
    bool ok = true;
    __try
    {
        proc(env, clazz, method, args);
    }
    __except (LogSehA(label, GetExceptionCode()))
    {
        ok = false;
    }
    return ok;
}

static jobject SafeCallObjectMethodA(
    CallObjectMethodAProc proc,
    JNIEnv* env,
    jobject object,
    jmethodID method,
    jvalue* args,
    const char* label)
{
    jobject result = nullptr;
    __try
    {
        result = proc(env, object, method, args);
    }
    __except (LogSehA(label, GetExceptionCode()))
    {
        result = nullptr;
    }
    return result;
}

static jobject SafeCallStaticObjectMethodA(
    CallStaticObjectMethodAProc proc,
    JNIEnv* env,
    jclass clazz,
    jmethodID method,
    jvalue* args,
    const char* label)
{
    jobject result = nullptr;
    __try
    {
        result = proc(env, clazz, method, args);
    }
    __except (LogSehA(label, GetExceptionCode()))
    {
        result = nullptr;
    }
    return result;
}

static bool BuildJavaStringArray(
    JNIEnv* env,
    JniApi const& api,
    std::vector<std::string> const& values,
    jobjectArray& array)
{
    jclass stringClass = SafeFindClass(api.findClass, env, "java/lang/String", "FindClass(java/lang/String)");
    if (!stringClass || ClearPendingJavaException(env, api, "FAIL: FindClass(java/lang/String) threw Java exception"))
    {
        WriteLogW(L"FAIL: FindClass(java/lang/String) failed");
        return false;
    }

    array = api.newObjectArray(env, static_cast<jsize>(values.size()), stringClass, nullptr);
    if (!array || ClearPendingJavaException(env, api, "FAIL: NewObjectArray(String[]) threw Java exception"))
    {
        WriteLogW(L"FAIL: NewObjectArray(String[]) failed");
        return false;
    }

    for (size_t i = 0; i < values.size(); ++i)
    {
        jstring value = api.newStringUtf(env, values[i].c_str());
        if (!value || ClearPendingJavaException(env, api, "FAIL: NewStringUTF threw Java exception"))
        {
            WriteLogF(L"FAIL: NewStringUTF failed for argument %zu", i);
            return false;
        }

        api.setObjectArrayElement(env, array, static_cast<jsize>(i), value);
        if (ClearPendingJavaException(env, api, "FAIL: SetObjectArrayElement threw Java exception"))
        {
            WriteLogF(L"FAIL: SetObjectArrayElement failed for argument %zu", i);
            return false;
        }
    }

    return true;
}

static jclass LoadClassWithSystemClassLoader(JNIEnv* env, JniApi const& api, const char* className)
{
    if (!api.callStaticObjectMethodA || !api.callObjectMethodA)
    {
        WriteLogW(L"FAIL: system ClassLoader fallback unavailable; JNI functions missing");
        return nullptr;
    }

    jclass classLoaderClass = SafeFindClass(api.findClass, env, "java/lang/ClassLoader", "FindClass(java/lang/ClassLoader)");
    bool classLoaderFindException = ClearPendingJavaException(env, api, "FAIL: FindClass(java/lang/ClassLoader) threw Java exception");
    if (!classLoaderClass || classLoaderFindException)
    {
        WriteLogW(L"FAIL: FindClass(java/lang/ClassLoader) failed");
        return nullptr;
    }

    jmethodID getSystemClassLoader = api.getStaticMethodId(
        env,
        classLoaderClass,
        "getSystemClassLoader",
        "()Ljava/lang/ClassLoader;");
    if (!getSystemClassLoader || ClearPendingJavaException(env, api, "FAIL: GetStaticMethodID(ClassLoader.getSystemClassLoader) threw Java exception"))
    {
        WriteLogW(L"FAIL: GetStaticMethodID(ClassLoader.getSystemClassLoader) failed");
        return nullptr;
    }

    jobject systemClassLoader = SafeCallStaticObjectMethodA(
        api.callStaticObjectMethodA,
        env,
        classLoaderClass,
        getSystemClassLoader,
        nullptr,
        "CallStaticObjectMethodA(ClassLoader.getSystemClassLoader)");
    if (!systemClassLoader || ClearPendingJavaException(env, api, "FAIL: ClassLoader.getSystemClassLoader threw Java exception"))
    {
        WriteLogW(L"FAIL: ClassLoader.getSystemClassLoader failed");
        return nullptr;
    }

    jmethodID loadClass = api.getMethodId(
        env,
        classLoaderClass,
        "loadClass",
        "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClass || ClearPendingJavaException(env, api, "FAIL: GetMethodID(ClassLoader.loadClass) threw Java exception"))
    {
        WriteLogW(L"FAIL: GetMethodID(ClassLoader.loadClass) failed");
        return nullptr;
    }

    std::string dottedName = className ? className : "";
    std::replace(dottedName.begin(), dottedName.end(), '/', '.');
    jstring javaName = api.newStringUtf(env, dottedName.c_str());
    if (!javaName || ClearPendingJavaException(env, api, "FAIL: NewStringUTF(ClassLoader class name) threw Java exception"))
    {
        WriteLogA(("FAIL: NewStringUTF(" + dottedName + ") failed").c_str());
        return nullptr;
    }

    jvalue args[1] = {};
    args[0].l = javaName;
    jobject loadedClass = SafeCallObjectMethodA(
        api.callObjectMethodA,
        env,
        systemClassLoader,
        loadClass,
        args,
        "CallObjectMethodA(ClassLoader.loadClass)");
    if (!loadedClass || ClearPendingJavaException(env, api, "FAIL: ClassLoader.loadClass threw Java exception"))
    {
        WriteLogA(("FAIL: ClassLoader.loadClass(" + dottedName + ") failed").c_str());
        return nullptr;
    }

    WriteLogA(("ClassLoader.loadClass(" + dottedName + ") succeeded").c_str());
    return reinterpret_cast<jclass>(loadedClass);
}

static jclass LoadClassFromBootModuleLayer(JNIEnv* env, JniApi const& api, const char* moduleName, const char* className)
{
    if (!api.callStaticObjectMethodA || !api.callObjectMethodA)
    {
        WriteLogW(L"FAIL: boot ModuleLayer fallback unavailable; JNI functions missing");
        return nullptr;
    }

    jclass moduleLayerClass = SafeFindClass(api.findClass, env, "java/lang/ModuleLayer", "FindClass(java/lang/ModuleLayer)");
    bool moduleLayerFindException = ClearPendingJavaException(env, api, "FAIL: FindClass(java/lang/ModuleLayer) threw Java exception");
    if (!moduleLayerClass || moduleLayerFindException)
    {
        WriteLogW(L"FAIL: FindClass(java/lang/ModuleLayer) failed");
        return nullptr;
    }

    jmethodID bootMethod = api.getStaticMethodId(env, moduleLayerClass, "boot", "()Ljava/lang/ModuleLayer;");
    if (!bootMethod || ClearPendingJavaException(env, api, "FAIL: GetStaticMethodID(ModuleLayer.boot) threw Java exception"))
    {
        WriteLogW(L"FAIL: GetStaticMethodID(ModuleLayer.boot) failed");
        return nullptr;
    }

    jobject bootLayer = SafeCallStaticObjectMethodA(
        api.callStaticObjectMethodA,
        env,
        moduleLayerClass,
        bootMethod,
        nullptr,
        "CallStaticObjectMethodA(ModuleLayer.boot)");
    if (!bootLayer || ClearPendingJavaException(env, api, "FAIL: ModuleLayer.boot threw Java exception"))
    {
        WriteLogW(L"FAIL: ModuleLayer.boot failed");
        return nullptr;
    }

    jmethodID findLoaderMethod = api.getMethodId(
        env,
        moduleLayerClass,
        "findLoader",
        "(Ljava/lang/String;)Ljava/lang/ClassLoader;");
    if (!findLoaderMethod || ClearPendingJavaException(env, api, "FAIL: GetMethodID(ModuleLayer.findLoader) threw Java exception"))
    {
        WriteLogW(L"FAIL: GetMethodID(ModuleLayer.findLoader) failed");
        return nullptr;
    }

    jstring javaModuleName = api.newStringUtf(env, moduleName);
    if (!javaModuleName || ClearPendingJavaException(env, api, "FAIL: NewStringUTF(module name) threw Java exception"))
    {
        WriteLogA(("FAIL: NewStringUTF(" + std::string(moduleName) + ") failed").c_str());
        return nullptr;
    }

    jvalue findLoaderArgs[1] = {};
    findLoaderArgs[0].l = javaModuleName;
    jobject moduleLoader = SafeCallObjectMethodA(
        api.callObjectMethodA,
        env,
        bootLayer,
        findLoaderMethod,
        findLoaderArgs,
        "CallObjectMethodA(ModuleLayer.findLoader)");
    bool findLoaderException =
        ClearPendingJavaException(env, api, "FAIL: ModuleLayer.findLoader threw Java exception");
    if (!moduleLoader || findLoaderException)
    {
        WriteLogA(("FAIL: ModuleLayer.findLoader(" + std::string(moduleName) + ") failed").c_str());
        return nullptr;
    }

    jclass classLoaderClass = SafeFindClass(api.findClass, env, "java/lang/ClassLoader", "FindClass(java/lang/ClassLoader)");
    bool classLoaderFindException = ClearPendingJavaException(env, api, "FAIL: FindClass(java/lang/ClassLoader) threw Java exception");
    if (!classLoaderClass || classLoaderFindException)
    {
        WriteLogW(L"FAIL: FindClass(java/lang/ClassLoader) failed");
        return nullptr;
    }

    jmethodID loadClass = api.getMethodId(
        env,
        classLoaderClass,
        "loadClass",
        "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClass || ClearPendingJavaException(env, api, "FAIL: GetMethodID(ClassLoader.loadClass) threw Java exception"))
    {
        WriteLogW(L"FAIL: GetMethodID(ClassLoader.loadClass) failed");
        return nullptr;
    }

    std::string dottedName = className ? className : "";
    std::replace(dottedName.begin(), dottedName.end(), '/', '.');
    jstring javaName = api.newStringUtf(env, dottedName.c_str());
    if (!javaName || ClearPendingJavaException(env, api, "FAIL: NewStringUTF(module class name) threw Java exception"))
    {
        WriteLogA(("FAIL: NewStringUTF(" + dottedName + ") failed").c_str());
        return nullptr;
    }

    jvalue loadClassArgs[1] = {};
    loadClassArgs[0].l = javaName;
    jobject loadedClass = SafeCallObjectMethodA(
        api.callObjectMethodA,
        env,
        moduleLoader,
        loadClass,
        loadClassArgs,
        "CallObjectMethodA(module ClassLoader.loadClass)");
    bool moduleLoadClassException =
        ClearPendingJavaException(env, api, "FAIL: module ClassLoader.loadClass threw Java exception");
    if (!loadedClass || moduleLoadClassException)
    {
        WriteLogA(("FAIL: ModuleLayer loader loadClass(" + dottedName + ") failed").c_str());
        return nullptr;
    }

    WriteLogA(("ModuleLayer loader loadClass(" + std::string(moduleName) + "/" + dottedName + ") succeeded").c_str());
    return reinterpret_cast<jclass>(loadedClass);
}

static bool InvokeJavaMain(
    JNIEnv* env,
    JniApi const& api,
    const char* className,
    std::vector<std::string> const& appArgs)
{
    jclass mainClass = SafeFindClass(api.findClass, env, className, "FindClass(main)");
    bool findClassException = ClearPendingJavaException(env, api, "FAIL: FindClass(main) threw Java exception");
    if (!mainClass || findClassException)
    {
        std::string classNameText = className ? className : "";
        if (classNameText == "cpw/mods/bootstraplauncher/BootstrapLauncher")
        {
            WriteLogA(("WARN: FindClass(" + classNameText + ") failed; trying boot ModuleLayer loader").c_str());
            mainClass = LoadClassFromBootModuleLayer(env, api, "cpw.mods.bootstraplauncher", className);
        }
        if (!mainClass)
        {
            WriteLogA(("WARN: FindClass(" + std::string(className) + ") failed; trying system ClassLoader").c_str());
            mainClass = LoadClassWithSystemClassLoader(env, api, className);
        }
    }
    if (!mainClass)
    {
        WriteLogA(("FAIL: FindClass(" + std::string(className) + ") failed").c_str());
        return false;
    }

    jmethodID mainMethod = api.getStaticMethodId(env, mainClass, "main", "([Ljava/lang/String;)V");
    if (!mainMethod || ClearPendingJavaException(env, api, "FAIL: GetStaticMethodID(main) threw Java exception"))
    {
        WriteLogA(("FAIL: GetStaticMethodID(" + std::string(className) + ".main) failed").c_str());
        return false;
    }

    jobjectArray javaArgs = nullptr;
    if (!BuildJavaStringArray(env, api, appArgs, javaArgs))
    {
        return false;
    }

    jvalue callArgs[1] = {};
    callArgs[0].l = javaArgs;
    WriteLogA(("Calling " + std::string(className) + ".main").c_str());
    if (!SafeCallStaticVoidMethodA(
        api.callStaticVoidMethodA,
        env,
        mainClass,
        mainMethod,
        callArgs,
        "CallStaticVoidMethodA(main)"))
    {
        return false;
    }

    if (ClearPendingJavaException(env, api, "FAIL: Java main threw a Java exception"))
    {
        return false;
    }

    WriteLogA((std::string(className) + ".main returned").c_str());
    return true;
}

static bool RunNativeJvmSmoke()
{
    WriteLogW(L"=== Native embedded JVM smoke starting ===");

    std::wstring localRoot;
    std::wstring packageRoot;
    try
    {
        localRoot = ApplicationData::Current().LocalFolder().Path().c_str();
        packageRoot = Package::Current().InstalledLocation().Path().c_str();
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"FAIL: unable to resolve app paths hr=0x%08X", static_cast<unsigned int>(ex.code()));
        return false;
    }

    std::wstring jreRoot = localRoot + L"\\runtime\\jre";
    std::wstring javaBin = jreRoot + L"\\bin";
    std::wstring serverDir = javaBin + L"\\server";
    std::wstring jvmPath = serverDir + L"\\jvm.dll";
    std::wstring localNativeDir = localRoot + L"\\native";
    std::wstring packageNativeDir = packageRoot + L"\\native";
    std::wstring nativeDir = DirectoryExists(localNativeDir) ? localNativeDir : packageNativeDir;
    std::wstring gameDir = localRoot + L"\\game";
    std::wstring tempDir = gameDir + L"\\tmp";
    std::wstring jnaTempDir = tempDir + L"\\jna";

    WriteLogF(L"JVM smoke LocalState=%s", localRoot.c_str());
    WriteLogF(L"JVM smoke PackageRoot=%s", packageRoot.c_str());
    WriteLogF(L"JVM smoke jvm.dll=%s exists=%d", jvmPath.c_str(), FileExists(jvmPath) ? 1 : 0);
    if (!FileExists(jvmPath))
    {
        WriteLogW(L"FAIL: embedded JVM smoke cannot find runtime\\jre\\bin\\server\\jvm.dll");
        return false;
    }

    std::wstring probeJar = ResolvePackagedOrLocalFile(localRoot, packageRoot, L"native\\XboxPathProbe.jar");
    std::wstring patchJar = ResolvePackagedOrLocalFile(localRoot, packageRoot, L"native\\xbox-jdk-patch.jar");
    WriteLogF(L"JVM smoke XboxPathProbe.jar=%s", probeJar.empty() ? L"<missing>" : probeJar.c_str());
    WriteLogF(L"JVM smoke xbox-jdk-patch.jar=%s", patchJar.empty() ? L"<missing>" : patchJar.c_str());
    if (probeJar.empty())
    {
        WriteLogW(L"FAIL: XboxPathProbe.jar missing from LocalState or package native directory");
        return false;
    }

    std::wstring mesaDir;
    ResolveMesaDirectory(mesaDir);
    EnsureDirectory(gameDir);
    EnsureDirectory(tempDir);
    EnsureDirectory(jnaTempDir);
    if (!SetCurrentDirectoryW(gameDir.c_str()))
    {
        WriteLogF(L"WARN: SetCurrentDirectoryW(gameDir) failed GetLastError=%lu", GetLastError());
    }

    SetEnvironmentVariableW(L"JAVA_HOME", jreRoot.c_str());
    SetEnvironmentVariableW(L"TMP", tempDir.c_str());
    SetEnvironmentVariableW(L"TEMP", tempDir.c_str());
    PrependEnvironmentPath({ serverDir, javaBin, nativeDir, mesaDir });

    const wchar_t* javaBinDeps[] =
    {
        L"vcruntime140.dll",
        L"vcruntime140_1.dll",
        L"msvcp140.dll",
        L"ucrtbase.dll",
        L"jli.dll",
        L"java.dll",
        L"jimage.dll",
        L"zip.dll",
        L"nio.dll",
        L"net.dll",
    };

    WriteLogW(L"Preloading Java runtime dependency DLLs");
    for (auto name : javaBinDeps)
    {
        if (FileExists(javaBin + L"\\" + name))
        {
            LoadDllFromDirectory(javaBin, name, false);
        }
    }

    HMODULE jvmLibrary = LoadDllFromDirectory(serverDir, L"jvm.dll", true);
    if (!jvmLibrary)
    {
        WriteLogW(L"FAIL: LoadLibraryExW(jvm.dll) failed for native JVM smoke");
        return false;
    }

    auto createJavaVm = reinterpret_cast<JNI_CreateJavaVMProc>(GetProcAddress(jvmLibrary, "JNI_CreateJavaVM"));
    if (!createJavaVm)
    {
        WriteLogW(L"FAIL: JNI_CreateJavaVM export missing from jvm.dll");
        return false;
    }

    std::vector<std::string> optionText;
    optionText.reserve(18);
    optionText.emplace_back("-Xrs");
    optionText.emplace_back("-Xms64m");
    optionText.emplace_back("-Xmx512m");
    optionText.emplace_back("-Djava.awt.headless=true");
    optionText.emplace_back("-Dsun.stdout.encoding=UTF-8");
    optionText.emplace_back("-Dsun.stderr.encoding=UTF-8");
    optionText.emplace_back(WideToUtf8(L"-Djava.class.path=" + probeJar));
    optionText.emplace_back(WideToUtf8(L"-Djava.io.tmpdir=" + tempDir));
    optionText.emplace_back(WideToUtf8(L"-Duser.home=" + gameDir));
    optionText.emplace_back(WideToUtf8(L"-Duser.dir=" + gameDir));
    optionText.emplace_back(WideToUtf8(L"-Djava.library.path=" + nativeDir + L";" + javaBin + L";" + serverDir));
    optionText.emplace_back(WideToUtf8(L"-Djna.tmpdir=" + jnaTempDir));
    optionText.emplace_back(WideToUtf8(L"-Djna.library.path=" + nativeDir));
    optionText.emplace_back(WideToUtf8(L"-Djna.boot.library.path=" + nativeDir));
    if (!patchJar.empty())
    {
        optionText.emplace_back(WideToUtf8(L"--patch-module=java.base=" + patchJar));
    }

    std::vector<JavaVMOption> options;
    options.reserve(optionText.size());
    for (auto& text : optionText)
    {
        JavaVMOption option = {};
        option.optionString = text.data();
        option.extraInfo = nullptr;
        options.push_back(option);
        WriteLogA(("JVM option: " + text).c_str());
    }

    JavaVMInitArgs args = {};
    args.version = JNI_VERSION_1_8_VALUE;
    args.nOptions = static_cast<jint>(options.size());
    args.options = options.data();
    args.ignoreUnrecognized = 1;

    JavaVM* javaVm = nullptr;
    JNIEnv* env = nullptr;
    WriteLogF(L"JNI_CreateJavaVM starting optionCount=%d", args.nOptions);
    jint createResult = SafeCreateJavaVm(createJavaVm, &javaVm, &env, &args);

    WriteLogF(L"JNI_CreateJavaVM result=%d vm=%p env=%p", createResult, javaVm, env);
    if (createResult != JNI_OK_VALUE || !javaVm || !env)
    {
        WriteLogW(L"FAIL: JNI_CreateJavaVM did not start the JVM");
        return false;
    }

    JniApi api = {};
    if (!InitializeJniApi(env, api))
    {
        return false;
    }

    jclass probeClass = SafeFindClass(api.findClass, env, "XboxPathProbe", "FindClass(XboxPathProbe)");
    if (!probeClass || ClearPendingJavaException(env, api, "FAIL: FindClass(XboxPathProbe) threw Java exception"))
    {
        WriteLogW(L"FAIL: FindClass(XboxPathProbe) failed");
        return false;
    }
    WriteLogW(L"FindClass(XboxPathProbe) succeeded");

    jmethodID mainMethod = api.getStaticMethodId(env, probeClass, "main", "([Ljava/lang/String;)V");
    if (!mainMethod || ClearPendingJavaException(env, api, "FAIL: GetStaticMethodID(XboxPathProbe.main) threw Java exception"))
    {
        WriteLogW(L"FAIL: GetStaticMethodID(XboxPathProbe.main) failed");
        return false;
    }
    WriteLogW(L"GetStaticMethodID(XboxPathProbe.main) succeeded");

    jclass stringClass = api.findClass(env, "java/lang/String");
    if (!stringClass || ClearPendingJavaException(env, api, "FAIL: FindClass(java/lang/String) threw Java exception"))
    {
        WriteLogW(L"FAIL: FindClass(java/lang/String) failed");
        return false;
    }

    jobjectArray mainArgs = api.newObjectArray(env, 1, stringClass, nullptr);
    if (!mainArgs || ClearPendingJavaException(env, api, "FAIL: NewObjectArray(String[]) threw Java exception"))
    {
        WriteLogW(L"FAIL: NewObjectArray(String[]) failed");
        return false;
    }

    std::string gameDirArg = WideToUtf8(gameDir);
    jstring gameDirString = api.newStringUtf(env, gameDirArg.c_str());
    if (!gameDirString || ClearPendingJavaException(env, api, "FAIL: NewStringUTF(gameDir) threw Java exception"))
    {
        WriteLogW(L"FAIL: NewStringUTF(gameDir) failed");
        return false;
    }

    api.setObjectArrayElement(env, mainArgs, 0, gameDirString);
    if (ClearPendingJavaException(env, api, "FAIL: SetObjectArrayElement(args[0]) threw Java exception"))
    {
        return false;
    }

    jvalue callArgs[1] = {};
    callArgs[0].l = mainArgs;
    WriteLogW(L"Calling XboxPathProbe.main from native JVM smoke");
    if (!SafeCallStaticVoidMethodA(
        api.callStaticVoidMethodA,
        env,
        probeClass,
        mainMethod,
        callArgs,
        "CallStaticVoidMethodA(XboxPathProbe.main)"))
    {
        return false;
    }

    if (ClearPendingJavaException(env, api, "FAIL: XboxPathProbe.main threw Java exception"))
    {
        return false;
    }

    WriteLogW(L"PASS: Native embedded JVM smoke returned from XboxPathProbe.main");
    return true;
}

static bool TryCreateSurface(
    EglGetErrorProc getError,
    EglCreateWindowSurfaceProc createWindowSurface,
    EglCreatePlatformWindowSurfaceExtProc createPlatformWindowSurfaceEXT,
    EglCreatePlatformWindowSurfaceProc createPlatformWindowSurface,
    EGLDisplay display,
    EGLConfig config,
    void* coreWindow,
    void* descriptor,
    EGLSurface& surface)
{
    struct Attempt
    {
        const char* name;
        void* nativeWindow;
        int method;
    };

    Attempt attempts[] =
    {
        { "eglCreateWindowSurface(CoreWindow)", coreWindow, 0 },
        { "eglCreatePlatformWindowSurfaceEXT(CoreWindow)", coreWindow, 1 },
        { "eglCreatePlatformWindowSurface(CoreWindow)", coreWindow, 2 },
        { "eglCreateWindowSurface(PropertySet)", descriptor, 0 },
        { "eglCreatePlatformWindowSurfaceEXT(PropertySet)", descriptor, 1 },
        { "eglCreatePlatformWindowSurface(PropertySet)", descriptor, 2 },
    };

    for (auto const& attempt : attempts)
    {
        if (!attempt.nativeWindow)
        {
            continue;
        }

        char begin[256] = {};
        std::snprintf(begin, sizeof(begin), "%s nativeWindow=%p", attempt.name, attempt.nativeWindow);
        WriteLogA(begin);

        surface = SafeCreateWindowSurface(
            attempt.name,
            createWindowSurface,
            createPlatformWindowSurfaceEXT,
            createPlatformWindowSurface,
            display,
            config,
            attempt.nativeWindow,
            attempt.method);
        if (surface != EGL_NO_SURFACE_VALUE)
        {
            char success[192] = {};
            std::snprintf(success, sizeof(success), "%s succeeded surface=%p", attempt.name, surface);
            WriteLogA(success);
            return true;
        }

        char failed[192] = {};
        std::snprintf(failed, sizeof(failed), "%s failed err=0x%04x", attempt.name, EglError(getError));
        WriteLogA(failed);
    }

    return false;
}

static bool ProbeMesaEgl(CoreWindow const& window, bool preloadOpenGl = true, bool useDesktopOpenGl = false)
{
    WriteLogW(L"=== Native CoreWindow EGL probe starting ===");
    if (!window)
    {
        WriteLogW(L"FAIL: SetWindow did not provide a CoreWindow");
        return false;
    }

    Rect bounds = window.Bounds();
    WriteLogF(L"CoreWindow bounds %.0fx%.0f visible=%d thread=0x%lx",
        bounds.Width,
        bounds.Height,
        window.Visible() ? 1 : 0,
        GetCurrentThreadId());
    g_eglState.width = bounds.Width > 1.0f ? static_cast<int>(bounds.Width + 0.5f) : g_surfaceWidth;
    g_eglState.height = bounds.Height > 1.0f ? static_cast<int>(bounds.Height + 0.5f) : g_surfaceHeight;

    SetEnvironmentVariableW(L"MC_GRAPHICS_RUNTIME", L"desktop");
    SetEnvironmentVariableW(L"EGL_PLATFORM", L"windows");
    SetEnvironmentVariableW(L"GALLIUM_DRIVER", L"d3d12");
    SetEnvironmentVariableW(L"MESA_LOADER_DRIVER_OVERRIDE", L"d3d12");

    std::wstring mesaDir;
    if (!ResolveMesaDirectory(mesaDir))
    {
        WriteLogW(L"FAIL: Mesa runtime directory not found");
        return false;
    }

    WriteLogF(L"Mesa directory selected: %s", mesaDir.c_str());
    PreloadMesaDependencies(mesaDir);
    g_eglState.openGl = preloadOpenGl ? LoadDllFromDirectory(mesaDir, L"opengl32.dll", true) : nullptr;
    g_eglState.egl = LoadDllFromDirectory(mesaDir, L"libEGL.dll", true);
    g_eglState.gles = LoadDllFromDirectory(mesaDir, L"libGLESv2.dll", true);
    if ((preloadOpenGl && !g_eglState.openGl) || !g_eglState.egl || !g_eglState.gles)
    {
        WriteLogW(L"FAIL: required Mesa DLL load failed");
        return false;
    }

    auto eglGetDisplay = reinterpret_cast<EglGetDisplayProc>(GetProcAddress(g_eglState.egl, "eglGetDisplay"));
    auto eglInitialize = reinterpret_cast<EglInitializeProc>(GetProcAddress(g_eglState.egl, "eglInitialize"));
    auto eglGetConfigs = reinterpret_cast<EglGetConfigsProc>(GetProcAddress(g_eglState.egl, "eglGetConfigs"));
    auto eglChooseConfig = reinterpret_cast<EglChooseConfigProc>(GetProcAddress(g_eglState.egl, "eglChooseConfig"));
    auto eglBindAPI = reinterpret_cast<EglBindApiProc>(GetProcAddress(g_eglState.egl, "eglBindAPI"));
    auto eglGetError = reinterpret_cast<EglGetErrorProc>(GetProcAddress(g_eglState.egl, "eglGetError"));
    auto eglGetProcAddress = reinterpret_cast<EglGetProcAddressProc>(GetProcAddress(g_eglState.egl, "eglGetProcAddress"));
    auto eglQueryString = reinterpret_cast<EglQueryStringProc>(GetProcAddress(g_eglState.egl, "eglQueryString"));
    auto eglCreateWindowSurface = reinterpret_cast<EglCreateWindowSurfaceProc>(GetProcAddress(g_eglState.egl, "eglCreateWindowSurface"));
    auto eglCreateContext = reinterpret_cast<EglCreateContextProc>(GetProcAddress(g_eglState.egl, "eglCreateContext"));
    auto eglMakeCurrent = reinterpret_cast<EglMakeCurrentProc>(GetProcAddress(g_eglState.egl, "eglMakeCurrent"));
    g_eglState.swapBuffers = reinterpret_cast<EglSwapBuffersProc>(GetProcAddress(g_eglState.egl, "eglSwapBuffers"));

    auto eglCreatePlatformWindowSurfaceEXT = reinterpret_cast<EglCreatePlatformWindowSurfaceExtProc>(
        LoadEglProc(g_eglState.egl, eglGetProcAddress, "eglCreatePlatformWindowSurfaceEXT"));
    auto eglCreatePlatformWindowSurface = reinterpret_cast<EglCreatePlatformWindowSurfaceProc>(
        LoadEglProc(g_eglState.egl, eglGetProcAddress, "eglCreatePlatformWindowSurface"));

    if (!eglGetDisplay || !eglInitialize || !eglGetConfigs || !eglChooseConfig || !eglBindAPI ||
        !eglGetError || !eglCreateContext || !eglMakeCurrent || !g_eglState.swapBuffers)
    {
        WriteLogW(L"FAIL: required EGL exports are missing");
        return false;
    }

    WriteLogW(L"eglGetDisplay(EGL_DEFAULT_DISPLAY)");
    g_eglState.display = SafeGetDisplay(eglGetDisplay, nullptr);
    if (g_eglState.display == EGL_NO_DISPLAY_VALUE)
    {
        WriteLogF(L"FAIL: eglGetDisplay returned EGL_NO_DISPLAY err=0x%04x", EglError(eglGetError));
        return false;
    }

    WriteLogW(L"eglInitialize");
    EGLint eglMajor = 0;
    EGLint eglMinor = 0;
    if (!SafeInitialize(eglInitialize, g_eglState.display, &eglMajor, &eglMinor))
    {
        WriteLogF(L"FAIL: eglInitialize failed err=0x%04x", EglError(eglGetError));
        return false;
    }
    g_eglState.major = eglMajor;
    g_eglState.minor = eglMinor;

    if (eglQueryString)
    {
        const char* vendor = eglQueryString(g_eglState.display, EGL_VENDOR_VALUE);
        const char* version = eglQueryString(g_eglState.display, EGL_VERSION_VALUE);
        const char* apis = eglQueryString(g_eglState.display, EGL_CLIENT_APIS_VALUE);
        const char* extensions = eglQueryString(g_eglState.display, EGL_EXTENSIONS_VALUE);
        char line[1024] = {};
        std::snprintf(line, sizeof(line), "eglQueryString vendor=%s version=%s clientApis=%s",
            vendor ? vendor : "<null>",
            version ? version : "<null>",
            apis ? apis : "<null>");
        WriteLogA(line);
        if (extensions)
        {
            char extLine[1024] = {};
            std::snprintf(extLine, sizeof(extLine), "eglQueryString extensions=%.850s", extensions);
            WriteLogA(extLine);
        }
    }

    EGLint totalConfigs = 0;
    eglGetConfigs(g_eglState.display, nullptr, 0, &totalConfigs);
    WriteLogF(L"eglGetConfigs count=%d err=0x%04x", totalConfigs, EglError(eglGetError));

    EGLint configAttribs[] =
    {
        EGL_SURFACE_TYPE_VALUE, EGL_WINDOW_BIT_VALUE,
        EGL_RENDERABLE_TYPE_VALUE, useDesktopOpenGl ? EGL_OPENGL_BIT_VALUE : EGL_OPENGL_ES3_BIT_VALUE,
        EGL_RED_SIZE_VALUE, 8,
        EGL_GREEN_SIZE_VALUE, 8,
        EGL_BLUE_SIZE_VALUE, 8,
        EGL_ALPHA_SIZE_VALUE, 8,
        EGL_DEPTH_SIZE_VALUE, 24,
        EGL_STENCIL_SIZE_VALUE, 8,
        EGL_NONE_VALUE
    };

    EGLint configCount = 0;
    WriteLogW(useDesktopOpenGl
        ? L"eglChooseConfig(window OpenGL rgba8 depth24 stencil8)"
        : L"eglChooseConfig(window GLES3 rgba8 depth24 stencil8)");
    if (!eglChooseConfig(g_eglState.display, configAttribs, &g_eglState.config, 1, &configCount) ||
        configCount <= 0 ||
        !g_eglState.config)
    {
        WriteLogF(L"FAIL: eglChooseConfig GLES3 failed count=%d err=0x%04x", configCount, EglError(eglGetError));
        return false;
    }
    WriteLogF(L"eglChooseConfig selected GLES3 window config count=%d", configCount);

    WriteLogW(useDesktopOpenGl ? L"eglBindAPI(OpenGL)" : L"eglBindAPI(OpenGL_ES)");
    if (!eglBindAPI(useDesktopOpenGl ? EGL_OPENGL_API_VALUE : EGL_OPENGL_ES_API_VALUE))
    {
        WriteLogF(L"FAIL: eglBindAPI failed err=0x%04x", EglError(eglGetError));
        return false;
    }

    PropertySet descriptor;
    descriptor.Insert(L"EGLNativeWindowTypeProperty", window);
    descriptor.Insert(L"EGLRenderSurfaceSizeProperty", PropertyValue::CreateSize(Size(bounds.Width, bounds.Height)));
    WriteLogF(L"PropertySet descriptor built iface=%p", winrt::get_abi(descriptor));

    void* coreWindowAbi = winrt::get_abi(window);
    void* descriptorAbi = winrt::get_abi(descriptor);
    WriteLogF(L"CoreWindow ABI pointer=%p descriptor ABI pointer=%p", coreWindowAbi, descriptorAbi);

    if (!TryCreateSurface(
        eglGetError,
        eglCreateWindowSurface,
        eglCreatePlatformWindowSurfaceEXT,
        eglCreatePlatformWindowSurface,
        g_eglState.display,
        g_eglState.config,
        coreWindowAbi,
        descriptorAbi,
        g_eglState.surface))
    {
        WriteLogW(L"FAIL: all CoreWindow/PropertySet EGL window surface attempts failed");
        return false;
    }

    EGLint contextAttribsGles[] =
    {
        EGL_CONTEXT_CLIENT_VERSION_VALUE, 3,
        EGL_NONE_VALUE
    };

    WriteLogW(useDesktopOpenGl ? L"eglCreateContext(OpenGL default)" : L"eglCreateContext(GLES3)");
    g_eglState.context = SafeCreateContext(
        useDesktopOpenGl ? "eglCreateContext(OpenGL default)" : "eglCreateContext(GLES3)",
        eglCreateContext,
        g_eglState.display,
        g_eglState.config,
        useDesktopOpenGl ? nullptr : contextAttribsGles);
    if (g_eglState.context == EGL_NO_CONTEXT_VALUE)
    {
        WriteLogF(L"FAIL: eglCreateContext(GLES3) failed err=0x%04x", EglError(eglGetError));
        return false;
    }

    WriteLogW(L"eglMakeCurrent");
    if (!SafeMakeCurrent(eglMakeCurrent, g_eglState.display, g_eglState.surface, g_eglState.context))
    {
        WriteLogF(L"FAIL: eglMakeCurrent failed err=0x%04x", EglError(eglGetError));
        return false;
    }

    auto loadGlProc = [&](const char* name) -> void*
    {
        void* proc = LoadEglProc(g_eglState.egl, eglGetProcAddress, name);
        if (!proc && g_eglState.gles)
        {
            proc = reinterpret_cast<void*>(GetProcAddress(g_eglState.gles, name));
        }
        if (!proc && g_eglState.openGl)
        {
            proc = reinterpret_cast<void*>(GetProcAddress(g_eglState.openGl, name));
        }
        return proc;
    };

    g_eglState.clearColor = reinterpret_cast<GlClearColorProc>(loadGlProc("glClearColor"));
    g_eglState.clear = reinterpret_cast<GlClearProc>(loadGlProc("glClear"));
    g_eglState.viewport = reinterpret_cast<GlViewportProc>(loadGlProc("glViewport"));
    g_eglState.enable = reinterpret_cast<GlEnableProc>(loadGlProc("glEnable"));
    g_eglState.disable = reinterpret_cast<GlDisableProc>(loadGlProc("glDisable"));
    g_eglState.scissor = reinterpret_cast<GlScissorProc>(loadGlProc("glScissor"));
    if (!g_eglState.clearColor)
    {
        g_eglState.clearColor = reinterpret_cast<GlClearColorProc>(GetProcAddress(g_eglState.gles, "glClearColor"));
    }
    if (!g_eglState.clear)
    {
        g_eglState.clear = reinterpret_cast<GlClearProc>(GetProcAddress(g_eglState.gles, "glClear"));
    }

    if (g_eglState.clearColor && g_eglState.clear)
    {
        g_eglState.clearColor(0.02f, 0.55f, 0.16f, 1.0f);
        g_eglState.clear(GL_COLOR_BUFFER_BIT_VALUE);
    }

    if (!SafeSwapBuffers(g_eglState.swapBuffers, g_eglState.display, g_eglState.surface))
    {
        WriteLogF(L"FAIL: first eglSwapBuffers failed err=0x%04x", EglError(eglGetError));
        return false;
    }

    g_eglState.ready = true;
    WriteLogF(L"PASS: Mesa EGL CoreWindow window surface ready EGL %d.%d surface=%p context=%p",
        g_eglState.major,
        g_eglState.minor,
        g_eglState.surface,
        g_eglState.context);
    return true;
}

static void CleanupProbeEgl()
{
    if (!g_eglState.egl || !g_eglState.display)
    {
        return;
    }

    auto eglMakeCurrent = reinterpret_cast<EglMakeCurrentProc>(GetProcAddress(g_eglState.egl, "eglMakeCurrent"));
    auto eglDestroySurface = reinterpret_cast<EglDestroySurfaceProc>(GetProcAddress(g_eglState.egl, "eglDestroySurface"));
    auto eglDestroyContext = reinterpret_cast<EglDestroyContextProc>(GetProcAddress(g_eglState.egl, "eglDestroyContext"));
    auto eglTerminate = reinterpret_cast<EglTerminateProc>(GetProcAddress(g_eglState.egl, "eglTerminate"));

    WriteLogW(L"Cleaning up standalone EGL probe context before Minecraft launch");
    if (eglMakeCurrent)
    {
        SafeMakeCurrent(eglMakeCurrent, g_eglState.display, EGL_NO_SURFACE_VALUE, EGL_NO_CONTEXT_VALUE);
    }
    if (eglDestroyContext && g_eglState.context != EGL_NO_CONTEXT_VALUE)
    {
        eglDestroyContext(g_eglState.display, g_eglState.context);
    }
    if (eglDestroySurface && g_eglState.surface != EGL_NO_SURFACE_VALUE)
    {
        eglDestroySurface(g_eglState.display, g_eglState.surface);
    }
    if (eglTerminate)
    {
        eglTerminate(g_eglState.display);
    }

    g_eglState.display = EGL_NO_DISPLAY_VALUE;
    g_eglState.surface = EGL_NO_SURFACE_VALUE;
    g_eglState.context = EGL_NO_CONTEXT_VALUE;
    g_eglState.swapBuffers = nullptr;
    g_eglState.clearColor = nullptr;
    g_eglState.clear = nullptr;
    g_eglState.viewport = nullptr;
    g_eglState.enable = nullptr;
    g_eglState.disable = nullptr;
    g_eglState.scissor = nullptr;
    g_eglState.width = 0;
    g_eglState.height = 0;
    g_eglState.ready = false;
}

static bool HandoffCoreWindowToGlfw(
    CoreWindow const& window,
    std::wstring const& glfwPath,
    int width,
    int height,
    int surfaceWidth,
    int surfaceHeight)
{
    if (!window)
    {
        WriteLogW(L"FAIL: cannot hand null CoreWindow to xbox-glfw");
        return false;
    }
    if (glfwPath.empty() || !FileExists(glfwPath))
    {
        WriteLogF(L"FAIL: xbox-glfw.dll missing: %s", glfwPath.empty() ? L"<empty>" : glfwPath.c_str());
        return false;
    }

    HMODULE glfw = LoadLibraryExW(glfwPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!glfw)
    {
        WriteLogF(L"FAIL: LoadLibraryExW(xbox-glfw.dll) failed %s GetLastError=%lu", glfwPath.c_str(), GetLastError());
        return false;
    }
    WriteLogF(L"Loaded xbox-glfw.dll for CoreWindow handoff: %s", glfwPath.c_str());

    using SetCoreWindowProc = void(__stdcall*)(void*);
    auto setCoreWindow = reinterpret_cast<SetCoreWindowProc>(GetProcAddress(glfw, "minecraft_xbox_glfw_set_core_window"));
    auto setDescriptor = reinterpret_cast<SetCoreWindowProc>(GetProcAddress(glfw, "minecraft_xbox_glfw_set_egl_window_descriptor"));
    if (!setCoreWindow || !setDescriptor)
    {
        WriteLogW(L"FAIL: xbox-glfw.dll is missing CoreWindow handoff exports");
        return false;
    }

    PropertySet descriptor;
    descriptor.Insert(L"EGLNativeWindowTypeProperty", window);
    descriptor.Insert(L"EGLRenderSurfaceSizeProperty", PropertyValue::CreateSize(Size(static_cast<float>(surfaceWidth), static_cast<float>(surfaceHeight))));
    CoreApplication::Properties().Insert(L"EGLNativeWindowTypeProperty", window);
    CoreApplication::Properties().Insert(L"EGLRenderSurfaceSizeProperty", descriptor.Lookup(L"EGLRenderSurfaceSizeProperty"));

    void* coreWindowAbi = winrt::get_abi(window);
    void* descriptorAbi = winrt::get_abi(descriptor);
    WriteLogF(
        L"Handing CoreWindow to xbox-glfw coreWindow=%p descriptor=%p render=%dx%d surface=%dx%d",
        coreWindowAbi,
        descriptorAbi,
        width,
        height,
        surfaceWidth,
        surfaceHeight);
    setCoreWindow(coreWindowAbi);
    setDescriptor(descriptorAbi);
    WriteLogW(L"VISIBLE-LAUNCH-FIX: CoreWindow handoff complete; no EGL warmup call will run before glfwCreateWindow");

    return true;
}

struct LaunchResolutionPreset
{
    wchar_t const* label;
    int width;
    int height;
    int surfaceWidth;
    int surfaceHeight;
};

static LaunchResolutionPreset const kLaunchResolutionPresets[] =
{
    { L"720p", 1280, 720, 1280, 720 },
    { L"1080p", 1920, 1080, 1920, 1080 },
    { L"1080p -> 4K", 1920, 1080, 3840, 2160 },
    { L"1440p", 2560, 1440, 2560, 1440 },
    { L"1440p -> 4K", 2560, 1440, 3840, 2160 },
    { L"4K", 3840, 2160, 3840, 2160 },
};

static void ApplyLaunchResolutionEnvironment(int width, int height, int surfaceWidth = 0, int surfaceHeight = 0)
{
    g_launchWidth = width;
    g_launchHeight = height;
    g_surfaceWidth = surfaceWidth > 0 ? surfaceWidth : width;
    g_surfaceHeight = surfaceHeight > 0 ? surfaceHeight : height;

    wchar_t buffer[32] = {};
    _snwprintf_s(buffer, _TRUNCATE, L"%d", width);
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_LAUNCH_WIDTH", buffer);
    _snwprintf_s(buffer, _TRUNCATE, L"%d", height);
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_LAUNCH_HEIGHT", buffer);
    _snwprintf_s(buffer, _TRUNCATE, L"%d", g_surfaceWidth);
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_SURFACE_WIDTH", buffer);
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_UPSCALE_TARGET_WIDTH", buffer);
    _snwprintf_s(buffer, _TRUNCATE, L"%d", g_surfaceHeight);
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_SURFACE_HEIGHT", buffer);
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_UPSCALE_TARGET_HEIGHT", buffer);
    _snwprintf_s(buffer, _TRUNCATE, L"%d", width);
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_UPSCALE_SOURCE_WIDTH", buffer);
    _snwprintf_s(buffer, _TRUNCATE, L"%d", height);
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_UPSCALE_SOURCE_HEIGHT", buffer);
    SetEnvironmentVariableW(
        L"MINECRAFT_XBOX_UPSCALE_TO_SURFACE",
        (g_surfaceWidth > width || g_surfaceHeight > height) ? L"1" : L"0");
#if defined(MINECRAFT_XBOX_TARGET_120FPS_BUILD)
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_REFRESH_RATE", L"120");
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_FORCE_SWAP_INTERVAL", L"0");
#endif
}

static bool TryParseResolutionJson(
    std::string const& json,
    int& width,
    int& height,
    int& surfaceWidth,
    int& surfaceHeight,
    std::wstring& label)
{
    auto readInt = [&](char const* lowerKey, char const* upperKey, int& out) -> bool
    {
        auto findKey = [&](char const* key, std::string& needle) -> size_t
        {
            needle = std::string("\"") + key + "\":";
            return json.find(needle);
        };

        std::string needle;
        size_t pos = findKey(lowerKey, needle);
        if (pos == std::string::npos)
        {
            pos = findKey(upperKey, needle);
        }
        if (pos == std::string::npos)
        {
            return false;
        }

        pos += needle.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
        {
            pos++;
        }

        out = std::atoi(json.c_str() + pos);
        return out > 0;
    };

    auto readLabel = [&](std::wstring& out) -> bool
    {
        size_t pos = json.find("\"label\"");
        if (pos == std::string::npos)
        {
            pos = json.find("\"Label\"");
        }
        if (pos == std::string::npos)
        {
            return false;
        }

        pos = json.find(':', pos);
        if (pos == std::string::npos)
        {
            return false;
        }

        pos = json.find('"', pos);
        if (pos == std::string::npos)
        {
            return false;
        }

        size_t end = json.find('"', pos + 1);
        if (end == std::string::npos)
        {
            return false;
        }

        out = Utf8ToWide(json.substr(pos + 1, end - pos - 1).c_str());
        return !out.empty();
    };

    int parsedWidth = 0;
    int parsedHeight = 0;
    int parsedSurfaceWidth = 0;
    int parsedSurfaceHeight = 0;
    std::wstring parsedLabel;
    if (!readInt("width", "Width", parsedWidth) || !readInt("height", "Height", parsedHeight))
    {
        return false;
    }

    readInt("surfaceWidth", "SurfaceWidth", parsedSurfaceWidth);
    readInt("surfaceHeight", "SurfaceHeight", parsedSurfaceHeight);
    readLabel(parsedLabel);
    width = parsedWidth;
    height = parsedHeight;
    surfaceWidth = parsedSurfaceWidth > 0 ? parsedSurfaceWidth : parsedWidth;
    surfaceHeight = parsedSurfaceHeight > 0 ? parsedSurfaceHeight : parsedHeight;
    label = parsedLabel;
    return true;
}

static void SaveLaunchResolutionJson(
    std::wstring const& localRoot,
    int width,
    int height,
    int surfaceWidth,
    int surfaceHeight,
    std::wstring const& label)
{
    std::string json =
        "{\"label\":\"" + WideToUtf8(label) + "\",\"width\":" + std::to_string(width) +
        ",\"height\":" + std::to_string(height) +
        ",\"surfaceWidth\":" + std::to_string(surfaceWidth) +
        ",\"surfaceHeight\":" + std::to_string(surfaceHeight) + "}";
    WriteTextFileUtf8(localRoot + L"\\launch-resolution.json", json);
}

static bool TryLoadSavedLaunchResolution(
    std::wstring const& localRoot,
    int& width,
    int& height,
    int& surfaceWidth,
    int& surfaceHeight,
    std::wstring& label)
{
    std::string json;
    if (!ReadSmallTextFileUtf8(localRoot + L"\\launch-resolution.json", json))
    {
        return false;
    }

    return TryParseResolutionJson(json, width, height, surfaceWidth, surfaceHeight, label);
}

static std::wstring CurrentPackageVersionString()
{
    try
    {
        PackageVersion version = Package::Current().Id().Version();
        return std::to_wstring(version.Major) + L"." +
            std::to_wstring(version.Minor) + L"." +
            std::to_wstring(version.Build) + L"." +
            std::to_wstring(version.Revision);
    }
    catch (...)
    {
        return L"unknown";
    }
}

static LaunchResolutionPreset const& DefaultLaunchResolutionPreset()
{
    return kLaunchResolutionPresets[1];
}

static LaunchResolutionPreset const& PresetForLabel(std::wstring const& label)
{
    for (size_t i = 0; i < std::size(kLaunchResolutionPresets); ++i)
    {
        if (label == kLaunchResolutionPresets[i].label)
        {
            return kLaunchResolutionPresets[i];
        }
    }

    return DefaultLaunchResolutionPreset();
}

static void ClampLaunchResolutionToBounds(CoreWindow const& window, int& width, int& height)
{
    LaunchResolutionPreset const& fallback = DefaultLaunchResolutionPreset();
    if (width <= 0 || height <= 0)
    {
        WriteLogF(L"Invalid launch resolution %dx%d; using %s", width, height, fallback.label);
        width = fallback.width;
        height = fallback.height;
    }

    int requestedWidth = width;
    int requestedHeight = height;
    LaunchResolutionPreset const& maxPreset = kLaunchResolutionPresets[std::size(kLaunchResolutionPresets) - 1];
    if (width > maxPreset.width)
    {
        width = maxPreset.width;
    }
    if (height > maxPreset.height)
    {
        height = maxPreset.height;
    }

    int boundsWidth = 0;
    int boundsHeight = 0;
    if (window)
    {
        Rect bounds = window.Bounds();
        boundsWidth = bounds.Width > 1.0f ? static_cast<int>(bounds.Width) : 0;
        boundsHeight = bounds.Height > 1.0f ? static_cast<int>(bounds.Height) : 0;
    }

    WriteLogF(
        L"Launch resolution retained %dx%d -> %dx%d (CoreWindow bounds %dx%d)",
        requestedWidth,
        requestedHeight,
        width,
        height,
        boundsWidth,
        boundsHeight);
}

static LaunchResolutionPreset const& PresetForDetectedResolution(int width, int height)
{
    if (height > width)
    {
        std::swap(width, height);
    }

    if (width >= 3840 && height >= 2160)
    {
        return PresetForLabel(L"1080p -> 4K");
    }
    if (width >= 2560 && height >= 1440)
    {
        return PresetForLabel(L"1440p");
    }
    if (width >= 1920 && height >= 1080)
    {
        return PresetForLabel(L"1080p");
    }
    if (width >= 1280 && height >= 720)
    {
        return PresetForLabel(L"720p");
    }

    return DefaultLaunchResolutionPreset();
}

static bool TryGetRawDisplayResolution(int& width, int& height, std::wstring& source)
{
    width = 0;
    height = 0;
    source.clear();

    try
    {
        DisplayInformation display = DisplayInformation::GetForCurrentView();
        width = static_cast<int>(display.ScreenWidthInRawPixels());
        height = static_cast<int>(display.ScreenHeightInRawPixels());
        if (width > 0 && height > 0)
        {
            source = L"DisplayInformation raw pixels";
            return true;
        }
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"Auto resolution raw display query failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
    }
    catch (...)
    {
        WriteLogW(L"Auto resolution raw display query failed with unknown exception");
    }

    return false;
}

static bool TryGetScaledCoreWindowResolution(CoreWindow const& window, int& width, int& height, std::wstring& source)
{
    width = 0;
    height = 0;
    source.clear();

    if (!window)
    {
        return false;
    }

    try
    {
        DisplayInformation display = DisplayInformation::GetForCurrentView();
        double scale = display.RawPixelsPerViewPixel();
        Rect bounds = window.Bounds();
        int boundsWidth = bounds.Width > 1.0f ? static_cast<int>(bounds.Width) : 0;
        int boundsHeight = bounds.Height > 1.0f ? static_cast<int>(bounds.Height) : 0;
        width = boundsWidth > 0 ? static_cast<int>((boundsWidth * scale) + 0.5) : 0;
        height = boundsHeight > 0 ? static_cast<int>((boundsHeight * scale) + 0.5) : 0;
        if (width > 0 && height > 0)
        {
            source = L"CoreWindow bounds scaled by RawPixelsPerViewPixel";
            WriteLogF(
                L"Auto resolution scaled candidate bounds=%dx%d scale=%d/100 raw=%dx%d",
                boundsWidth,
                boundsHeight,
                static_cast<int>((scale * 100.0) + 0.5),
                width,
                height);
            return true;
        }
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"Auto resolution scaled CoreWindow query failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
    }
    catch (...)
    {
        WriteLogW(L"Auto resolution scaled CoreWindow query failed with unknown exception");
    }

    return false;
}

static bool TryGetCoreWindowResolution(CoreWindow const& window, int& width, int& height, std::wstring& source)
{
    width = 0;
    height = 0;
    source.clear();

    if (!window)
    {
        return false;
    }

    Rect bounds = window.Bounds();
    width = bounds.Width > 1.0f ? static_cast<int>(bounds.Width) : 0;
    height = bounds.Height > 1.0f ? static_cast<int>(bounds.Height) : 0;
    if (width > 0 && height > 0)
    {
        source = L"CoreWindow bounds";
        return true;
    }

    return false;
}

static unsigned long long ResolutionArea(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return 0;
    }

    return static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height);
}

static void ResolveAutomaticLaunchResolution(CoreWindow const& window, std::wstring const& localRoot)
{
    int detectedWidth = 0;
    int detectedHeight = 0;
    std::wstring source;
    std::vector<std::tuple<int, int, std::wstring>> candidates;

    int candidateWidth = 0;
    int candidateHeight = 0;
    std::wstring candidateSource;
    if (TryGetRawDisplayResolution(candidateWidth, candidateHeight, candidateSource))
    {
        candidates.push_back({ candidateWidth, candidateHeight, candidateSource });
    }
    if (TryGetScaledCoreWindowResolution(window, candidateWidth, candidateHeight, candidateSource))
    {
        candidates.push_back({ candidateWidth, candidateHeight, candidateSource });
    }
    if (TryGetCoreWindowResolution(window, candidateWidth, candidateHeight, candidateSource))
    {
        candidates.push_back({ candidateWidth, candidateHeight, candidateSource });
    }

    for (auto const& candidate : candidates)
    {
        int width = std::get<0>(candidate);
        int height = std::get<1>(candidate);
        std::wstring const& candidateName = std::get<2>(candidate);
        WriteLogF(L"Auto resolution candidate source=%s size=%dx%d", candidateName.c_str(), width, height);
        if (ResolutionArea(width, height) > ResolutionArea(detectedWidth, detectedHeight))
        {
            detectedWidth = width;
            detectedHeight = height;
            source = candidateName;
        }
    }

#if defined(MINECRAFT_XBOX_TRUE_4K_BUILD)
    LaunchResolutionPreset const& preset = PresetForLabel(L"4K");
    wchar_t const* buildFlavor = L"TRUE_4K";
#elif defined(MINECRAFT_XBOX_TRUE_1440P_BUILD)
    LaunchResolutionPreset const& preset = PresetForLabel(L"1440p");
    wchar_t const* buildFlavor = L"TRUE_1440P";
#elif defined(MINECRAFT_XBOX_TRUE_HD_BUILD)
    LaunchResolutionPreset const& preset = PresetForLabel(L"1080p");
    wchar_t const* buildFlavor = L"TRUE_HD";
#else
    LaunchResolutionPreset const& preset = PresetForLabel(L"1080p -> 4K");
    wchar_t const* buildFlavor = L"DEFAULT_1080P_TO_4K";
#endif
    int width = preset.width;
    int height = preset.height;
    ClampLaunchResolutionToBounds(window, width, height);
    g_launchResolutionLabel = preset.label;
    ApplyLaunchResolutionEnvironment(width, height, preset.surfaceWidth, preset.surfaceHeight);
    SaveLaunchResolutionJson(localRoot, g_launchWidth, g_launchHeight, g_surfaceWidth, g_surfaceHeight, g_launchResolutionLabel);

    WriteLogF(
        L"Forced package launch resolution build=%s selected=%s render=%dx%d surface=%dx%d upscale=%d; detected source=%s detected=%dx%d ignored",
        buildFlavor,
        g_launchResolutionLabel.c_str(),
        g_launchWidth,
        g_launchHeight,
        g_surfaceWidth,
        g_surfaceHeight,
        (g_surfaceWidth > g_launchWidth || g_surfaceHeight > g_launchHeight) ? 1 : 0,
        source.empty() ? L"<fallback>" : source.c_str(),
        detectedWidth,
        detectedHeight);
}

static int DefaultCommandIndexForLabel(std::wstring const& label)
{
    for (size_t i = 0; i < std::size(kLaunchResolutionPresets); ++i)
    {
        if (label == kLaunchResolutionPresets[i].label)
        {
            return static_cast<int>(i);
        }
    }

    return 1;
}

static bool PickLaunchResolutionInteractive(CoreWindow const& window, std::wstring const& suggestedLabel)
{
    auto dispatcher = window.Dispatcher();
    try
    {
        if (window)
        {
            window.Activate();
        }
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"Resolution picker window activate failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
    }
    catch (...)
    {
        WriteLogW(L"Resolution picker window activate failed with unknown exception");
    }

    for (int i = 0; i < 90; ++i)
    {
        dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        Sleep(16);
    }

    try
    {
        MessageDialog dialog(L"Choose the Minecraft render resolution before launch.");
        for (LaunchResolutionPreset const& preset : kLaunchResolutionPresets)
        {
            std::wstring command = std::wstring(preset.label) + L" (" +
                std::to_wstring(preset.width) + L"x" + std::to_wstring(preset.height);
            if (preset.surfaceWidth != preset.width || preset.surfaceHeight != preset.height)
            {
                command += L" -> " + std::to_wstring(preset.surfaceWidth) + L"x" +
                    std::to_wstring(preset.surfaceHeight);
            }
            command += L")";
            dialog.Commands().Append(UICommand(command));
        }

        dialog.DefaultCommandIndex(DefaultCommandIndexForLabel(suggestedLabel));

        IUICommand selection = ShowDialogAndPump(window, dialog);
        std::wstring label = selection ? selection.Label().c_str() : std::wstring();
        for (LaunchResolutionPreset const& preset : kLaunchResolutionPresets)
        {
            if (label.find(preset.label) == 0)
            {
                int width = preset.width;
                int height = preset.height;
                ClampLaunchResolutionToBounds(window, width, height);
                g_launchResolutionLabel = preset.label;
                ApplyLaunchResolutionEnvironment(width, height, preset.surfaceWidth, preset.surfaceHeight);
                WriteLogF(
                    L"Selected launch resolution %s render=%dx%d surface=%dx%d upscale=%d",
                    preset.label,
                    width,
                    height,
                    g_surfaceWidth,
                    g_surfaceHeight,
                    (g_surfaceWidth > width || g_surfaceHeight > height) ? 1 : 0);
                return true;
            }
        }

        WriteLogW(L"Resolution picker returned unknown choice; using suggested/default resolution");
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"Resolution picker failed hr=0x%08X; using suggested/default resolution", static_cast<unsigned int>(ex.code()));
    }
    catch (...)
    {
        WriteLogW(L"Resolution picker failed with unknown exception; using suggested/default resolution");
    }

    LaunchResolutionPreset const& fallback = PresetForLabel(suggestedLabel);
    int width = fallback.width;
    int height = fallback.height;
    ClampLaunchResolutionToBounds(window, width, height);
    g_launchResolutionLabel = fallback.label;
    ApplyLaunchResolutionEnvironment(width, height, fallback.surfaceWidth, fallback.surfaceHeight);
    return false;
}

static void ResolveLaunchResolution(CoreWindow const& window, std::wstring const& localRoot)
{
    std::wstring forcePickPath = localRoot + L"\\force-resolution-pick";
    bool forcePick = FileExists(forcePickPath) ||
        IsTruthyEnvironment(L"MINECRAFT_XBOX_ENABLE_RESOLUTION_PICKER", false);
    if (!forcePick)
    {
        WriteLogW(L"Forced package launch resolution enabled; skipping interactive resolution picker");
        ResolveAutomaticLaunchResolution(window, localRoot);
        return;
    }

    std::wstring pickerVersion = CurrentPackageVersionString();
    std::wstring pickerVersionPath = localRoot + L"\\resolution-picker-version.txt";
    std::string savedPickerVersionBytes;
    bool pickerAlreadyShownForVersion = false;
    if (ReadSmallTextFileUtf8(pickerVersionPath, savedPickerVersionBytes))
    {
        std::wstring savedPickerVersion = Utf8ToWide(savedPickerVersionBytes.c_str());
        while (!savedPickerVersion.empty() && (savedPickerVersion.back() == L'\r' || savedPickerVersion.back() == L'\n'))
        {
            savedPickerVersion.pop_back();
        }
        pickerAlreadyShownForVersion = savedPickerVersion == pickerVersion;
    }
    bool firstRunForPackageVersion = !pickerAlreadyShownForVersion;
    int width = 0;
    int height = 0;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    std::wstring label;
    bool hasSaved = TryLoadSavedLaunchResolution(localRoot, width, height, surfaceWidth, surfaceHeight, label);
    if (hasSaved)
    {
        WriteLogF(
            L"Saved launch resolution on disk: %s render=%dx%d surface=%dx%d",
            label.c_str(),
            width,
            height,
            surfaceWidth,
            surfaceHeight);
    }

    if (forcePick)
    {
        WriteLogW(L"force-resolution-pick present; showing resolution picker");
    }
    else if (firstRunForPackageVersion)
    {
        WriteLogF(L"Resolution picker has not completed for package version %s; showing picker", pickerVersion.c_str());
    }

    if (hasSaved && !forcePick && !firstRunForPackageVersion)
    {
        ClampLaunchResolutionToBounds(window, width, height);
        g_launchResolutionLabel = label.empty() ? (std::to_wstring(width) + L"x" + std::to_wstring(height)) : label;
        ApplyLaunchResolutionEnvironment(width, height, surfaceWidth, surfaceHeight);
        WriteLogF(
            L"Using saved launch resolution without picker: %s render=%dx%d surface=%dx%d",
            g_launchResolutionLabel.c_str(),
            g_launchWidth,
            g_launchHeight,
            g_surfaceWidth,
            g_surfaceHeight);
        SaveLaunchResolutionJson(localRoot, g_launchWidth, g_launchHeight, g_surfaceWidth, g_surfaceHeight, g_launchResolutionLabel);
        return;
    }

    WriteLogW(L"No saved launch resolution, or picker was forced; showing resolution picker");
    bool picked = PickLaunchResolutionInteractive(window, hasSaved ? label : DefaultLaunchResolutionPreset().label);

    if (picked || hasSaved)
    {
        SaveLaunchResolutionJson(localRoot, g_launchWidth, g_launchHeight, g_surfaceWidth, g_surfaceHeight, g_launchResolutionLabel);
    }
    if (picked)
    {
        WriteTextFileUtf8(pickerVersionPath, WideToUtf8(pickerVersion) + "\r\n");
    }
    else
    {
        WriteLogW(L"Resolution picker did not complete; using fallback for this launch only");
    }
    if (forcePick && picked)
    {
        DeleteFileW(forcePickPath.c_str());
    }
}

static bool RunNativeMinecraft(CoreWindow const& window)
{
    WriteLogW(L"=== Native embedded Minecraft launch starting ===");

    std::wstring localRoot;
    std::wstring packageRoot;
    try
    {
        localRoot = ApplicationData::Current().LocalFolder().Path().c_str();
        packageRoot = Package::Current().InstalledLocation().Path().c_str();
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"FAIL: unable to resolve app paths for Minecraft launch hr=0x%08X", static_cast<unsigned int>(ex.code()));
        return false;
    }

    MinecraftAuthSession authSession = ResolveMinecraftAuthSession(window, localRoot);
    ResolveLaunchResolution(window, localRoot);

    int width = g_launchWidth;
    int height = g_launchHeight;
    int surfaceWidth = g_surfaceWidth > 0 ? g_surfaceWidth : width;
    int surfaceHeight = g_surfaceHeight > 0 ? g_surfaceHeight : height;
    Rect bounds = window ? window.Bounds() : Rect();
    int boundsWidth = bounds.Width > 1.0f ? static_cast<int>(bounds.Width) : 0;
    int boundsHeight = bounds.Height > 1.0f ? static_cast<int>(bounds.Height) : 0;
    WriteLogF(
        L"Minecraft launch render size %s %dx%d surface=%dx%d upscale=%d (CoreWindow bounds %dx%d)",
        g_launchResolutionLabel.c_str(),
        width,
        height,
        surfaceWidth,
        surfaceHeight,
        (surfaceWidth > width || surfaceHeight > height) ? 1 : 0,
        boundsWidth,
        boundsHeight);

    CopyPackagedLocalStatePayloadIfPresent(localRoot, packageRoot);

    MinecraftLaunchProfile launchProfile = PickMinecraftLaunchProfileInteractive(window, localRoot);
    if (launchProfile.root.empty())
    {
        return false;
    }
    g_downloadWorkPerformed.store(false, std::memory_order_release);
    if (!EnsureProfileDownloadsBeforeLaunch(window, launchProfile.root, localRoot))
    {
        WriteLogF(L"FAIL: profile download hydration failed for %s", launchProfile.root.c_str());
        return false;
    }
    if (g_downloadWorkPerformed.exchange(false, std::memory_order_acq_rel))
    {
        WriteLogW(L"DOWNLOAD first-launch hydration performed; deferring Minecraft launch until next clean app start");
        WriteTextFileUtf8(
            localRoot + L"\\first-launch-download-complete.txt",
            "Minecraft files finished downloading. Relaunch the app once to start Minecraft with a clean graphics window.\r\n");
        try
        {
            MessageDialog dialog(L"Downloads are complete.\r\n\r\nLaunch the app again to start Minecraft cleanly.");
            ShowDialogAndPump(window, dialog);
        }
        catch (hresult_error const& ex)
        {
            WriteLogF(L"DOWNLOAD completion dialog failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
        }
        catch (...)
        {
            WriteLogW(L"DOWNLOAD completion dialog failed with unknown exception");
        }
        return true;
    }

    std::wstring profileRoot = launchProfile.root;
    std::wstring minecraftVersion = ResolveStagedMinecraftVersion(profileRoot);
    std::wstring baseMinecraftVersion = ResolveStagedBaseMinecraftVersion(profileRoot, minecraftVersion);
    std::wstring assetIndex = ResolveStagedAssetIndex(profileRoot, baseMinecraftVersion.empty() ? minecraftVersion : baseMinecraftVersion);
    std::wstring mainClass = ResolveStagedMainClass(profileRoot, minecraftVersion);
    std::wstring fabricLoaderVersion = ResolveStagedFabricLoaderVersion(profileRoot);
    std::wstring launcherVersionName = ResolveStagedLauncherVersionName(profileRoot, minecraftVersion);
    int javaRuntimeMajor = ResolveStagedJavaRuntimeMajor(profileRoot, minecraftVersion);
    bool legacyMinecraftArgs = UsesLegacyMinecraftArguments(minecraftVersion);
    std::wstring lowerMainClass = ToLowerInvariant(mainClass);
    std::wstring lowerModLoader = ToLowerInvariant(launchProfile.modLoader);
    bool neoforgeProfile =
        lowerModLoader.find(L"neoforge") != std::wstring::npos;
    bool profileLooksModded =
        !launchProfile.modLoader.empty() ||
        lowerMainClass.find(L"fabric") != std::wstring::npos ||
        lowerMainClass.find(L"forge") != std::wstring::npos ||
        lowerMainClass.find(L"quilt") != std::wstring::npos ||
        lowerMainClass.find(L"launchwrapper") != std::wstring::npos ||
        lowerMainClass.find(L"bootstraplauncher") != std::wstring::npos ||
        neoforgeProfile;
    bool fabricProfile = lowerMainClass.find(L"fabric") != std::wstring::npos ||
        lowerModLoader.find(L"fabric") != std::wstring::npos;
    bool forgeProfile = !neoforgeProfile &&
        (lowerMainClass.find(L"launchwrapper") != std::wstring::npos ||
            lowerMainClass.find(L"forge") != std::wstring::npos ||
            lowerMainClass.find(L"bootstraplauncher") != std::wstring::npos ||
            lowerModLoader.find(L"forge") != std::wstring::npos);
    bool includeBaseVersionModLibrary = !profileLooksModded;

    std::wstring jreRoot = ResolveProfileJavaRuntimeRoot(profileRoot);
    std::wstring javaBin = jreRoot + L"\\bin";
    std::wstring serverDir = javaBin + L"\\server";
    std::wstring jvmPath = serverDir + L"\\jvm.dll";
    std::wstring localNativeDir = localRoot + L"\\native";
    std::wstring packageNativeDir = packageRoot + L"\\native";
    std::wstring nativeDir = DirectoryExists(packageNativeDir) ? packageNativeDir : localNativeDir;
    std::wstring profileNativeDir = profileRoot + L"\\native";
    std::vector<std::wstring> nativeSearchDirs;
    if (DirectoryExists(profileNativeDir))
    {
        nativeSearchDirs.push_back(profileNativeDir);
    }
    nativeSearchDirs.push_back(nativeDir);
    std::wstring nativeSearchPath = JoinPaths(nativeSearchDirs, L";");
    std::wstring lwjglLibraryPath = legacyMinecraftArgs && DirectoryExists(profileNativeDir)
        ? profileNativeDir
        : nativeSearchPath;
    std::wstring gameDir = localRoot + L"\\game\\" + SanitizePathSegment(minecraftVersion);
    std::wstring sharedAssetsDir = localRoot + L"\\assets";
    std::wstring profileAssetsDir = profileRoot + L"\\assets";
    std::wstring sharedAssetIndex = sharedAssetsDir + L"\\indexes\\" + assetIndex + L".json";
    std::wstring assetsDir = FileExists(sharedAssetIndex) ? sharedAssetsDir : profileAssetsDir;
    std::wstring logsDir = localRoot + L"\\logs";
    std::wstring tempDir = gameDir + L"\\tmp";
    std::wstring jnaTempDir = tempDir + L"\\jna";
    std::wstring lwjglTempDir = tempDir + L"\\lwjgl";
    std::wstring nettyTempDir = tempDir + L"\\netty";

    EnsureDirectory(gameDir);
    EnsureDirectory(logsDir);
    EnsureDirectory(tempDir);
    EnsureDirectory(jnaTempDir);
    EnsureDirectory(lwjglTempDir);
    EnsureDirectory(nettyTempDir);
    if (fabricProfile)
    {
        std::wstring remappedJarsDir = gameDir + L"\\.fabric\\remappedJars";
        EnsureDirectory(remappedJarsDir);
        if (!fabricLoaderVersion.empty())
        {
            std::wstring remapCacheDir = remappedJarsDir + L"\\minecraft-" +
                SanitizePathSegment(minecraftVersion) + L"-" +
                SanitizePathSegment(fabricLoaderVersion);
            EnsureDirectory(remapCacheDir);

            std::wstring clientTmpJar = remapCacheDir + L"\\client-intermediary.jar.tmp";
            SetFileAttributesW(clientTmpJar.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(clientTmpJar.c_str());

            std::wstring serverTmpJar = remapCacheDir + L"\\server-intermediary.jar.tmp";
            SetFileAttributesW(serverTmpJar.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(serverTmpJar.c_str());

            WriteLogF(L"Fabric remap cache dir=%s", remapCacheDir.c_str());
        }
        else
        {
            WriteLogF(L"Fabric remap cache root=%s", remappedJarsDir.c_str());
        }
    }

    std::wstring mesaDir;
    if (!ResolveMesaDirectory(mesaDir))
    {
        WriteLogW(L"FAIL: Mesa runtime directory not found for Minecraft launch");
        return false;
    }

    std::wstring mesaOpenGl = mesaDir + L"\\opengl32.dll";
    std::wstring xboxOpenGl = ResolvePackageOrLocalFile(localRoot, packageRoot, L"native\\xbox-opengl.dll");
    std::wstring xboxGlfw = ResolvePackageOrLocalFile(localRoot, packageRoot, L"native\\xbox-glfw.dll");
    std::wstring xboxOpenAl = ResolvePackageOrLocalFile(localRoot, packageRoot, L"native\\xbox-openal.dll");
    std::wstring patchJar = ResolvePackageOrLocalFile(localRoot, packageRoot, L"native\\xbox-jdk-patch.jar");
    std::wstring linkPatchJar = ResolvePackageOrLocalFile(localRoot, packageRoot, L"native\\xbox-jdk-link-patch.jar");
    std::wstring lwjgl2PatchAgent = ResolvePackageOrLocalFile(localRoot, packageRoot, L"native\\xbox-lwjgl2-patch-agent.jar");
    std::wstring modernLanAgent = ResolvePackageOrLocalFile(localRoot, packageRoot, L"native\\xbox-modern-lan-agent.jar");
    std::wstring fabricLanDiscoveryMod = ResolvePackageOrLocalFile(localRoot, packageRoot, L"native\\xbox-lan-discovery-fabric.jar");
    std::wstring opengl32Shim = ResolvePackageOrLocalFile(localRoot, packageRoot, L"native\\opengl32.dll");
    std::wstring nativeGlfwLog = logsDir + L"\\native-glfw-from-native-host.log";
    std::wstring minecraftLog = logsDir + L"\\native-minecraft.log";
    std::wstring gcLog = logsDir + L"\\native-jvm-gc.log";
    std::wstring log4jConfig = gameDir + L"\\log4j2-native-file.xml";
    SetFileAttributesW(gcLog.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(gcLog.c_str());

    WriteLogF(L"Minecraft launch profile=%s root=%s", launchProfile.label.c_str(), profileRoot.c_str());
    WriteLogF(L"Minecraft launch jvm.dll=%s exists=%d", jvmPath.c_str(), FileExists(jvmPath) ? 1 : 0);
    WriteLogF(L"Minecraft launch version=%s", minecraftVersion.c_str());
    WriteLogF(L"Minecraft launch baseVersion=%s assetIndex=%s modLoader=%s forgeProfile=%d fabricProfile=%d neoforgeProfile=%d includeBaseVersionLibrary=%d",
        baseMinecraftVersion.c_str(),
        assetIndex.c_str(),
        launchProfile.modLoader.empty() ? L"<none>" : launchProfile.modLoader.c_str(),
        forgeProfile ? 1 : 0,
        fabricProfile ? 1 : 0,
        neoforgeProfile ? 1 : 0,
        includeBaseVersionModLibrary ? 1 : 0);
    WriteLogF(L"Minecraft launch mainClass=%s", mainClass.c_str());
    WriteLogF(L"Minecraft launch launcherVersionName=%s", launcherVersionName.c_str());
    WriteLogF(L"Minecraft launch assetsDir=%s", assetsDir.c_str());
    WriteLogF(L"Minecraft launch javaRuntimeMajor=%d legacyArgs=%d", javaRuntimeMajor, legacyMinecraftArgs ? 1 : 0);
    WriteLogF(L"Minecraft launch nativeDir=%s", nativeDir.c_str());
    WriteLogF(L"Minecraft launch profileNativeDir=%s exists=%d", profileNativeDir.c_str(), DirectoryExists(profileNativeDir) ? 1 : 0);
    WriteLogF(L"Minecraft launch nativeSearchPath=%s", nativeSearchPath.c_str());
    WriteLogF(L"Minecraft launch lwjglLibraryPath=%s", lwjglLibraryPath.c_str());
    WriteLogF(L"Minecraft launch mesaOpenGl=%s exists=%d", mesaOpenGl.c_str(), FileExists(mesaOpenGl) ? 1 : 0);
#ifdef MINECRAFT_JAVA_TEST_FIXES
    WriteLogF(L"Minecraft launch lwjglOpenGl=%s (xbox-opengl EGL forwarder)", xboxOpenGl.empty() ? L"<missing>" : xboxOpenGl.c_str());
#else
    WriteLogF(L"Minecraft launch xboxOpenGl=%s", xboxOpenGl.empty() ? L"<missing>" : xboxOpenGl.c_str());
#endif
    WriteLogF(L"Minecraft launch xboxGlfw=%s", xboxGlfw.empty() ? L"<missing>" : xboxGlfw.c_str());
    WriteLogF(L"Minecraft launch xboxOpenAl=%s", xboxOpenAl.empty() ? L"<missing>" : xboxOpenAl.c_str());
    WriteLogF(L"Minecraft launch jdkPatchJar=%s", patchJar.empty() ? L"<missing>" : patchJar.c_str());
    WriteLogF(L"Minecraft launch jdkLinkPatchJar=%s", linkPatchJar.empty() ? L"<missing>" : linkPatchJar.c_str());
    if (legacyMinecraftArgs)
    {
        WriteLogF(L"Minecraft launch lwjgl2PatchAgent=%s", lwjgl2PatchAgent.empty() ? L"<missing>" : lwjgl2PatchAgent.c_str());
        WriteLogF(L"Minecraft launch opengl32Shim=%s", opengl32Shim.empty() ? L"<missing>" : opengl32Shim.c_str());
    }
    if (javaRuntimeMajor >= 9)
    {
        WriteLogF(L"Minecraft launch modernLanAgent=%s", modernLanAgent.empty() ? L"<missing>" : modernLanAgent.c_str());
    }
    if (fabricProfile)
    {
        WriteLogF(L"Minecraft launch fabricLanDiscoveryMod=%s", fabricLanDiscoveryMod.empty() ? L"<missing>" : fabricLanDiscoveryMod.c_str());
    }
#ifdef MINECRAFT_JAVA_TEST_FIXES
    if (!FileExists(jvmPath) || !FileExists(mesaOpenGl) || xboxOpenGl.empty() || xboxGlfw.empty())
#else
    if (!FileExists(jvmPath) || !FileExists(mesaOpenGl) || xboxOpenGl.empty() || xboxGlfw.empty())
#endif
    {
        WriteLogW(L"FAIL: required runtime/native files missing for Minecraft launch");
        return false;
    }

    ApplyChunkLoadMitigationOptions(
        gameDir,
        minecraftVersion,
        baseMinecraftVersion,
        launchProfile.label,
        legacyMinecraftArgs);

    std::vector<std::wstring> classpathEntries = BuildMinecraftClasspath(profileRoot);
    WriteLogF(L"Minecraft classpath entries=%zu", classpathEntries.size());
    if (classpathEntries.empty())
    {
        WriteLogW(L"FAIL: Minecraft classpath is empty");
        return false;
    }
    std::wstring classpath = JoinPaths(classpathEntries, L";");
    std::vector<std::string> profileJvmArgs = ResolveProfileArguments(
        profileRoot,
        "ProfileJvmArgs",
        L"minecraft-jvm-args.txt",
        launcherVersionName);
    std::vector<std::string> profileGameArgs = ResolveProfileArguments(
        profileRoot,
        "ProfileGameArgs",
        L"minecraft-game-args.txt",
        launcherVersionName);
    WriteLogF(
        L"Minecraft launch profile JVM args=%zu game args=%zu",
        profileJvmArgs.size(),
        profileGameArgs.size());

    std::string log4jXml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
        "<Configuration status=\"WARN\">\r\n"
        "  <Appenders>\r\n"
        "    <File name=\"File\" fileName=\"" + WideToUtf8(minecraftLog) + "\" append=\"false\">\r\n"
        "      <PatternLayout pattern=\"[%d{HH:mm:ss}] [%t/%level]: %msg%n%xEx\"/>\r\n"
        "    </File>\r\n"
        "  </Appenders>\r\n"
        "  <Loggers>\r\n"
        "    <Root level=\"INFO\"><AppenderRef ref=\"File\"/></Root>\r\n"
        "  </Loggers>\r\n"
        "</Configuration>\r\n";
    WriteTextFileUtf8(log4jConfig, log4jXml);
    DeleteFileW(nativeGlfwLog.c_str());
    WriteTextFileUtf8(nativeGlfwLog, "[NativeCoreWindowProbe] Native GLFW log started from native CoreWindow host\r\n");

    PickAndStageModsInteractive(
        window,
        localRoot,
        profileRoot,
        gameDir,
        minecraftVersion,
        baseMinecraftVersion,
        includeBaseVersionModLibrary,
        profileLooksModded,
        fabricProfile,
        fabricLanDiscoveryMod);
    if (forgeProfile)
    {
        ForceForgeSplashDisabled(gameDir);
    }
    if (neoforgeProfile)
    {
        ForceModLauncherEarlyWindowDisabled(gameDir, L"NeoForge");
    }
    else if (forgeProfile && !legacyMinecraftArgs)
    {
        ForceModLauncherEarlyWindowDisabled(gameDir, L"Forge");
    }

    bool controlifyStaged = HasStagedMod(gameDir, L"controlify");
    bool controllableStaged = HasStagedMod(gameDir, L"controllable");
    bool legacy4JStaged = HasStagedMod(gameDir, L"Legacy4J");
    bool cobblemonStaged = HasStagedMod(gameDir, L"cobblemon");
    if (controlifyStaged)
    {
        ForceControlifyGlfwOnlyConfig(gameDir);
    }
    bool legacyControllableSyntheticController = legacyMinecraftArgs && controllableStaged;
    bool controllerModStaged =
        controlifyStaged ||
        legacy4JStaged ||
        controllableStaged;
    SetEnvironmentVariableW(
        L"MINECRAFT_XBOX_DISABLE_GAMEPAD_KEYBOARD_BRIDGE",
        controllerModStaged ? L"1" : L"0");
    SetEnvironmentVariableW(
        L"MINECRAFT_XBOX_GLFW_DISABLE_GAMEPAD_MOUSE",
        legacyControllableSyntheticController ? L"1" : L"0");
    const wchar_t* bridgeReason = controllerModStaged
        ? (legacyControllableSyntheticController
            ? L"disabled because legacy Controllable uses the synthetic Xbox controller"
            : L"disabled because a controller mod is staged")
        : L"enabled because no controller mod is staged";
    WriteLogF(
        L"Gamepad-to-keyboard CoreWindow bridge %s for this launch",
        bridgeReason);
    if (legacyControllableSyntheticController)
    {
        WriteLogW(L"GLFW controller mouse mode disabled for legacy Controllable; Controllable owns pointer mode");
    }
    ApplyGamepadMouseSettings(localRoot);

    if (!SetCurrentDirectoryW(gameDir.c_str()))
    {
        WriteLogF(L"WARN: SetCurrentDirectoryW(gameDir) failed GetLastError=%lu", GetLastError());
    }

    SetEnvironmentVariableW(L"JAVA_HOME", jreRoot.c_str());
    SetEnvironmentVariableW(L"TMP", tempDir.c_str());
    SetEnvironmentVariableW(L"TEMP", tempDir.c_str());
    SetEnvironmentVariableW(L"USERPROFILE", gameDir.c_str());
    SetEnvironmentVariableW(L"APPDATA", gameDir.c_str());
    SetEnvironmentVariableW(L"LOCALAPPDATA", gameDir.c_str());
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_NATIVE_LOG", nativeGlfwLog.c_str());
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_GLFW_MESA_EGL_CONTEXT", L"1");
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_FATAL_ON_SURFACELESS", L"1");
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_COREWINDOW_HOST", L"1");
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_FRAME_TIMING", L"0");
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_GLFW_PREFER_NATIVE_COREWINDOW", L"1");
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_GLFW_PREFER_NATIVE_DESCRIPTOR", L"1");
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_OPENGL_DLL", mesaOpenGl.c_str());
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_REAL_OPENGL_DLL", mesaOpenGl.c_str());
#ifdef MINECRAFT_JAVA_TEST_FIXES
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_OPENGL_FORWARD_MESA", L"1");
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_OPENGL_EGL_PROC_ONLY", L"1");
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_OPENGL_COMMAND_BRIDGE", L"0");
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_OPENGL_HYBRID_SYNC", L"0");
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_GLFW_SKIP_WGL_GALLIUM", L"1");
    WriteLogW(L"MinecraftJavaTestFixes: LWJGL via xbox-opengl EGL forwarder (no libgallium_wgl, command bridge disabled, hybrid GL sync disabled for stability)");
#else
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_OPENGL_FORWARD_MESA", L"1");
#endif
    SetEnvironmentVariableW(L"MINECRAFT_XBOX_FORCE_COMPAT_GL_IDENTITY", L"0");
    SetEnvironmentVariableW(L"MC_GRAPHICS_RUNTIME", L"desktop");
    SetEnvironmentVariableW(L"EGL_PLATFORM", L"windows");
    SetEnvironmentVariableW(L"GALLIUM_DRIVER", L"d3d12");
    SetEnvironmentVariableW(L"MESA_LOADER_DRIVER_OVERRIDE", L"d3d12");
    SetEnvironmentVariableW(L"MESA_FMALLOC_CACHE_FILE", (ParentDirectory(mesaDir) + L"\\mesa-fmalloc-java.swap").c_str());
    SetEnvironmentVariableW(L"MESA_FMALLOC_CACHE_MB", L"512");
    bool creativeSearchMesaSafetyMode = IsCreativeSearchMesaRiskVersion(baseMinecraftVersion);
    SetEnvironmentVariableW(L"mesa_glthread", creativeSearchMesaSafetyMode ? L"false" : nullptr);
    SetEnvironmentVariableW(L"MESA_GLTHREAD", creativeSearchMesaSafetyMode ? L"false" : nullptr);
    SetEnvironmentVariableW(L"MESA_SHADER_CACHE_DISABLE", creativeSearchMesaSafetyMode ? L"true" : nullptr);
    SetEnvironmentVariableW(L"GALLIUM_THREAD", creativeSearchMesaSafetyMode ? L"0" : nullptr);
    WriteLogF(
        L"Creative search Mesa safety mode baseVersion=%s enabled=%d",
        baseMinecraftVersion.c_str(),
        creativeSearchMesaSafetyMode ? 1 : 0);
    PrependEnvironmentPath({ serverDir, javaBin, profileNativeDir, nativeDir, mesaDir });

    WriteLogW(legacyMinecraftArgs
        ? L"Preloading Mesa runtime DLLs for legacy LWJGL2"
        : L"Preloading Mesa runtime DLLs for xbox-glfw without initializing EGL");
    PreloadMesaDependencies(mesaDir);
    if (legacyMinecraftArgs)
    {
        if (!opengl32Shim.empty())
        {
            LoadDllFromDirectory(ParentDirectory(opengl32Shim), L"opengl32.dll", true);
        }
        WriteLogW(L"LWJGL2 mode: creating CoreWindow EGL context before JVM");
        if (!ProbeMesaEgl(window, false, true))
        {
            WriteLogW(L"FAIL: legacy LWJGL2 EGL context setup failed");
            return false;
        }
    }
    else
    {
        HMODULE mesaOpenGlModule = LoadDllFromDirectory(mesaDir, L"opengl32.dll", true);
        if (!mesaOpenGlModule)
        {
            WriteLogW(L"FAIL: Mesa opengl32.dll preload failed before xbox-glfw warmup");
            return false;
        }
        TryCallMesaProcInit(mesaOpenGlModule);

        if (!HandoffCoreWindowToGlfw(window, xboxGlfw, width, height, surfaceWidth, surfaceHeight))
        {
            return false;
        }
    }

    auto runJvm = [&]() -> bool
    {
    const wchar_t* javaBinDeps[] =
    {
        L"vcruntime140.dll",
        L"vcruntime140_1.dll",
        L"msvcp140.dll",
        L"ucrtbase.dll",
        L"jli.dll",
        L"java.dll",
        L"jimage.dll",
        L"zip.dll",
        L"nio.dll",
        L"net.dll",
    };
    WriteLogW(L"Preloading Java runtime dependency DLLs for Minecraft launch");
    for (auto name : javaBinDeps)
    {
        if (FileExists(javaBin + L"\\" + name))
        {
            LoadDllFromDirectory(javaBin, name, false);
        }
    }

    HMODULE jvmLibrary = LoadDllFromDirectory(serverDir, L"jvm.dll", true);
    if (!jvmLibrary)
    {
        WriteLogW(L"FAIL: LoadLibraryExW(jvm.dll) failed for Minecraft launch");
        return false;
    }

    auto createJavaVm = reinterpret_cast<JNI_CreateJavaVMProc>(GetProcAddress(jvmLibrary, "JNI_CreateJavaVM"));
    if (!createJavaVm)
    {
        WriteLogW(L"FAIL: JNI_CreateJavaVM export missing for Minecraft launch");
        return false;
    }

    size_t normalizedJvmPairs = 0;
    std::vector<std::string> invocationProfileJvmArgs =
        NormalizeJvmArgumentsForInvocation(profileJvmArgs, &normalizedJvmPairs);
    if (normalizedJvmPairs > 0)
    {
        WriteLogF(
            L"Normalized %zu split profile JVM option pairs for JNI invocation",
            normalizedJvmPairs);
    }

    std::vector<std::string> optionText;
    optionText.reserve(48 + invocationProfileJvmArgs.size());
    optionText.emplace_back("-Xrs");
    optionText.emplace_back(WideToUtf8(L"-XX:ErrorFile=" + logsDir + L"\\hs_err_pid%p.log"));
    bool fourKLaunch =
        width >= 3840 ||
        height >= 2160 ||
        surfaceWidth >= 3840 ||
        surfaceHeight >= 2160;
    int defaultModernHeapMb = 3072;
    int defaultModernInitialHeapMb = fourKLaunch ? 2048 : 1536;
    int defaultModernSoftHeapMb = 3072;
    int heapMaxMb = GetEnvironmentInt(
        L"MINECRAFT_XBOX_JAVA_HEAP_MB",
        legacyMinecraftArgs ? 2048 : defaultModernHeapMb,
        1024,
        4096);
    int heapInitialMb = GetEnvironmentInt(
        L"MINECRAFT_XBOX_JAVA_INITIAL_HEAP_MB",
        legacyMinecraftArgs ? 1024 : defaultModernInitialHeapMb,
        512,
        heapMaxMb);
    if (defaultModernSoftHeapMb > heapMaxMb)
    {
        defaultModernSoftHeapMb = heapMaxMb;
    }
    int softHeapMb = GetEnvironmentInt(
        L"MINECRAFT_XBOX_JAVA_SOFT_HEAP_MB",
        legacyMinecraftArgs ? heapMaxMb : defaultModernSoftHeapMb,
        1024,
        heapMaxMb);
    optionText.emplace_back("-Xms" + std::to_string(heapInitialMb) + "M");
    optionText.emplace_back("-Xmx" + std::to_string(heapMaxMb) + "M");
    if (javaRuntimeMajor >= 17 && softHeapMb < heapMaxMb)
    {
        optionText.emplace_back("-XX:SoftMaxHeapSize=" + std::to_string(softHeapMb) + "M");
    }
    WriteLogF(
        L"Xbox JVM heap options initial=%dM max=%dM soft=%dM javaMajor=%d legacy=%d fourKLaunch=%d",
        heapInitialMb,
        heapMaxMb,
        softHeapMb,
        javaRuntimeMajor,
        legacyMinecraftArgs ? 1 : 0,
        fourKLaunch ? 1 : 0);
    AppendChunkLoadMitigationJvmOptions(optionText, legacyMinecraftArgs, javaRuntimeMajor);
    bool gcLoggingEnabled = IsTruthyEnvironment(L"MINECRAFT_XBOX_ENABLE_GC_LOG", false);
    if (gcLoggingEnabled && javaRuntimeMajor >= 9)
    {
        optionText.emplace_back("-Xlog:gc*:file=../../logs/native-jvm-gc.log:uptime,level,tags");
        WriteLogF(L"JVM GC logging enabled path=%s", gcLog.c_str());
    }
    else if (gcLoggingEnabled && javaRuntimeMajor > 0)
    {
        optionText.emplace_back("-Xloggc:..\\..\\logs\\native-jvm-gc.log");
        optionText.emplace_back("-XX:+PrintGCDetails");
        optionText.emplace_back("-XX:+PrintGCTimeStamps");
        WriteLogF(L"Legacy JVM GC logging enabled path=%s", gcLog.c_str());
    }
    else
    {
        WriteLogW(L"JVM GC logging disabled; set MINECRAFT_XBOX_ENABLE_GC_LOG=1 to enable diagnostics");
    }
    if (javaRuntimeMajor >= 9 && javaRuntimeMajor < 21 && !patchJar.empty())
    {
        optionText.emplace_back(WideToUtf8(L"--patch-module=java.base=" + patchJar));
        WriteLogW(L"JDK base filesystem patch enabled for Java 9-20 runtime");
    }
    else if (javaRuntimeMajor >= 21 && !linkPatchJar.empty())
    {
        optionText.emplace_back(WideToUtf8(L"--patch-module=java.base=" + linkPatchJar));
        WriteLogW(L"JDK link-support filesystem patch enabled for Java 21+ runtime");
    }
    else if (javaRuntimeMajor >= 21 && !patchJar.empty())
    {
        WriteLogW(L"JDK base filesystem patch skipped for Java 21+ runtime; Java 21 link patch missing");
    }
    if (legacyMinecraftArgs && !lwjgl2PatchAgent.empty())
    {
        optionText.emplace_back(WideToUtf8(L"-javaagent:" + lwjgl2PatchAgent));
        optionText.emplace_back("-Dminecraft.xbox.lwjgl2.patch=true");
    }
    bool modernLanAgentRequested =
        IsTruthyEnvironment(L"MINECRAFT_XBOX_MODERN_LAN_AGENT", false) ||
        FileExists(gameDir + L"\\xbox-modern-lan-agent.enable") ||
        FileExists(localRoot + L"\\xbox-modern-lan-agent.enable");
    bool cobblemonShowdownFsPatchEnabled =
        cobblemonStaged &&
        IsTruthyEnvironment(L"MINECRAFT_XBOX_COBBLEMON_SHOWDOWN_FS_PATCH", true);
    bool modernPatchAgentRequested =
        modernLanAgentRequested ||
        cobblemonShowdownFsPatchEnabled;
    if (!legacyMinecraftArgs && javaRuntimeMajor >= 9 && !modernLanAgent.empty() && modernPatchAgentRequested)
    {
        if (modernLanAgentRequested)
        {
            optionText.emplace_back("-Dminecraft.xbox.lan.discoveryPatch=true");
        }
        if (cobblemonShowdownFsPatchEnabled)
        {
            optionText.emplace_back("-Dminecraft.xbox.cobblemon.showdownFsPatch=true");
        }
        optionText.emplace_back(WideToUtf8(L"-javaagent:" + modernLanAgent));
        WriteLogF(
            L"Modern patch javaagent enabled lanDiscovery=%d cobblemonShowdownFsPatch=%d",
            modernLanAgentRequested ? 1 : 0,
            cobblemonShowdownFsPatchEnabled ? 1 : 0);
    }
    else if (!legacyMinecraftArgs && javaRuntimeMajor >= 9 && !modernLanAgent.empty())
    {
        WriteLogW(L"Modern patch javaagent disabled; no LAN or Cobblemon Showdown patch requested");
    }
    if (fabricProfile)
    {
        optionText.emplace_back("-DFabricMcEmu= net.minecraft.client.main.Main ");
    }
    for (auto const& arg : invocationProfileJvmArgs)
    {
        optionText.emplace_back(arg);
    }
    std::wstring lanBindAddress = GetPreferredNativeLanIpv4Address();
    if (!lanBindAddress.empty())
    {
        optionText.emplace_back(WideToUtf8(L"-Dminecraft.xbox.lan.bindAddress=" + lanBindAddress));
        WriteLogF(L"LAN discovery JVM bind address set to %s", lanBindAddress.c_str());
    }
    else
    {
        WriteLogW(L"LAN discovery JVM bind address unavailable; Fabric LAN patch will use Java interface enumeration");
    }
    if (fabricProfile || legacyMinecraftArgs)
    {
        EnsureManualLanServerTemplate(localRoot);
        optionText.emplace_back(WideToUtf8(L"-Dminecraft.xbox.lan.manualServers=" + localRoot + L"\\xbox-lan-servers.txt;" + gameDir + L"\\xbox-lan-servers.txt"));
        WriteLogF(L"LAN manual server fallback file=%s", (localRoot + L"\\xbox-lan-servers.txt").c_str());
    }
    AppendLanPlayJvmOptions(optionText, minecraftLog, launchProfile.label);
    if (fabricProfile || legacyMinecraftArgs)
    {
        StartNativeLanDiscoveryBridge();
    }
    StartNativeLanAdvertiser(minecraftLog, launchProfile.label);
    if (legacyMinecraftArgs && controllableStaged)
    {
        optionText.emplace_back("-Dcontrollable.xbox.disable_sdl2=true");
        WriteLogW(L"Legacy Controllable detected; disabling bundled SDL2 backend through LWJGL2 agent");
    }
    if (controlifyStaged)
    {
        std::wstring missingSdl3 = gameDir + L"\\xbox-disabled-sdl3\\SDL3.dll";
        optionText.emplace_back(WideToUtf8(L"-Dcontrolify.debug.sdl_natives_override=" + missingSdl3));
        WriteLogF(L"Controlify detected; forcing GLFW controller manager by blocking SDL3 native load at %s", missingSdl3.c_str());
    }
    if (javaRuntimeMajor >= 9)
    {
        optionText.emplace_back("--add-opens=java.base/sun.nio.fs=ALL-UNNAMED");
        optionText.emplace_back("--add-opens=java.base/java.nio.file=ALL-UNNAMED");
        optionText.emplace_back("--add-opens=java.base/java.lang=ALL-UNNAMED");
        optionText.emplace_back("--add-opens=java.base/java.lang.invoke=ALL-UNNAMED");
        optionText.emplace_back("--add-opens=java.base/java.util.jar=ALL-UNNAMED");
    }
    optionText.emplace_back(WideToUtf8(L"-Djava.home=" + jreRoot));
    optionText.emplace_back(WideToUtf8(L"-Djava.class.path=" + classpath));
    optionText.emplace_back(WideToUtf8(L"-Djava.library.path=" + nativeSearchPath));
    optionText.emplace_back(WideToUtf8(L"-Dorg.lwjgl.librarypath=" + lwjglLibraryPath));
    optionText.emplace_back(WideToUtf8(L"-Djava.io.tmpdir=" + tempDir));
    optionText.emplace_back(WideToUtf8(L"-Duser.home=" + gameDir));
    optionText.emplace_back(WideToUtf8(L"-Duser.dir=" + gameDir));
    optionText.emplace_back("-Djava.awt.headless=true");
    optionText.emplace_back(WideToUtf8(L"-Djna.tmpdir=" + jnaTempDir));
    optionText.emplace_back(WideToUtf8(L"-Djna.boot.library.path=" + nativeSearchPath));
    optionText.emplace_back(WideToUtf8(L"-Djna.library.path=" + nativeSearchPath));
    optionText.emplace_back(WideToUtf8(L"-Djna.platform.library.path=" + nativeSearchPath));
    optionText.emplace_back("-Djna.nosys=true");
    optionText.emplace_back(WideToUtf8(L"-Dlog4j2.configurationFile=" + log4jConfig));
    optionText.emplace_back(WideToUtf8(L"-Dlog4j.configurationFile=" + log4jConfig));
    optionText.emplace_back(WideToUtf8(L"-Dorg.lwjgl.system.SharedLibraryExtractPath=" + lwjglTempDir));
    optionText.emplace_back(WideToUtf8(L"-Dorg.lwjgl.glfw.libname=" + xboxGlfw));
    optionText.emplace_back(WideToUtf8(L"-Dorg.lwjgl.opengl.libname=" + xboxOpenGl));
    if (!xboxOpenAl.empty())
    {
        optionText.emplace_back(WideToUtf8(L"-Dorg.lwjgl.openal.libname=" + xboxOpenAl));
    }
    optionText.emplace_back(WideToUtf8(L"-Dio.netty.native.workdir=" + nettyTempDir));
    optionText.emplace_back("-Dorg.lwjgl.util.Debug=false");
    optionText.emplace_back("-Dorg.lwjgl.util.DebugLoader=false");
    optionText.emplace_back("-Dminecraft.launcher.brand=xbox-uwp-native");
#ifdef MINECRAFT_JAVA_TEST_FIXES
    optionText.emplace_back("-Dminecraft.launcher.version=1.0.3-testfixes");
#else
    optionText.emplace_back("-Dminecraft.launcher.version=1.0.127");
#endif
    optionText.emplace_back("-Dminecraft.graphics.backend=d3d12-opengl");
    optionText.emplace_back("-Dminecraft.audio.backend=xbox");
    optionText.emplace_back("-Dminecraft.input.backend=gamepad-bridge");
    optionText.emplace_back("-Dminecraft.window.width=" + std::to_string(width));
    optionText.emplace_back("-Dminecraft.window.height=" + std::to_string(height));

    std::vector<JavaVMOption> options;
    options.reserve(optionText.size());
    for (auto& text : optionText)
    {
        JavaVMOption option = {};
        option.optionString = text.data();
        option.extraInfo = nullptr;
        options.push_back(option);
    }

    JavaVMInitArgs vmArgs = {};
    vmArgs.version = JNI_VERSION_1_8_VALUE;
    vmArgs.nOptions = static_cast<jint>(options.size());
    vmArgs.options = options.data();
    vmArgs.ignoreUnrecognized = 1;

    JavaVM* javaVm = nullptr;
    JNIEnv* env = nullptr;
    WriteLogF(L"JNI_CreateJavaVM for Minecraft starting optionCount=%d", vmArgs.nOptions);
    jint createResult = SafeCreateJavaVm(createJavaVm, &javaVm, &env, &vmArgs);
    WriteLogF(L"JNI_CreateJavaVM for Minecraft result=%d vm=%p env=%p", createResult, javaVm, env);
    if (createResult != JNI_OK_VALUE || !javaVm || !env)
    {
        WriteLogW(L"FAIL: JNI_CreateJavaVM did not start Minecraft JVM");
        return false;
    }

    JniApi api = {};
    if (!InitializeJniApi(env, api))
    {
        return false;
    }

    std::vector<std::string> appArgs;
    auto addArg = [&appArgs](const char* text) { appArgs.emplace_back(text); };
    auto addArgW = [&appArgs](std::wstring const& text) { appArgs.emplace_back(WideToUtf8(text)); };
    addArg("--version");
    addArgW(minecraftVersion);
    addArg("--gameDir");
    addArgW(gameDir);
    addArg("--assetsDir");
    addArgW(assetsDir);
    addArg("--assetIndex");
    addArgW(assetIndex);
    addArg("--username");
    addArgW(authSession.username);
    addArg("--uuid");
    addArgW(authSession.uuid);
    addArg("--accessToken");
    addArgW(authSession.accessToken);
    if (legacyMinecraftArgs)
    {
        addArg("--userProperties");
        addArg("{}");
        addArg("--userType");
        addArg(authSession.online ? "msa" : "mojang");
        if (forgeProfile)
        {
            addArg("--tweakClass");
            addArg("net.minecraftforge.fml.common.launcher.FMLTweaker");
        }
    }
    else
    {
        addArg("--clientId");
        addArgW(authSession.clientId);
        addArg("--xuid");
        addArgW(authSession.xuid);
    }
    for (auto const& arg : profileGameArgs)
    {
        appArgs.push_back(arg);
    }
    addArg("--versionType");
    addArg(neoforgeProfile ? "NeoForge" : (forgeProfile ? "Forge" : "release"));
    addArg("--width");
    appArgs.emplace_back(std::to_string(width));
    addArg("--height");
    appArgs.emplace_back(std::to_string(height));
    addArg("--fullscreen");

    std::string mainClassUtf8 = WideToUtf8(mainClass);
    WriteLogF(L"Invoking Minecraft main class %s with %zu args. Minecraft log=%s nativeGlfwLog=%s",
        mainClass.c_str(),
        appArgs.size(),
        minecraftLog.c_str(),
        nativeGlfwLog.c_str());
    bool invoked = InvokeJavaMain(env, api, mainClassUtf8.c_str(), appArgs);
    WriteLogW(invoked ? L"WARN: Minecraft Main.main returned to native host" : L"FAIL: Minecraft Main.main failed before returning");
    return invoked;
    };

    WriteLogF(L"Minecraft JVM running on CoreWindow thread=0x%lx; xbox-glfw will pump dispatcher events", GetCurrentThreadId());
    bool ok = runJvm();
    WriteLogF(L"Minecraft JVM returned on CoreWindow thread result=%d", ok ? 1 : 0);
    return ok;
}

static void RenderProbeFrame(uint32_t frame)
{
    if (!g_eglState.ready || !g_eglState.clearColor || !g_eglState.clear || !g_eglState.swapBuffers)
    {
        return;
    }

    float pulse = static_cast<float>((frame % 120) / 119.0);
    g_eglState.clearColor(0.02f, 0.35f + 0.35f * pulse, 0.16f, 1.0f);
    g_eglState.clear(GL_COLOR_BUFFER_BIT_VALUE);
    SafeSwapBuffers(g_eglState.swapBuffers, g_eglState.display, g_eglState.surface);
}

static void ClearDownloadStatusRect(int x, int y, int width, int height, float red, float green, float blue)
{
    if (!g_eglState.enable || !g_eglState.disable || !g_eglState.scissor || width <= 0 || height <= 0)
    {
        return;
    }

    g_eglState.enable(GL_SCISSOR_TEST_VALUE);
    g_eglState.scissor(x, y, width, height);
    g_eglState.clearColor(red, green, blue, 1.0f);
    g_eglState.clear(GL_COLOR_BUFFER_BIT_VALUE);
    g_eglState.disable(GL_SCISSOR_TEST_VALUE);
}

static void DrawDownloadStatusDigit(int digit, int x, int y, int width, int height, int thickness, float red, float green, float blue)
{
    if (digit < 0 || digit > 9 || width <= 0 || height <= 0)
    {
        return;
    }

    static const unsigned char masks[10] =
    {
        0x3F, // 0
        0x06, // 1
        0x5B, // 2
        0x4F, // 3
        0x66, // 4
        0x6D, // 5
        0x7D, // 6
        0x07, // 7
        0x7F, // 8
        0x6F, // 9
    };

    int t = std::max(2, thickness);
    int half = height / 2;
    unsigned char mask = masks[digit];
    auto segment = [&](unsigned char bit, int sx, int sy, int sw, int sh)
    {
        if ((mask & bit) != 0)
        {
            ClearDownloadStatusRect(sx, sy, sw, sh, red, green, blue);
        }
    };

    segment(0x01, x + t, y + height - t, width - (2 * t), t);
    segment(0x02, x + width - t, y + half, t, height - half - t);
    segment(0x04, x + width - t, y + t, t, half - t);
    segment(0x08, x + t, y, width - (2 * t), t);
    segment(0x10, x, y + t, t, half - t);
    segment(0x20, x, y + half, t, height - half - t);
    segment(0x40, x + t, y + half - (t / 2), width - (2 * t), t);
}

static void DrawDownloadStatusSlash(int x, int y, int width, int height, int thickness, float red, float green, float blue)
{
    int steps = std::max(4, height / std::max(2, thickness));
    int t = std::max(2, thickness);
    for (int i = 0; i < steps; ++i)
    {
        int sy = y + (i * height) / steps;
        int sx = x + width - ((i + 1) * width) / steps;
        ClearDownloadStatusRect(sx, sy, t, std::max(t, height / steps + 1), red, green, blue);
    }
}

static void DrawDownloadStatusPercent(int x, int y, int width, int height, int thickness, float red, float green, float blue)
{
    int dot = std::max(3, thickness + 1);
    ClearDownloadStatusRect(x, y + height - dot, dot, dot, red, green, blue);
    ClearDownloadStatusRect(x + width - dot, y, dot, dot, red, green, blue);
    DrawDownloadStatusSlash(x, y, width, height, thickness, red, green, blue);
}

static int DownloadStatusGlyphWidth(char ch, int digitWidth)
{
    if (ch >= '0' && ch <= '9')
    {
        return digitWidth;
    }
    if (ch == '/')
    {
        return std::max(8, digitWidth / 2);
    }
    if (ch == '%')
    {
        return std::max(10, digitWidth * 2 / 3);
    }
    return digitWidth / 2;
}

static void DrawDownloadStatusText(std::string const& text, int centerX, int y, int digitWidth, int digitHeight, int spacing, float red, float green, float blue)
{
    if (text.empty())
    {
        return;
    }

    int thickness = std::max(2, digitWidth / 6);
    int totalWidth = 0;
    for (char ch : text)
    {
        if (totalWidth > 0)
        {
            totalWidth += spacing;
        }
        totalWidth += DownloadStatusGlyphWidth(ch, digitWidth);
    }

    int x = centerX - (totalWidth / 2);
    for (char ch : text)
    {
        int glyphWidth = DownloadStatusGlyphWidth(ch, digitWidth);
        if (ch >= '0' && ch <= '9')
        {
            DrawDownloadStatusDigit(ch - '0', x, y, glyphWidth, digitHeight, thickness, red, green, blue);
        }
        else if (ch == '/')
        {
            DrawDownloadStatusSlash(x, y, glyphWidth, digitHeight, thickness, red, green, blue);
        }
        else if (ch == '%')
        {
            DrawDownloadStatusPercent(x, y, glyphWidth, digitHeight, thickness, red, green, blue);
        }
        x += glyphWidth + spacing;
    }
}

static void RenderDownloadStatusFrame(uint32_t frame)
{
    if (!g_eglState.ready || !g_eglState.clearColor || !g_eglState.clear || !g_eglState.swapBuffers)
    {
        return;
    }

    int width = g_eglState.width > 0 ? g_eglState.width : g_surfaceWidth;
    int height = g_eglState.height > 0 ? g_eglState.height : g_surfaceHeight;
    width = std::max(width, 320);
    height = std::max(height, 240);
    if (g_eglState.viewport)
    {
        g_eglState.viewport(0, 0, width, height);
    }

    unsigned long long completed = g_downloadUiCompleted.load(std::memory_order_acquire);
    unsigned long long total = g_downloadUiTotal.load(std::memory_order_acquire);
    unsigned long long phase = g_downloadUiPhase.load(std::memory_order_acquire);
    if (total > 0 && completed > total)
    {
        completed = total;
    }

    float pulse = static_cast<float>((frame % 120) / 119.0);
    float wave = pulse < 0.5f ? pulse * 2.0f : (1.0f - pulse) * 2.0f;
    bool assetPhase = phase == 2;
    float bgRed = assetPhase ? 0.015f : 0.015f;
    float bgGreen = assetPhase ? 0.12f + 0.06f * wave : 0.07f + 0.04f * wave;
    float bgBlue = assetPhase ? 0.09f + 0.03f * wave : 0.18f + 0.07f * wave;
    g_eglState.clearColor(bgRed, bgGreen, bgBlue, 1.0f);
    g_eglState.clear(GL_COLOR_BUFFER_BIT_VALUE);

    int trackWidth = std::max(width * 3 / 4, 180);
    trackWidth = std::min(trackWidth, width - 48);
    int trackHeight = std::max(height / 38, 18);
    trackHeight = std::min(trackHeight, 72);
    int trackX = (width - trackWidth) / 2;
    int trackY = std::max(height / 11, 28);

    ClearDownloadStatusRect(trackX - 4, trackY - 4, trackWidth + 8, trackHeight + 8, 0.0f, 0.0f, 0.0f);
    ClearDownloadStatusRect(trackX, trackY, trackWidth, trackHeight, 0.12f, 0.13f, 0.16f);

    int fillWidth = 0;
    unsigned long long percent = 0;
    if (total > 0)
    {
        double progress = static_cast<double>(completed) / static_cast<double>(total);
        fillWidth = static_cast<int>(trackWidth * progress);
        percent = static_cast<unsigned long long>(progress * 100.0 + 0.5);
        if (completed > 0)
        {
            fillWidth = std::max(fillWidth, 3);
        }
        fillWidth = std::min(fillWidth, trackWidth);
        percent = std::min<unsigned long long>(percent, 100);
    }

    float fillRed = assetPhase ? 0.16f : 0.20f;
    float fillGreen = assetPhase ? 0.72f : 0.56f;
    float fillBlue = assetPhase ? 0.38f : 0.92f;
    if (fillWidth > 0)
    {
        ClearDownloadStatusRect(trackX, trackY, fillWidth, trackHeight, fillRed, fillGreen, fillBlue);
    }

    int markerWidth = std::max(trackWidth / 12, 18);
    int markerTravel = std::max(trackWidth - markerWidth, 1);
    int markerX = trackX + static_cast<int>((static_cast<unsigned long long>(frame) * 9ull) % static_cast<unsigned long long>(markerTravel));
    if (fillWidth > markerWidth)
    {
        markerX = std::min(markerX, trackX + fillWidth - markerWidth);
    }
    ClearDownloadStatusRect(markerX, trackY, markerWidth, trackHeight, fillRed + 0.12f, std::min(fillGreen + 0.16f, 1.0f), std::min(fillBlue + 0.12f, 1.0f));

    int percentHeight = std::max(42, height / 8);
    percentHeight = std::min(percentHeight, 120);
    int percentWidth = std::max(24, percentHeight * 5 / 8);
    int percentY = std::min(trackY + trackHeight + std::max(24, height / 18), height - percentHeight - 20);
    DrawDownloadStatusText(
        std::to_string(percent) + "%",
        width / 2,
        percentY,
        percentWidth,
        percentHeight,
        std::max(5, percentWidth / 5),
        0.84f,
        0.96f,
        0.88f);

    int countHeight = std::max(22, height / 18);
    countHeight = std::min(countHeight, 56);
    int countWidth = std::max(13, countHeight * 5 / 8);
    int countY = std::max(18, trackY - countHeight - std::max(18, height / 30));
    DrawDownloadStatusText(
        std::to_string(completed) + "/" + std::to_string(total),
        width / 2,
        countY,
        countWidth,
        countHeight,
        std::max(3, countWidth / 5),
        0.62f,
        0.78f,
        0.70f);

    SafeSwapBuffers(g_eglState.swapBuffers, g_eglState.display, g_eglState.surface);
}

static std::wstring CurrentLocalStatePath()
{
    try
    {
        return ApplicationData::Current().LocalFolder().Path().c_str();
    }
    catch (...)
    {
        return std::wstring();
    }
}

static std::wstring PointerDeviceTypeName(PointerDeviceType type)
{
    switch (type)
    {
    case PointerDeviceType::Mouse:
        return L"mouse";
    case PointerDeviceType::Pen:
        return L"pen";
    case PointerDeviceType::Touch:
        return L"touch";
    default:
        return L"unknown";
    }
}

static void LogPointerEvent(wchar_t const* eventName, PointerEventArgs const& args)
{
    PointerPoint point = args.CurrentPoint();
    Point position = point.Position();
    PointerPointProperties properties = point.Properties();
    std::wstring pointerType = L"unknown";
    try
    {
        PointerDevice device = PointerDevice::GetPointerDevice(point.PointerId());
        if (device)
        {
            pointerType = PointerDeviceTypeName(device.PointerDeviceType());
        }
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(
            L"MOUSETEST pointer device lookup failed id=%u hr=0x%08X",
            point.PointerId(),
            static_cast<unsigned int>(ex.code()));
    }

    WriteLogF(
        L"MOUSETEST %s id=%u type=%s x=%.1f y=%.1f wheel=%d left=%d right=%d middle=%d x1=%d x2=%d",
        eventName,
        point.PointerId(),
        pointerType.c_str(),
        position.X,
        position.Y,
        properties.MouseWheelDelta(),
        properties.IsLeftButtonPressed() ? 1 : 0,
        properties.IsRightButtonPressed() ? 1 : 0,
        properties.IsMiddleButtonPressed() ? 1 : 0,
        properties.IsXButton1Pressed() ? 1 : 0,
        properties.IsXButton2Pressed() ? 1 : 0);
}

static bool MousePointerTestEnabled()
{
    std::wstring localRoot = CurrentLocalStatePath();
    if (localRoot.empty())
    {
        return IsTruthyEnvironment(L"MINECRAFT_XBOX_MOUSE_POINTER_TEST", false);
    }

    return FileExists(localRoot + L"\\enable-mouse-test") ||
        IsTruthyEnvironment(L"MINECRAFT_XBOX_MOUSE_POINTER_TEST", false);
}

struct GameInputMouseProbeState
{
    std::atomic<uint64_t> callbackCount{ 0 };
};

static void LogGameInputMouseReading(
    const wchar_t* source,
    IGameInputReading* reading,
    bool& hasLast,
    GameInputMouseState& lastState)
{
    if (!reading)
    {
        WriteLogF(L"GAMEINPUT %s reading=null", source);
        return;
    }

    GameInputKind kind = reading->GetInputKind();
    GameInputMouseState state = {};
    bool gotMouseState = reading->GetMouseState(&state);
    if (!gotMouseState)
    {
        WriteLogF(
            L"GAMEINPUT %s no mouse state kind=0x%08X timestamp=%llu",
            source,
            static_cast<unsigned int>(kind),
            static_cast<unsigned long long>(reading->GetTimestamp()));
        return;
    }

    long long deltaX = hasLast ? static_cast<long long>(state.positionX - lastState.positionX) : 0;
    long long deltaY = hasLast ? static_cast<long long>(state.positionY - lastState.positionY) : 0;
    long long deltaWheelX = hasLast ? static_cast<long long>(state.wheelX - lastState.wheelX) : 0;
    long long deltaWheelY = hasLast ? static_cast<long long>(state.wheelY - lastState.wheelY) : 0;
    bool changed = !hasLast ||
        state.positionX != lastState.positionX ||
        state.positionY != lastState.positionY ||
        state.wheelX != lastState.wheelX ||
        state.wheelY != lastState.wheelY ||
        state.buttons != lastState.buttons;

    if (changed)
    {
        WriteLogF(
            L"GAMEINPUT %s kind=0x%08X timestamp=%llu pos=(%lld,%lld) delta=(%lld,%lld) wheel=(%lld,%lld) wheelDelta=(%lld,%lld) buttons=0x%08X",
            source,
            static_cast<unsigned int>(kind),
            static_cast<unsigned long long>(reading->GetTimestamp()),
            static_cast<long long>(state.positionX),
            static_cast<long long>(state.positionY),
            deltaX,
            deltaY,
            static_cast<long long>(state.wheelX),
            static_cast<long long>(state.wheelY),
            deltaWheelX,
            deltaWheelY,
            static_cast<unsigned int>(state.buttons));
    }

    lastState = state;
    hasLast = true;
}

static void CALLBACK GameInputMouseReadingCallback(
    GameInputCallbackToken,
    void* context,
    IGameInputReading* reading,
    bool hasOverrunOccurred)
{
    auto* state = static_cast<GameInputMouseProbeState*>(context);
    uint64_t callbackCount = state ? ++state->callbackCount : 0;
    GameInputMouseState lastMouseState = {};
    bool hasLastMouseState = false;
    WriteLogF(
        L"GAMEINPUT callback fired count=%llu overrun=%d",
        static_cast<unsigned long long>(callbackCount),
        hasOverrunOccurred ? 1 : 0);
    LogGameInputMouseReading(L"callback", reading, hasLastMouseState, lastMouseState);
}

static bool RunMousePointerTest(CoreWindow const& window, bool& closed)
{
    if (!window)
    {
        WriteLogW(L"MOUSETEST failed: no CoreWindow");
        return false;
    }

    WriteLogW(L"MOUSETEST starting CoreWindow pointer test; move/click/wheel a USB mouse connected to the Xbox");
    try
    {
        window.PointerCursor(nullptr);
        WriteLogW(L"MOUSETEST CoreWindow pointer cursor hidden for relative mouse mode");
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"MOUSETEST hiding CoreWindow pointer cursor failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
    }
    catch (...)
    {
        WriteLogW(L"MOUSETEST hiding CoreWindow pointer cursor failed with unknown exception");
    }

    try
    {
        MouseCapabilities caps;
        WriteLogF(
            L"MOUSETEST mouse capabilities present=%d buttons=%u verticalWheel=%d horizontalWheel=%d",
            caps.MousePresent(),
            caps.NumberOfButtons(),
            caps.VerticalWheelPresent(),
            caps.HorizontalWheelPresent());
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"MOUSETEST mouse capabilities query failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
    }
    catch (...)
    {
        WriteLogW(L"MOUSETEST mouse capabilities query failed with unknown exception");
    }

    MouseDevice mouseDevice{ nullptr };
    event_token rawMoved{};
    try
    {
        mouseDevice = MouseDevice::GetForCurrentView();
        if (mouseDevice)
        {
            rawMoved = mouseDevice.MouseMoved([](MouseDevice const&, MouseEventArgs const& args)
            {
                auto delta = args.MouseDelta();
                WriteLogF(L"MOUSETEST rawmove dx=%d dy=%d", delta.X, delta.Y);
            });
            WriteLogW(L"MOUSETEST MouseDevice::MouseMoved handler registered");
        }
        else
        {
            WriteLogW(L"MOUSETEST MouseDevice::GetForCurrentView returned null");
        }
    }
    catch (hresult_error const& ex)
    {
        WriteLogF(L"MOUSETEST MouseDevice handler registration failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
    }
    catch (...)
    {
        WriteLogW(L"MOUSETEST MouseDevice handler registration failed with unknown exception");
    }

    winrt::com_ptr<IGameInput> gameInput;
    GameInputCallbackToken gameInputCallbackToken = GAMEINPUT_INVALID_CALLBACK_TOKEN_VALUE;
    GameInputMouseProbeState gameInputProbeState;
    bool gameInputPollHasLast = false;
    GameInputMouseState gameInputPollLast = {};
    uint32_t gameInputNoReadingFrames = 0;
    uint32_t gameInputUnchangedFrames = 0;
    try
    {
        IGameInput* rawGameInput = nullptr;
        HRESULT hr = GameInputCreate(&rawGameInput);
        if (SUCCEEDED(hr) && rawGameInput)
        {
            gameInput.attach(rawGameInput);
            WriteLogF(
                L"GAMEINPUT create ok timestamp=%llu",
                static_cast<unsigned long long>(gameInput->GetCurrentTimestamp()));

            gameInput->SetFocusPolicy(GameInputExclusiveForegroundInput);
            WriteLogW(L"GAMEINPUT focus policy set to GameInputExclusiveForegroundInput");

            HRESULT callbackHr = gameInput->RegisterReadingCallback(
                nullptr,
                GameInputKindMouse,
                0.0f,
                &gameInputProbeState,
                GameInputMouseReadingCallback,
                &gameInputCallbackToken);
            WriteLogF(
                L"GAMEINPUT RegisterReadingCallback mouse hr=0x%08X token=%llu",
                static_cast<unsigned int>(callbackHr),
                static_cast<unsigned long long>(gameInputCallbackToken));

            winrt::com_ptr<IGameInputReading> currentReading;
            IGameInputReading* rawReading = nullptr;
            HRESULT readingHr = gameInput->GetCurrentReading(GameInputKindMouse, nullptr, &rawReading);
            if (SUCCEEDED(readingHr) && rawReading)
            {
                currentReading.attach(rawReading);
                LogGameInputMouseReading(L"initial", currentReading.get(), gameInputPollHasLast, gameInputPollLast);
            }
            else
            {
                WriteLogF(L"GAMEINPUT initial GetCurrentReading mouse hr=0x%08X", static_cast<unsigned int>(readingHr));
            }
        }
        else
        {
            WriteLogF(L"GAMEINPUT create failed hr=0x%08X ptr=%p", static_cast<unsigned int>(hr), rawGameInput);
            if (rawGameInput)
            {
                rawGameInput->Release();
            }
        }
    }
    catch (...)
    {
        WriteLogW(L"GAMEINPUT setup threw unknown exception");
    }

    auto moved = window.PointerMoved([](CoreWindow const&, PointerEventArgs const& args)
    {
        LogPointerEvent(L"move", args);
    });
    auto pressed = window.PointerPressed([](CoreWindow const&, PointerEventArgs const& args)
    {
        LogPointerEvent(L"press", args);
    });
    auto released = window.PointerReleased([](CoreWindow const&, PointerEventArgs const& args)
    {
        LogPointerEvent(L"release", args);
    });
    auto wheel = window.PointerWheelChanged([](CoreWindow const&, PointerEventArgs const& args)
    {
        LogPointerEvent(L"wheel", args);
    });
    auto entered = window.PointerEntered([](CoreWindow const&, PointerEventArgs const& args)
    {
        LogPointerEvent(L"enter", args);
    });
    auto exited = window.PointerExited([](CoreWindow const&, PointerEventArgs const& args)
    {
        LogPointerEvent(L"exit", args);
    });

    auto dispatcher = window.Dispatcher();
    uint32_t frame = 0;
    while (!closed)
    {
        dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        if (gameInput)
        {
            winrt::com_ptr<IGameInputReading> currentReading;
            IGameInputReading* rawReading = nullptr;
            HRESULT readingHr = gameInput->GetCurrentReading(GameInputKindMouse, nullptr, &rawReading);
            if (SUCCEEDED(readingHr) && rawReading)
            {
                currentReading.attach(rawReading);
                GameInputMouseState beforeState = gameInputPollLast;
                bool beforeHasLast = gameInputPollHasLast;
                LogGameInputMouseReading(L"poll", currentReading.get(), gameInputPollHasLast, gameInputPollLast);
                if (beforeHasLast &&
                    beforeState.positionX == gameInputPollLast.positionX &&
                    beforeState.positionY == gameInputPollLast.positionY &&
                    beforeState.wheelX == gameInputPollLast.wheelX &&
                    beforeState.wheelY == gameInputPollLast.wheelY &&
                    beforeState.buttons == gameInputPollLast.buttons)
                {
                    ++gameInputUnchangedFrames;
                    if ((gameInputUnchangedFrames % 300) == 0)
                    {
                        WriteLogF(
                            L"GAMEINPUT poll unchanged frames=%u pos=(%lld,%lld) wheel=(%lld,%lld) buttons=0x%08X callbacks=%llu",
                            gameInputUnchangedFrames,
                            static_cast<long long>(gameInputPollLast.positionX),
                            static_cast<long long>(gameInputPollLast.positionY),
                            static_cast<long long>(gameInputPollLast.wheelX),
                            static_cast<long long>(gameInputPollLast.wheelY),
                            static_cast<unsigned int>(gameInputPollLast.buttons),
                            static_cast<unsigned long long>(gameInputProbeState.callbackCount.load()));
                    }
                }
                else
                {
                    gameInputUnchangedFrames = 0;
                }
            }
            else
            {
                ++gameInputNoReadingFrames;
                if (gameInputNoReadingFrames == 1 || (gameInputNoReadingFrames % 300) == 0)
                {
                    WriteLogF(
                        L"GAMEINPUT poll GetCurrentReading no mouse reading hr=0x%08X frames=%u callbacks=%llu",
                        static_cast<unsigned int>(readingHr),
                        gameInputNoReadingFrames,
                        static_cast<unsigned long long>(gameInputProbeState.callbackCount.load()));
                }
            }
        }
        RenderProbeFrame(frame++);
        Sleep(16);
    }

    window.PointerMoved(moved);
    window.PointerPressed(pressed);
    window.PointerReleased(released);
    window.PointerWheelChanged(wheel);
    window.PointerEntered(entered);
    window.PointerExited(exited);
    if (mouseDevice)
    {
        mouseDevice.MouseMoved(rawMoved);
    }
    if (gameInput && gameInputCallbackToken != GAMEINPUT_INVALID_CALLBACK_TOKEN_VALUE)
    {
        bool unregistered = gameInput->UnregisterCallback(gameInputCallbackToken, 1000000);
        WriteLogF(
            L"GAMEINPUT callback unregister result=%d callbacks=%llu",
            unregistered ? 1 : 0,
            static_cast<unsigned long long>(gameInputProbeState.callbackCount.load()));
    }
    try
    {
        window.PointerCursor(CoreCursor(CoreCursorType::Arrow, 0));
    }
    catch (...) {}
    WriteLogW(L"MOUSETEST stopped");
    return true;
}

struct App : implements<App, IFrameworkViewSource, IFrameworkView>
{
    CoreWindow m_window{ nullptr };
    bool m_closed = false;

    IFrameworkView CreateView()
    {
        return *this;
    }

    void Initialize(CoreApplicationView const&)
    {
        InitializeLog();
        WriteLogW(L"Initialize");
    }

    void Load(hstring const&)
    {
        WriteLogW(L"Load");
    }

    void Uninitialize()
    {
        WriteLogW(L"Uninitialize");
    }

    void SetWindow(CoreWindow const& window)
    {
        InitializeLog();
        TryDisableXboxLayoutScaling();
        m_window = window;
        WriteLogF(L"SetWindow received CoreWindow abi=%p thread=0x%lx", winrt::get_abi(window), GetCurrentThreadId());
        window.Closed({ this, &App::OnClosed });
        window.VisibilityChanged({ this, &App::OnVisibilityChanged });

        try
        {
            CoreApplication::Properties().Insert(L"EGLNativeWindowTypeProperty", window);
            WriteLogW(L"CoreWindow published to CoreApplication.Properties");
        }
        catch (hresult_error const& ex)
        {
            WriteLogF(L"CoreWindow property publish failed hr=0x%08X", static_cast<unsigned int>(ex.code()));
        }
    }

    void Run()
    {
        InitializeLog();
        WriteLogF(L"Run started thread=0x%lx", GetCurrentThreadId());
        if (m_window)
        {
            m_window.Activate();
            WriteLogW(L"CoreWindow.Activate called");
            auto dispatcher = m_window.Dispatcher();
            for (int i = 0; i < 120 && !m_window.Visible(); ++i)
            {
                dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
                Sleep(16);
            }
            WriteLogF(L"CoreWindow visibility before native Minecraft launch visible=%d", m_window.Visible() ? 1 : 0);
        }

        WriteLogW(L"Standalone EGL probe skipped for Minecraft launch; xbox-glfw will initialize Mesa first");
        if (m_window && MousePointerTestEnabled())
        {
            WriteLogW(L"Mouse pointer test enabled; skipping Minecraft launch");
            RunMousePointerTest(m_window, m_closed);
            return;
        }

        if (m_window)
        {
            bool minecraftLaunchPassed = false;
            try
            {
                minecraftLaunchPassed = RunNativeMinecraft(m_window);
            }
            catch (hresult_error const& ex)
            {
                WriteLogF(L"Native Minecraft launch threw hresult_error hr=0x%08X", static_cast<unsigned int>(ex.code()));
            }
            catch (...)
            {
                WriteLogW(L"Native Minecraft launch threw unknown C++ exception");
            }
            WriteLogW(minecraftLaunchPassed ? L"Native embedded Minecraft launch result=RETURNED" : L"Native embedded Minecraft launch result=FAIL");
        }

        auto dispatcher = m_window ? m_window.Dispatcher() : CoreWindow::GetForCurrentThread().Dispatcher();
        uint32_t frame = 0;
        while (!m_closed)
        {
            dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            RenderProbeFrame(frame++);
            Sleep(16);
        }
    }

    void OnClosed(CoreWindow const&, CoreWindowEventArgs const&)
    {
        WriteLogW(L"CoreWindow closed");
        m_closed = true;
    }

    void OnVisibilityChanged(CoreWindow const&, VisibilityChangedEventArgs const& args)
    {
        WriteLogF(L"CoreWindow visibility changed visible=%d", args.Visible() ? 1 : 0);
    }
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment(apartment_type::multi_threaded);
    CoreApplication::Run(make<App>());
    return 0;
}
