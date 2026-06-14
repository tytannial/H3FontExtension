#pragma once

#include <unordered_map>

#include <toml.hpp>

#define _H3API_PATCHER_X86_
#include <H3API.hpp>

static Patcher* _P;
static PatcherInstance* _PI;

namespace H3FontExtension
{
    /* 双字节码表定义
     * GBK范围
     * 0x8140-A0FE，收录 CJK 汉字 6080 个
     * 0xA1A1-A9FE，除 GB 2312 的符号外，还增补了其它符号
     * 0xAA40-A0FE，收录 CJK 汉字和增补的汉字 8160 个
     * 0xA840-A9A0，扩除非汉字区
     * 0xB0A1-F7FE，收录 GB 2312 汉字 6763 个，按原序排列
     * BIG5范围
     * 0xA140-0xA3BF：标点符号、希腊字母及特殊符号
     * 0xA440-0xC67E：常用汉字，先按笔划再按部首排序
     */

    // ============================================================
    // 常量定义
    // ============================================================

    /** @brief 双字节编码区码起始值（GBK/DBCS 首字节判定阈值） */
    constexpr uint8_t DBCS_SECTION = 0x81;

    /** @brief 双字节编码位码起始值（GBK/DBCS 第二字节判定阈值） */
    constexpr uint8_t DBCS_POSITION = 0x40;

    /** @brief 字符阴影颜色索引 */
    constexpr uint16_t ShadowColor = 0;

    // ============================================================
    // 枚举
    // ============================================================

    /**
     * @brief 可见字符类型，用于文本扫描遍历
     */
    enum class TokenType : uint8_t {
        Newline,    ///< 换行符 '\n'
        Space,      ///< 空格 ' '
        SingleByte, ///< ASCII 单字节可见字符
        DoubleByte  ///< 双字节字符（GBK 汉字等）
    };

    // ============================================================
    // 数据结构
    // ============================================================

    /**
     * @brief 原始文本行（含颜色码，拆分后未预处理）
     */
    struct TextLineStruct
    {
        std::string Text;   ///< 行文本内容（含颜色码 {~...} / { / }）
        int lineWidth;      ///< 该行可见字符的总像素宽度
    };

    /**
     * @brief 扩展字库（统一使用 GDI/ClearType 实时渲染）
     *
     * 所有字符（ASCII 和 GBK 汉字）均通过系统 GDI 运行时渲染字形，
     * 缓存抗锯齿 RGBA 数据和单个字符的像素宽度。
     */
    struct ExtFont
    {
    public:
        // ---- 字体度量 ----
        INT8   Height          = 0;      ///< 字形渲染高度（像素）
        int    Width           = 0;      ///< 字形渲染宽度（像素，不含边距）
        bool   Bold            = false;  ///< 是否加粗（true = FW_BOLD）
        bool   AntiAlias       = true;   ///< 是否启用 ClearType 抗锯齿
        int    MarginLeft      = 0;      ///< 左侧边距（像素）
        int    MarginRight     = 0;      ///< 右侧边距（像素）
        int    MarginBottom    = 0;      ///< 底部边距（像素），扩大学形缓冲区高度以容纳字体下降部分（如微软雅黑）
        int    LineSpacing     = 0;      ///< 行间距（像素），增加行与行之间的额外空白
        int    GlyphWidth      = 0;      ///< DIB 缓冲区宽度 = MarginLeft + Width + MarginRight
        bool   DrawShadow      = true;   ///< 是否绘制阴影

        /** @brief 返回有效字形高度（含底部边距） */
        int EffectiveHeight() const { return Height + MarginBottom; }

        // ---- GDI 渲染资源 ----
        HDC     hdcGlyph     = nullptr;  ///< 字形渲染内存 DC
        HBITMAP hbmGlyph     = nullptr;  ///< 字形渲染 32-bit DIB
        void*   pGlyphBits   = nullptr;  ///< DIB 位图原始数据指针
        HFONT   hGlyphFont   = nullptr;  ///< GDI 字体句柄

