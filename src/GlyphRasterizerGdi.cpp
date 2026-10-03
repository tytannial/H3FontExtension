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

        HDC     hdcGlyph_ = nullptr;   ///< 字形渲染内存 DC
        HBITMAP hbmGlyph_ = nullptr;   ///< 字形渲染 32-bit DIB
        void*   pGlyphBits_ = nullptr; ///< DIB 位图原始数据指针
        HFONT   hGlyphFont_ = nullptr; ///< GDI 字体句柄
        HGDIOBJ hOldBitmap_ = nullptr; ///< DIB 选入 DC 前的原位图
        HGDIOBJ hOldFont_ = nullptr;   ///< 字体选入 DC 前的原字体
        int glyphWidth_ = 0;           ///< 字形缓冲宽度（含左右边距）
        int effectiveHeight_ = 0;      ///< 字形缓冲高度（含底部扩展）
        int marginLeft_ = 0;           ///< 左边距
        int marginRight_ = 0;          ///< 右边距
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
        glyphWidth_ = config.bufferWidth;
        effectiveHeight_ = config.bufferHeight;
        marginLeft_ = config.marginLeft;
        marginRight_ = config.marginRight;

        // 创建 32-bit DIB section 作为字形渲染目标（top-down）
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = glyphWidth_;
        bmi.bmiHeader.biHeight = -effectiveHeight_;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC hdcScreen = ::GetDC(nullptr);
        hdcGlyph_ = ::CreateCompatibleDC(hdcScreen);
        ::ReleaseDC(nullptr, hdcScreen);
        if (!hdcGlyph_)
            return false;

        hbmGlyph_ = ::CreateDIBSection(hdcGlyph_, &bmi, DIB_RGB_COLORS, &pGlyphBits_, nullptr, 0);
        if (!hbmGlyph_)
        {
            Release();
            return false;
        }

        hOldBitmap_ = ::SelectObject(hdcGlyph_, hbmGlyph_);
        ::SetBkMode(hdcGlyph_, TRANSPARENT);
        ::SetTextAlign(hdcGlyph_, TA_TOP | TA_LEFT);
        ::SetTextColor(hdcGlyph_, RGB(255, 255, 255)); // 固定白色绘制，alpha 掩膜提取只需覆盖度

        const int weight = config.bold ? FW_BOLD : FW_NORMAL;
        const DWORD quality = config.antiAlias ? CLEARTYPE_QUALITY : NONANTIALIASED_QUALITY;

        hGlyphFont_ = CreateGdiFont(config.fontName, config.heightPx, weight, quality);
        if (!hGlyphFont_)
        {
            Release();
            return false;
        }
        hOldFont_ = ::SelectObject(hdcGlyph_, hGlyphFont_);
        return true;
    }

    bool GdiGlyphRasterizer::RenderGlyph(wchar_t ch, uint8_t* outAlpha)
    {
        if (!hdcGlyph_ || !pGlyphBits_ || !outAlpha)
            return false;

        // 清除背景：top-down DIB 是连续内存，直接 memset，无需创建 GDI 画刷
        ::memset(pGlyphBits_, 0, static_cast<size_t>(glyphWidth_) * effectiveHeight_ * 4);

        // 以白色绘制字符（ClearType 会生成彩色子像素）
        ::TextOutW(hdcGlyph_, marginLeft_, 0, &ch, 1);
        ::GdiFlush();

        // 提取 alpha 掩膜：max(R,G,B) 作为覆盖度
        const uint8_t* src = static_cast<const uint8_t*>(pGlyphBits_);
        const size_t pixelCount = static_cast<size_t>(glyphWidth_) * effectiveHeight_;
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
        if (!hdcGlyph_)
            return glyphWidth_; // 回退

        SIZE sz = { 0, 0 };
        ::GetTextExtentPoint32W(hdcGlyph_, &ch, 1, &sz);
        return sz.cx + marginLeft_ + marginRight_;
    }

    void GdiGlyphRasterizer::Release()
    {
        if (hdcGlyph_)
        {
            // 选入 DC 的对象必须先恢复原对象才能成功删除，否则 DeleteObject 静默失败导致 GDI 句柄泄漏
            if (hGlyphFont_)
            {
                if (hOldFont_)
                    ::SelectObject(hdcGlyph_, hOldFont_);
                ::DeleteObject(hGlyphFont_);
                hGlyphFont_ = nullptr;
                hOldFont_ = nullptr;
            }
            if (hbmGlyph_)
            {
                if (hOldBitmap_)
                    ::SelectObject(hdcGlyph_, hOldBitmap_);
                ::DeleteObject(hbmGlyph_);
                hbmGlyph_ = nullptr;
                hOldBitmap_ = nullptr;
                pGlyphBits_ = nullptr;
            }
            ::DeleteDC(hdcGlyph_);
            hdcGlyph_ = nullptr;
        }
        else
        {
            // DC 不存在（创建失败或已释放）：直接删除可能残留的独立对象
            if (hGlyphFont_)
            {
                ::DeleteObject(hGlyphFont_);
                hGlyphFont_ = nullptr;
            }
            if (hbmGlyph_)
            {
                ::DeleteObject(hbmGlyph_);
                hbmGlyph_ = nullptr;
                pGlyphBits_ = nullptr;
            }
        }
    }

    std::unique_ptr<IGlyphRasterizer> CreateGdiGlyphRasterizer()
    {
        return std::make_unique<GdiGlyphRasterizer>();
    }

} // namespace H3FontExtension
