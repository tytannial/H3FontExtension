#include "H3FontExtension.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    TCHAR pName[MAX_PATH];
    if (!GetModuleFileName(NULL, pName, MAX_PATH))
    {
        return FALSE;
    }
    auto pEnd = wcsrchr(pName, '\\');

    if (_wcsicmp(pEnd, L"\\h3era.exe") != 0
        && _wcsicmp(pEnd, L"\\h3era HD.exe") != 0)
    {
        return FALSE;
    }

    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        return H3FontExtension::Init();
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