        // ---- 字形缓存 ----
        /// 字形 RGBA 缓存：wchar_t → RGBA 像素数据（GlyphWidth × Height × 4 字节）
        mutable std::unordered_map<wchar_t, std::vector<uint8_t>> glyphCache;
        /// 字符宽度缓存：wchar_t → GDI 测量的像素宽度（advance width，含左右边距）
        mutable std::unordered_map<wchar_t, int> widthCache;

        ExtFont() = default;

        /**
         * @brief 构造并加载系统字体
         * @param lpFileName    系统字体名（如 "SimSun", "Microsoft YaHei"）
         * @param iHeight       字形渲染高度
         * @param iWidth        字形渲染宽度
         * @param bBold         是否加粗
         * @param bAntiAlias    是否启用 ClearType 抗锯齿
         * @param iMarginLeft   左边距
         * @param iMarginRight  右边距
         * @param iMarginBottom 底部边距（扩大学形缓冲区，用于容纳字体下降部分）
         * @param iLineSpacing  行间距（增加行与行之间的额外空白）
         * @param bDrawShadow   是否绘制阴影
         */
        ExtFont(LPCSTR lpFileName, int iHeight, int iWidth, bool bBold, bool bAntiAlias,
            int iMarginLeft, int iMarginRight, int iMarginBottom, int iLineSpacing, bool bDrawShadow);

        /** @brief 释放 GDI 资源和字形缓存 */
        ~ExtFont();

        // 禁止拷贝（GDI 句柄不可浅拷贝），允许移动
        ExtFont(const ExtFont&) = delete;
        ExtFont& operator=(const ExtFont&) = delete;
        ExtFont(ExtFont&& other) noexcept;
        ExtFont& operator=(ExtFont&& other) noexcept;

        /**
         * @brief 通过 GDI 加载系统字体
         * @param fontName     系统字体名（如 "SimSun", "Microsoft YaHei"）
         * @param iHeight      字形渲染高度
         * @param iWidth       字形渲染宽度
         * @param bBold        是否加粗
         * @param bAntiAlias   是否启用 ClearType
         * @param iMarginLeft  左边距
         * @param iMarginRight 右边距
         * @param iMarginBottom 底部边距
         * @param iLineSpacing 行间距
         * @param bDrawShadow  是否绘制阴影
         */
        bool __fastcall LoadGdiFont(const char* fontName, int iHeight, int iWidth, bool bBold, bool bAntiAlias,
            int iMarginLeft, int iMarginRight, int iMarginBottom, int iLineSpacing, bool bDrawShadow);

        /**
         * @brief GBK 区码/位码 → wchar_t 转换
         */
        static wchar_t GbkToWchar(uint8_t section, uint8_t position);

        /**
         * @brief 获取字形的抗锯齿 RGBA 数据（按需 GDI 渲染 + 缓存）
         * @return GlyphWidth×Height×4 字节 RGBA 数据指针，失败返回 nullptr
         */
        const uint8_t* GetGlyphRGBA(wchar_t ch) const;

        /**
         * @brief 获取单个字符的 GDI 渲染像素宽度（advance width，含左右边距）
         * @note 首次调用时通过 GetTextExtentPoint32W 测量并缓存
         */
        int GetCharWidth(wchar_t ch) const;
    };

    // ---- 色深自适应：从 surface 读取像素（用于 GDI 模式下 alpha 混合） ----
    extern DWORD(__fastcall* ReadPixel)(const PUINT8 rowBuffer, int col);

    /**
     * @brief 扩展后的 H3Font 结构体（比原始 H3Font 多 8 字节）
     *
     * 通过修改游戏内存分配大小实现，额外字段存储原始高度和扩展字库指针。
     */
    struct H3FontExt : h3::H3Font
    {
        int      oriHeight; ///< 原始字体高度（扩展前的 height 值）
        ExtFont* ExtData;   ///< 关联的扩展字库指针
    };

    /**
     * @brief 颜色变更点：记录纯文本中某个字节偏移处的颜色切换
     */
    struct ColorStop
    {
        size_t bytePos; ///< 在 cleanText 中的字节偏移
        DWORD  color;   ///< 切换后的渲染颜色（已转换为当前色深格式）
    };

