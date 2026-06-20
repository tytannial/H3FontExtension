#include "H3FontExtension.h"

using namespace h3;
using namespace std;

namespace H3FontExtension
{

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

    /** @brief 释放 GDI 资源 */
    ExtFont::~ExtFont()
    {
        if (hGlyphFont)
        {
            DeleteObject(hGlyphFont);
            hGlyphFont = nullptr;
        }
        if (hbmGlyph)
        {
            DeleteObject(hbmGlyph);
            hbmGlyph = nullptr;
        }
        if (hdcGlyph)
        {
            DeleteDC(hdcGlyph);
            hdcGlyph = nullptr;
        }
        glyphCache.clear();
        widthCache.clear();
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
        , glyphCache(std::move(other.glyphCache))
        , widthCache(std::move(other.widthCache))
    {
        other.hdcGlyph = nullptr;
        other.hbmGlyph = nullptr;
        other.pGlyphBits = nullptr;
        other.hGlyphFont = nullptr;
    }

    /** @brief 移动赋值：转移 GDI 资源所有权 */
    ExtFont& ExtFont::operator=(ExtFont&& other) noexcept
    {
        if (this != &other)
        {
            // 先释放当前资源
            if (hGlyphFont) { DeleteObject(hGlyphFont); }
            if (hbmGlyph) { DeleteObject(hbmGlyph); }
            if (hdcGlyph) { DeleteDC(hdcGlyph); }

            // 转移数据
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
        hbmGlyph = CreateDIBSection(hdcGlyph, &bmi, DIB_RGB_COLORS, &pGlyphBits, nullptr, 0);
        ReleaseDC(nullptr, hdcScreen);

        if (!hbmGlyph)
        {
            return false;
        }

        SelectObject(hdcGlyph, hbmGlyph);
        SetBkMode(hdcGlyph, TRANSPARENT);
        SetTextAlign(hdcGlyph, TA_TOP | TA_LEFT);

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

        SelectObject(hdcGlyph, hGlyphFont);

        // 预缓存常用 ASCII 字形及宽度
        for (wchar_t c = L' '; c <= L'~'; ++c)
        {
            GetGlyphRGBA(c);
            GetCharWidth(c);
        }

        return false; // 游戏引擎约定
    }

    /**
     * @brief GBK 区码/位码 → wchar_t
     */
    wchar_t ExtFont::GbkToWchar(uint8_t section, uint8_t position)
    {
        char gbk[3] = { (char)section, (char)position, 0 };
        wchar_t wch = L'?';
        MultiByteToWideChar(936, 0, gbk, 2, &wch, 1); // CP936 = GBK
        return wch;
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
     * @brief 获取字形的抗锯齿 RGBA 数据（按需 GDI 渲染 + 缓存）
     *
     * 渲染流程：
     *   1. 在内存 DC 上以白色文字 + 黑色背景绘制单个字符
     *   2. 读取每个像素的 ClearType 子像素颜色
     *   3. 取 R 通道值作为 alpha（ClearType 灰度字体时 R=G=B），写入缓存
     *
     * @return GlyphWidth×Height×4 字节 RGBA 数据指针，失败返回 nullptr
     */
    const uint8_t* ExtFont::GetGlyphRGBA(wchar_t ch) const
    {
        if (!hdcGlyph)
            return nullptr;

        // 检查缓存
        auto it = glyphCache.find(ch);
        if (it != glyphCache.end())
            return it->second.data();

        // 按需渲染
        const int bufW = GlyphWidth;
        const int bufH = Height + MarginBottom;  // 有效缓冲区高度（含底部扩展空间）
        const size_t imgSize = bufW * bufH * 4;
        std::vector<uint8_t> pixels(imgSize, 0);

        // 清除背景（黑色）
        RECT rc = { 0, 0, bufW, bufH };
        HBRUSH hbrBlack = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdcGlyph, &rc, hbrBlack);
        DeleteObject(hbrBlack);

        // 以白色绘制字符（ClearType 会生成彩色子像素）
        SetTextColor(hdcGlyph, RGB(255, 255, 255));
        TextOutW(hdcGlyph, MarginLeft, 0, &ch, 1);
        GdiFlush();

        // 提取 RGBA：遍历整个 DIB，R 通道值作为 alpha
        const uint8_t* src = (const uint8_t*)pGlyphBits;
        for (int i = 0; i < bufW * bufH; ++i)
        {
            const int srcOff = i * 4;
            const uint8_t r = src[srcOff + 2];
            const uint8_t g = src[srcOff + 1];
            const uint8_t b = src[srcOff];

            const int dstOff = i * 4;
            pixels[dstOff] = r;
            pixels[dstOff + 1] = g;
            pixels[dstOff + 2] = b;
            pixels[dstOff + 3] = r; // 灰度字体下 R=G=B，取 R 作为 alpha
        }

        glyphCache[ch] = std::move(pixels);
        return glyphCache[ch].data();
    }

    /**
     * @brief 获取单个字符的 GDI 渲染像素宽度（含左右边距）
     *
     * 使用 GetTextExtentPoint32W 测量字符在 GDI 字体下的实际宽度，
     * 结果取整后加上 MarginLeft + MarginRight 并缓存。
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
        widthCache[ch] = w;
        return w;
    }

    // ============================================================
    // Section 2: 渲染后端（色深适配）
    // ============================================================
    //
    // 游戏支持 16 位（RGB565）和 32 位（RGB888）两种色深模式。
    // 通过函数指针在 DDraw 初始化时动态绑定对应实现。

    /** @brief 当前是否为 32 位色深模式 */
    static bool Is32BitMode = false;

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

    /** @brief 色深自适应：获取调色板颜色的函数指针 */
    DWORD(__fastcall* GetColor)(const H3BasePalette565& palette, int colorIdx);

    /** @brief 32 位色深：alpha 混合前景色（读 bg → 混合 fgColor → 写 R8G8B8） */
    static void __fastcall BlendPixel32(const PUINT8 rowBuf, int col, DWORD fgColor, uint8_t alpha)
    {
        const DWORD bg = *((DWORD*)rowBuf + col);
        const uint8_t br = (bg >> 16) & 0xFF, bgg = (bg >> 8) & 0xFF, bb = bg & 0xFF;
        const uint8_t fr = (fgColor >> 16) & 0xFF, fgg = (fgColor >> 8) & 0xFF, fb = fgColor & 0xFF;
        *((DWORD*)rowBuf + col) =
            ((((fr * alpha + br * (255 - alpha)) / 255) << 16) |
             (((fgg * alpha + bgg * (255 - alpha)) / 255) << 8) |
             (((fb * alpha + bb * (255 - alpha)) / 255)));
    }

    /** @brief 16 位色深：alpha 混合前景色（读 RGB565 → 展开 8-bit 通道混合 → 写回 RGB565） */
    static void __fastcall BlendPixel16(const PUINT8 rowBuf, int col, DWORD fgColor, uint8_t alpha)
    {
        const DWORD bg = *((WORD*)rowBuf + col);
        const uint8_t br = ((bg >> 11) & 0x1F) << 3;
        const uint8_t bgg = ((bg >> 5) & 0x3F) << 2;
        const uint8_t bb = (bg & 0x1F) << 3;
        const uint8_t fr = ((fgColor >> 11) & 0x1F) << 3;
        const uint8_t fgg = ((fgColor >> 5) & 0x3F) << 2;
        const uint8_t fb = (fgColor & 0x1F) << 3;
        *((WORD*)rowBuf + col) = (WORD)(
            (((((fr * alpha + br * (255 - alpha)) / 255) >> 3) << 11) |
             ((((fgg * alpha + bgg * (255 - alpha)) / 255) >> 2) << 5) |
             (((fb * alpha + bb * (255 - alpha)) / 255) >> 3)));
    }

    /** @brief 色深自适应：alpha 混合前景色的函数指针 */
    void(__fastcall* BlendPixel)(const PUINT8 rowBuf, int col, DWORD fgColor, uint8_t alpha);

    /** @brief 32 位色深：alpha 混合阴影色 */
    static void __fastcall BlendShadow32(const PUINT8 rowBuf, int col, DWORD shadowColor, uint8_t alpha)
    {
        const DWORD bg = *((DWORD*)rowBuf + col);
        const uint8_t sr = (shadowColor >> 16) & 0xFF, sg = (shadowColor >> 8) & 0xFF, sb = shadowColor & 0xFF;
        const uint8_t br = (bg >> 16) & 0xFF, bgg = (bg >> 8) & 0xFF, bb = bg & 0xFF;
        *((DWORD*)rowBuf + col) =
            ((((sr * alpha + br * (255 - alpha)) / 255) << 16) |
             (((sg * alpha + bgg * (255 - alpha)) / 255) << 8) |
             (((sb * alpha + bb * (255 - alpha)) / 255)));
    }

    /** @brief 16 位色深：阴影直接写入（不混合 alpha，保持原风格） */
    static void __fastcall BlendShadow16(const PUINT8 rowBuf, int col, DWORD shadowColor, uint8_t)
    {
        *((WORD*)rowBuf + col) = (WORD)shadowColor;
    }

    /** @brief 色深自适应：alpha 混合阴影色的函数指针 */
    void(__fastcall* BlendShadow)(const PUINT8 rowBuf, int col, DWORD shadowColor, uint8_t alpha);

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
                                newColor = (ec == std::errc()) ? c : defaultColor;
                            }
                            else
                            {
                                std::string key(raw.data() + codeStart, i - codeStart);
                                newColor = TextColorMap[key].value_or(defaultColor);
                                if (newColor != defaultColor && !is32bit)
                                    newColor = RGB888toRGB565(newColor);
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
     * @param pFont     ASCII 字体指针
     * @param szText    输入文本（以 '\0' 结尾）
     * @param iBoxWidth 文本框像素宽度
     * @param lines     [out] 拆分结果（含颜色码的原始行）
     */
     /**
      * @brief 扫描行缓冲，提取末尾活跃的颜色状态（用于跨行继承）
      *
      * 当拆行推送 lineBuf 后，下一行需从头继承前行的颜色（{~ColorName} 或 { 高亮）。
      * 本函数遍历整个 buffer，追踪 {~...} / { / } 的状态变化，返回最终活跃状态。
      */
    static void GetActiveColorState(const std::string& buf, std::string& outTag, bool& outHighlight)
    {
        outTag.clear();
        outHighlight = false;

        const char* p = buf.c_str();
        const char* const end = p + buf.size();

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
                if (IsTextColorEnable)
                {
                    const char* start = p;
                    p += 2;
                    while (p < end && *p != '}')
                        ++p;
                    if (p < end && *p == '}')
                        ++p;
                    outTag.assign(start, p - start);
                    outHighlight = false;
                }
                else
                {
                    p += 2;
                    while (p < end && *p != '}')
                        ++p;
                    if (p < end && *p == '}')
                        ++p;
                    outHighlight = true;
                    outTag.clear();
                }
            }
            else if (*p == '{')
            {
                outHighlight = true;
                outTag.clear();
                ++p;
            }
            else if (*p == '}')
            {
                outHighlight = false;
                outTag.clear();
                ++p;
            }
            else
            {
                ++p;
            }
        }
    }

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

