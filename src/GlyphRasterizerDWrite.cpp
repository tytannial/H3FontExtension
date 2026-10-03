//===----------------------------------------------------------------------===//
// DirectWrite 字形光栅化后端（Windows 7 及以上）
//
// 动态加载 dwrite.dll（不引入导入表依赖，避免破坏 Windows XP 兼容），
// 通过 IDWriteGlyphRunAnalysis 获取 GDI 兼容的 alpha 纹理：
//   - 抗锯齿：GDI_NATURAL + CLEARTYPE_3x1（子像素三通道取最大值）
//   - 非抗锯齿：ALIASED + ALIASED_1x1
//===----------------------------------------------------------------------===//

#include "GlyphRasterizer.h"

#include <Windows.h>
#include <dwrite.h>
#include <cstring>
#include <vector>

namespace H3FontExtension
{
    /** @brief DirectWrite 工厂创建函数类型（动态加载） */
    using DWriteCreateFactoryFn = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);

    /** @brief 获取 DWriteCreateFactory（进程内仅解析一次；dwrite.dll 不可用返回 nullptr） */
    static DWriteCreateFactoryFn GetDWriteCreateFactory()
    {
        static DWriteCreateFactoryFn fn = []() -> DWriteCreateFactoryFn
        {
            HMODULE dwrite = ::LoadLibraryW(L"dwrite.dll");
            if (!dwrite)
                return nullptr;
            return reinterpret_cast<DWriteCreateFactoryFn>(
                ::GetProcAddress(dwrite, "DWriteCreateFactory"));
        }();
        return fn;
    }

    /** @brief DirectWrite 字形光栅化后端 */
    class DWriteGlyphRasterizer final : public IGlyphRasterizer
    {
    public:
        ~DWriteGlyphRasterizer() override
        {
            if (fontFace_)
                fontFace_->Release();
            if (font_)
                font_->Release();
            if (family_)
                family_->Release();
            if (collection_)
                collection_->Release();
            if (factory_)
                factory_->Release();
        }

        bool Initialize(const GlyphRasterizerConfig& config) override;
        bool RenderGlyph(wchar_t ch, uint8_t* outAlpha) override;
        int GetAdvanceWidth(wchar_t ch) override;

    private:
        /** @brief 从系统字体集合解析字体面（失败回退宋体） */
        bool ResolveFontFace(const char* fontName, bool bold);

        /** @brief 查询字形索引与 GDI 兼容度量 */
        bool GetGlyphIndex(wchar_t ch, UINT16& glyphIndex, DWRITE_GLYPH_METRICS& metrics) const;

        /** @brief 渲染字形 alpha 纹理到覆盖度缓冲；子像素/灰度由 antiAlias_ 决定 */
        bool RenderGlyphTexture(IDWriteGlyphRunAnalysis* analysis, bool clearType, uint8_t* outAlpha);

        IDWriteFactory* factory_ = nullptr;
        IDWriteFontCollection* collection_ = nullptr;
        IDWriteFontFamily* family_ = nullptr;
        IDWriteFont* font_ = nullptr;
        IDWriteFontFace* fontFace_ = nullptr;

        float fontSize_ = 0.0f;         ///< em 尺寸（像素，按字形高度校准）
        float designUnitsPerEm_ = 0.0f; ///< 字体设计单位/em（度量缩放基准）
        float pixelsPerDip_ = 1.0f;     ///< DPI 缩放（游戏固定 96 DPI）
        float ascent_ = 0.0f;           ///< 基线相对缓冲顶部的像素距离
        int marginLeft_ = 0;         ///< 左边距（glyph run 原点 X）
        int marginRight_ = 0;        ///< 右边距
        int bufferWidth_ = 0;        ///< 字形缓冲宽度
        int bufferHeight_ = 0;       ///< 字形缓冲高度
        bool antiAlias_ = true;      ///< 是否启用抗锯齿
    };

    bool DWriteGlyphRasterizer::Initialize(const GlyphRasterizerConfig& config)
    {
        DWriteCreateFactoryFn createFactory = GetDWriteCreateFactory();
        if (!createFactory)
            return false;

        HRESULT hr = createFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&factory_));
        if (FAILED(hr) || !factory_)
            return false;

        hr = factory_->GetSystemFontCollection(&collection_, FALSE);
        if (FAILED(hr) || !collection_)
            return false;

        if (!ResolveFontFace(config.fontName, config.bold))
            return false;

        // 校准 em 尺寸：使可见字符高度（ascent + descent，设计单位）等于配置的字形像素高度，
        // 与 GDI 路径 CreateFont(-height) 的字符高度语义保持一致
        DWRITE_FONT_METRICS design = {};
        fontFace_->GetMetrics(&design);
        designUnitsPerEm_ = static_cast<float>(design.designUnitsPerEm);
        const UINT32 charHeightUnits = static_cast<UINT32>(design.ascent) + design.descent;
        fontSize_ = (charHeightUnits > 0 && designUnitsPerEm_ > 0.0f)
            ? static_cast<float>(config.heightPx) * designUnitsPerEm_ / charHeightUnits
            : static_cast<float>(config.heightPx);

        // GDI 兼容度量返回设计单位，换算为像素（缩放因子 = fontSize / designUnitsPerEm）；
        // ascent 为基线相对字符单元格顶部的像素距离
        const float toPixels = fontSize_ / designUnitsPerEm_;
        DWRITE_FONT_METRICS gdi = {};
        hr = fontFace_->GetGdiCompatibleMetrics(fontSize_, pixelsPerDip_, nullptr, &gdi);
        if (FAILED(hr))
            return false;
        ascent_ = static_cast<float>(gdi.ascent) * toPixels;

        marginLeft_ = config.marginLeft;
        marginRight_ = config.marginRight;
        bufferWidth_ = config.bufferWidth;
        bufferHeight_ = config.bufferHeight;
        antiAlias_ = config.antiAlias;
        return true;
    }

    bool DWriteGlyphRasterizer::ResolveFontFace(const char* fontName, bool bold)
    {
        wchar_t wideName[128] = {};
        if (::MultiByteToWideChar(CP_ACP, 0, fontName, -1, wideName, 128) <= 0)
            return false;

        BOOL exists = FALSE;
        UINT32 index = 0;
        HRESULT hr = collection_->FindFamilyName(wideName, &index, &exists);
        if (FAILED(hr) || !exists)
        {
            // 回退宋体
            hr = collection_->FindFamilyName(L"SimSun", &index, &exists);
            if (FAILED(hr) || !exists)
                return false;
        }

        hr = collection_->GetFontFamily(index, &family_);
        if (FAILED(hr) || !family_)
            return false;

        const DWRITE_FONT_WEIGHT weight = bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        hr = family_->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, &font_);
        if (FAILED(hr) || !font_)
            return false;

        hr = font_->CreateFontFace(&fontFace_);
        return SUCCEEDED(hr) && fontFace_ != nullptr;
    }

    bool DWriteGlyphRasterizer::GetGlyphIndex(wchar_t ch, UINT16& glyphIndex,
        DWRITE_GLYPH_METRICS& metrics) const
    {
        const UINT32 codePoint = static_cast<UINT32>(static_cast<uint16_t>(ch));
        glyphIndex = 0;
        HRESULT hr = fontFace_->GetGlyphIndices(&codePoint, 1, &glyphIndex);
        if (FAILED(hr) || glyphIndex == 0)
            return false;

        hr = fontFace_->GetGdiCompatibleGlyphMetrics(fontSize_, pixelsPerDip_, nullptr, FALSE,
            &glyphIndex, 1, &metrics, FALSE);
        return SUCCEEDED(hr);
    }

    bool DWriteGlyphRasterizer::RenderGlyphTexture(IDWriteGlyphRunAnalysis* analysis,
        bool clearType, uint8_t* outAlpha)
    {
        const DWRITE_TEXTURE_TYPE textureType =
            clearType ? DWRITE_TEXTURE_CLEARTYPE_3x1 : DWRITE_TEXTURE_ALIASED_1x1;

        RECT bounds = {};
        HRESULT hr = analysis->GetAlphaTextureBounds(textureType, &bounds);
        if (FAILED(hr))
            return false;

        ::memset(outAlpha, 0, static_cast<size_t>(bufferWidth_) * bufferHeight_);

        const int textureWidth = bounds.right - bounds.left;
        const int textureHeight = bounds.bottom - bounds.top;
        if (textureWidth <= 0 || textureHeight <= 0)
            return true; // 空白字形（如空格）

        const size_t channels = clearType ? 3 : 1;
        const size_t textureBytes = static_cast<size_t>(textureWidth) * textureHeight * channels;
        std::vector<uint8_t> texture(textureBytes);

        hr = analysis->CreateAlphaTexture(textureType, &bounds,
            texture.data(), static_cast<UINT32>(textureBytes));
        if (FAILED(hr))
            return false;

        // 纹理 (0,0) 为 glyph run 原点坐标系原点；bounds 为绝对坐标（已包含原点偏移），
        // 直接作为字形缓冲坐标使用，越界部分裁剪
        for (int y = 0; y < textureHeight; ++y)
        {
            const int dstY = bounds.top + y;
            if (dstY < 0 || dstY >= bufferHeight_)
                continue;

            uint8_t* dstRow = outAlpha + static_cast<size_t>(dstY) * bufferWidth_;
            for (int x = 0; x < textureWidth; ++x)
            {
                const int dstX = bounds.left + x;
                if (dstX < 0 || dstX >= bufferWidth_)
                    continue;

                if (clearType)
                {
                    // 子像素三通道取最大值作为覆盖度
                    const uint8_t* px = &texture[(static_cast<size_t>(y) * textureWidth + x) * 3];
                    dstRow[dstX] = (px[0] > px[1]) ? ((px[0] > px[2]) ? px[0] : px[2])
                                                   : ((px[1] > px[2]) ? px[1] : px[2]);
                }
                else
                {
                    dstRow[dstX] = texture[static_cast<size_t>(y) * textureWidth + x];
                }
            }
        }
        return true;
    }

    bool DWriteGlyphRasterizer::RenderGlyph(wchar_t ch, uint8_t* outAlpha)
    {
        if (!fontFace_ || !factory_ || !outAlpha)
            return false;

        UINT16 glyphIndex = 0;
        DWRITE_GLYPH_METRICS metrics = {};
        if (!GetGlyphIndex(ch, glyphIndex, metrics))
            return false;

        const float toPixels = fontSize_ / designUnitsPerEm_;
        const FLOAT advance = static_cast<FLOAT>(metrics.advanceWidth) * toPixels;
        const DWRITE_GLYPH_OFFSET offset = { 0.0f, 0.0f };
        DWRITE_GLYPH_RUN run = {};
        run.fontFace = fontFace_;
        run.fontEmSize = fontSize_;
        run.glyphCount = 1;
        run.glyphIndices = &glyphIndex;
        run.glyphAdvances = &advance;
        run.glyphOffsets = &offset;
        run.isSideways = FALSE;
        run.bidiLevel = 0;

        const bool clearType = antiAlias_;
        const DWRITE_RENDERING_MODE renderMode = clearType
            ? DWRITE_RENDERING_MODE_GDI_NATURAL
            : DWRITE_RENDERING_MODE_ALIASED;

        IDWriteGlyphRunAnalysis* analysis = nullptr;
        HRESULT hr = factory_->CreateGlyphRunAnalysis(&run, pixelsPerDip_, nullptr,
            renderMode, DWRITE_MEASURING_MODE_NATURAL,
            static_cast<FLOAT>(marginLeft_), ascent_, &analysis);
        if (FAILED(hr) || !analysis)
            return false;

        const bool ok = RenderGlyphTexture(analysis, clearType, outAlpha);
        analysis->Release();
        return ok;
    }

    int DWriteGlyphRasterizer::GetAdvanceWidth(wchar_t ch)
    {
        UINT16 glyphIndex = 0;
        DWRITE_GLYPH_METRICS metrics = {};
        if (!GetGlyphIndex(ch, glyphIndex, metrics))
            return bufferWidth_; // 回退：无字形时使用缓冲宽度

        // GDI 兼容度量返回设计单位，换算为像素后四舍五入
        const float advance = static_cast<FLOAT>(metrics.advanceWidth) * (fontSize_ / designUnitsPerEm_);
        return static_cast<int>(advance + 0.5f) + marginLeft_ + marginRight_;
    }

    std::unique_ptr<IGlyphRasterizer> CreateDWriteGlyphRasterizer()
    {
        if (!GetDWriteCreateFactory())
            return nullptr;
        return std::make_unique<DWriteGlyphRasterizer>();
    }

} // namespace H3FontExtension