    /**
     * @brief 预处理后的文本行
     *
     * 颜色码已从 text 中剥离，颜色信息独立存储在 stops 中。
     * 渲染时逐字节遍历 text，遇到 stop 点切换颜色。
     */
    struct CleanLine
    {
        std::string text;              ///< 纯可见字符（无颜色码、无控制字符）
        int         lineWidth;         ///< 该行可见字符的总像素宽度
        std::vector<ColorStop> stops;  ///< 按 bytePos 升序排列的颜色变更点
    };

    // ============================================================
    // 全局状态
    // ============================================================

    /** @brief 是否启用文本颜色功能 */
    static bool IsTextColorEnable = true;

    /** @brief 命名颜色映射表（从配置文件加载） */
    static toml::table TextColorMap;

    /** @brief 文本框最小宽度限制 */
    static int BoxWidthMin = 0;

    /** @brief 文本框最大宽度限制 */
    static int BoxWidthMax = 0;

    /** @brief 字体名 → 扩展字库映射表（key 为小写字体名，如 "medfont.fnt"） */
    static std::unordered_map<std::string, ExtFont> g_ExtFontTable;

    // ============================================================
    // 内联工具函数
    // ============================================================

    /**
     * @brief 计算单个 ASCII 字符的渲染宽度
     * @param widthArr H3 字体间距表
     * @param code     字符编码
     * @return 像素宽度 = 左边距 + 字形跨度 + 右边距
     */
    inline static int GetH3CharWidth(const h3::H3Font::FontSpacing* widthArr, uint8_t code)
    {
        return widthArr[code].leftMargin + widthArr[code].span + widthArr[code].rightMargin;
    }

    /**
     * @brief 判断当前字节是否为合法的双字节字符首字节（GBK/DBCS）
     * @param code     当前字节
     * @param nextCode 下一字节
     * @return true 表示 code/nextCode 构成一个合法的双字节字符
     */
    inline static bool IsDBCSLeadByte(uint8_t code, uint8_t nextCode)
    {
        return code >= DBCS_SECTION && nextCode && nextCode != 0xFF && nextCode >= DBCS_POSITION;
    }

    /**
     * @brief 判断当前字节是否为单字节字符（ASCII 或 0xFF 填充）
     * @param code 当前字节
     * @return true 表示该字节为单字节字符
     */
    inline static bool IsSingleByte(uint8_t code)
    {
        return code < DBCS_SECTION || code == 0xFF;
    }

    /**
     * @brief 获取一个字符的字节数和渲染宽度
     * @param code       当前字节
     * @param nextCode   下一字节（用于双字节合法性判定）
     * @param widthArr   ASCII 字体宽度表
     * @param glyphWidth 双字节字符固定宽度
     * @param outWidth   [out] 该字符的像素宽度
     * @return 字符所占字节数（1 或 2），无效编码返回 1 且宽度为 0
     */
    inline static int GetCharMetrics(uint8_t code, uint8_t nextCode,
        const h3::H3Font::FontSpacing* widthArr, int glyphWidth, int& outWidth)
    {
        if (IsSingleByte(code))
        {
            outWidth = GetH3CharWidth(widthArr, code);
            return 1;
        }
        if (IsDBCSLeadByte(code, nextCode))
        {
            outWidth = glyphWidth;
            return 2;
        }
        outWidth = 0;
        return 1; // 无效双字节首字节，当单字节跳过
    }

    /**
     * @brief 将 24 位 RGB888 颜色值转换为 16 位 RGB565 格式
     * @param color 32 位颜色值（仅低 24 位有效）
     * @return RGB565 格式的 16 位颜色值
     */
    constexpr inline static WORD RGB888toRGB565(DWORD color)
    {
        return ((((color >> 16) & 0xFF) >> 3) & 0x1F) << 11 | ((((color >> 8) & 0xFF) >> 2) & 0x3F) << 5 |
            (((color & 0xFF) >> 3) & 0x1F);
    }

    // ============================================================
    // 公共 API
    // ============================================================

    /**
     * @brief 插件初始化入口
     *
     * 加载配置文件，注册所有 Hook 函数。
     * @return 初始化是否成功
     */
    bool Init();

} // namespace H3FontExtension
