//===----------------------------------------------------------------------===//
// ExtFont：扩展字库实现（字形取模后端封装 + 缓存）
//===----------------------------------------------------------------------===//

#include "ExtFont.h"

#include <Windows.h>
#include <cstring>

namespace H3FontExtension
{
    /**
     * @brief GBK 双字节 → wchar_t 全局查表缓存
     *
     * 索引为 (区码 << 8) | 位码，0 表示尚未转换。
     * 拆行、测量、绘制阶段反复查询同一字符时避免重复调用 MultiByteToWideChar。
     */
    static wchar_t g_GbkWideTable[0x10000];

    /**
     * @brief wchar_t → 是否 GBK 可表示 全局查表（0 = 不可表示）
     *
     * 初始化时对 GBK 全码位（0x8140-0xFEFE，约 2.4 万）做一次
     * MultiByteToWideChar 扫描，得到精确的 GBK 可表示字符集。
     * 绘制阶段 O(1) 查表替代区间判断。
     */
    static uint8_t g_IsGbkCharTable[0x10000];

    void ExtFont::InitGbkTables()
    {
        ::memset(g_IsGbkCharTable, 0, sizeof(g_IsGbkCharTable));

        // GBK 单字节区：ASCII 可打印字符
        for (int c = 0x20; c <= 0x7E; ++c)
            g_IsGbkCharTable[c] = 1;

        // GBK 双字节区全扫描
        char gbk[2];
        for (int section = DBCS_SECTION; section <= 0xFE; ++section)
        {
            for (int position = DBCS_POSITION; position <= 0xFE; ++position)
            {
                if (position == 0x7F)
                    continue;
                gbk[0] = static_cast<char>(section);
                gbk[1] = static_cast<char>(position);
                wchar_t wch = 0;
                if (::MultiByteToWideChar(936, 0, gbk, 2, &wch, 1) == 1)
                    g_IsGbkCharTable[wch] = 1;
            }
        }
    }

    wchar_t ExtFont::GbkToWchar(uint8_t section, uint8_t position)
    {
        wchar_t& cached = g_GbkWideTable[(section << 8) | position];
        if (cached)
            return cached;

        char gbk[3] = { static_cast<char>(section), static_cast<char>(position), 0 };
        wchar_t wch = L'?';
        if (::MultiByteToWideChar(936, 0, gbk, 2, &wch, 1) <= 0) // CP936 = GBK
            wch = L'?';
        cached = wch;
        return cached;
    }

    bool ExtFont::IsGbkChar(wchar_t wch)
    {
        return g_IsGbkCharTable[wch] != 0;
    }

    uint8_t* ExtFont::GlyphPool::Allocate()
    {
        if (glyphBytes_ == 0)
            return nullptr;

        if (chunks_.empty() || used_ + glyphBytes_ > ChunkBytes)
        {
            const std::size_t bytes = (glyphBytes_ > ChunkBytes) ? glyphBytes_ : ChunkBytes;
            chunks_.push_back(std::make_unique<uint8_t[]>(bytes));
            used_ = 0;
        }

        uint8_t* result = chunks_.back().get() + used_;
        used_ += glyphBytes_;
        return result;
    }

    ExtFont::ExtFont(const char* lpFileName, int iHeight, int iWidth, bool bBold, bool bAntiAlias,
        int iMarginLeft, int iMarginRight, int iMarginBottom, int iLineSpacing, bool bDrawShadow,
        bool bOriginalAscii)
    {
        Load(lpFileName, iHeight, iWidth, bBold, bAntiAlias,
            iMarginLeft, iMarginRight, iMarginBottom, iLineSpacing, bDrawShadow, bOriginalAscii);
    }

    bool ExtFont::Load(const char* fontName, int iHeight, int iWidth, bool bBold, bool bAntiAlias,
        int iMarginLeft, int iMarginRight, int iMarginBottom, int iLineSpacing, bool bDrawShadow,
        bool bOriginalAscii)
    {
        this->Height = static_cast<int8_t>(iHeight); // 核心字形高度（不含底部边距）
        this->Width = iWidth;
        this->Bold = bBold;
        this->AntiAlias = bAntiAlias;
        this->MarginLeft = iMarginLeft;
        this->MarginRight = iMarginRight;
        this->MarginBottom = iMarginBottom;
        this->LineSpacing = iLineSpacing;
        this->DrawShadow = bDrawShadow;
        this->OriginalAscii = bOriginalAscii;
        this->GlyphWidth = iMarginLeft + iWidth + iMarginRight;

        const int bufferWidth = GlyphWidth;
        const int bufferHeight = iHeight + iMarginBottom;

        GlyphRasterizerConfig config;
        config.fontName = fontName;
        config.heightPx = iHeight;
        config.bold = bBold;
        config.antiAlias = bAntiAlias;
        config.marginLeft = iMarginLeft;
        config.marginRight = iMarginRight;
        config.marginBottom = iMarginBottom;
        config.bufferWidth = bufferWidth;
        config.bufferHeight = bufferHeight;

        // 字形取模后端：DirectWrite 优先，初始化失败回退 GDI
        rasterizer_ = CreateGlyphRasterizer();
        if (!rasterizer_ || !rasterizer_->Initialize(config))
        {
            rasterizer_ = CreateGdiGlyphRasterizer();
            if (!rasterizer_ || !rasterizer_->Initialize(config))
            {
                rasterizer_.reset();
                return false;
            }
        }

        pool_.Init(static_cast<std::size_t>(bufferWidth) * bufferHeight);
        glyphCache_ = std::make_unique<uint8_t*[]>(0x10000);  // 值初始化 = nullptr
        widthCache_ = std::make_unique<int16_t[]>(0x10000);   // 值初始化 = 0

        // 预缓存常用 ASCII 字形及宽度（原版位图模式下 ASCII 不经后端，无需预热）
        if (!bOriginalAscii)
        {
            for (wchar_t c = L' '; c <= L'~'; ++c)
            {
                GetGlyphAlpha(c);
                GetAdvanceWidth(c);
            }
        }

        return true;
    }

    const uint8_t* ExtFont::GetGlyphAlpha(wchar_t ch)
    {
        if (!rasterizer_ || !glyphCache_)
            return nullptr;

        // 扁平表直接命中
        uint8_t*& slot = glyphCache_[static_cast<uint16_t>(ch)];
        if (slot)
            return slot;

        // 按需渲染（从字形池分配，地址稳定、无单独堆分配）
        uint8_t* glyph = pool_.Allocate();
        if (!glyph || !rasterizer_->RenderGlyph(ch, glyph))
            return nullptr;

        slot = glyph;
        return slot;
    }

    int ExtFont::GetAdvanceWidth(wchar_t ch)
    {
        if (!rasterizer_ || !widthCache_)
            return GlyphWidth; // 回退

        const int16_t cached = widthCache_[static_cast<uint16_t>(ch)];
        if (cached != 0)
            return cached;

        const int w = rasterizer_->GetAdvanceWidth(ch);
        widthCache_[static_cast<uint16_t>(ch)] = static_cast<int16_t>(w);
        return w;
    }

} // namespace H3FontExtension