        // 推送当前行并继承颜色状态到下一行
        auto pushAndInheritLine = [&]()
            {
                std::string activeTag;
                bool highlightActive = false;
                GetActiveColorState(lineBuf, activeTag, highlightActive);

                lines.push_back({ std::move(lineBuf), lineWidth });
                lineBuf.clear();
                lineWidth = 0;

                if (!activeTag.empty())
                    lineBuf = activeTag;
                else if (highlightActive)
                    lineBuf.push_back('{');
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
                    lines.push_back({ std::move(lineBuf), lineWidth });
                    lineBuf.clear();
                    lineWidth = 0;
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
                szText = (realEnd > wordStart) ? realEnd : (wordStart + 1);
                continue;
            }

            // ---- 阶段 3：拆行判定 ----
            if (lineWidth + wordWidth + blankWidth > iBoxWidth)
            {
                if (lineWidth > 0)
                    pushAndInheritLine();
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
                            if (code == '{' && (p + 1 < realEnd) && *(p + 1) == '~')
                            {
                                const char* tagStart = p;
                                p += 2; // 跳过 "{~"
                                while (p < realEnd && *p != '}')
                                    ++p;
                                if (p < realEnd && *p == '}') // 跳过 '}'
                                    ++p;
                                lineBuf.append(tagStart, p - tagStart);
                                continue;
                            }
                            // 单个 { 或 }
                            lineBuf.push_back(*p);
                            ++p;
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
                            pushAndInheritLine();

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
            szText = realEnd;
        }

