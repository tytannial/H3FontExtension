#include "H3FontExtension.h"

#include <toml.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>

using namespace h3;
using namespace std;

namespace H3FontExtension
{

    // ============================================================
    // 全局状态（仅本翻译单元可见）
    // ============================================================

    static Patcher* _P = nullptr;
    static PatcherInstance* _PI = nullptr;

    /** @brief 是否启用文本颜色功能 */
    static bool IsTextColorEnable = true;

    /** @brief 命名颜色条目（加载配置时预转换两种色深，渲染期零转换开销） */
    struct NamedColor
    {
        DWORD rgb888; ///< 32 位色深格式
        WORD  rgb565; ///< 16 位色深格式
    };

    /** @brief 命名颜色映射表（从配置文件加载） */
    static std::unordered_map<std::string, NamedColor> TextColorMap;

    /** @brief 文本框最小宽度限制 */
    static int BoxWidthMin = 0;

    /** @brief 文本框最大宽度限制 */
    static int BoxWidthMax = 0;

    /** @brief 字体名 → 扩展字库映射表（key 为小写字体名，如 "medfont.fnt"） */
    static std::unordered_map<std::string, ExtFont> g_ExtFontTable;

    /** @brief 当前是否为 32 位色深模式（DDraw 初始化时更新） */
    static bool Is32BitMode = false;

    /**
     * @brief GBK 双字节 → wchar_t 全局查表缓存
     *
     * 索引为 (区码 << 8) | 位码，0 表示尚未转换。
     * 拆行、测量、绘制阶段反复查询同一字符时避免重复调用 MultiByteToWideChar。
     */
    static wchar_t g_GbkWideTable[0x10000];

    /** @brief 拆行复用缓冲（游戏渲染线程单线程，跨调用保留容量以避免每帧堆分配） */
    static std::vector<TextLineStruct> g_SplitScratch;

    // ============================================================
    // Section 1: ExtFont 实现
    // ============================================================

    /** @brief 构造函数：统一加载 GDI 系统字体 */
    ExtFont::ExtFont(LPCSTR lpFileName, int iHeight, int iWidth, bool bBold, bool bAntiAlias,
        int iMarginLeft, int iMarginRight, int iMarginBottom, int iLineSpacing, bool bDrawShadow)
    {
        LoadGdiFont(lpFileName, iHeight, iWidth, bBold, bAntiAlias,
            iMarginLeft, iMarginRight, iMarginBottom, iLineSpacing, bDrawShadow);
    }

    /** @brief 释放 GDI 资源（先恢复原对象再删除，避免句柄泄漏） */
    void ExtFont::ReleaseGdiResources()
    {
        if (hdcGlyph)
        {
            // 选入 DC 的对象必须先恢复原对象才能成功删除，否则 DeleteObject 静默失败导致 GDI 句柄泄漏
            if (hGlyphFont)
            {
                if (hOldFont)
                    SelectObject(hdcGlyph, hOldFont);
                DeleteObject(hGlyphFont);
                hGlyphFont = nullptr;
                hOldFont = nullptr;
            }
            if (hbmGlyph)
            {
                if (hOldBitmap)
                    SelectObject(hdcGlyph, hOldBitmap);
                DeleteObject(hbmGlyph);
                hbmGlyph = nullptr;
                hOldBitmap = nullptr;
                pGlyphBits = nullptr;
            }
            DeleteDC(hdcGlyph);
            hdcGlyph = nullptr;
        }
        else
        {
            // DC 不存在（创建失败或已释放）：直接删除可能残留的独立对象
            if (hGlyphFont)
            {
                DeleteObject(hGlyphFont);
                hGlyphFont = nullptr;
            }
            if (hbmGlyph)
            {
                DeleteObject(hbmGlyph);
                hbmGlyph = nullptr;
                pGlyphBits = nullptr;
            }
        }
    }

    /** @brief 释放 GDI 资源和字形缓存 */
    ExtFont::~ExtFont()
    {
        ReleaseGdiResources();
    }

    /** @brief 移动构造：转移 GDI 资源所有权 */
    ExtFont::ExtFont(ExtFont&& other) noexcept
        : Height(other.Height), Width(other.Width)
        , Bold(other.Bold), AntiAlias(other.AntiAlias)
        , MarginLeft(other.MarginLeft), MarginRight(other.MarginRight)
        , MarginBottom(other.MarginBottom), LineSpacing(other.LineSpacing)
        , GlyphWidth(other.GlyphWidth)
        , DrawShadow(other.DrawShadow)
        , hdcGlyph(other.hdcGlyph), hbmGlyph(other.hbmGlyph)
        , pGlyphBits(other.pGlyphBits), hGlyphFont(other.hGlyphFont)
        , hOldBitmap(other.hOldBitmap), hOldFont(other.hOldFont)
        , glyphCache(std::move(other.glyphCache))
        , widthCache(std::move(other.widthCache))
    {
        other.hdcGlyph = nullptr;
        other.hbmGlyph = nullptr;
        other.pGlyphBits = nullptr;
        other.hGlyphFont = nullptr;
        other.hOldBitmap = nullptr;
        other.hOldFont = nullptr;
    }

    /** @brief 移动赋值：转移 GDI 资源所有权 */
    ExtFont& ExtFont::operator=(ExtFont&& other) noexcept
    {
        if (this != &other)
        {
            ReleaseGdiResources();

            Height = other.Height; Width = other.Width;
            Bold = other.Bold; AntiAlias = other.AntiAlias;
            MarginLeft = other.MarginLeft; MarginRight = other.MarginRight;
            MarginBottom = other.MarginBottom; LineSpacing = other.LineSpacing;
            GlyphWidth = other.GlyphWidth;
            DrawShadow = other.DrawShadow;
            hdcGlyph = other.hdcGlyph; other.hdcGlyph = nullptr;
            hbmGlyph = other.hbmGlyph; other.hbmGlyph = nullptr;
            pGlyphBits = other.pGlyphBits; other.pGlyphBits = nullptr;
            hGlyphFont = other.hGlyphFont; other.hGlyphFont = nullptr;
            hOldBitmap = other.hOldBitmap; other.hOldBitmap = nullptr;
            hOldFont = other.hOldFont; other.hOldFont = nullptr;
            glyphCache = std::move(other.glyphCache);
            widthCache = std::move(other.widthCache);
        }
        return *this;
    }

