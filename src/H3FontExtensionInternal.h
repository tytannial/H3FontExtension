#pragma once

#include "H3FontExtension.h"

namespace H3FontExtension
{
    // ============================================================
    // 跨翻译单元共享状态（定义位于各 .cpp）
    // ============================================================

    /** @brief 绑定当前色深的调色板取色函数（TextLayout.cpp 定义，DDraw 初始化后更新） */
    extern DWORD(__fastcall* gGetColor)(const TPalette16& palette, int colorIdx);

    /** @brief 当前色深模式：-1 = 尚未判定，0 = 16 位，1 = 32 位 */
    extern int gBitMode;

    /** @brief 是否启用文本颜色功能 */
    extern bool gTextColorEnabled;

    /** @brief 命名颜色条目（加载配置时预转换两种色深，渲染期零转换开销） */
    struct NamedColor
    {
        DWORD rgb888; ///< 32 位色深格式
        WORD  rgb565; ///< 16 位色深格式
    };

    /** @brief 命名颜色映射表（从配置文件加载；仅在布局缓存未命中的拆行/预处理阶段查询） */
    extern std::unordered_map<std::string, NamedColor> gTextColorMap;

    /** @brief 文本框最小宽度限制 */
    extern int gBoxWidthMin;

    /** @brief 文本框最大宽度限制 */
    extern int gBoxWidthMax;

    // ============================================================
    // TextLayout.cpp 导出
    // ============================================================

    /** @brief 清空布局缓存（分辨率/色深切换、字体表重建时调用） */
    void clearLayoutCache();

    /** @brief 检测并更新色深模式（16/32 位） */
    void updateBitMode();

    /**
     * @brief 检测文本 Hook 是否被外部覆盖（ERA 共存适配；定义于 H3FontExtension.cpp）
     *
     * 旧版 ERA Rainbow 与 patcher_x86 引擎不互通，会在运行期直接覆盖同址 Hook；
     * 此函数按安装快照比对并记录日志（每地址仅报告一次），供 DDraw 初始化与绘制路径调用。
     */
    void detectHookOverwrites();

    /**
     * @brief 安装测量类 Hook（定义于 H3FontExtension.cpp，首次文本绘制时调用一次）
     *
     * 旧版 ERA Rainbow 在 OnAfterWoG（本插件 DllMain 之后）用独立 Hook 引擎
     * 直接覆盖 0x4B5580/5680/56F0/5770/57E0/58F0。将这些 Hook 延迟到首次
     * 文本绘制时安装，可确保位于 ERA Hook 链的外层，测量结果不被 ERA 接管。
     */
    void installMeasureHooks();

    /**
     * @brief 恢复被 wogcn 固定的对话框宽度计算（定义于 H3FontExtension.cpp）
     *
     * 检测 0x4F6569/0x4F6599 是否被 wogcn 改写为固定宽度，若是则恢复原始指令，
     * 使弹出框大小重新由测量 Hook 动态计算。幂等且低开销，可在绘制路径调用。
     */
    void restoreDynamicDialogWidth();

    /**
     * @brief 获取文本的预处理布局（缓存命中则零解析，未命中则构建并填充缓存）
     */
    const std::vector<CleanLine>& getOrCreateLayout(H3FontExt* pFont, LPCSTR szText, int iBoxWidth,
        DWORD defaultColor, DWORD highlightColor, bool is32bit);

    // ============================================================
    // Hook 入口（由 H3FontExtension.cpp 的 init 注册）
    // ============================================================

    /** @brief Hook: 在指定矩形区域内绘制多行文本（TextRender.cpp） */
    void __stdcall H3Font_DrawText(HiHook* h, H3FontExt* pFont, char* szText, Bitmap16Bit* pPcx,
        int iX, int iY, int iBoxWidth, int iBoxHeight,
        uint32_t uColorIdx, uint32_t uAlignFlags, int iFontStyle);

    /** @brief Hook: 将文本拆分为多行（TextLayout.cpp） */
    void __stdcall H3Font_SplitTextIntoLines(HiHook* h, H3FontExt* pFont, char* szText,
        const int iBoxWidth, exe_vector<exe_string>& lines);
    /** @brief Hook: 计算文本中最长单词的宽度（TextLayout.cpp） */
    int __stdcall H3Font_GetWordWidth(HiHook* h, H3FontExt* pFont, char* szText);
    /** @brief Hook: 计算文本换行后的最大行宽（TextLayout.cpp） */
    int __stdcall H3Font_GetLineWrapWidth(HiHook* h, H3FontExt* pFont, char* szText, int iBoxWidth);
    /** @brief Hook: 计算文本在指定宽度下的总行数（TextLayout.cpp） */
    int __stdcall H3Font_GetLineCount(HiHook* h, H3FontExt* pFont, char* szText, int iBoxWidth);
    /** @brief Hook: 计算文本首行的像素宽度（TextLayout.cpp） */
    int __stdcall H3Font_LineWidth(HiHook* h, H3FontExt* pFont, char* szText);
    /** @brief Hook: 计算文本中最长行的像素宽度（TextLayout.cpp） */
    int __stdcall H3Font_GetLineWidth(HiHook* h, H3FontExt* pFont, char* szText);
    /** @brief Hook: 计算字符串的总像素宽度（TextLayout.cpp） */
    int __stdcall H3Font_GetStringWidth(HiHook* h, H3FontExt* pFont, char* szText);
    /** @brief Hook: 单字节字符宽度查询（GBK 感知状态机，TextLayout.cpp） */
    int __stdcall H3Font_GetCharacterWidth(HiHook* h, H3FontExt* pFont, uint8_t currChar);

} // namespace H3FontExtension
