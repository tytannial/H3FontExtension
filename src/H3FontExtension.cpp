#include "H3FontExtensionInternal.h"

#include "Log.h"
#include "TextLayoutCore.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <exception>
#include <unordered_map>
#include <utility>

namespace H3FontExtension
{
    // ============================================================
    // 全局状态（仅本翻译单元可见）
    // ============================================================

    static Patcher* gPatcher = nullptr;
    static PatcherInstance* gPluginInstance = nullptr;

    /** @brief 字体名 → 扩展字库映射表（key 为小写字体名，如 "medfont.fnt"） */
    static std::unordered_map<std::string, ExtFont> gExtFontTable;

    // ============================================================
    // Section 6.5: ERA 共存适配（模块检测与 Hook 覆盖检测）
    // ============================================================
    //
    // 旧版 ERA（3.9.x）的 Rainbow 使用独立的 ApiJack/PatchForge Hook 引擎，
    // 不注册到 patcher_x86 全局树，无法通过 patcher API 互检；且其文本 Hook
    // 安装时机（OnAfterWoG）晚于本插件（BinkW32 代理，进程启动早期），会直接
    // 覆盖同址的 patcher_x86 跳转。
    //
    // 兼容策略：
    //   1. 与 ERA Rainbow 重叠的 6 个测量 Hook 延迟到首次文本绘制时安装，
    //      使其位于 ERA Hook 链外层（后安装者在链外层），测量结果不再被 ERA 接管；
    //   2. 其余 Hook 正常在 DllMain 安装，并按首字节快照检测意外覆盖。

    /** @brief 文本/测量 Hook 地址（与 ERA Rainbow 文本 Hook 重叠的地址集合） */
    constexpr uintptr_t kTextHookAddrs[] = {
        0x4B51F0, // 文本绘制入口（ERA 不 hook，立即安装）
        0x4B5500, // 单字符宽度（ERA 不 hook，立即安装）
        0x4B5530, // 字符串总宽度（ERA 不 hook，立即安装）
        0x4B5580, // 文本行数（ERA 会覆盖，延迟安装）
        0x4B5680, // 首行宽度（ERA 会覆盖，延迟安装）
        0x4B56F0, // 最长行宽度（ERA 会覆盖，延迟安装）
        0x4B5770, // 最长单词宽度（ERA 会覆盖，延迟安装）
        0x4B57E0, // 换行后最大宽度（ERA 会覆盖，延迟安装）
        0x4B58F0, // 文本拆分（ERA 会覆盖，延迟安装）
    };
    constexpr std::size_t kNumTextHooks = sizeof(kTextHookAddrs) / sizeof(kTextHookAddrs[0]);
    constexpr std::size_t kImmediateHookCount = 3; // 前 3 个在 init 中立即安装

    static uint8_t gHookSnapshot[kNumTextHooks] = {};
    static bool gSnapshotCaptured[kNumTextHooks] = {};
    static bool gConflictReported[kNumTextHooks] = {};
    static bool gEraPresenceLogged = false;
    static bool gMeasureHooksInstalled = false;

    // ------------------------------------------------------------
    // wogcn 对话框宽度固定补丁的检测与恢复（动态弹出框适配）
    // ------------------------------------------------------------
    //
    // 原版 CalculateNormalDialogSize 在 0x4F6569/0x4F6599 调用
    // font::longest_word_length / font::LongestLineWidth 动态计算宽度；
    // 旧版 wogcn（Chinese_Rainbow_Plugin/main.pas）把这两处 8 字节改写为
    // "mov eax, 0x180 + nop×3"（固定 384px），导致弹出框大小钉死。
    // 本插件 DllMain 早于 wogcn（由 ERA 运行期 LoadLibrary）执行，
    // 可快照原始指令；运行期发现不符即恢复，宽度重归测量 Hook 动态计算。

