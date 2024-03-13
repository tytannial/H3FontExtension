#include "H3FontExtension.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    TCHAR pName[MAX_PATH];
    if (!GetModuleFileName(NULL, pName, MAX_PATH))
    {
        return FALSE;
    }
    auto pEnd = wcsrchr(pName, '\\');

    if (_wcsicmp(pEnd, L"\\h3hota HD.exe") != 0 && _wcsicmp(pEnd, L"\\Heroes3 HD.exe") != 0)
    {
        return FALSE;
    }

    static bool plugin_On = false;

    if (DLL_PROCESS_ATTACH == ul_reason_for_call)
    {
        if (!plugin_On)
        {
            plugin_On = true;
            H3FontExtension::Init();
        }
    }

    return TRUE;
}