    /**
     * @brief 通过 GDI 加载系统字体（唯一模式）
     *
     * 创建 DIB Section + 兼容 DC + 指定字体，用于运行时渲染单个字形。
     * 字形和单个字符宽度在首次使用时按需渲染/测量并缓存。
     */
    bool __fastcall ExtFont::LoadGdiFont(const char* fontName, int iHeight, int iWidth, bool bBold, bool bAntiAlias,
        int iMarginLeft, int iMarginRight, int iMarginBottom, int iLineSpacing, bool bDrawShadow)
    {
        this->Height = iHeight;              // 核心字形高度（不含底部边距）
        this->Width = iWidth;
        this->Bold = bBold;
        this->AntiAlias = bAntiAlias;
        this->MarginLeft = iMarginLeft;
        this->MarginRight = iMarginRight;
        this->MarginBottom = iMarginBottom;  // 额外底部空间（用于容纳字体下降部分）
        this->LineSpacing = iLineSpacing;    // 额外行间距
        this->DrawShadow = bDrawShadow;
        this->GlyphWidth = iMarginLeft + iWidth + iMarginRight;

        // 创建 32-bit DIB section 作为字形渲染目标
        // 缓冲区高度 = 核心字形高度 + 底部边距（解决微软雅黑等字体下降部分被裁剪的问题）
        const int bufferHeight = iHeight + iMarginBottom;

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = GlyphWidth;
        bmi.bmiHeader.biHeight = -bufferHeight; // 负值 = top-down DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC hdcScreen = GetDC(nullptr);
        hdcGlyph = CreateCompatibleDC(hdcScreen);
        ReleaseDC(nullptr, hdcScreen);

        if (!hdcGlyph)
        {
            return false;
        }

        hbmGlyph = CreateDIBSection(hdcGlyph, &bmi, DIB_RGB_COLORS, &pGlyphBits, nullptr, 0);
        if (!hbmGlyph)
        {
            ReleaseGdiResources(); // 清理已创建的 DC
            return false;
        }

        hOldBitmap = SelectObject(hdcGlyph, hbmGlyph);
        SetBkMode(hdcGlyph, TRANSPARENT);
        SetTextAlign(hdcGlyph, TA_TOP | TA_LEFT);
        SetTextColor(hdcGlyph, RGB(255, 255, 255)); // 固定白色绘制，alpha 掩膜提取只需覆盖度

        // 负 lfHeight = 字符高度（像素），不包含行间距
        const int fontHeightPx = -Height;
        const int fontWeight = bBold ? FW_BOLD : FW_NORMAL;
        const DWORD quality = bAntiAlias ? CLEARTYPE_QUALITY : NONANTIALIASED_QUALITY;

        // 尝试从系统名创建字体，失败则使用备选方案
        hGlyphFont = CreateFontA(
            fontHeightPx,                   // lfHeight（负值 = 字符高度）
            0,                              // lfWidth（0 = 自适应）
            0, 0,                           // escapement, orientation
            fontWeight,                     // fontWeight（FW_BOLD 或 FW_NORMAL）
            FALSE, FALSE, FALSE,            // italic, underline, strikeout
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            quality,                        // CLEARTYPE_QUALITY 或 NONANTIALIASED_QUALITY
            DEFAULT_PITCH | FF_DONTCARE,
            fontName);

        if (!hGlyphFont)
        {
            // 回退：使用宋体
            hGlyphFont = CreateFontA(
                fontHeightPx, 0, 0, 0, fontWeight,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                quality, DEFAULT_PITCH | FF_DONTCARE, "SimSun");
        }

        if (!hGlyphFont)
        {
            ReleaseGdiResources(); // 清理 DC + DIB
            return false;
        }

        hOldFont = SelectObject(hdcGlyph, hGlyphFont);

        // 预缓存常用 ASCII 字形及宽度
        glyphCache.reserve(256);
        widthCache.reserve(256);
        for (wchar_t c = L' '; c <= L'~'; ++c)
        {
            GetGlyphAlpha(c);
            GetCharWidth(c);
        }

        return false; // 游戏引擎约定
    }

    /**
     * @brief GBK 区码/位码 → wchar_t（带全局查表缓存）
     */
    wchar_t ExtFont::GbkToWchar(uint8_t section, uint8_t position)
    {
        wchar_t& cached = g_GbkWideTable[(section << 8) | position];
        if (cached)
            return cached;

        char gbk[3] = { (char)section, (char)position, 0 };
        wchar_t wch = L'?';
        if (MultiByteToWideChar(936, 0, gbk, 2, &wch, 1) <= 0) // CP936 = GBK
            wch = L'?';
        cached = wch;
        return cached;
    }

    /**
     * @brief 判断 wchar_t 是否在 GBK 可表示范围内
     *
     * 直接通过 Unicode 区间判断，无需 API 调用。
     * 非 GBK 字符由 GDI 绘制为方块（tofu），提前过滤可避免渲染开销。
     */
    bool ExtFont::IsGbkChar(wchar_t wch)
    {
        // ASCII 可打印 (U+0020..U+007E)
        if (wch >= 0x20 && wch <= 0x7E)
            return true;
        // CJK 核心汉字 (U+4E00..U+9FFF) —— 覆盖 99% 常用中文
        if (wch >= 0x4E00 && wch <= 0x9FFF)
            return true;

        return (wch >= 0x3000 && wch <= 0x303F)  // CJK 标点符号
            || (wch >= 0x3400 && wch <= 0x4DBF)  // CJK 扩展 A
            || (wch >= 0xF900 && wch <= 0xFAFF)  // CJK 兼容汉字
            || (wch >= 0xFF00 && wch <= 0xFFEF)  // 全角/半角形式（含全角字母数字）
            || (wch >= 0xFE30 && wch <= 0xFE4F)  // CJK 兼容形式
            || (wch >= 0x3040 && wch <= 0x30FF)  // 日文假名（GBK 包含）
            || (wch >= 0x2010 && wch <= 0x2040)  // 通用标点（破折号等）
            || (wch >= 0x3105 && wch <= 0x3129)  // 注音符号
            || (wch >= 0x2160 && wch <= 0x24FF)  // 罗马数字 / 带圈字母数字
            || (wch >= 0x2500 && wch <= 0x25FF)  // 表格线 / 方块元素
            || (wch >= 0x2600 && wch <= 0x26FF)  // 杂项符号
            || (wch >= 0x2190 && wch <= 0x22FF)  // 箭头 / 数学符号
            || (wch >= 0x31C0 && wch <= 0x31EF)  // CJK 笔画
            || (wch >= 0x3220 && wch <= 0x32B0)  // 带圈汉字
            || (wch >= 0xE000 && wch <= 0xF8FF)  // 私有使用区
            ;
    }

    /**
     * @brief 获取字形的抗锯齿 alpha 掩膜（按需 GDI 渲染 + 缓存）
     *
     * 渲染流程：
     *   1. 在内存 DC 上以白色文字绘制单个字符（背景为黑色 DIB）
     *   2. 读取每个像素的 ClearType 子像素颜色
     *   3. 取 max(R,G,B) 作为覆盖度（alpha）写入缓存
     *      （灰度字体下 R=G=B；ClearType 彩边下取最大值可保留边缘覆盖）
     *
     * 绘制阶段只消费 alpha（前景色来自调色板），因此不缓存 RGB 分量，
     * 相比 RGBA 缓存节省 75% 内存并提升混合时的缓存局部性。
     *
     * @return GlyphWidth×EffectiveHeight 字节 alpha 数据指针，失败返回 nullptr
     */
    const uint8_t* ExtFont::GetGlyphAlpha(wchar_t ch) const
    {
        if (!hdcGlyph || !pGlyphBits)
            return nullptr;

        // 检查缓存
        auto it = glyphCache.find(ch);
        if (it != glyphCache.end())
            return it->second.data();

        // 按需渲染
        const int bufW = GlyphWidth;
        const int bufH = EffectiveHeight(); // 有效缓冲区高度（含底部扩展空间）
        if (bufW <= 0 || bufH <= 0)
            return nullptr;

        // 清除背景：top-down DIB 是连续内存，直接 memset，无需创建 GDI 画刷
        memset(pGlyphBits, 0, static_cast<size_t>(bufW) * bufH * 4);

        // 以白色绘制字符（ClearType 会生成彩色子像素）
        TextOutW(hdcGlyph, MarginLeft, 0, &ch, 1);
        GdiFlush();

        // 提取 alpha 掩膜：max(R,G,B) 作为覆盖度
        std::vector<uint8_t> alpha(static_cast<size_t>(bufW) * bufH);
        const uint8_t* src = static_cast<const uint8_t*>(pGlyphBits);
        for (size_t i = 0; i < alpha.size(); ++i)
        {
            const uint8_t b = src[i * 4];
            const uint8_t g = src[i * 4 + 1];
            const uint8_t r = src[i * 4 + 2];
            alpha[i] = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
        }

        const auto inserted = glyphCache.emplace(ch, std::move(alpha));
        return inserted.first->second.data();
    }

