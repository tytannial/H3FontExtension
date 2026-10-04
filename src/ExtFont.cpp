//===----------------------------------------------------------------------===//
// ExtFont：扩展字库实现（字形取模后端封装 + 缓存）
//===----------------------------------------------------------------------===//

#include "ExtFont.h"

#include "Log.h"

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
    static wchar_t gGbkWideTable[0x10000];

    /**
     * @brief wchar_t → 是否 GBK 可表示 全局查表（0 = 不可表示）
     *
     * 初始化时对 GBK 全码位（0x8140-0xFEFE，约 2.4 万）做一次
     * MultiByteToWideChar 扫描，得到精确的 GBK 可表示字符集。
     * 绘制阶段 O(1) 查表替代区间判断。
     */
    static uint8_t gGbkCharTable[0x10000];

    void ExtFont::InitGbkTables()
    {
        ::memset(gGbkCharTable, 0, sizeof(gGbkCharTable));

        // GBK 单字节区：ASCII 可打印字符
        for (int c = 0x20; c <= 0x7E; ++c)
            gGbkCharTable[c] = 1;

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
                    gGbkCharTable[wch] = 1;
            }
        }
    }

    wchar_t ExtFont::GbkToWchar(uint8_t section, uint8_t position)
    {
        wchar_t& cached = gGbkWideTable[(section << 8) | position];
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
        return gGbkCharTable[wch] != 0;
    }

    uint8_t* ExtFont::GlyphPool::Allocate()
    {
        if (glyph_bytes == 0)
            return nullptr;

        if (chunks.empty() || used + glyph_bytes > ChunkBytes)
        {
            const std::size_t bytes = (glyph_bytes > ChunkBytes) ? glyph_bytes : ChunkBytes;
            chunks.push_back(std::make_unique<uint8_t[]>(bytes));
            used = 0;
        }

        uint8_t* result = chunks.back().get() + used;
        used += glyph_bytes;
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
        this->height = static_cast<int8_t>(iHeight); // 核心字形高度（不含底部边距）
        this->width = iWidth;
        this->bold = bBold;
        this->anti_alias = bAntiAlias;
        this->margin_left = iMarginLeft;
        this->margin_right = iMarginRight;
        this->margin_bottom = iMarginBottom;
        this->line_spacing = iLineSpacing;
        this->draw_shadow = bDrawShadow;
        this->original_ascii = bOriginalAscii;
        this->glyph_width = iMarginLeft + iWidth + iMarginRight;

        const int bufferWidth = glyph_width;
        const int bufferHeight = iHeight + iMarginBottom;

        GlyphRasterizerConfig config;
        config.font_name = fontName;
        config.height_px = iHeight;
        config.bold = bBold;
        config.anti_alias = bAntiAlias;
        config.margin_left = iMarginLeft;
        config.margin_right = iMarginRight;
        config.margin_bottom = iMarginBottom;
        config.buffer_width = bufferWidth;
        config.buffer_height = bufferHeight;

        // 字形取模后端：DirectWrite 优先，初始化失败回退 GDI
        rasterizer = createGlyphRasterizer();
        if (!rasterizer || !rasterizer->Initialize(config))
        {
            logInfo("DirectWrite 字形后端不可用，回退 GDI（字体 %s）", fontName);
            rasterizer = createGdiGlyphRasterizer();
            if (!rasterizer || !rasterizer->Initialize(config))
            {
                logError("字形后端初始化失败（字体 %s，%d×%d）", fontName, iWidth, iHeight);
                rasterizer.reset();
                return false;
            }
        }

        pool.Init(static_cast<std::size_t>(bufferWidth) * bufferHeight);
        glyph_cache = std::make_unique<uint8_t*[]>(0x10000);  // 值初始化 = nullptr
        shadow_cache = std::make_unique<uint8_t*[]>(0x10000); // 值初始化 = nullptr
        width_cache = std::make_unique<int16_t[]>(0x10000);   // 值初始化 = 0

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
        if (!rasterizer || !glyph_cache)
            return nullptr;

        // 扁平表直接命中
        uint8_t*& slot = glyph_cache[static_cast<uint16_t>(ch)];
        if (slot)
            return slot;

        // 按需渲染（从字形池分配，地址稳定、无单独堆分配）
        uint8_t* glyph = pool.Allocate();
        if (!glyph || !rasterizer->RenderGlyph(ch, glyph))
            return nullptr;

        slot = glyph;
        return slot;
    }

    int ExtFont::GetAdvanceWidth(wchar_t ch)
    {
        if (!rasterizer || !width_cache)
            return glyph_width; // 回退

        const int16_t cached = width_cache[static_cast<uint16_t>(ch)];
        if (cached != 0)
            return cached;

        const int w = rasterizer->GetAdvanceWidth(ch);
        width_cache[static_cast<uint16_t>(ch)] = static_cast<int16_t>(w);
        return w;
    }

    const uint8_t* ExtFont::GetGlyphShadow(wchar_t ch)
    {
        if (!rasterizer || !shadow_cache)
            return nullptr;

        uint8_t*& slot = shadow_cache[static_cast<uint16_t>(ch)];
        if (slot)
            return slot;

        const uint8_t* alpha = GetGlyphAlpha(ch);
        if (!alpha)
            return nullptr;

        uint8_t* shadow = pool.Allocate();
        if (!shadow)
            return nullptr;

        // 预合成阴影掩膜：alpha 非零且右下方无覆盖（重叠）的位置保留覆盖度
        const int glyphW = glyph_width;
        const int glyphH = EffectiveHeight();
        for (int y = 0; y < glyphH; ++y)
        {
            const uint8_t* srcRow = alpha + static_cast<std::size_t>(y) * glyphW;
            uint8_t* dstRow = shadow + static_cast<std::size_t>(y) * glyphW;
            const uint8_t* belowRow = (y + 1 < glyphH) ? srcRow + glyphW : nullptr;

            for (int x = 0; x < glyphW; ++x)
            {
                const uint8_t a = srcRow[x];
                const bool overlapped = belowRow && (x + 1 < glyphW) && belowRow[x + 1];
                dstRow[x] = (a != 0 && !overlapped) ? a : 0;
            }
        }

        slot = shadow;
        return slot;
    }

} // namespace H3FontExtension