        // 输出最后一行
        if (lineWidth > 0 || !lineBuf.empty())
        {
            lines.push_back({ std::move(lineBuf), lineWidth });
        }
    }

    // ============================================================
    // Section 4: 字符 & 文本渲染
    // ============================================================

    /**
     * @brief 绘制单个字符到 PCX 缓冲区（统一 GDI 渲染）
     *
     * 所有字符（ASCII 和 GBK 汉字）均通过 GDI 实时渲染 + alpha 混合。
     * cLoCode == 0 表示 ASCII 单字节字符，cLoCode != 0 表示双字节 GBK 字符。
     *
     * @param pFont       字体指针
     * @param pOutputPcx  目标 PCX 图像缓冲区
     * @param cHiCode     字符编码高字节（ASCII 时为字节码，GBK 时为区码）
     * @param cLoCode     字符编码低字节（0 = ASCII，非 0 = GBK 位码）
     * @param iX          绘制起点 X 坐标
     * @param iY          绘制起点 Y 坐标
     * @param uFontColor  前景色（已转换为当前色深格式）
     * @return 始终返回 true
     */
    static bool __fastcall H3Font_DrawChar(H3FontExt* pFont, H3LoadedPcx16* pOutputPcx, uint8_t cHiCode,
        uint8_t cLoCode, int iX, int iY, DWORD uFontColor)
    {
        auto cFont = pFont->ExtData;

        // 将字节序列转换为 wchar_t
        wchar_t wch;
        if (cLoCode == 0)
        {
            // ASCII 单字节字符：直接转换
            wch = (wchar_t)cHiCode;
        }
        else
        {
            // GBK 双字节字符：通过 CP936 转换
            wch = ExtFont::GbkToWchar(cHiCode, cLoCode);
        }

        // 非 GBK 字符 → 跳过（避免 GDI 渲染为方块）
        if (!ExtFont::IsGbkChar(wch))
            return false;

        const uint8_t* glyph = cFont->GetGlyphRGBA(wch);
        if (!glyph)
            return false;

        const int glyphW = cFont->GlyphWidth;
        const int glyphH = cFont->EffectiveHeight();  // 含底部扩展空间

        for (int rowIdx = 0; rowIdx < glyphH; ++rowIdx)
        {
            PUINT8 rowBuf = pOutputPcx->GetRow(iY + rowIdx);
            PUINT8 shadowRowBuf = cFont->DrawShadow ? pOutputPcx->GetRow(iY + rowIdx + 1) : nullptr;

            for (int colIdx = 0; colIdx < glyphW; ++colIdx)
            {
                const int off = (rowIdx * glyphW + colIdx) * 4;
                const uint8_t alpha = glyph[off + 3];
                if (alpha == 0)
                    continue;

                const int px = iX + colIdx;

                // Alpha 混合前景色（读 bg → 混合 fgColor → 写入）
                BlendPixel(rowBuf, px, uFontColor, alpha);

                // Alpha 混合阴影（右下偏移 1 像素）
                if (shadowRowBuf)
                    BlendShadow(shadowRowBuf, px + 1, ShadowColor, alpha);
            }
        }

        return true;
    }

    /**
     * @brief 在指定矩形区域内绘制多行文本
     *
     * 完整渲染管线：
     *   1. SplitTextIntoLines  → 拆行
     *   2. 计算垂直对齐偏移
     *   3. PreprocessLine      → 逐行剥离颜色码
     *   4. 逐行逐字符绘制（含颜色切换）
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
        if (!*szText || iBoxWidth == 0)
            return;

        // ========== 阶段一：拆行 ==========
        vector<TextLineStruct> textLines;
        SplitTextIntoLines(pFont, szText, iBoxWidth, textLines);
        if (textLines.empty())
            return;

        const int lineCount = static_cast<int>(textLines.size());

        // ========== 阶段二：缓存字体度量 ==========
        auto* extFont = pFont->ExtData;
        const int fontHeight = pFont->height;
        const int effHeight = extFont->EffectiveHeight(); // 含底部边距的有效字形高度

        // 统一垂直偏移：在行高内居中有效字形区域
        const int shift = (fontHeight - effHeight) / 2;

        // ========== 阶段三：解析颜色 ==========
        const uint32_t ci = (uColorIdx & 0x100) ? (uColorIdx & 0xFE) : (uColorIdx + 9);
        const DWORD defaultColor = GetColor(pFont->palette, ci);
        const DWORD highlightColor = GetColor(pFont->palette, ci + 1);
        const bool is32bit = (H3BitMode::Get() == 4);

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

        // ========== 阶段五：预处理所有行（剥离颜色码 + 预解析颜色）==========
        std::vector<CleanLine> cleanLines;
        cleanLines.reserve(lineCount);

        for (int i = 0; i < lineCount; ++i)
        {
            cleanLines.emplace_back();
            PreprocessLine(textLines[i], defaultColor, highlightColor, is32bit, cleanLines.back());
        }

        // ========== 阶段六：逐行绘制（统一 GDI 渲染，不区分 ASCII/DBCS）==========
        const int bottomBound = iY + iBoxHeight;

        for (int rowIdx = 0; rowIdx < lineCount; ++rowIdx)
        {
            const CleanLine& line = cleanLines[rowIdx];
            if (line.lineWidth == 0)
                continue;

            const int lineY = iY + startY + rowIdx * fontHeight;
            if (lineY + pFont->oriHeight > bottomBound)
                break;

            // 水平对齐
            int startX = 0;
            switch (uAlignFlags)
            {
            case 1: startX = (iBoxWidth - line.lineWidth) / 2; break; // 居中
            case 2: startX = iBoxWidth - line.lineWidth;         break; // 右对齐
            }

            int curX = iX + startX;
            const char* const textData = line.text.data();
            const char* p = textData;
            const char* const end = p + line.text.size();

            // 颜色跟踪
            DWORD curColor = defaultColor;
            size_t stopIdx = 0;
            const size_t stopCount = line.stops.size();

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
                    H3Font_DrawChar(pFont, pPcx, code, 0,
                        curX, lineY + shift, curColor);
                    curX += extFont->GetCharWidth((wchar_t)code);
                    ++p;
                }
                else
                {
                    uint8_t nextCode = (end - p >= 2) ? static_cast<uint8_t>(p[1]) : 0;
                    if (IsDBCSLeadByte(code, nextCode))
                    {
                        H3Font_DrawChar(pFont, pPcx, code, nextCode,
                            curX, lineY + shift, curColor);
                        curX += extFont->GetCharWidth(ExtFont::GbkToWchar(code, nextCode));
                        p += 2;
                    }
                    else
                    {
                        // 无效双字节，当单字节处理
                        H3Font_DrawChar(pFont, pPcx, code, 0,
                            curX, lineY + shift, curColor);
                        curX += extFont->GetCharWidth((wchar_t)code);
                        ++p;
                    }
                }
            }
        }
    }

    // ============================================================
    // Section 5: Hook 包装函数
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

        if (!szText || !*szText)
            return;

        vector<TextLineStruct> vlines;
        SplitTextIntoLines(pFont, szText, iBoxWidth, vlines);
        for (auto& line : vlines)
        {
            lines.Add(H3String(std::move(line.Text).c_str()));
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
        if (!szText || !*szText)
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
        if (!szText || !*szText)
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
        if (!szText || !*szText)
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
        if (!szText || !*szText)
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
    // Section 6: 生命周期 Hook
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
        font->ExtData = &g_ExtFontTable[fntName];
        if (!font->ExtData)
        {
            font->ExtData = &g_ExtFontTable.at("medfont.fnt");
        }
        font->oriHeight = font->height;
        // 行高 = max(原始高度, 有效字形高度 + 行间距)
        font->height = std::max((int)font->height, font->ExtData->EffectiveHeight() + font->ExtData->LineSpacing);
        return font;
    }

    /**
     * @brief Hook: DirectDraw 初始化后配置渲染后端
     *
     * 根据游戏色深模式（16/32 位）绑定对应的 GetColor / DrawPixcel 实现。
     * 同时根据屏幕分辨率计算文本框宽度限制。
     */
    static void __stdcall Main_DirectDrawInit_Hook(HiHook* h)
    {
        FASTCALL_0(void, h->GetDefaultFunc());

        // 根据游戏的图像模式初始化图像渲染函数指针
        Is32BitMode = (H3BitMode::Get() == 4);
        if (Is32BitMode)
        {
            GetColor = GetColor32;
            BlendPixel = BlendPixel32;
            BlendShadow = BlendShadow32;
        }
        else
        {
            GetColor = GetColor16;
            BlendPixel = BlendPixel16;
            BlendShadow = BlendShadow16;
        }

        auto maxWidth = H3GameWidth::Get() - 64 * 2;
        BoxWidthMax < 0 ? BoxWidthMax = maxWidth : BoxWidthMax = clamp(BoxWidthMax, 256, maxWidth);
        BoxWidthMin < 0 ? BoxWidthMin = maxWidth : BoxWidthMin = clamp(BoxWidthMin, 256, BoxWidthMax);
    }

    // ============================================================
    // Section 7: 插件入口
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
            toml::array fontArr = *config["Fonts"].as_array();
            for (const auto& item : fontArr)
            {
                const auto& fontCfg = item.as_table();
                g_ExtFontTable[_strlwr((char*)fontCfg->get("Name")->value_or(""))] =
                    ExtFont(
                        fontCfg->get("ExtFont")->value_or("")
                        , fontCfg->get("Height")->value_or(0)
                        , fontCfg->get("Width")->value_or(0)
                        , fontCfg->get("Bold")->value_or(false)
                        , fontCfg->get("AntiAlias")->value_or(true)
                        , fontCfg->get("MarginLeft")->value_or(1)
                        , fontCfg->get("MarginRight")->value_or(0)
                        , fontCfg->get("MarginBottom")->value_or(0)
                        , fontCfg->get("LineSpacing")->value_or(0)
                        , fontCfg->get("DrawShadow")->value_or(true)
                    );
            }

            // 文本颜色：[General].TextColor 开关 + [TextColor] 命名颜色表
            IsTextColorEnable = config["General"]["TextColor"].value_or(true);
            if (IsTextColorEnable)
            {
                TextColorMap = *config["TextColor"].as_table();
            }

            // 消息框宽度限制：[MessageBox]
            BoxWidthMin = config["MessageBox"]["BoxWidthMin"].value_or(0);
            BoxWidthMax = config["MessageBox"]["BoxWidthMax"].value_or(-1);
        }
        catch (const std::exception&)
        {
            MessageBoxW(H3Hwnd::Get(), L"配置文件加载失败", L"错误", 0);
        }

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
