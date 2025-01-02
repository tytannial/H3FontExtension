#include "H3FontExtension.h"
#include "hook/binkw32hack.h"

static bool plugin_On = false;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	if (DLL_PROCESS_ATTACH == ul_reason_for_call)
	{
		if (plugin_On)
		{
			return TRUE;
		}
		plugin_On = true;

		LoadBinkw32FuncEntry();

		TCHAR pName[MAX_PATH];
		if (!GetModuleFileName(NULL, pName, MAX_PATH))
		{
			return FALSE;
		}

		auto pEnd = wcsrchr(pName, '\\');
		if (_wcsicmp(pEnd, L"\\h3hota HD.exe") != 0
			&& _wcsicmp(pEnd, L"\\h3hota_HD.exe") != 0
			&& _wcsicmp(pEnd, L"\\Heroes3 HD.exe") != 0
			&& _wcsicmp(pEnd, L"\\Heroes3_HD.exe") != 0)
		{
			return FALSE;
		}

		H3FontExtension::Init();
	}
	return TRUE;
}