    /** @brief wogcn 固定对话框宽度的代码位置 */
    constexpr uintptr_t kDialogWidthAddrs[] = { 0x4F6569, 0x4F6599 };
    constexpr std::size_t kNumDialogWidthPatches = sizeof(kDialogWidthAddrs) / sizeof(kDialogWidthAddrs[0]);
    constexpr std::size_t kDialogWidthPatchLen = 8; // wogcn 写入 5 字节 mov + 3 字节 nop

    static uint8_t gDialogWidthSnapshot[kNumDialogWidthPatches][kDialogWidthPatchLen] = {};
    static bool gDialogWidthSnapshotValid = false;

    /** @brief 读取代码地址首字节 */
    static inline uint8_t readCodeByte(uintptr_t addr)
    {
        return *reinterpret_cast<volatile const uint8_t*>(addr);
    }

    /** @brief 判断地址是否位于指定模块的映射范围内 */
    static bool isAddrInModule(HMODULE module, uintptr_t addr)
    {
        if (!module)
            return false;

        const auto* base = reinterpret_cast<const uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return false;

        const uintptr_t begin = reinterpret_cast<uintptr_t>(base);
        return addr >= begin && addr < begin + nt->OptionalHeader.SizeOfImage;
    }

    /** @brief 获取 ERA 主模块句柄（旧部署为 era_.dll，兼容 era.dll 命名） */
    static HMODULE findEraModule()
    {
        HMODULE era = ::GetModuleHandleA("era_.dll");
        return era ? era : ::GetModuleHandleA("era.dll");
    }

    /** @brief 首次检测到 ERA 模块时记录日志（含模块路径） */
    static void logEraPresenceOnce()
    {
        if (gEraPresenceLogged)
            return;

        const HMODULE era = findEraModule();
        if (!era)
            return;

        gEraPresenceLogged = true;

        char path[MAX_PATH] = {};
        ::GetModuleFileNameA(era, path, MAX_PATH);
        logInfo("检测到 ERA 模块：%s", path);
        logInfo("ERA 共存模式：将检测文本 Hook 覆盖情况（旧版 ERA Rainbow 与 patcher_x86 引擎不互通）");
    }

    /**
     * @brief 检测文本 Hook 是否被外部覆盖（每个地址仅报告一次）
     *
     * 在 DDraw 初始化与文本绘制早期调用。若发现首字节与安装时快照不一致，
     * 解析跳转目标并判定是否为 ERA 模块，输出错误日志。
     */
    void detectHookOverwrites()
    {
        for (std::size_t i = 0; i < kNumTextHooks; ++i)
        {
            if (!gSnapshotCaptured[i] || gConflictReported[i])
                continue;

            const uintptr_t addr = kTextHookAddrs[i];
            const uint8_t now = readCodeByte(addr);
            if (now == gHookSnapshot[i])
                continue;

            gConflictReported[i] = true;

            uintptr_t target = 0;
            if (now == 0xE9)
            {
                int32_t rel = 0;
                std::memcpy(&rel, reinterpret_cast<const void*>(addr + 1), sizeof(rel));
                target = addr + 5 + static_cast<uintptr_t>(static_cast<intptr_t>(rel));
            }

            const HMODULE era = findEraModule();
            if (target != 0 && era != nullptr && isAddrInModule(era, target))
            {
                logError("文本 Hook 0x%08X 已被旧版 ERA（Rainbow）接管（跳转目标 0x%08X 位于 ERA 模块），"
                    "该地址的字体测量/绘制将由 ERA 逻辑优先处理",
                    static_cast<unsigned>(addr), static_cast<unsigned>(target));
            }
            else
            {
                logError("文本 Hook 0x%08X 被外部程序覆盖：首字节 0x%02X -> 0x%02X%s",
                    static_cast<unsigned>(addr), gHookSnapshot[i], now,
                    target != 0 ? "（跳转指令）" : "");
            }
        }
    }

