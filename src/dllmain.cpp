#include "H3FontExtension.h"
#include "hook/binkw32hack.h"

static bool plugin_On = false;

/**
 * @brief 读取游戏可执行文件的 PE 入口点（RVA）识别版本
 *
 * 与 H3Version 的做法一致：纯内存读取，不依赖 HD Mod / patcher，
 * 可在 DllMain（loader lock 内）安全调用。
 */
static DWORD GetGameEntryPoint()
{
    const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(0x400000);
    const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const BYTE*>(dosHeader) + dosHeader->e_lfanew);
    return ntHeaders->OptionalHeader.AddressOfEntryPoint;
}

/** @brief 当前可执行文件是否为受支持的游戏版本 */
static bool IsSupportedGame()
{
    switch (GetGameEntryPoint())
    {
    case 0x19A0AE: // RoE 1.0
    case 0x21A2B4: // SoD 3.1（中文版）
    case 0x21A884: // SoD Complete
    case 0x21A944: // SoD（旧版）
    case 0x21AB84: // SoD Polish Gold
    case 0x239C00: // HotA
        return true;
    default:
        return false;
    }
}

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
    if (DLL_PROCESS_ATTACH == ul_reason_for_call)
    {
        if (plugin_On)
        {
            return TRUE;
        }
        plugin_On = true;

        // BinkW32 代理必须始终保持加载：版本不匹配时仅跳过插件初始化，
        // 绝不返回 FALSE 卸载 DLL（否则游戏将无法解析 binkw32 导入导致启动失败）
        LoadBinkw32FuncEntry();

        if (IsSupportedGame())
            H3FontExtension::Init();
    }
    return TRUE;
}
