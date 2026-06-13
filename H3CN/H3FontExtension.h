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
     * @brief 扩展字库（GBK 双字节汉字位图字体）
     *
     * 从外部二进制文件加载，存储为灰度位图。
     * 每个汉字固定宽高，通过区码/位码索引。
     */
    struct ExtFont
    {
    public:
        PUINT8 FontFileBuffer = nullptr; ///< 字库位图数据缓冲区
        INT8   Height          = 0;      ///< 单个汉字位图高度（像素）
        int    Width           = 0;      ///< 单个汉字位图宽度（像素）
        int    MarginLeft      = 0;      ///< 汉字左侧边距（像素）
        int    MarginRight     = 0;      ///< 汉字右侧边距（像素）
        int    MarginBottom    = 0;      ///< 汉字底部边距（像素）
        int    GlyphWidth      = 0;      ///< 汉字总宽度 = MarginLeft + Width + MarginRight
        bool   DrawShadow      = true;   ///< 是否绘制汉字阴影
        //int LineHeightAdjust = 0;

        ExtFont() = default;

        /**
         * @brief 构造并加载扩展字库
         * @param lpFileName    字库文件路径
         * @param iHeight       单字高度
         * @param iWidth        单字宽度
         * @param iMarginLeft   左边距
         * @param iMarginRight  右边距
         * @param iMarginBottom 底部边距
         * @param bDrawShadow   是否绘制阴影
         */
        ExtFont(LPCSTR lpFileName, int iHeight, int iWidth, int iMarginLeft, int iMarginRight, int iMarginBottom,
            bool bDrawShadow/*, int lineHeightAdjust*/);

        /**
         * @brief 从二进制文件加载扩展字库数据
         * @return 始终返回 false（游戏引擎约定）
         */
        bool __fastcall LoadHzhFont(LPCSTR lpFileName, int iHeight, int iWidth, int iMarginLeft, int iMarginRight,
            int iMarginBottom, bool bDrawShadow/*, int lineHeightAdjust*/);

        /**
         * @brief 根据区码/位码获取汉字位图数据指针
         * @param section  区码（高字节）
         * @param position 位码（低字节）
         * @return 该汉字位图在 FontFileBuffer 中的起始地址，无效编码返回 nullptr
         */
        inline PUINT8 __fastcall GetExtGlyphDataPtr(UINT8 section, UINT8 position) const
        {
            if (section < DBCS_SECTION || position < DBCS_POSITION)
                return nullptr;
            return this->FontFileBuffer +
                this->Width * this->Height * ((section - DBCS_SECTION) * 0xBF + position - DBCS_POSITION);
        }
    };

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