    /**
     * @brief 获取单个字符的 GDI 渲染像素宽度（含左右边距）
     *
     * 使用 GetTextExtentPoint32W 测量字符在 GDI 字体下的实际宽度，
     * 结果加上 MarginLeft + MarginRight 并缓存。
     */
    int ExtFont::GetCharWidth(wchar_t ch) const
    {
        if (!hdcGlyph)
            return GlyphWidth; // 回退

        auto it = widthCache.find(ch);
        if (it != widthCache.end())
            return it->second;

        SIZE sz = { 0, 0 };
        GetTextExtentPoint32W(hdcGlyph, &ch, 1, &sz);
        const int w = sz.cx + MarginLeft + MarginRight;
        widthCache.emplace(ch, w);
        return w;
    }

    // ============================================================
    // Section 2: 渲染后端（色深适配 + 像素混合）
    // ============================================================
    //
    // 游戏支持 16 位（RGB565）和 32 位（RGB888）两种色深模式。
    // GetColor 在 DDraw 初始化时绑定对应实现；
    // 像素混合通过模板参数在绘制入口处一次性分派，内循环可完全内联。

    /** @brief 16 位色深：从调色板获取颜色 */
    static DWORD __fastcall GetColor16(const H3BasePalette565& palette, int colorIdx)
    {
        return palette.color[colorIdx].Value();
    }

    /** @brief 32 位色深：从调色板获取颜色 */
    static DWORD __fastcall GetColor32(const H3BasePalette565& palette, int colorIdx)
    {
        return palette.palette32->colors[colorIdx];
    }

    /** @brief 色深自适应：获取调色板颜色的函数指针（默认 16 位，DDraw 初始化后更新） */
    static DWORD(__fastcall* GetColor)(const H3BasePalette565& palette, int colorIdx) = GetColor16;

    /**
     * @brief 精确除以 255（避免整数除法指令）
     *
     * 对 0 ≤ x ≤ 255×255 恒有 floor(x/255) == (x + (x>>8) + 1) >> 8。
     * 混合运算 fg*a + bg*(255-a) 是凸组合，最大值 255×255，在精确域内。
     */
    static inline uint8_t Div255(uint32_t x)
    {
        return static_cast<uint8_t>((x + (x >> 8) + 1) >> 8);
    }