    /**
     * @brief 延迟安装测量 Hook（详见文件头 Section 6.5 注释）
     *
     * 在首次文本绘制时调用一次：此时 ERA 的 OnAfterWoG Hook 已安装完毕，
     * 本插件补装的 Hook 位于其链外层，测量结果由本插件优先提供。
     */
    void installMeasureHooks()
    {
        if (gMeasureHooksInstalled)
            return;
        gMeasureHooksInstalled = true;

        gPluginInstance->WriteHiHook(0x4B5580, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetLineCount);
        gPluginInstance->WriteHiHook(0x4B5680, SPLICE_, EXTENDED_, THISCALL_, H3Font_LineWidth);
        gPluginInstance->WriteHiHook(0x4B56F0, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetLineWidth);
        gPluginInstance->WriteHiHook(0x4B5770, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetWordWidth);
        gPluginInstance->WriteHiHook(0x4B57E0, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetLineWrapWidth);
        gPluginInstance->WriteHiHook(0x4B58F0, SPLICE_, EXTENDED_, THISCALL_, H3Font_SplitTextIntoLines);

        // 补采快照：供后续覆盖检测（正常情况下此后不应再被覆盖）
        for (std::size_t i = kImmediateHookCount; i < kNumTextHooks; ++i)
        {
            gHookSnapshot[i] = readCodeByte(kTextHookAddrs[i]);
            gSnapshotCaptured[i] = true;
        }

        logInfo("测量 Hook 已延迟安装（首绘时机，位于 ERA Hook 链外层）");
    }

    /**
     * @brief 恢复被 wogcn 固定的对话框宽度计算（动态弹出框适配）
     *
     * 原版 CalculateNormalDialogSize 在 0x4F6569/0x4F6599 调用
     * font::longest_word_length / font::LongestLineWidth 计算动态宽度；
     * 旧版 wogcn 把这两处改写为 mov eax, 0x180（固定 384px）。
     * 检测到与启动快照不符时恢复原始指令，使宽度重新由本插件的
     * 测量 Hook（0x4B5770/0x4B56F0）动态提供。幂等：无改动时零开销。
     */
    void restoreDynamicDialogWidth()
    {
        if (!gDialogWidthSnapshotValid)
            return;

        for (std::size_t i = 0; i < kNumDialogWidthPatches; ++i)
        {
            const uintptr_t addr = kDialogWidthAddrs[i];
            auto* code = reinterpret_cast<uint8_t*>(addr);
            if (std::memcmp(code, gDialogWidthSnapshot[i], kDialogWidthPatchLen) == 0)
                continue;

            DWORD oldProtect = 0;
            if (!::VirtualProtect(code, kDialogWidthPatchLen, PAGE_EXECUTE_READWRITE, &oldProtect))
                continue;

            std::memcpy(code, gDialogWidthSnapshot[i], kDialogWidthPatchLen);
            ::VirtualProtect(code, kDialogWidthPatchLen, oldProtect, &oldProtect);
            ::FlushInstructionCache(::GetCurrentProcess(), code, kDialogWidthPatchLen);

            logInfo("检测到 wogcn 固定对话框宽度补丁：已恢复 0x%08X 的动态宽度计算",
                static_cast<unsigned>(addr));
        }
    }

    // ============================================================
    // Section 7: 生命周期 Hook
    // ============================================================

