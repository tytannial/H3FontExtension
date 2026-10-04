//===----------------------------------------------------------------------===//
// GDI 字形光栅化后端（Windows XP 兼容路径）
//
// CreateFontA 创建字形字体，TextOutW 绘制到 32-bit DIB，
// 提取 max(R,G,B) 作为覆盖度（ClearType 子像素取最大值保留边缘覆盖）。
//===----------------------------------------------------------------------===//

#include "GlyphRasterizer.h"

#include <Windows.h>
#include <cstring>

namespace H3FontExtension
{
    /** @brief GDI 字形光栅化后端 */
    class GdiGlyphRasterizer final : public IGlyphRasterizer
    {
    public:
        ~GdiGlyphRasterizer() override { Release(); }

        bool Initialize(const GlyphRasterizerConfig& config) override;
        bool RenderGlyph(wchar_t ch, uint8_t* outAlpha) override;
        int GetAdvanceWidth(wchar_t ch) override;

    private:
        /** @brief 创建 GDI 字体；指定字体名失败时回退宋体 */
        static HFONT CreateGdiFont(const char* fontName, int heightPx, int weight, DWORD quality);

        /** @brief 释放全部 GDI 资源（先恢复原对象再删除，避免句柄泄漏） */
        void Release();

        HDC     hdc_glyph = nullptr;   ///< 字形渲染内存 DC
        HBITMAP hbm_glyph = nullptr;   ///< 字形渲染 32-bit DIB
        void*   glyph_bits = nullptr; ///< DIB 位图原始数据指针
        HFONT   glyph_font = nullptr; ///< GDI 字体句柄
        HGDIOBJ old_bitmap = nullptr; ///< DIB 选入 DC 前的原位图
        HGDIOBJ old_font = nullptr;   ///< 字体选入 DC 前的原字体
        int glyph_width = 0;           ///< 字形缓冲宽度（含左右边距）
        int effective_height = 0;      ///< 字形缓冲高度（含底部扩展）
        int margin_left = 0;           ///< 左边距
        int margin_right = 0;          ///< 右边距
    };

    HFONT GdiGlyphRasterizer::CreateGdiFont(const char* fontName, int heightPx, int weight, DWORD quality)
    {
        // lfHeight 负值 = 字符高度（像素），不包含行间距
        HFONT font = ::CreateFontA(-heightPx, 0, 0, 0, weight,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, quality,
            DEFAULT_PITCH | FF_DONTCARE, fontName);

        if (!font)
        {
            // 回退：使用宋体
            font = ::CreateFontA(-heightPx, 0, 0, 0, weight,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, quality,
                DEFAULT_PITCH | FF_DONTCARE, "SimSun");
        }
        return font;
    }

    bool GdiGlyphRasterizer::Initialize(const GlyphRasterizerConfig& config)
    {
        glyph_width = config.buffer_width;
        effective_height = config.buffer_height;
        margin_left = config.margin_left;
        margin_right = config.margin_right;

        // 创建 32-bit DIB section 作为字形渲染目标（top-down）
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = glyph_width;
        bmi.bmiHeader.biHeight = -effective_height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC hdcScreen = ::GetDC(nullptr);
        hdc_glyph = ::CreateCompatibleDC(hdcScreen);
        ::ReleaseDC(nullptr, hdcScreen);
        if (!hdc_glyph)
            return false;

        hbm_glyph = ::CreateDIBSection(hdc_glyph, &bmi, DIB_RGB_COLORS, &glyph_bits, nullptr, 0);
        if (!hbm_glyph)
        {
            Release();
            return false;
        }

        old_bitmap = ::SelectObject(hdc_glyph, hbm_glyph);
        ::SetBkMode(hdc_glyph, TRANSPARENT);
        ::SetTextAlign(hdc_glyph, TA_TOP | TA_LEFT);
        ::SetTextColor(hdc_glyph, RGB(255, 255, 255)); // 固定白色绘制，alpha 掩膜提取只需覆盖度

        const int weight = config.bold ? FW_BOLD : FW_NORMAL;
        const DWORD quality = config.anti_alias ? CLEARTYPE_QUALITY : NONANTIALIASED_QUALITY;

        glyph_font = CreateGdiFont(config.font_name, config.height_px, weight, quality);
        if (!glyph_font)
        {
            Release();
            return false;
        }
        old_font = ::SelectObject(hdc_glyph, glyph_font);
        return true;
    }

    bool GdiGlyphRasterizer::RenderGlyph(wchar_t ch, uint8_t* outAlpha)
    {
        if (!hdc_glyph || !glyph_bits || !outAlpha)
            return false;

        // 清除背景：top-down DIB 是连续内存，直接 memset，无需创建 GDI 画刷
        ::memset(glyph_bits, 0, static_cast<size_t>(glyph_width) * effective_height * 4);

        // 以白色绘制字符（ClearType 会生成彩色子像素）
        ::TextOutW(hdc_glyph, margin_left, 0, &ch, 1);
        ::GdiFlush();

        // 提取 alpha 掩膜：max(R,G,B) 作为覆盖度
        const uint8_t* src = static_cast<const uint8_t*>(glyph_bits);
        const size_t pixelCount = static_cast<size_t>(glyph_width) * effective_height;
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const uint8_t b = src[i * 4];
            const uint8_t g = src[i * 4 + 1];
            const uint8_t r = src[i * 4 + 2];
            outAlpha[i] = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
        }
        return true;
    }

    int GdiGlyphRasterizer::GetAdvanceWidth(wchar_t ch)
    {
        if (!hdc_glyph)
            return glyph_width; // 回退

        SIZE sz = { 0, 0 };
        ::GetTextExtentPoint32W(hdc_glyph, &ch, 1, &sz);
        return sz.cx + margin_left + margin_right;
    }

    void GdiGlyphRasterizer::Release()
    {
        if (hdc_glyph)
        {
            // 选入 DC 的对象必须先恢复原对象才能成功删除，否则 DeleteObject 静默失败导致 GDI 句柄泄漏
            if (glyph_font)
            {
                if (old_font)
                    ::SelectObject(hdc_glyph, old_font);
                ::DeleteObject(glyph_font);
                glyph_font = nullptr;
                old_font = nullptr;
            }
            if (hbm_glyph)
            {
                if (old_bitmap)
                    ::SelectObject(hdc_glyph, old_bitmap);
                ::DeleteObject(hbm_glyph);
                hbm_glyph = nullptr;
                old_bitmap = nullptr;
                glyph_bits = nullptr;
            }
            ::DeleteDC(hdc_glyph);
            hdc_glyph = nullptr;
        }
        else
        {
            // DC 不存在（创建失败或已释放）：直接删除可能残留的独立对象
            if (glyph_font)
            {
                ::DeleteObject(glyph_font);
                glyph_font = nullptr;
            }
            if (hbm_glyph)
            {
                ::DeleteObject(hbm_glyph);
                hbm_glyph = nullptr;
                glyph_bits = nullptr;
            }
        }
    }

    std::unique_ptr<IGlyphRasterizer> createGdiGlyphRasterizer()
    {
        return std::make_unique<GdiGlyphRasterizer>();
    }

} // namespace H3FontExtension