    /** @brief alpha 混合前景色（Is32=true: RGB888，false: RGB565） */
    template<bool Is32>
    static inline void BlendPixel(PUINT8 rowBuf, int col, DWORD fgColor, uint8_t alpha)
    {
        if constexpr (Is32)
        {
            DWORD* dst = reinterpret_cast<DWORD*>(rowBuf) + col;
            const DWORD bg = *dst;
            const uint32_t inv = 255u - alpha;
            const uint32_t r = Div255(((fgColor >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * inv);
            const uint32_t g = Div255(((fgColor >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * inv);
            const uint32_t b = Div255((fgColor & 0xFF) * alpha + (bg & 0xFF) * inv);
            *dst = (r << 16) | (g << 8) | b;
        }
        else
        {
            WORD* dst = reinterpret_cast<WORD*>(rowBuf) + col;
            const DWORD bg = *dst;
            const uint32_t inv = 255u - alpha;
            // RGB565 → 8-bit 通道展开 → 混合 → 压回 RGB565
            const uint32_t br = ((bg >> 11) & 0x1F) << 3;
            const uint32_t bgg = ((bg >> 5) & 0x3F) << 2;
            const uint32_t bb = (bg & 0x1F) << 3;
            const uint32_t fr = ((fgColor >> 11) & 0x1F) << 3;
            const uint32_t fgg = ((fgColor >> 5) & 0x3F) << 2;
            const uint32_t fb = (fgColor & 0x1F) << 3;
            const uint32_t r = Div255(fr * alpha + br * inv);
            const uint32_t g = Div255(fgg * alpha + bgg * inv);
            const uint32_t b = Div255(fb * alpha + bb * inv);
            *dst = static_cast<WORD>((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        }
    }

    /** @brief alpha 混合阴影色（Is32=true: RGB888 混合，false: 直写保持原风格） */
    template<bool Is32>
    static inline void BlendShadow(PUINT8 rowBuf, int col, DWORD shadowColor, uint8_t alpha)
    {
        if constexpr (Is32)
        {
            DWORD* dst = reinterpret_cast<DWORD*>(rowBuf) + col;
            const DWORD bg = *dst;
            const uint32_t inv = 255u - alpha;
            const uint32_t r = Div255(((shadowColor >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * inv);
            const uint32_t g = Div255(((shadowColor >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * inv);
            const uint32_t b = Div255((shadowColor & 0xFF) * alpha + (bg & 0xFF) * inv);
            *dst = (r << 16) | (g << 8) | b;
        }
        else
        {
            // 16 位色深：阴影直接写入（不混合 alpha，保持原风格）
            *(reinterpret_cast<WORD*>(rowBuf) + col) = static_cast<WORD>(shadowColor);
        }
    }

    // ============================================================
    // Section 3: 文本扫描与处理
    // ============================================================

    /**
     * @brief 遍历文本中所有"可见 token"，自动跳过颜色码 {~RRGGBB} / { } / }
     *
     * 颜色码格式：
     *   - {~#RRGGBB}  十六进制颜色（或 {~colorname} 命名颜色）
     *   - { / }        高亮切换（{ 进入高亮，} 恢复默认）
     *
     * 这些控制字符在遍历中被跳过，回调只接收实际可见字符。
     *
     * @param pFont   字体指针
     * @param szText  输入文本（以 '\0' 结尾）
     * @param fn      回调：fn(TokenType type, int width, int charBytes) -> bool
     *                返回 false 可提前终止遍历
     * @return        遍历结束时的指针位置（调用方可用于恢复 szText 推进）
     */
    template<typename Func>
    static const char* ForEachVisibleChar(H3FontExt* pFont, LPCSTR szText, Func&& fn)
    {
        auto* extFont = pFont->ExtData;

        while (*szText)
        {
            uint8_t code = static_cast<uint8_t>(*szText);

            // -------- 跳过颜色码 --------
            if (code == '{' || code == '}')
            {
                if (code == '{' && *(szText + 1) == '~')
                {
                    szText += 2; // 跳过 "{~"
                    while (*szText && *szText != '}' && *szText != ' ' && *szText != '\n')
                        ++szText;
                    if (*szText == '}') // 跳过 '}'
                        ++szText;
                    continue;
                }
                ++szText;
                continue;
            }

            // -------- 换行 --------
            if (code == '\n')
            {
                if (!fn(TokenType::Newline, 0, 1))
                    return szText;
                ++szText;
                continue;
            }

            // -------- 空格 --------
            if (code == ' ')
            {
                const int spaceW = extFont->GetCharWidth(L' ');
                if (!fn(TokenType::Space, spaceW, 1))
                    return szText;
                ++szText;
                continue;
            }

            // -------- 单字节字符 --------
            if (IsSingleByte(code))
            {
                const wchar_t wch = (wchar_t)code;
                const int charW = extFont->GetCharWidth(wch);
                if (!fn(TokenType::SingleByte, charW, 1))
                    return szText;
                ++szText;
                continue;
            }

            // -------- 双字节字符 --------
            uint8_t nc = static_cast<uint8_t>(*(szText + 1));
            if (IsDBCSLeadByte(code, nc))
            {
                const wchar_t wch = ExtFont::GbkToWchar(code, nc);
                const int charW = extFont->GetCharWidth(wch);
                if (!fn(TokenType::DoubleByte, charW, 2))
                    return szText;
                szText += 2;
            }
            else
            {
                ++szText; // 无效的双字节首字节，跳过
            }
        }
        return szText;
    }

    /**
     * @brief 增量更新颜色状态（扫描追加到行缓冲的内容）
     *
     * 追踪 {~...} / { / } 的状态变化，维护"当前行末尾活跃的颜色状态"，
     * 供折行时向下一行继承。相比每次折行重扫整个行缓冲（O(n²)），
     * 增量扫描只处理新追加的片段，总复杂度 O(n)。
     *
     * @param p               扫描起点
     * @param end             扫描终点
     * @param activeTag       [in/out] 活跃的自定义颜色标签（含大括号）
     * @param highlightActive [in/out] 活跃的高亮状态
     */
    static void UpdateColorState(const char* p, const char* end, std::string& activeTag, bool& highlightActive)
    {
        while (p < end)
        {
            const uint8_t b = static_cast<uint8_t>(*p);

            // 跳过双字节 GBK 字符，避免第二字节碰巧为 0x7B/0x7D 造成误判
            if (!IsSingleByte(b) && (p + 1 < end))
            {
                const uint8_t nb = static_cast<uint8_t>(*(p + 1));
                if (IsDBCSLeadByte(b, nb))
                {
                    p += 2;
                    continue;
                }
            }

            if (*p == '{' && (p + 1 < end) && *(p + 1) == '~')
            {
                // 自定义颜色标签 {~ColorName}：颜色关闭时等同高亮 {
                const char* start = p;
                p += 2;
                while (p < end && *p != '}')
                    ++p;
                if (p < end && *p == '}')
                    ++p;
                if (IsTextColorEnable)
                {
                    activeTag.assign(start, p - start);
                    highlightActive = false;
                }
                else
                {
                    highlightActive = true;
                    activeTag.clear();
                }
            }
            else if (*p == '{')
            {
                highlightActive = true;
                activeTag.clear();
                ++p;
            }
            else if (*p == '}')
            {
                highlightActive = false;
                activeTag.clear();
                ++p;
            }
            else
            {
                ++p;
            }
        }
    }

    /**
     * @brief 将一行原始文本（含颜色码）预处理为 CleanLine
     *
     * 剥离所有颜色控制字符 {~...} / { / }，生成纯可见字符序列，
     * 同时将颜色变更信息记录为 ColorStop 列表。
     *
     * @param src            原始拆行结果（含颜色码）
     * @param defaultColor   默认渲染颜色（当前色深格式）
     * @param highlightColor { } 切换的高亮颜色
     * @param is32bit        当前是否为 32 位色深模式
     * @param dst            [out] 预处理结果
     */
    static void PreprocessLine(const TextLineStruct& src, DWORD defaultColor, DWORD highlightColor, bool is32bit, CleanLine& dst)
    {
        dst.lineWidth = src.lineWidth;
        dst.text.clear();
        dst.stops.clear();

        if (src.lineWidth == 0)
            return;

        const std::string& raw = src.Text;
        const size_t len = raw.size();

        // 预分配：clean text 最多和 raw 一样长，通常更短
        dst.text.reserve(len);

        DWORD curColor = defaultColor;

        for (size_t i = 0; i < len; )
        {
            const uint8_t code = static_cast<uint8_t>(raw[i]);

            // ============ '{' —— 进入高亮或自定义颜色 ============
            if (code == '{')
            {
                DWORD newColor = highlightColor;
                ++i;

                if (i < len && raw[i] == '~')
                {
                    // 仅在颜色功能开启时解析自定义颜色值，否则跳过整个标签
                    if (IsTextColorEnable)
                    {
                        ++i;
                        const size_t codeStart = i;
                        while (i < len && raw[i] != '}')
                            ++i;

                        if (i > codeStart)
                        {
                            if (raw[codeStart] == '#')
                            {
                                DWORD c = 0;
                                auto [_, ec] = std::from_chars(
                                    raw.data() + codeStart + 1,
                                    raw.data() + i,
                                    c, 16);
                                if (ec == std::errc())
                                {
                                    // 十六进制颜色按当前色深转换（修复 16 位模式下颜色错误）
                                    newColor = is32bit ? c : static_cast<DWORD>(RGB888toRGB565(c));
                                }
                                else
                                {
                                    newColor = defaultColor;
                                }
                            }
                            else
                            {
                                const std::string key(raw.data() + codeStart, i - codeStart);
                                const auto it = TextColorMap.find(key);
                                if (it != TextColorMap.end())
                                    newColor = is32bit ? it->second.rgb888 : it->second.rgb565;
                                else
                                    newColor = defaultColor;
                            }
                        }
                        else
                        {
                            newColor = defaultColor;
                        }
                    }
                    else
                    {
                        // 颜色功能关闭：跳到 '}' 之后，颜色保持 highlightColor（黄色）
                        ++i;
                        while (i < len && raw[i] != '}')
                            ++i;
                    }

                    // 跳过 '}'
                    if (i < len && raw[i] == '}')
                        ++i;
                }

                if (newColor != curColor)
                {
                    curColor = newColor;
                    dst.stops.push_back({ dst.text.size(), curColor });
                }
                continue;
            }

            // ============ '}' —— 恢复默认颜色 ============
            if (code == '}')
            {
                if (curColor != defaultColor)
                {
                    curColor = defaultColor;
                    dst.stops.push_back({ dst.text.size(), curColor });
                }
                ++i;
                continue;
            }

            // ============ 可见字符：拷贝到 clean text ============
            if (IsSingleByte(code))
            {
                dst.text.push_back(static_cast<char>(code));
                ++i;
            }
            else
            {
                // 双字节字符：先检查下一字节是否存在且有效
                if (i + 1 < len)
                {
                    const uint8_t nextCode = static_cast<uint8_t>(raw[i + 1]);
                    if (IsDBCSLeadByte(code, nextCode))
                    {
                        dst.text.push_back(raw[i]);
                        dst.text.push_back(raw[i + 1]);
                        i += 2;
                        continue;
                    }
                }
                // 无效双字节或尾部不完整，跳过首字节
                ++i;
            }
        }
    }

    /**
     * @brief 将文本按指定宽度拆分为多行
     *
     * 算法流程（按词为单位）：
     *   1. 跳过行首空白和换行符
     *   2. 用 ForEachVisibleChar 确定下一个"词"的边界
     *   3. 若当前行放不下该词，输出当前行
     *   4. 若词本身超宽，逐字符强制拆行
     *   5. 将词追加到当前行
     *
     * 颜色状态在追加内容时增量维护，折行推送后 O(1) 继承到下一行。
     *
     * @param pFont     ASCII 字体指针
     * @param szText    输入文本（以 '\0' 结尾）
     * @param iBoxWidth 文本框像素宽度
     * @param lines     [out] 拆分结果（含颜色码的原始行）
     */
    static void __stdcall SplitTextIntoLines(H3FontExt* pFont, LPCSTR szText, const int iBoxWidth,
        vector<TextLineStruct>& lines)
    {
        if (!*szText)
            return;

        // 缓存常用参数
        auto* extFont = pFont->ExtData;
        const int spaceWidth = extFont->GetCharWidth(L' ');

        std::string lineBuf;
        int lineWidth = 0;

        // 当前行末尾的活跃颜色状态（随追加内容增量更新）
        std::string activeTag;
        bool highlightActive = false;

        // 推送当前行
        // inherit = true ：自动折行，颜色状态继承到下一行
        // inherit = false：显式换行符，颜色状态重置
        auto pushLine = [&](bool inherit)
        {
            lines.push_back({ std::move(lineBuf), lineWidth });
            lineBuf.clear();
            lineWidth = 0;

            if (inherit)
            {
                if (!activeTag.empty())
                    lineBuf = activeTag;
                else if (highlightActive)
                    lineBuf.push_back('{');
            }
            else
            {
                activeTag.clear();
                highlightActive = false;
            }
        };

        // ============ 主循环：按词为单位处理 ============
        while (*szText)
        {
            // ---- 阶段 1：处理行首空白和换行符 ----
            int blankCount = 0;
            int blankWidth = 0;

            while (true)
            {
                uint8_t ch = static_cast<uint8_t>(*szText);
                if (ch == ' ')
                {
                    blankWidth += spaceWidth;
                    ++blankCount;
                    ++szText;
                }
                else if (ch == '\n')
                {
                    // 换行：当前行强制输出，重置状态
                    pushLine(false);
                    blankCount = 0;
                    blankWidth = 0;
                    ++szText;
                }
                else
                {
                    break;
                }
            }

            if (!*szText)
                break;

            // ---- 阶段 2：取词——使用 ForEachVisibleChar 确定词边界 ----
            int wordWidth = 0;
            const char* wordStart = szText;

            // realEnd 指向第一个 Space/Newline（触发停止处），或 '\0'
            const char* realEnd = ForEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*charBytes*/)
                {
                    if (type == TokenType::Space || type == TokenType::Newline)
                        return false;
                    wordWidth += width;
                    return true;
                });

            // 没有可见字符（如纯颜色码后跟空格/结尾）：安全推进 szText
            if (wordWidth == 0)
            {
                // 仍需更新颜色状态，保证跨行继承正确
                UpdateColorState(wordStart, realEnd, activeTag, highlightActive);
                szText = (realEnd > wordStart) ? realEnd : (wordStart + 1);
                continue;
            }

            // ---- 阶段 3：拆行判定 ----
            if (lineWidth + wordWidth + blankWidth > iBoxWidth)
            {
                if (lineWidth > 0)
                    pushLine(true);
                blankCount = 0;
                blankWidth = 0;

                // 如果词本身就超宽，逐字符拆行
                if (wordWidth > iBoxWidth)
                {
                    LPCSTR p = wordStart;
                    while (p < realEnd)
                    {
                        const uint8_t code = static_cast<uint8_t>(*p);

                        // ---- 颜色标记：保留到行缓冲但不计入宽度 ----
                        if (code == '{' || code == '}')
                        {
                            const char* tagStart = p;
                            if (code == '{' && (p + 1 < realEnd) && *(p + 1) == '~')
                            {
                                p += 2; // 跳过 "{~"
                                while (p < realEnd && *p != '}')
                                    ++p;
                                if (p < realEnd && *p == '}') // 跳过 '}'
                                    ++p;
                            }
                            else
                            {
                                // 单个 { 或 }
                                ++p;
                            }
                            lineBuf.append(tagStart, p - tagStart);
                            UpdateColorState(tagStart, p, activeTag, highlightActive);
                            continue;
                        }

                        // ---- 可见字符：统一通过 GDI 获取宽度 ----
                        int charW = 0;
                        int charBytes = 0;
                        if (IsSingleByte(code))
                        {
                            charW = extFont->GetCharWidth((wchar_t)code);
                            charBytes = 1;
                        }
                        else
                        {
                            const uint8_t nextCode = (p + 1 < realEnd) ? static_cast<uint8_t>(*(p + 1)) : 0;
                            if (IsDBCSLeadByte(code, nextCode))
                            {
                                charW = extFont->GetCharWidth(ExtFont::GbkToWchar(code, nextCode));
                                charBytes = 2;
                            }
                            else
                            {
                                charW = 0;
                                charBytes = 1;
                            }
                        }

                        if (lineWidth + charW > iBoxWidth && lineWidth > 0)
                            pushLine(true);

                        lineBuf.append(p, charBytes);
                        lineWidth += charW;
                        p += charBytes;
                    }
                    szText = realEnd;
                    continue; // 词已处理完毕，跳过阶段 4
                }
            }

            // ---- 阶段 4：将词追加到当前行 ----
            if (blankCount > 0)
                lineBuf.append(blankCount, ' ');

            // 保留颜色标记原始字节（PreprocessLine 将在渲染阶段剥离）
            lineBuf.append(wordStart, static_cast<size_t>(realEnd - wordStart));
            lineWidth += wordWidth + blankWidth;
            UpdateColorState(wordStart, realEnd, activeTag, highlightActive);
            szText = realEnd;
        }

        // 输出最后一行
        if (lineWidth > 0 || !lineBuf.empty())
        {
            lines.push_back({ std::move(lineBuf), lineWidth });
        }
    }

    // ============================================================
    // Section 4: 布局缓存
    // ============================================================
    //
    // UI 文本（按钮、状态栏、对话框等）每帧重绘但内容大多不变。
    // 缓存"拆行 + 颜色码剥离"的预处理结果，命中时跳过全部解析开销。
    // 键为 (文本内容 + 拆行宽度 + 字体 + 颜色 + 色深)，覆盖所有影响输出的输入。

    /** @brief 布局缓存条目 */
    struct LayoutEntry
    {
        uint64_t hash = 0;             ///< 文本 FNV-1a 哈希（0 = 空槽位）
        std::string text;              ///< 原始文本（精确比较，防哈希碰撞误命中）
        ExtFont* font = nullptr;       ///< 扩展字库
        int boxWidth = 0;              ///< 拆行宽度
        DWORD defaultColor = 0;        ///< 默认颜色（当前色深格式）
        DWORD highlightColor = 0;      ///< 高亮颜色
        bool is32bit = false;          ///< 创建时的色深
        std::vector<CleanLine> lines;  ///< 预处理结果
    };

    static constexpr size_t LayoutCacheSize = 64;
    static LayoutEntry g_LayoutCache[LayoutCacheSize];

    /** @brief FNV-1a 64 位字符串哈希 */
    static uint64_t Fnv1aHash(const char* s)
    {
        uint64_t h = 0xcbf29ce484222325ULL;
        while (*s)
        {
            h ^= static_cast<uint8_t>(*s++);
            h *= 0x100000001b3ULL;
        }
        return h;
    }

    /** @brief 清空布局缓存（分辨率/色深切换、字体表重建时调用） */
    static void ClearLayoutCache()
    {
        for (auto& slot : g_LayoutCache)
        {
            slot.hash = 0;
            slot.font = nullptr;
            slot.text.clear();
            slot.lines.clear();
        }
    }

    /**
     * @brief 获取文本的预处理布局（缓存命中则零解析，未命中则构建并填充缓存）
     */
    static const std::vector<CleanLine>& GetOrCreateLayout(H3FontExt* pFont, LPCSTR szText, int iBoxWidth,
        DWORD defaultColor, DWORD highlightColor, bool is32bit)
    {
        auto* extFont = pFont->ExtData;
        const uint64_t hash = Fnv1aHash(szText);
        LayoutEntry& slot = g_LayoutCache[hash % LayoutCacheSize];

        if (slot.hash == hash
            && slot.font == extFont
            && slot.boxWidth == iBoxWidth
            && slot.defaultColor == defaultColor
            && slot.highlightColor == highlightColor
            && slot.is32bit == is32bit
            && slot.text == szText)
        {
            return slot.lines;
        }

        // 未命中：重建该槽位
        slot.hash = hash;
        slot.text = szText;
        slot.font = extFont;
        slot.boxWidth = iBoxWidth;
        slot.defaultColor = defaultColor;
        slot.highlightColor = highlightColor;
        slot.is32bit = is32bit;

        g_SplitScratch.clear();
        SplitTextIntoLines(pFont, szText, iBoxWidth, g_SplitScratch);

        slot.lines.clear();
        slot.lines.reserve(g_SplitScratch.size());
        for (const auto& rawLine : g_SplitScratch)
        {
            slot.lines.emplace_back();
            PreprocessLine(rawLine, defaultColor, highlightColor, is32bit, slot.lines.back());
        }
        return slot.lines;
    }

    // ============================================================
    // Section 5: 字符 & 文本渲染
    // ============================================================

    /**
     * @brief 绘制单个字符到 PCX 缓冲区（统一 GDI 渲染）
     *
     * 所有字符（ASCII 和 GBK 汉字）均通过 GDI 实时渲染 + alpha 混合，
     * 并按目标 PCX 边界裁剪，杜绝越界写入。
     *
     * @tparam Is32      是否 32 位色深
     * @param pFont      字体指针
     * @param pOutputPcx 目标 PCX 图像缓冲区
     * @param wch        待绘制字符（wchar_t）
     * @param iX         绘制起点 X 坐标
     * @param iY         绘制起点 Y 坐标
     * @param uFontColor 前景色（已转换为当前色深格式）
     * @return 是否实际绘制了像素
     */
    template<bool Is32>
    static bool H3Font_DrawChar(H3FontExt* pFont, H3LoadedPcx16* pOutputPcx, wchar_t wch,
        int iX, int iY, DWORD uFontColor)
    {
        auto cFont = pFont->ExtData;

        // 非 GBK 字符 → 跳过（避免 GDI 渲染为方块）
        if (!ExtFont::IsGbkChar(wch))
            return false;

        const uint8_t* glyph = cFont->GetGlyphAlpha(wch);
        if (!glyph)
            return false;

        const int glyphW = cFont->GlyphWidth;
        const int glyphH = cFont->EffectiveHeight(); // 含底部扩展空间
        const int pcxW = pOutputPcx->width;
        const int pcxH = pOutputPcx->height;

        // ---- 按目标位图边界裁剪（GetRow 无边界检查，越界会破坏内存）----
        int colStart = 0, colEnd = glyphW;
        if (iX < 0)
            colStart = -iX;
        if (iX + colEnd > pcxW)
            colEnd = pcxW - iX;
        if (colStart >= colEnd)
            return false;

        int rowStart = 0, rowEnd = glyphH;
        if (iY < 0)
            rowStart = -iY;
        if (iY + rowEnd > pcxH)
            rowEnd = pcxH - iY;
        if (rowStart >= rowEnd)
            return false;

        const bool drawShadow = cFont->DrawShadow;

        for (int rowIdx = rowStart; rowIdx < rowEnd; ++rowIdx)
        {
            PUINT8 rowBuf = pOutputPcx->GetRow(iY + rowIdx);
            const uint8_t* glyphRow = glyph + static_cast<size_t>(rowIdx) * glyphW;

            // 阴影行（下移 1 像素），越界则不绘制
            PUINT8 shadowRowBuf = nullptr;
            if (drawShadow && iY + rowIdx + 1 < pcxH)
                shadowRowBuf = pOutputPcx->GetRow(iY + rowIdx + 1);

            for (int colIdx = colStart; colIdx < colEnd; ++colIdx)
            {
                const uint8_t alpha = glyphRow[colIdx];
                if (alpha == 0)
                    continue;

                const int px = iX + colIdx;

                // Alpha 混合前景色（读 bg → 混合 fgColor → 写入）
                BlendPixel<Is32>(rowBuf, px, uFontColor, alpha);

                // Alpha 混合阴影（右下偏移 1 像素）
                // 若目标位置会被字形自身像素覆盖则跳过，避免黑色融入字体内部
                if (shadowRowBuf && px + 1 < pcxW)
                {
                    const bool overlap = (rowIdx + 1 < glyphH && colIdx + 1 < glyphW)
                        && glyphRow[glyphW + colIdx + 1] > 0;
                    if (!overlap)
                        BlendShadow<Is32>(shadowRowBuf, px + 1, ShadowColor, alpha);
                }
            }
        }

        return true;
    }

    /**
     * @brief 逐行绘制预处理后的文本（含颜色切换与水平对齐）
     *
     * @tparam Is32       是否 32 位色深
     * @param pFont       字体指针
     * @param pPcx        目标 PCX 图像缓冲区
     * @param textLines   预处理后的文本行
     * @param iX/iY       文本框左上角坐标
     * @param startY      垂直对齐偏移
     * @param iBoxWidth   文本框宽度
     * @param iBoxHeight  文本框高度
     * @param uAlignFlags 水平对齐标志（VCENTER/VBOTTOM 位已清除）
     * @param defaultColor 默认渲染颜色
     * @param shift       字形在行高内的垂直居中偏移
     * @param fontHeight  行高
     */
    template<bool Is32>
    static void DrawLines(H3FontExt* pFont, H3LoadedPcx16* pPcx, const std::vector<CleanLine>& textLines,
        int iX, int iY, int startY, int iBoxWidth, int iBoxHeight, uint32_t uAlignFlags,
        DWORD defaultColor, int shift, int fontHeight)
    {
        auto* extFont = pFont->ExtData;
        const int lineCount = static_cast<int>(textLines.size());
        const int bottomBound = iY + iBoxHeight;

        for (int rowIdx = 0; rowIdx < lineCount; ++rowIdx)
        {
            const CleanLine& line = textLines[rowIdx];
            if (line.lineWidth == 0)
                continue;

            // 防止法术书计算有误
            if (iY + startY + (rowIdx + 1) * pFont->oriHeight > bottomBound)
                break;

            // 水平对齐
            int startX = 0;
            switch (uAlignFlags)
            {
            case eTextAlignment::HCENTER: startX = (iBoxWidth - line.lineWidth) / 2; break; // 居中
            case eTextAlignment::HRIGHT:  startX = iBoxWidth - line.lineWidth;        break; // 右对齐
            }

            int curX = iX + startX;
            const char* const textData = line.text.data();
            const char* p = textData;
            const char* const end = p + line.text.size();

            // 颜色跟踪
            DWORD curColor = defaultColor;
            size_t stopIdx = 0;
            const size_t stopCount = line.stops.size();

            const int lineY = iY + startY + rowIdx * fontHeight;
            while (p < end)
            {
                // 检查当前位置是否有颜色变更
                const size_t bytePos = static_cast<size_t>(p - textData);
                while (stopIdx < stopCount && line.stops[stopIdx].bytePos == bytePos)
                {
                    curColor = line.stops[stopIdx].color;
                    ++stopIdx;
                }

                const uint8_t code = static_cast<uint8_t>(*p);

                if (IsSingleByte(code))
                {
                    H3Font_DrawChar<Is32>(pFont, pPcx, (wchar_t)code,
                        curX, lineY + shift, curColor);
                    curX += extFont->GetCharWidth((wchar_t)code);
                    ++p;
                }
                else
                {
                    uint8_t nextCode = (end - p >= 2) ? static_cast<uint8_t>(p[1]) : 0;
                    if (IsDBCSLeadByte(code, nextCode))
                    {
                        const wchar_t wch = ExtFont::GbkToWchar(code, nextCode);
                        H3Font_DrawChar<Is32>(pFont, pPcx, wch,
                            curX, lineY + shift, curColor);
                        curX += extFont->GetCharWidth(wch);
                        p += 2;
                    }
                    else
                    {
                        // 无效双字节，当单字节处理
                        H3Font_DrawChar<Is32>(pFont, pPcx, (wchar_t)code,
                            curX, lineY + shift, curColor);
                        curX += extFont->GetCharWidth((wchar_t)code);
                        ++p;
                    }
                }
            }
        }
    }

    /**
     * @brief 在指定矩形区域内绘制多行文本
     *
     * 完整渲染管线：
     *   1. GetOrCreateLayout → 拆行 + 颜色解析（布局缓存命中时零开销）
     *   2. 计算垂直对齐偏移
     *   3. 按色深模板化逐行逐字符绘制（含颜色切换）
     *
     * @param h           Hook 句柄（未使用）
     * @param pFont       字体指针
     * @param szText      输入文本（含颜色码和控制字符）
     * @param pPcx        目标 PCX 图像缓冲区
     * @param iX          文本框左上角 X 坐标
     * @param iY          文本框左上角 Y 坐标
     * @param iBoxWidth   文本框像素宽度
     * @param iBoxHeight  文本框像素高度
     * @param uColorIdx   颜色索引（游戏调色板索引）
     * @param uAlignFlags 对齐标志（eTextAlignment 位掩码）
     * @param iFontStyle  字体样式（未使用）
     */
    static void __stdcall H3Font_DrawText(HiHook* h, H3FontExt* pFont, char* szText, H3LoadedPcx16* pPcx,
        int iX, int iY, int iBoxWidth, int iBoxHeight,
        uint32_t uColorIdx, uint32_t uAlignFlags, int iFontStyle)
    {
        if (!szText || !*szText || iBoxWidth == 0 || !pPcx || !pFont->ExtData)
            return;

        // ========== 阶段一：解析颜色 ==========
        const uint32_t ci = (uColorIdx & 0x100) ? (uColorIdx & 0xFE) : (uColorIdx + 9);
        const DWORD defaultColor = GetColor(pFont->palette, ci);
        const DWORD highlightColor = GetColor(pFont->palette, ci + 1);
        const bool is32bit = Is32BitMode;

        // ========== 阶段二：获取布局（缓存命中 → 跳过拆行与颜色解析）==========
        const std::vector<CleanLine>& textLines =
            GetOrCreateLayout(pFont, szText, iBoxWidth, defaultColor, highlightColor, is32bit);
        if (textLines.empty())
            return;

        const int lineCount = static_cast<int>(textLines.size());

        // ========== 阶段三：缓存字体度量 ==========
        auto* extFont = pFont->ExtData;
        const int fontHeight = pFont->height;
        const int effHeight = extFont->EffectiveHeight(); // 含底部边距的有效字形高度

        // 统一垂直偏移：在行高内居中有效字形区域
        const int shift = (fontHeight - effHeight) / 2;

        // ========== 阶段四：垂直对齐 ==========
        int startY = 0;

        if (uAlignFlags & eTextAlignment::VCENTER)
        {
            uAlignFlags &= ~eTextAlignment::VCENTER;
            const int totalH = fontHeight * lineCount;
            if (totalH >= iBoxHeight)
            {
                if (iBoxHeight < 2 * fontHeight)
                    startY = (iBoxHeight - fontHeight) / 2;
            }
            else
            {
                startY = (iBoxHeight - totalH) / 2;
            }
        }
        else if (uAlignFlags & eTextAlignment::VBOTTOM)
        {
            uAlignFlags &= ~eTextAlignment::VBOTTOM;
            const int totalH = fontHeight * lineCount;
            if (totalH < iBoxHeight)
                startY = iBoxHeight - totalH;
        }

        // ========== 阶段五：逐行绘制（按色深一次性分派，内循环完全内联）==========
        if (is32bit)
        {
            DrawLines<true>(pFont, pPcx, textLines, iX, iY, startY, iBoxWidth, iBoxHeight,
                uAlignFlags, defaultColor, shift, fontHeight);
        }
        else
        {
            DrawLines<false>(pFont, pPcx, textLines, iX, iY, startY, iBoxWidth, iBoxHeight,
                uAlignFlags, defaultColor, shift, fontHeight);
        }
    }

    // ============================================================
    // Section 6: Hook 包装函数
    // ============================================================
    //
    // 以下函数是对游戏引擎原始字体接口的 Hook 替换。
    // 签名与原始函数一致（__stdcall，首个参数为 HiHook*），
    // 内部使用 H3FontExt 扩展字体实现中文支持。

    /**
     * @brief Hook: 将文本拆分为多行（返回 H3Vector<H3String>）
     *
     * 替换游戏原始的文本拆分逻辑，支持双字节字符和颜色码。
     */
    static void __stdcall H3Font_SplitTextIntoLines(HiHook* h, H3FontExt* pFont, char* szText, const int iBoxWidth,
        H3Vector<H3String>& lines)
    {
        lines.RemoveAll();

        if (!szText || !*szText || !pFont->ExtData)
            return;

        g_SplitScratch.clear();
        SplitTextIntoLines(pFont, szText, iBoxWidth, g_SplitScratch);
        for (const auto& line : g_SplitScratch)
        {
            lines.Add(H3String(line.Text.c_str()));
        }
    }

    /**
     * @brief Hook: 计算文本中最长单词的宽度
     *
     * 用于消息框等需要根据内容自适应宽度的场景。
     * 结果受 BoxWidthMin / BoxWidthMax 约束。
     */
    static int __stdcall H3Font_GetWordWidth(HiHook* h, H3FontExt* pFont, char* szText)
    {
        if (!szText || !*szText || !pFont->ExtData)
            return 0;

        int maxWidth = 0;
        int wordWidth = 0;

        ForEachVisibleChar(pFont, szText,
            [&](TokenType type, int width, int /*bytes*/)
            {
                if (type == TokenType::Newline || type == TokenType::Space || type == TokenType::DoubleByte)
                {
                    if (wordWidth > maxWidth)
                        maxWidth = wordWidth;
                    wordWidth = 0;
                }
                else
                {
                    wordWidth += width;
                }
                return true;
            });

        if (wordWidth > maxWidth)
            maxWidth = wordWidth;
        return clamp(maxWidth, BoxWidthMin, BoxWidthMax);
    }

    /**
     * @brief Hook: 计算文本在指定宽度下的实际换行后最大行宽
     *
     * 模拟换行行为，返回所有行中最宽一行的像素宽度。
     */
    static int __stdcall H3Font_GetLineWrapWidth(HiHook* h, H3FontExt* pFont, char* szText, int iBoxWidth)
    {
        if (!szText || !*szText || !pFont->ExtData)
            return 0;

        int maxWidth = 0;
        int lineWidth = 0;

        ForEachVisibleChar(pFont, szText,
            [&](TokenType type, int width, int /*bytes*/)
            {
                if (type == TokenType::Newline)
                {
                    if (lineWidth > maxWidth)
                        maxWidth = lineWidth;
                    lineWidth = 0;
                }
                else
                {
                    if (lineWidth + width > iBoxWidth)
                    {
                        if (lineWidth > maxWidth)
                            maxWidth = lineWidth;
                        lineWidth = 0;
                    }
                    lineWidth += width;
                }
                return true;
            });

        if (lineWidth > maxWidth)
            maxWidth = lineWidth;
        return maxWidth;
    }

    /**
     * @brief Hook: 计算文本在指定宽度下的总行数
     */
    static int __stdcall H3Font_GetLineCount(HiHook* h, H3FontExt* pFont, char* szText, int iBoxWidth)
    {
        if (!szText || !*szText || !pFont->ExtData)
            return 0;

        int lineCount = 1;
        int lineWidth = 0;

        ForEachVisibleChar(pFont, szText,
            [&](TokenType type, int width, int /*bytes*/)
            {
                if (type == TokenType::Newline)
                {
                    ++lineCount;
                    lineWidth = 0;
                }
                else
                {
                    if (lineWidth + width > iBoxWidth)
                    {
                        ++lineCount;
                        lineWidth = 0; // 当前字符归入新行
                    }
                    lineWidth += width;
                }
                return true;
            });

        return lineCount;
    }

    /**
     * @brief Hook: 计算文本中最长行的像素宽度（不换行）
     */
    static int __stdcall H3Font_GetLineWidth(HiHook* h, H3FontExt* pFont, char* szText)
    {
        if (!szText || !*szText || !pFont->ExtData)
            return 0;

        int maxWidth = 0;
        int lineWidth = 0;

        ForEachVisibleChar(pFont, szText,
            [&](TokenType type, int width, int /*bytes*/)
            {
                if (type == TokenType::Newline)
                {
                    if (lineWidth > maxWidth)
                        maxWidth = lineWidth;
                    lineWidth = 0;
                }
                else
                {
                    lineWidth += width;
                }
                return true;
            });

        if (lineWidth > maxWidth)
            maxWidth = lineWidth;
        return maxWidth;
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
    static H3Font* __stdcall H3Font_Load_Hook(HiHook* h, char* name)
    {
        auto fntName = _strlwr(name);
        auto font = FASTCALL_1(H3FontExt*, h->GetDefaultFunc(), fntName);
        if (!font)
            return font;

        // 精确匹配 → 回退默认字体；不使用 operator[] 以免为未知字体插入空条目
        auto it = g_ExtFontTable.find(fntName);
        if (it == g_ExtFontTable.end())
            it = g_ExtFontTable.find("medfont.fnt");
        if (it == g_ExtFontTable.end())
            return font;

        font->ExtData = &it->second;
        font->oriHeight = font->height;
        // 行高 = max(原始高度, 有效字形高度 + 行间距)
        font->height = std::max((int)font->height, font->ExtData->EffectiveHeight() + font->ExtData->LineSpacing);
        return font;
    }

    /**
     * @brief Hook: DirectDraw 初始化后配置渲染后端
     *
     * 根据游戏色深模式（16/32 位）绑定对应的 GetColor 实现。
     * 同时根据屏幕分辨率计算文本框宽度限制，并清空布局缓存
     * （分辨率/色深变化后旧缓存不再有意义）。
     */
    static void __stdcall Main_DirectDrawInit_Hook(HiHook* h)
    {
        FASTCALL_0(void, h->GetDefaultFunc());

        // 根据游戏的图像模式初始化图像渲染函数指针
        Is32BitMode = (H3BitMode::Get() == 4);
        GetColor = Is32BitMode ? GetColor32 : GetColor16;

        ClearLayoutCache();

        const auto maxWidth = H3GameWidth::Get() - 64 * 2;
        if (BoxWidthMax < 0)
            BoxWidthMax = maxWidth;
        else
            BoxWidthMax = clamp(BoxWidthMax, 256, maxWidth);

        if (BoxWidthMin < 0)
            BoxWidthMin = maxWidth;
        else
            BoxWidthMin = clamp(BoxWidthMin, 256, BoxWidthMax);
    }

    // ============================================================
    // Section 8: 插件入口
    // ============================================================

    /**
     * @brief 插件初始化入口
     *
     * 执行流程：
     *   1. 获取 Patcher 实例
     *   2. 加载 H3CN.toml 配置文件（字体映射、颜色表、宽度限制）
     *   3. 注册所有 Hook（字体加载、文本渲染、布局计算）
     */
    bool Init()
    {
#ifndef NDEBUG
        MessageBoxW(H3Hwnd::Get(), L"注入成功", L"调试中", 0);
#endif

        // ---------- 获取 Patcher 实例 ----------
        _P = GetPatcher();
        _PI = _P->GetInstance("HD.Plugin.H3FontExtension");
        if (!_PI)
        {
            _PI = _P->CreateInstance("HD.Plugin.H3FontExtension");
        }
        else
        {
            _PI->UndoAll();
        }

        // ---------- 加载配置文件 ----------
        try
        {
            auto config = toml::parse_file("H3CN.toml");

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
                    const int height = (*fontCfg)["Height"].value_or(0);
                    const int width = (*fontCfg)["Width"].value_or(0);

                    // 字体名小写化（匹配规则）；跳过无效条目
                    for (char& c : name)
                        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                    if (name.empty() || height <= 0 || width <= 0)
                        continue;

                    g_ExtFontTable.insert_or_assign(name,
                        ExtFont(
                            extFontName.c_str()
                            , height
                            , width
                            , (*fontCfg)["Bold"].value_or(false)
                            , (*fontCfg)["AntiAlias"].value_or(true)
                            , (*fontCfg)["MarginLeft"].value_or(1)
                            , (*fontCfg)["MarginRight"].value_or(0)
                            , (*fontCfg)["MarginBottom"].value_or(0)
                            , (*fontCfg)["LineSpacing"].value_or(0)
                            , (*fontCfg)["DrawShadow"].value_or(true)
                        ));
                }
            }

            // 文本颜色：[General].TextColor 开关 + [TextColor] 命名颜色表
            IsTextColorEnable = config["General"]["TextColor"].value_or(true);
            TextColorMap.clear();
            if (IsTextColorEnable)
            {
                if (const auto colorNode = config["TextColor"]; colorNode.is_table())
                {
                    for (const auto& [key, value] : *colorNode.as_table())
                    {
                        // 预转换两种色深格式，渲染期直接取用
                        if (const auto c = value.value<int32_t>())
                        {
                            const DWORD rgb = static_cast<DWORD>(*c) & 0xFFFFFF;
                            TextColorMap.emplace(std::string(key), NamedColor{ rgb, RGB888toRGB565(rgb) });
                        }
                    }
                }
            }

            // 消息框宽度限制：[MessageBox]
            BoxWidthMin = config["MessageBox"]["BoxWidthMin"].value_or(0);
            BoxWidthMax = config["MessageBox"]["BoxWidthMax"].value_or(-1);
        }
        catch (const std::exception&)
        {
            MessageBoxW(H3Hwnd::Get(), L"配置文件加载失败", L"错误", 0);
        }

        // 字体表可能已重建：清空布局缓存避免引用过期字库
        ClearLayoutCache();

        // ---------- 注册 Hook ----------

        // DDraw 初始化时检测色深模式
        _PI->WriteHiHook(0x601AB0, SPLICE_, FASTCALL_, EXTENDED_, Main_DirectDrawInit_Hook);

        // 扩展字体内存分配（H3Font → H3FontExt，+8 字节）
        _PI->WriteDword(0x55B9CE + 1, sizeof(H3FontExt));

        // 字体加载后挂载扩展字库
        _PI->WriteHiHook(0x55BAE0, SPLICE_, THISCALL_, EXTENDED_, H3Font_Load_Hook);

        // 文本渲染与布局 Hook
        _PI->WriteHiHook(0x4B51F0, SPLICE_, THISCALL_, H3Font_DrawText);           // 文本绘制
        _PI->WriteHiHook(0x4B5580, SPLICE_, THISCALL_, H3Font_GetLineCount);       // 计算文本行数
        _PI->WriteHiHook(0x4B56F0, SPLICE_, THISCALL_, H3Font_GetLineWidth);       // 最长文本行宽度
        _PI->WriteHiHook(0x4B5770, SPLICE_, THISCALL_, H3Font_GetWordWidth);       // 最长单词宽度
        _PI->WriteHiHook(0x4B57E0, SPLICE_, THISCALL_, H3Font_GetLineWrapWidth);   // 最长换行后宽度
        _PI->WriteHiHook(0x4B58F0, SPLICE_, THISCALL_, H3Font_SplitTextIntoLines); // 拆分文本行

        return true;
    }

} // namespace H3FontExtension
