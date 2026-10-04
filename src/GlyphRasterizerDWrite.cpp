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
    static DWriteCreateFactoryFn getDWriteCreateFactory()
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
            if (font_face)
                font_face->Release();
            if (font)
                font->Release();
            if (family)
                family->Release();
            if (collection)
                collection->Release();
            if (factory)
                factory->Release();
        }

        bool Initialize(const GlyphRasterizerConfig& config) override;
        bool RenderGlyph(wchar_t ch, uint8_t* outAlpha) override;
        int GetAdvanceWidth(wchar_t ch) override;

    private:
        /** @brief 从系统字体集合解析字体面（失败回退宋体） */
        bool ResolveFontFace(const char* fontName, bool bold);

        /** @brief 查询字形索引与 GDI 兼容度量 */
        bool GetGlyphIndex(wchar_t ch, UINT16& glyphIndex, DWRITE_GLYPH_METRICS& metrics) const;

        /** @brief 渲染字形 alpha 纹理到覆盖度缓冲；子像素/灰度由 anti_alias 决定 */
        bool RenderGlyphTexture(IDWriteGlyphRunAnalysis* analysis, bool clearType, uint8_t* outAlpha);

        IDWriteFactory* factory = nullptr;
        IDWriteFontCollection* collection = nullptr;
        IDWriteFontFamily* family = nullptr;
        IDWriteFont* font = nullptr;
        IDWriteFontFace* font_face = nullptr;

        float font_size = 0.0f;         ///< em 尺寸（像素，按字形高度校准）
        float design_units_per_em = 0.0f; ///< 字体设计单位/em（度量缩放基准）
        float pixels_per_dip = 1.0f;     ///< DPI 缩放（游戏固定 96 DPI）
        float ascent = 0.0f;           ///< 基线相对缓冲顶部的像素距离
        int margin_left = 0;         ///< 左边距（glyph run 原点 X）
        int margin_right = 0;        ///< 右边距
        int buffer_width = 0;        ///< 字形缓冲宽度
        int buffer_height = 0;       ///< 字形缓冲高度
        bool anti_alias = true;      ///< 是否启用抗锯齿
    };

    bool DWriteGlyphRasterizer::Initialize(const GlyphRasterizerConfig& config)
    {
        DWriteCreateFactoryFn createFactory = getDWriteCreateFactory();
        if (!createFactory)
            return false;

        HRESULT hr = createFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&factory));
        if (FAILED(hr) || !factory)
            return false;

        hr = factory->GetSystemFontCollection(&collection, FALSE);
        if (FAILED(hr) || !collection)
            return false;

        if (!ResolveFontFace(config.font_name, config.bold))
            return false;

        // 校准 em 尺寸：使可见字符高度（ascent + descent，设计单位）等于配置的字形像素高度，
        // 与 GDI 路径 CreateFont(-height) 的字符高度语义保持一致
        DWRITE_FONT_METRICS design = {};
        font_face->GetMetrics(&design);
        design_units_per_em = static_cast<float>(design.designUnitsPerEm);
        const UINT32 charHeightUnits = static_cast<UINT32>(design.ascent) + design.descent;
        font_size = (charHeightUnits > 0 && design_units_per_em > 0.0f)
            ? static_cast<float>(config.height_px) * design_units_per_em / charHeightUnits
            : static_cast<float>(config.height_px);

        // GDI 兼容度量返回设计单位，换算为像素（缩放因子 = fontSize / designUnitsPerEm）；
        // ascent 为基线相对字符单元格顶部的像素距离
        const float toPixels = font_size / design_units_per_em;
        DWRITE_FONT_METRICS gdi = {};
        hr = font_face->GetGdiCompatibleMetrics(font_size, pixels_per_dip, nullptr, &gdi);
        if (FAILED(hr))
            return false;
        ascent = static_cast<float>(gdi.ascent) * toPixels;

        margin_left = config.margin_left;
        margin_right = config.margin_right;
        buffer_width = config.buffer_width;
        buffer_height = config.buffer_height;
        anti_alias = config.anti_alias;
        return true;
    }

    bool DWriteGlyphRasterizer::ResolveFontFace(const char* fontName, bool bold)
    {
        wchar_t wideName[128] = {};
        if (::MultiByteToWideChar(CP_ACP, 0, fontName, -1, wideName, 128) <= 0)
            return false;

        BOOL exists = FALSE;
        UINT32 index = 0;
        HRESULT hr = collection->FindFamilyName(wideName, &index, &exists);
        if (FAILED(hr) || !exists)
        {
            // 回退宋体
            hr = collection->FindFamilyName(L"SimSun", &index, &exists);
            if (FAILED(hr) || !exists)
                return false;
        }

        hr = collection->GetFontFamily(index, &family);
        if (FAILED(hr) || !family)
            return false;

        const DWRITE_FONT_WEIGHT weight = bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        hr = family->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, &font);
        if (FAILED(hr) || !font)
            return false;

        hr = font->CreateFontFace(&font_face);
        return SUCCEEDED(hr) && font_face != nullptr;
    }

    bool DWriteGlyphRasterizer::GetGlyphIndex(wchar_t ch, UINT16& glyphIndex,
        DWRITE_GLYPH_METRICS& metrics) const
    {
        const UINT32 codePoint = static_cast<UINT32>(static_cast<uint16_t>(ch));
        glyphIndex = 0;
        HRESULT hr = font_face->GetGlyphIndices(&codePoint, 1, &glyphIndex);
        if (FAILED(hr) || glyphIndex == 0)
            return false;

        hr = font_face->GetGdiCompatibleGlyphMetrics(font_size, pixels_per_dip, nullptr, FALSE,
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

        ::memset(outAlpha, 0, static_cast<size_t>(buffer_width) * buffer_height);

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
            if (dstY < 0 || dstY >= buffer_height)
                continue;

            uint8_t* dstRow = outAlpha + static_cast<size_t>(dstY) * buffer_width;
            for (int x = 0; x < textureWidth; ++x)
            {
                const int dstX = bounds.left + x;
                if (dstX < 0 || dstX >= buffer_width)
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
        if (!font_face || !factory || !outAlpha)
            return false;

        UINT16 glyphIndex = 0;
        DWRITE_GLYPH_METRICS metrics = {};
        if (!GetGlyphIndex(ch, glyphIndex, metrics))
            return false;

        const float toPixels = font_size / design_units_per_em;
        const FLOAT advance = static_cast<FLOAT>(metrics.advanceWidth) * toPixels;
        const DWRITE_GLYPH_OFFSET offset = { 0.0f, 0.0f };
        DWRITE_GLYPH_RUN run = {};
        run.fontFace = font_face;
        run.fontEmSize = font_size;
        run.glyphCount = 1;
        run.glyphIndices = &glyphIndex;
        run.glyphAdvances = &advance;
        run.glyphOffsets = &offset;
        run.isSideways = FALSE;
        run.bidiLevel = 0;

        const bool clearType = anti_alias;
        const DWRITE_RENDERING_MODE renderMode = clearType
            ? DWRITE_RENDERING_MODE_GDI_NATURAL
            : DWRITE_RENDERING_MODE_ALIASED;

        IDWriteGlyphRunAnalysis* analysis = nullptr;
        HRESULT hr = factory->CreateGlyphRunAnalysis(&run, pixels_per_dip, nullptr,
            renderMode, DWRITE_MEASURING_MODE_NATURAL,
            static_cast<FLOAT>(margin_left), ascent, &analysis);
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
            return buffer_width; // 回退：无字形时使用缓冲宽度

        // GDI 兼容度量返回设计单位，换算为像素后四舍五入
        const float advance = static_cast<FLOAT>(metrics.advanceWidth) * (font_size / design_units_per_em);
        return static_cast<int>(advance + 0.5f) + margin_left + margin_right;
    }

    std::unique_ptr<IGlyphRasterizer> createDWriteGlyphRasterizer()
    {
        if (!getDWriteCreateFactory())
            return nullptr;
        return std::make_unique<DWriteGlyphRasterizer>();
    }

} // namespace H3FontExtension