    /**
     * @brief Hook: 字体加载后注入扩展字库
     *
     * 在游戏加载字体后，将对应的 ExtFont 挂载到 H3FontExt 上，
     * 并根据扩展字库高度调整字体行高。
     *
     * 字体名匹配规则：小写字体名精确匹配 → 回退到 "medfont.fnt"。
     */
    static font* __stdcall H3Font_Load_Hook(HiHook* h, char* name)
    {
        // 复制到局部缓冲区再小写化，避免原地修改游戏内存中的字体名
        // （若指针指向只读内存将导致访问违例）
        char fntName[64];
        strncpy_s(fntName, name, sizeof(fntName) - 1);
        _strlwr(fntName);

        auto pFont = FASTCALL_1(H3FontExt*, h->GetDefaultFunc(), fntName);
        if (!pFont)
            return pFont;

        // 精确匹配 → 回退默认字体；不使用 operator[] 以免为未知字体插入空条目
        auto it = gExtFontTable.find(fntName);
        if (it == gExtFontTable.end())
            it = gExtFontTable.find("medfont.fnt");
        if (it == gExtFontTable.end())
            return pFont;

        pFont->ext_data = &it->second;
        pFont->ori_height = pFont->fr.height;
        // 行高 = max(原始高度, 有效字形高度 + 行间距)
        pFont->fr.height = static_cast<uint8_t>(std::max(static_cast<int>(pFont->fr.height),
            pFont->ext_data->EffectiveHeight() + pFont->ext_data->line_spacing));
        return pFont;
    }

    /**
     * @brief Hook: DirectDraw 初始化后配置渲染后端
     *
     * 根据游戏色深模式（16/32 位）绑定对应的 gGetColor 实现。
     * 同时根据屏幕分辨率计算文本框宽度限制，并清空布局缓存
     * （分辨率/色深变化后旧缓存不再有意义）。
     */
    static void __stdcall Main_DirectDrawInit_Hook(HiHook* h)
    {
        FASTCALL_0(void, h->GetDefaultFunc());

        // 根据游戏的图像模式初始化图像渲染函数指针
        updateBitMode();

        // ERA 共存适配：DDraw 初始化时机晚于 ERA 主模块加载，检测文本 Hook 覆盖情况
        logEraPresenceOnce();
        detectHookOverwrites();

        const auto maxWidth = getScreenWidth(gPatcher) - 64 * 2;
        if (gBoxWidthMax < 0)
            gBoxWidthMax = maxWidth;
        else
            gBoxWidthMax = std::clamp(gBoxWidthMax, 256, maxWidth);

        if (gBoxWidthMin < 0)
            gBoxWidthMin = maxWidth;
        else
            gBoxWidthMin = std::clamp(gBoxWidthMin, 256, gBoxWidthMax);
    }

    // ============================================================
    // Section 8: 插件入口
    // ============================================================

    /** @brief 游戏版本名称（按 PE 入口点识别，仅用于日志） */
    static const char* describeGameVersion(uint32_t entryPoint)
    {
        switch (entryPoint)
        {
        case 0x19A0AE: return "RoE 1.0";
        case 0x21A2B4: return "SoD 3.1（中文版）";
        case 0x21A884: return "SoD Complete";
        case 0x21A944: return "SoD（旧版）";
        case 0x21AB84: return "SoD Polish Gold";
        case 0x239C00: return "HotA";
        case 0x301000: return "ERA / WoG（h3era.exe / h3era HD.exe）";
        default:       return "未知版本";
        }
    }

