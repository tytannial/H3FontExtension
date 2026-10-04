#pragma once

#include <cstdint>
#include <memory>

namespace H3FontExtension
{
    /**
     * @brief 字形光栅化后端配置
     */
    struct GlyphRasterizerConfig
    {
        const char* font_name = nullptr; ///< 系统字体名（如 "SimSun"）
        int  height_px = 0;              ///< 字形像素高度（正数）
        bool bold = false;               ///< 是否加粗
        bool anti_alias = true;          ///< 是否启用抗锯齿（子像素/ClearType）
        int  margin_left = 0;            ///< 左边距（渲染起点 X，像素）
        int  margin_right = 0;           ///< 右边距（像素）
        int  margin_bottom = 0;          ///< 底部扩展（像素，容纳字形下降部分）
        int  buffer_width = 0;           ///< 字形缓冲宽度（含左右边距）
        int  buffer_height = 0;          ///< 字形缓冲高度（含底部扩展）
    };

    /**
     * @brief 字形光栅化后端接口（"取字符图"抽象层）
     *
     * 负责把单个字符渲染为覆盖度（alpha）位图，并测量字符步进宽度。
     * 平台实现：
     *   - GDI（Windows XP）：CreateFontA + TextOutW + DIB 像素提取
     *   - DirectWrite（Windows 7 及以上）：IDWriteFontFace + IDWriteGlyphRunAnalysis
     */
    class IGlyphRasterizer
    {
    public:
        virtual ~IGlyphRasterizer() = default;

        /** @brief 初始化后端；失败返回 false（由调用方决定是否回退其他实现） */
        virtual bool Initialize(const GlyphRasterizerConfig& config) = 0;

        /**
         * @brief 把字符渲染到覆盖度缓冲
         *
         * 缓冲尺寸 = bufferWidth × bufferHeight（行主序单字节），
         * (0,0) 为字形缓冲左上角。实现负责清零缓冲后写入覆盖度
         * （0 = 完全透明，255 = 完全不透明）。
         *
         * @return 渲染成功返回 true；字符无可用字形时返回 false
         */
        virtual bool RenderGlyph(wchar_t ch, uint8_t* outAlpha) = 0;

        /** @brief 测量字符步进宽度（像素，含左右边距） */
        virtual int GetAdvanceWidth(wchar_t ch) = 0;
    };

    /**
     * @brief 创建平台最优的字形光栅化后端
     *
     * Windows 7 及以上优先使用 DirectWrite（dwrite.dll 动态加载），
     * 不可用时回退到 GDI（Windows XP 路径）。
     */
    std::unique_ptr<IGlyphRasterizer> createGlyphRasterizer();

    // ---- 内部工厂（供 createGlyphRasterizer 选择实现） ----

    /** @brief 创建 GDI 后端（始终可用） */
    std::unique_ptr<IGlyphRasterizer> createGdiGlyphRasterizer();

    /** @brief 创建 DirectWrite 后端（dwrite.dll 不可用时返回 nullptr） */
    std::unique_ptr<IGlyphRasterizer> createDWriteGlyphRasterizer();

} // namespace H3FontExtension
