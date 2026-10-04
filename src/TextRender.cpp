//===----------------------------------------------------------------------===//
// 文本渲染：像素混合（16/32 位双路径）、字形绘制、DrawBoundedString Hook
//
// 布局与测量见 TextLayout.cpp，插件入口见 H3FontExtension.cpp。
//===----------------------------------------------------------------------===//

#include "H3FontExtensionInternal.h"

#include <cstring>

namespace H3FontExtension
{
    // ============================================================
    // Section 2: 像素混合（色深模板化）
    // ============================================================
    /**
     * @brief 精确除以 255（避免整数除法指令）
     *
     * 对 0 ≤ x ≤ 255×255 恒有 floor(x/255) == (x + (x>>8) + 1) >> 8。
     * 混合运算 fg*a + bg*(255-a) 是凸组合，最大值 255×255，在精确域内。
     */
    static inline uint8_t div255(uint32_t x)
    {
        return static_cast<uint8_t>((x + (x >> 8) + 1) >> 8);
    }

    /** @brief alpha 混合前景色（Is32=true: RGB888，false: RGB565） */
    template<bool Is32>
    static inline void blendPixel(uint8_t* rowBuf, int col, DWORD fgColor, uint8_t alpha)
    {
        // 快速路径：完全不透明像素直接写入（非抗锯齿字形的绝大多数像素）
        if (alpha == 255)
        {
            if constexpr (Is32)
                *(reinterpret_cast<DWORD*>(rowBuf) + col) = fgColor;
            else
                *(reinterpret_cast<WORD*>(rowBuf) + col) = static_cast<WORD>(fgColor);
            return;
        }

        if constexpr (Is32)
        {
            DWORD* dst = reinterpret_cast<DWORD*>(rowBuf) + col;
            const DWORD bg = *dst;
            const uint32_t inv = 255u - alpha;
            const uint32_t r = div255(((fgColor >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * inv);
            const uint32_t g = div255(((fgColor >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * inv);
            const uint32_t b = div255((fgColor & 0xFF) * alpha + (bg & 0xFF) * inv);
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
            const uint32_t r = div255(fr * alpha + br * inv);
            const uint32_t g = div255(fgg * alpha + bgg * inv);
            const uint32_t b = div255(fb * alpha + bb * inv);
            *dst = static_cast<WORD>((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        }
    }

    /** @brief alpha 混合阴影色（Is32=true: RGB888 混合，false: 直写保持原风格） */
    template<bool Is32>
    static inline void blendShadow(uint8_t* rowBuf, int col, DWORD shadowColor, uint8_t alpha)
    {
        if constexpr (Is32)
        {
            // 快速路径：完全不透明像素直接写入
            if (alpha == 255)
            {
                *(reinterpret_cast<DWORD*>(rowBuf) + col) = shadowColor;
                return;
            }

            DWORD* dst = reinterpret_cast<DWORD*>(rowBuf) + col;
            const DWORD bg = *dst;
            const uint32_t inv = 255u - alpha;
            const uint32_t r = div255(((shadowColor >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * inv);
            const uint32_t g = div255(((shadowColor >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * inv);
            const uint32_t b = div255((shadowColor & 0xFF) * alpha + (bg & 0xFF) * inv);
            *dst = (r << 16) | (g << 8) | b;
        }
        else
        {
            // 16 位色深：阴影直接写入（不混合 alpha，保持原风格）
            *(reinterpret_cast<WORD*>(rowBuf) + col) = static_cast<WORD>(shadowColor);
        }
    }

    /** @brief 直接写入像素（无混合，用于原版位图字体的硬边缘像素） */
    template<bool Is32>
    static inline void writePixel(uint8_t* rowBuf, int col, DWORD color)
    {
        if constexpr (Is32)
            *(reinterpret_cast<DWORD*>(rowBuf) + col) = color;
        else
            *(reinterpret_cast<WORD*>(rowBuf) + col) = static_cast<WORD>(color);
    }
    // ============================================================
    // Section 5: 字符 & 文本渲染
    // ============================================================

    /** @brief 位图行起始指针（对应旧 H3LoadedPcx16::GetRow，调用方负责边界检查） */
    static inline uint8_t* getPcxRow(Bitmap16Bit* pcx, int row)
    {
        return reinterpret_cast<uint8_t*>(pcx->map) + static_cast<std::size_t>(row) * pcx->Pitch;
    }

    /**
     * @brief 使用游戏原版 .fnt 位图绘制单个 ASCII 字符
     *
     * 逐像素读取字形数据：0 = 透明跳过，255 = 前景色，其余 = 阴影色
     * （原版字体的阴影/描边信息直接烘焙在位图中），硬边直写不做混合。
     * 按目标 PCX 边界裁剪，杜绝越界写入。
     *
     * @tparam Is32      是否 32 位色深
     * @param pFont      字体指针
     * @param pOutputPcx 目标 PCX 图像缓冲区
     * @param ch         待绘制字符（0x20-0x7E）
     * @param iX         绘制起点 X 坐标（字身左边缘，不含左边距）
     * @param iY         绘制起点 Y 坐标（字形顶部）
     * @param uFontColor 前景色（已转换为当前色深格式）
     * @return 是否实际绘制了像素
     */
    template<bool Is32>
    static bool drawCharBitmap(H3FontExt* pFont, Bitmap16Bit* pOutputPcx, wchar_t ch,
        int iX, int iY, DWORD uFontColor, DWORD shadowColor)
    {
        const auto& spacing = pFont->fr.abc[static_cast<uint8_t>(ch)];
        const int span = spacing.abcB;
        const int rows = pFont->ori_height;
        const int startX = iX + spacing.abcA;
        if (span <= 0 || rows <= 0 || !pFont->Data)
            return false;

        // 字形偏移与尺寸越界保护（防御损坏/异常的字体数据）
        const uint32_t glyphOffset = pFont->fr.Offset[static_cast<uint8_t>(ch)];
        if (glyphOffset >= pFont->DataSize ||
            glyphOffset + static_cast<size_t>(span) * rows > pFont->DataSize)
            return false;

        const uint8_t* fontBuf = pFont->Data + glyphOffset;

        const int pcxW = pOutputPcx->Width;
        const int pcxH = pOutputPcx->Height;

        int colStart = 0, colEnd = span;
        if (startX < 0)
            colStart = -startX;
        if (startX + colEnd > pcxW)
            colEnd = pcxW - startX;
        if (colStart >= colEnd)
            return false;

        int rowStart = 0, rowEnd = rows;
        if (iY < 0)
            rowStart = -iY;
        if (iY + rowEnd > pcxH)
            rowEnd = pcxH - iY;
        if (rowStart >= rowEnd)
            return false;

        uint8_t* rowBuf = getPcxRow(pOutputPcx, iY + rowStart);
        const uint8_t* srcRow = fontBuf + static_cast<size_t>(rowStart) * span;

        for (int rowIdx = rowStart; rowIdx < rowEnd; ++rowIdx)
        {
            for (int colIdx = colStart; colIdx < colEnd; ++colIdx)
            {
                const uint8_t pixel = srcRow[colIdx];
                if (!pixel)
                    continue;

                // 255 为正常前景，其余非零值为原版位图自带的阴影/过渡色（调色板索引 32）
                const DWORD color = (pixel == 0xFF) ? uFontColor : shadowColor;
                writePixel<Is32>(rowBuf, startX + colIdx, color);
            }

            rowBuf += pOutputPcx->Pitch;
            srcRow += span;
        }

        return true;
    }

    /**
     * @brief 绘制单个字符到 PCX 缓冲区（GDI 渲染 / 原版 ASCII 位图双路径）
     *
     * GBK 汉字一律通过 GDI 实时渲染 + alpha 混合；ASCII 字符按字体的
     * OriginalAscii 开关决定：开启时走原版 .fnt 位图，关闭时同样走 GDI。
     * 两条路径均按目标 PCX 边界裁剪，杜绝越界写入。
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
    static bool drawChar(H3FontExt* pFont, Bitmap16Bit* pOutputPcx, wchar_t wch,
        int iX, int iY, DWORD uFontColor, DWORD shadowColor)
    {
        auto cFont = pFont->ext_data;

        // 原版 ASCII 位图路径（字体缓冲区缺失时回退 GDI）
        if (cFont->UsesOriginalAscii(wch) && pFont->Data)
            return drawCharBitmap<Is32>(pFont, pOutputPcx, wch, iX, iY, uFontColor, shadowColor);

        // 非 GBK 字符 → 跳过（避免 GDI 渲染为方块）
        if (!ExtFont::IsGbkChar(wch))
            return false;

        const uint8_t* glyph = cFont->GetGlyphAlpha(wch);
        if (!glyph)
            return false;

        const int glyphW = cFont->glyph_width;
        const int glyphH = cFont->EffectiveHeight(); // 含底部扩展空间
        const int pcxW = pOutputPcx->Width;
        const int pcxH = pOutputPcx->Height;

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

        const bool drawShadow = cFont->draw_shadow;

        uint8_t* rowBuf = getPcxRow(pOutputPcx, iY + rowStart);
        const uint8_t* glyphRow = glyph + static_cast<size_t>(rowStart) * glyphW;

        // 预合成阴影掩膜（字形内部重叠位置已抑制，渲染期无需再读相邻像素）
        const uint8_t* shadowData = drawShadow ? cFont->GetGlyphShadow(wch) : nullptr;
        const uint8_t* shadowRow = shadowData
            ? shadowData + static_cast<size_t>(rowStart) * glyphW : nullptr;

        for (int rowIdx = rowStart; rowIdx < rowEnd; ++rowIdx)
        {
            // 阴影行（下移 1 像素），越界则不绘制
            uint8_t* shadowRowBuf = nullptr;
            if (shadowRow && iY + rowIdx + 1 < pcxH)
                shadowRowBuf = getPcxRow(pOutputPcx, iY + rowIdx + 1);

            for (int colIdx = colStart; colIdx < colEnd; ++colIdx)
            {
                const uint8_t alpha = glyphRow[colIdx];
                if (alpha == 0)
                    continue;

                const int px = iX + colIdx;

                // Alpha 混合前景色（读 bg → 混合 fgColor → 写入）
                blendPixel<Is32>(rowBuf, px, uFontColor, alpha);

                // Alpha 混合阴影（右下偏移 1 像素）
                if (shadowRowBuf && px + 1 < pcxW && shadowRow[colIdx])
                    blendShadow<Is32>(shadowRowBuf, px + 1, shadowColor, shadowRow[colIdx]);
            }

            rowBuf += pOutputPcx->Pitch;
            glyphRow += glyphW;
            if (shadowRow)
                shadowRow += glyphW;
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
     * @param default_color 默认渲染颜色
     * @param shift       字形在行高内的垂直居中偏移
     * @param fontHeight  行高
     * @param cursorPos   输入光标位置（可见字符索引，-1 = 无光标；每行从 0 起算）
     */
    template<bool Is32>
    static void drawLines(H3FontExt* pFont, Bitmap16Bit* pPcx, const std::vector<CleanLine>& textLines,
        int iX, int iY, int startY, int iBoxWidth, int iBoxHeight, uint32_t uAlignFlags,
        DWORD default_color, DWORD shadowColor, int shift, int fontHeight, int cursorPos)
    {
        auto* extFont = pFont->ext_data;
        const int lineCount = static_cast<int>(textLines.size());
        const int bottomBound = iY + iBoxHeight;
        // 原版位图 ASCII 位置：行内居中 + 原版基线偏移（对应原版 DrawStringExecute 的 y + fr.baseyoffset）
        const int asciiShift = (fontHeight - pFont->ori_height) / 2 + pFont->fr.baseyoffset;

        for (int rowIdx = 0; rowIdx < lineCount; ++rowIdx)
        {
            const CleanLine& line = textLines[rowIdx];
            if (line.line_width == 0)
                continue;

            // 行超出文本框底部时停止（与原版 DrawStringExecute 按 fr.height 的行裁剪一致）
            if (iY + startY + (rowIdx + 1) * fontHeight > bottomBound)
                break;

            // 水平对齐
            int startX = 0;
            switch (uAlignFlags)
            {
            case font::CENTER_JUSTIFIED: startX = (iBoxWidth - line.line_width) / 2; break; // 居中
            case font::RIGHT_JUSTIFIED:  startX = iBoxWidth - line.line_width;        break; // 右对齐
            }

            int curX = iX + startX;
            const char* const textData = line.text.data();
            const char* p = textData;
            const char* const end = p + line.text.size();

            // 颜色跟踪
            DWORD curColor = default_color;
            std::size_t stopIdx = 0;
            const std::size_t stopCount = line.stops.size();

            const int lineY = iY + startY + rowIdx * fontHeight;
            const int asciiLineY = lineY + asciiShift; // 原版位图 ASCII 行位置
            const int gdiLineY = lineY + shift;        // GDI 字形行内居中位置

            // 原版位图 ASCII 与 GDI 字形的垂直偏移不同，绘制时按字符类型选择
            auto glyphY = [&](wchar_t wch) -> int
            {
                return extFont->UsesOriginalAscii(wch) ? asciiLineY : gdiLineY;
            };

            // 本行可见字符索引（与原始引擎 DrawStringExecute 的 colora 计数一致）
            int charIdx = 0;

            while (p < end)
            {
                // 检查当前位置是否有颜色变更
                const std::size_t byte_pos = static_cast<std::size_t>(p - textData);
                while (stopIdx < stopCount && line.stops[stopIdx].byte_pos == byte_pos)
                {
                    curColor = line.stops[stopIdx].color;
                    ++stopIdx;
                }

                const uint8_t code = static_cast<uint8_t>(*p);

                if (isSingleByte(code))
                {
                    drawChar<Is32>(pFont, pPcx, (wchar_t)code,
                        curX, glyphY((wchar_t)code), curColor, shadowColor);
                    if (cursorPos == charIdx)
                        drawChar<Is32>(pFont, pPcx, L'_', curX, glyphY(L'_'), default_color, shadowColor);
                    curX += line.advances[charIdx]; // 布局阶段预计算，绘制期零测量
                    ++p;
                    ++charIdx;
                }
                else
                {
                    uint8_t nextCode = (end - p >= 2) ? static_cast<uint8_t>(p[1]) : 0;
                    if (isDBCSLeadByte(code, nextCode))
                    {
                        const wchar_t wch = ExtFont::GbkToWchar(code, nextCode);
                        drawChar<Is32>(pFont, pPcx, wch,
                            curX, glyphY(wch), curColor, shadowColor);
                        if (cursorPos == charIdx)
                            drawChar<Is32>(pFont, pPcx, L'_', curX, glyphY(L'_'), default_color, shadowColor);
                        curX += line.advances[charIdx]; // 布局阶段预计算，绘制期零测量
                        p += 2;
                        ++charIdx;
                    }
                    else
                    {
                        // 无效双字节，当单字节处理
                        drawChar<Is32>(pFont, pPcx, (wchar_t)code,
                            curX, glyphY((wchar_t)code), curColor, shadowColor);
                        if (cursorPos == charIdx)
                            drawChar<Is32>(pFont, pPcx, L'_', curX, glyphY(L'_'), default_color, shadowColor);
                        curX += line.advances[charIdx]; // 布局阶段预计算，绘制期零测量
                        ++p;
                        ++charIdx;
                    }
                }
            }

            // 行尾光标（cursorPos 等于本行可见字符数）
            if (cursorPos != -1 && cursorPos == charIdx)
                drawChar<Is32>(pFont, pPcx, L'_', curX, glyphY(L'_'), curColor, shadowColor);
        }
    }

    /**
     * @brief 在指定矩形区域内绘制多行文本
     *
     * 完整渲染管线：
     *   1. getOrCreateLayout → 拆行 + 颜色解析（布局缓存命中时零开销）
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
     * @param uAlignFlags 对齐标志（font::EJustify 位掩码）
     * @param iFontStyle  输入光标位置（可见字符索引，-1 = 无光标）
     */
    void __stdcall H3Font_DrawText(HiHook* /*h*/, H3FontExt* pFont, char* szText, Bitmap16Bit* pPcx,
        int iX, int iY, int iBoxWidth, int iBoxHeight,
        uint32_t uColorIdx, uint32_t uAlignFlags, int iFontStyle)
    {
        if (!szText || iBoxWidth == 0 || !pPcx || !pFont->ext_data)
            return;

        // 绘制前重新判定色深：此时 HD Mod 已完全初始化，判定最可靠，
        // 且能感知运行中的色深切换
        updateBitMode();

        // ========== 阶段一：解析颜色与字体度量 ==========
        const uint32_t ci = (uColorIdx & 0x100) ? (uColorIdx & 0xFE) : (uColorIdx + 9);
        const DWORD default_color = gGetColor(pFont->p16, ci);
        const DWORD highlight_color = gGetColor(pFont->p16, ci + 1);
        const DWORD shadowColor = gGetColor(pFont->p16, ShadowColorIndex);
        const bool is32bit = gBitMode != 0;

        auto* extFont = pFont->ext_data;
        const int fontHeight = pFont->fr.height;
        const int effHeight = extFont->EffectiveHeight(); // 含底部边距的有效字形高度

        // 统一垂直偏移：在行高内居中有效字形区域
        const int shift = (fontHeight - effHeight) / 2;

        // ========== 空文本：原始引擎仍绘制输入光标（'_'） ==========
        const int cursorPos = iFontStyle;
        if (!*szText)
        {
            if (cursorPos != -1)
            {
                // 光标为 ASCII，原版位图模式下按原始字高居中并叠加原版基线偏移
                const int cursorY = iY + (extFont->UsesOriginalAscii(L'_')
                    ? (fontHeight - pFont->ori_height) / 2 + pFont->fr.baseyoffset : shift);

                if (is32bit)
                    drawChar<true>(pFont, pPcx, L'_', iX, cursorY, default_color, shadowColor);
                else
                    drawChar<false>(pFont, pPcx, L'_', iX, cursorY, default_color, shadowColor);
            }
            return;
        }

        // ========== 阶段二：获取布局（缓存命中 → 跳过拆行与颜色解析）==========
        const std::vector<CleanLine>& textLines =
            getOrCreateLayout(pFont, szText, iBoxWidth, default_color, highlight_color, is32bit);
        if (textLines.empty())
            return;

        const int lineCount = static_cast<int>(textLines.size());

        // ========== 阶段三：垂直对齐 ==========
        int startY = 0;

        if (uAlignFlags & font::VERT_CENTER_JUSTIFIED)
        {
            uAlignFlags &= ~font::VERT_CENTER_JUSTIFIED;
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
        else if (uAlignFlags & font::BOTTOM_JUSTIFIED)
        {
            uAlignFlags &= ~font::BOTTOM_JUSTIFIED;
            const int totalH = fontHeight * lineCount;
            if (totalH < iBoxHeight)
                startY = iBoxHeight - totalH;
        }

        // ========== 阶段四：逐行绘制（按色深一次性分派，内循环完全内联）==========
        if (is32bit)
        {
            drawLines<true>(pFont, pPcx, textLines, iX, iY, startY, iBoxWidth, iBoxHeight,
                uAlignFlags, default_color, shadowColor, shift, fontHeight, cursorPos);
        }
        else
        {
            drawLines<false>(pFont, pPcx, textLines, iX, iY, startY, iBoxWidth, iBoxHeight,
                uAlignFlags, default_color, shadowColor, shift, fontHeight, cursorPos);
        }
    }

} // namespace H3FontExtension
