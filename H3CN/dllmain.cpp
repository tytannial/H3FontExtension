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
		auto gameVersion = h3::H3Version();
		if (!gameVersion.hota() && !gameVersion.sod()) {
			return FALSE;
		}

		H3FontExtension::Init();
	}
	return TRUE;
}