    /**
     * @brief 插件初始化入口
     *
     * 执行流程：
     *   1. 获取 Patcher 实例
     *   2. 从插件 DLL 同目录加载 H3CN.toml 配置（字体映射、颜色表、折行规则、宽度限制）
     *   3. 注册所有 Hook（字体加载、文本渲染、布局计算）
     *
     * @param hModule 插件自身模块句柄（DllMain 传入，用于定位配置文件）
     * @param gameEntryPoint 游戏可执行文件 PE 入口点（版本标识，仅用于日志）
     */
    bool init(HMODULE hModule, uint32_t gameEntryPoint)
    {
        // ---------- 初始化日志（插件 DLL 同目录） ----------
        initLog(hModule);
        logInfo("游戏入口点：0x%08X（%s）", gameEntryPoint, describeGameVersion(gameEntryPoint));

        // ERA 环境下记录共存提示（模块可能晚于本插件加载，DDraw 初始化时会再次检测）
        logEraPresenceOnce();

#ifndef NDEBUG
        logInfo("插件注入成功（调试构建）");
#endif

        // ---------- 初始化 GBK 查表 ----------
        ExtFont::InitGbkTables();

        // ---------- 获取 Patcher 实例 ----------
        gPatcher = GetPatcher();
        gPluginInstance = gPatcher->GetInstance("HD.Plugin.H3FontExtension");
        if (!gPluginInstance)
        {
            gPluginInstance = gPatcher->CreateInstance("HD.Plugin.H3FontExtension");
        }
        else
        {
            gPluginInstance->UndoAll();
        }

        // ---------- 加载配置文件 ----------
        // 配置文件固定读取插件 DLL 同目录，避免游戏快捷方式工作目录不一致导致读取失败；
        // toml++ 的宽路径重载在 Windows 上以宽字符 API 打开文件，中文路径安全
        wchar_t configPath[MAX_PATH] = {};
        const DWORD pathLen = ::GetModuleFileNameW(hModule, configPath, MAX_PATH);
        if (pathLen > 0 && pathLen < MAX_PATH)
        {
            wchar_t* fileName = ::wcsrchr(configPath, L'\\');
            if (fileName)
            {
                *(fileName + 1) = L'\0';
                ::wcscat_s(configPath, L"H3CN.toml");
            }
        }
        const wchar_t* configFile = (configPath[0] != L'\0') ? configPath : L"H3CN.toml";

        try
        {
            auto config = toml::parse_file(configFile);

            // 字体映射：[Fonts] 节，每个条目定义游戏字体名与扩展字库的对应关系
            if (const auto fontsNode = config["Fonts"]; fontsNode.is_array())
            {
                for (const auto& item : *fontsNode.as_array())
                {
                    const auto* fontCfg = item.as_table();
                    if (!fontCfg)
                        continue;

                    std::string name = (*fontCfg)["Name"].value_or(std::string{});
                    const std::string extFontName = (*fontCfg)["ExtFont"].value_or(std::string{});

                    // 字体名小写化（匹配规则）；跳过无效条目
                    for (char& c : name)
                        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                    if (name.empty())
                        continue;

                    // 参数校验与钳制：字形边长上限 64 像素（防异常配置撑爆字形池），边距非负
                    constexpr int MaxGlyphSize = 64;
                    const int height = std::clamp((*fontCfg)["Height"].value_or(0), 0, MaxGlyphSize);
                    const int width = std::clamp((*fontCfg)["Width"].value_or(0), 0, MaxGlyphSize);
                    if (height <= 0 || width <= 0)
                    {
                        logError("字体 %s 的 Height/Width 无效（%d×%d），该条目已跳过",
                            name.c_str(), height, width);
                        continue;
                    }

                    ExtFont extFont(
                        extFontName.c_str(),
                        height,
                        width,
                        (*fontCfg)["Bold"].value_or(false),
                        (*fontCfg)["AntiAlias"].value_or(true),
                        std::max((*fontCfg)["MarginLeft"].value_or(1), 0),
                        std::max((*fontCfg)["MarginRight"].value_or(0), 0),
                        std::max((*fontCfg)["MarginBottom"].value_or(0), 0),
                        std::max((*fontCfg)["LineSpacing"].value_or(0), 0),
                        (*fontCfg)["DrawShadow"].value_or(true),
                        (*fontCfg)["OriginalAscii"].value_or(true));
                    if (!extFont.IsLoaded())
                    {
                        logError("字体 %s 扩展字库加载失败（系统字体 %s），该条目已跳过",
                            name.c_str(), extFontName.c_str());
                        continue;
                    }
                    gExtFontTable.insert_or_assign(name, std::move(extFont));
                }
            }

            // 文本颜色：[General].TextColor 开关 + [TextColor] 命名颜色表
            gTextColorEnabled = config["General"]["TextColor"].value_or(true);
            gTextColorMap.clear();
            if (gTextColorEnabled)
            {
                if (const auto colorNode = config["TextColor"]; colorNode.is_table())
                {
                    for (const auto& [key, value] : *colorNode.as_table())
                    {
                        // 预转换两种色深格式，渲染期直接取用
                        if (const auto c = value.value<int32_t>())
                        {
                            const DWORD rgb = static_cast<DWORD>(*c) & 0xFFFFFF;
                            // 键统一小写：原版/ERA 颜色码大小写不敏感（{~RED} 与 {~Red} 等价）
                            std::string colorKey(key);
                            for (char& ch : colorKey)
                                ch = asciiToLower(ch);
                            gTextColorMap.emplace(std::move(colorKey), NamedColor{ rgb, rgb888ToRgb565(rgb) });
                        }
                    }
                }
            }

            // 消息框宽度限制：[MessageBox]
            gBoxWidthMin = config["MessageBox"]["BoxWidthMin"].value_or(0);
            gBoxWidthMax = config["MessageBox"]["BoxWidthMax"].value_or(-1);
        }
        catch (const std::exception& e)
        {
            logError("H3CN.toml 配置加载失败：%s", e.what());
        }

        logInfo("配置摘要：%u 个字体映射、%u 个命名颜色；文字颜色 %s；消息框宽度限制 [%d, %d]",
            static_cast<unsigned>(gExtFontTable.size()),
            static_cast<unsigned>(gTextColorMap.size()),
            gTextColorEnabled ? "开启" : "关闭",
            gBoxWidthMin, gBoxWidthMax);

        // 字体表可能已重建：清空布局缓存避免引用过期字库
        clearLayoutCache();

        // ---------- 注册 Hook ----------

        // DDraw 初始化时检测色深模式
        gPluginInstance->WriteHiHook(0x601AB0, SPLICE_, EXTENDED_, FASTCALL_, Main_DirectDrawInit_Hook);

        // 扩展字体内存分配（font → H3FontExt，+8 字节）
        gPluginInstance->WriteDword(0x55B9CE + 1, sizeof(H3FontExt));

        // 字体加载后挂载扩展字库
        gPluginInstance->WriteHiHook(0x55BAE0, SPLICE_, EXTENDED_, THISCALL_, H3Font_Load_Hook);

        // 文本渲染与布局 Hook（前 3 个与 ERA 不冲突，立即安装）
        gPluginInstance->WriteHiHook(0x4B51F0, SPLICE_, EXTENDED_, THISCALL_, H3Font_DrawText);           // 文本绘制
        gPluginInstance->WriteHiHook(0x4B5500, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetCharacterWidth);  // 单字符宽度（GBK 状态机）
        gPluginInstance->WriteHiHook(0x4B5530, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetStringWidth);     // 字符串总宽度

        // 测量 Hook（0x4B5580/5680/56F0/5770/57E0/58F0）由 installMeasureHooks()
        // 在首次文本绘制时安装：ERA Rainbow 在 OnAfterWoG 用独立引擎覆盖这些地址，
        // 延迟安装可确保本插件的 Hook 位于其链外层（详见 Section 6.5 注释）。

        // 采集已安装 Hook 首字节快照：供运行期覆盖检测（ERA 共存适配）
        for (std::size_t i = 0; i < kImmediateHookCount; ++i)
        {
            gHookSnapshot[i] = readCodeByte(kTextHookAddrs[i]);
            gSnapshotCaptured[i] = true;
        }

        // 快照对话框宽度计算指令（wogcn 会固定为 0x180；wogcn 由 ERA 运行期
        // LoadLibrary，晚于本插件 DllMain，此处可确保快照为原始指令）
        for (std::size_t i = 0; i < kNumDialogWidthPatches; ++i)
        {
            std::memcpy(gDialogWidthSnapshot[i],
                reinterpret_cast<const void*>(kDialogWidthAddrs[i]),
                kDialogWidthPatchLen);
        }
        gDialogWidthSnapshotValid = true;

        logInfo("初始化完成：基础 Hook 已注册（测量 Hook 将于首次绘制时安装）");
        return true;
    }

} // namespace H3FontExtension