#include "H3FontExtensionInternal.h"

#include "Log.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
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
        logInfo("游戏入口点：0x%08X", gameEntryPoint);

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
                            gTextColorMap.emplace(std::string(key), NamedColor{ rgb, rgb888ToRgb565(rgb) });
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

        // 文本渲染与布局 Hook
        gPluginInstance->WriteHiHook(0x4B51F0, SPLICE_, EXTENDED_, THISCALL_, H3Font_DrawText);           // 文本绘制
        gPluginInstance->WriteHiHook(0x4B5500, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetCharacterWidth);  // 单字符宽度（GBK 状态机）
        gPluginInstance->WriteHiHook(0x4B5530, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetStringWidth);     // 字符串总宽度
        gPluginInstance->WriteHiHook(0x4B5580, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetLineCount);       // 计算文本行数
        gPluginInstance->WriteHiHook(0x4B5680, SPLICE_, EXTENDED_, THISCALL_, H3Font_LineWidth);          // 首行文本像素宽度
        gPluginInstance->WriteHiHook(0x4B56F0, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetLineWidth);       // 最长文本行宽度
        gPluginInstance->WriteHiHook(0x4B5770, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetWordWidth);       // 最长单词宽度
        gPluginInstance->WriteHiHook(0x4B57E0, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetLineWrapWidth);   // 最长换行后宽度
        gPluginInstance->WriteHiHook(0x4B58F0, SPLICE_, EXTENDED_, THISCALL_, H3Font_SplitTextIntoLines); // 拆分文本行

        logInfo("初始化完成：已注册全部文本相关 Hook");
        return true;
    }

} // namespace H3FontExtension