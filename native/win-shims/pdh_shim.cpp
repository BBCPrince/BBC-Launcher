// Xbox/UWP shim for Pdh.dll.
// Provides symbols oshi/JNA look up so the DLL loads and Minecraft's hardware probe degrades gracefully.

#include <windows.h>

#define MGB_PDH_EXPORT extern "C" __declspec(dllexport)

#ifndef PDH_NO_DATA
#define PDH_NO_DATA               ((LONG)0x800007D5L)
#endif
#ifndef PDH_INVALID_HANDLE
#define PDH_INVALID_HANDLE        ((LONG)0xC0000BBCL)
#endif
#ifndef PDH_CSTATUS_NO_OBJECT
#define PDH_CSTATUS_NO_OBJECT     ((LONG)0xC0000BB8L)
#endif
#ifndef PDH_MORE_DATA
#define PDH_MORE_DATA             ((LONG)0x800007D2L)
#endif

typedef LONG PDH_STATUS;
typedef HANDLE PDH_HQUERY;
typedef HANDLE PDH_HCOUNTER;

MGB_PDH_EXPORT PDH_STATUS WINAPI PdhOpenQueryW(LPCWSTR, DWORD_PTR, PDH_HQUERY* phQuery)
{
    if (phQuery != nullptr)
    {
        *phQuery = nullptr;
    }
    return PDH_NO_DATA;
}

MGB_PDH_EXPORT PDH_STATUS WINAPI PdhOpenQueryA(LPCSTR, DWORD_PTR, PDH_HQUERY* phQuery)
{
    if (phQuery != nullptr)
    {
        *phQuery = nullptr;
    }
    return PDH_NO_DATA;
}

MGB_PDH_EXPORT PDH_STATUS WINAPI PdhAddCounterW(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER* phCounter)
{
    if (phCounter != nullptr)
    {
        *phCounter = nullptr;
    }
    return PDH_NO_DATA;
}

MGB_PDH_EXPORT PDH_STATUS WINAPI PdhAddEnglishCounterW(
    PDH_HQUERY,
    LPCWSTR,
    DWORD_PTR,
    PDH_HCOUNTER* phCounter)
{
    if (phCounter != nullptr)
    {
        *phCounter = nullptr;
    }
    return PDH_NO_DATA;
}

MGB_PDH_EXPORT PDH_STATUS WINAPI PdhCollectQueryData(PDH_HQUERY)
{
    return PDH_NO_DATA;
}

MGB_PDH_EXPORT PDH_STATUS WINAPI PdhCollectQueryDataEx(
    PDH_HQUERY,
    DWORD,
    HANDLE)
{
    return PDH_NO_DATA;
}

MGB_PDH_EXPORT PDH_STATUS WINAPI PdhGetFormattedCounterValue(
    PDH_HCOUNTER,
    DWORD,
    DWORD* lpdwType,
    LPVOID lpValue)
{
    if (lpdwType != nullptr)
    {
        *lpdwType = 0;
    }
    if (lpValue != nullptr)
    {
        ZeroMemory(lpValue, sizeof(LARGE_INTEGER));
    }
    return PDH_NO_DATA;
}

MGB_PDH_EXPORT PDH_STATUS WINAPI PdhGetFormattedCounterArrayW(
    PDH_HCOUNTER,
    DWORD,
    LPDWORD lpdwBufferSize,
    LPDWORD lpdwItemCount,
    LPVOID)
{
    if (lpdwBufferSize != nullptr)
    {
        *lpdwBufferSize = 0;
    }
    if (lpdwItemCount != nullptr)
    {
        *lpdwItemCount = 0;
    }
    return PDH_NO_DATA;
}

MGB_PDH_EXPORT PDH_STATUS WINAPI PdhCloseQuery(PDH_HQUERY)
{
    return 0;
}

MGB_PDH_EXPORT PDH_STATUS WINAPI PdhMakeCounterPathW(LPVOID, LPWSTR szFullPathBuffer, LPDWORD pcchBufferSize, DWORD)
{
    if (szFullPathBuffer != nullptr && pcchBufferSize != nullptr && *pcchBufferSize > 0)
    {
        szFullPathBuffer[0] = L'\0';
    }
    return PDH_NO_DATA;
}

MGB_PDH_EXPORT PDH_STATUS WINAPI PdhLookupPerfNameByIndexW(
    LPCWSTR,
    DWORD,
    LPWSTR szNameBuffer,
    LPDWORD pcchNameBufferSize)
{
    if (szNameBuffer != nullptr && pcchNameBufferSize != nullptr && *pcchNameBufferSize > 0)
    {
        szNameBuffer[0] = L'\0';
    }
    return PDH_NO_DATA;
}

MGB_PDH_EXPORT PDH_STATUS WINAPI PdhLookupPerfIndexByNameW(LPCWSTR, LPCWSTR, LPDWORD pdwIndex)
{
    if (pdwIndex != nullptr)
    {
        *pdwIndex = 0;
    }
    return PDH_NO_DATA;
}
