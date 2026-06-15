// Xbox/UWP shim for Ole32.dll.
// Provides the symbols oshi/JNA call so DLL load succeeds and HW probe degrades gracefully.
// Intentionally avoids including <objbase.h>/<combaseapi.h> so the SDK's dllimport
// declarations don't conflict with our dllexports.

#include <windows.h>

// Local forward declarations -- enough to satisfy linker without colliding with SDK headers.
typedef LONG MGB_HRESULT;
typedef void* MGB_PTR;
typedef const wchar_t* MGB_LPCOLESTR;
typedef wchar_t* MGB_LPOLESTR;

#ifndef S_OK
#define S_OK             ((MGB_HRESULT)0L)
#endif
#ifndef S_FALSE
#define S_FALSE          ((MGB_HRESULT)1L)
#endif
#ifndef E_FAIL
#define E_FAIL           ((MGB_HRESULT)0x80004005L)
#endif
#ifndef E_NOINTERFACE
#define E_NOINTERFACE    ((MGB_HRESULT)0x80004002L)
#endif
#ifndef E_INVALIDARG
#define E_INVALIDARG     ((MGB_HRESULT)0x80070057L)
#endif

#define MGB_OLE32_EXPORT extern "C" __declspec(dllexport)

static FARPROC GetCombaseProc(const char* name)
{
    static HMODULE combase = LoadLibraryW(L"combase.dll");
    return combase != nullptr ? GetProcAddress(combase, name) : nullptr;
}

MGB_OLE32_EXPORT MGB_HRESULT WINAPI CoInitializeEx(MGB_PTR, DWORD)
{
    return S_OK;
}

MGB_OLE32_EXPORT MGB_HRESULT WINAPI CoInitialize(MGB_PTR)
{
    return S_OK;
}

MGB_OLE32_EXPORT void WINAPI CoUninitialize()
{
}

MGB_OLE32_EXPORT MGB_HRESULT WINAPI CoInitializeSecurity(
    MGB_PTR,
    LONG,
    MGB_PTR,
    MGB_PTR,
    DWORD,
    DWORD,
    MGB_PTR,
    DWORD,
    MGB_PTR)
{
    return S_OK;
}

MGB_OLE32_EXPORT MGB_HRESULT WINAPI CoCreateInstance(
    MGB_PTR,
    MGB_PTR,
    DWORD,
    MGB_PTR,
    MGB_PTR* ppv)
{
    if (ppv != nullptr)
    {
        *ppv = nullptr;
    }
    return E_NOINTERFACE;
}

MGB_OLE32_EXPORT MGB_HRESULT WINAPI CoCreateFreeThreadedMarshaler(
    MGB_PTR punkOuter,
    MGB_PTR* ppunkMarshal)
{
    using CoCreateFreeThreadedMarshalerProc = MGB_HRESULT (WINAPI *)(MGB_PTR, MGB_PTR*);
    auto realProc = reinterpret_cast<CoCreateFreeThreadedMarshalerProc>(
        GetCombaseProc("CoCreateFreeThreadedMarshaler"));
    if (realProc != nullptr)
    {
        return realProc(punkOuter, ppunkMarshal);
    }

    if (ppunkMarshal != nullptr)
    {
        *ppunkMarshal = nullptr;
    }
    return E_NOINTERFACE;
}

MGB_OLE32_EXPORT MGB_HRESULT WINAPI CoGetContextToken(MGB_PTR* pToken)
{
    using CoGetContextTokenProc = MGB_HRESULT (WINAPI *)(MGB_PTR*);
    auto realProc = reinterpret_cast<CoGetContextTokenProc>(
        GetCombaseProc("CoGetContextToken"));
    if (realProc != nullptr)
    {
        return realProc(pToken);
    }

    if (pToken != nullptr)
    {
        *pToken = nullptr;
    }
    return E_NOINTERFACE;
}

MGB_OLE32_EXPORT MGB_HRESULT WINAPI CoSetProxyBlanket(
    MGB_PTR,
    DWORD,
    DWORD,
    MGB_LPOLESTR,
    DWORD,
    DWORD,
    MGB_PTR,
    DWORD)
{
    return S_OK;
}

MGB_OLE32_EXPORT MGB_PTR WINAPI CoTaskMemAlloc(SIZE_T cb)
{
    return HeapAlloc(GetProcessHeap(), 0, cb);
}

MGB_OLE32_EXPORT void WINAPI CoTaskMemFree(MGB_PTR pv)
{
    if (pv != nullptr)
    {
        HeapFree(GetProcessHeap(), 0, pv);
    }
}

MGB_OLE32_EXPORT MGB_HRESULT WINAPI CLSIDFromString(MGB_LPCOLESTR, MGB_PTR lpclsid)
{
    if (lpclsid != nullptr)
    {
        ZeroMemory(lpclsid, 16);
    }
    return E_INVALIDARG;
}

MGB_OLE32_EXPORT MGB_HRESULT WINAPI StringFromCLSID(MGB_PTR, MGB_LPOLESTR* lplpsz)
{
    if (lplpsz != nullptr)
    {
        *lplpsz = nullptr;
    }
    return E_INVALIDARG;
}

MGB_OLE32_EXPORT MGB_HRESULT WINAPI CoGetClassObject(
    MGB_PTR,
    DWORD,
    MGB_PTR,
    MGB_PTR,
    MGB_PTR* ppv)
{
    if (ppv != nullptr)
    {
        *ppv = nullptr;
    }
    return E_NOINTERFACE;
}
