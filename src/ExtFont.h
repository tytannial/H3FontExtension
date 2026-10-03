#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "GlyphRasterizer.h"

namespace H3FontExtension
{
    /** @brief 双字节编码区码起始值（GBK/DBCS 首字节判定阈值） */
    constexpr uint8_t DBCS_SECTION = 0x81;

    /** @brief 双字节编码位码起始值（GBK/DBCS 第二字节判定阈值） */
    constexpr uint8_t DBCS_POSITION = 0x40;

    /**
     * @brief 扩展字库（字形取模后端 + 字形/宽度缓存）
     *
     * 把系统字体渲染为游戏 .fnt 所需的字形位图与宽度：
     *   - Windows 7 及以上通过 DirectWrite 取字，Windows XP 回退 GDI
     *     （平台选择见 GlyphRasterizer.h）
     *   - 字形覆盖度与字符宽度按需生成并缓存
     *
     * 字形缓存仅保存 alpha 通道（1 字节/像素）：绘制时前景色来自
     * 调色板，字形自身的 RGB 不参与混合，只用作透明度掩膜。
     */
    class ExtFont
    {
    public:
        // ---- 字体度量（配置，绘制逻辑直接读取） ----
        int8_t Height = 0;          ///< 字形渲染高度（像素）
        int    Width = 0;           ///< 字形渲染宽度（像素，不含边距）
        bool   Bold = false;        ///< 是否加粗（true = FW_BOLD）
        bool   AntiAlias = true;    ///< 是否启用抗锯齿（子像素/ClearType）
        int    MarginLeft = 0;      ///< 左侧边距（像素）
        int    MarginRight = 0;     ///< 右侧边距（像素）
        int    MarginBottom = 0;    ///< 底部边距（像素），扩大学形缓冲区高度以容纳字体下降部分
        int    LineSpacing = 0;     ///< 行间距（像素），增加行与行之间的额外空白
        int    GlyphWidth = 0;      ///< DIB 缓冲区宽度 = MarginLeft + Width + MarginRight
        bool   DrawShadow = true;   ///< 是否绘制阴影
        bool   OriginalAscii = true;///< ASCII 字符（0x20-0x7E）是否使用原版 .fnt 位图字体

        /** @brief 返回有效字形高度（含底部边距） */
        int EffectiveHeight() const { return Height + MarginBottom; }

        /** @brief 该字符是否走原版 ASCII 位图路径（开关开启且为可打印 ASCII） */
        bool UsesOriginalAscii(wchar_t ch) const { return OriginalAscii && ch >= 0x20 && ch <= 0x7E; }

        ExtFont() = default;

        /**
         * @brief 构造并加载系统字体
         * @param lpFileName    系统字体名（如 "SimSun", "Microsoft YaHei"）
         * @param iHeight       字形渲染高度
         * @param iWidth        字形渲染宽度
         * @param bBold         是否加粗
         * @param bAntiAlias    是否启用抗锯齿
         * @param iMarginLeft   左边距
         * @param iMarginRight  右边距
         * @param iMarginBottom 底部边距（扩大学形缓冲区，用于容纳字体下降部分）
         * @param iLineSpacing  行间距（增加行与行之间的额外空白）
         * @param bDrawShadow   是否绘制阴影
         * @param bOriginalAscii ASCII 字符是否使用原版 .fnt 位图字体
         */
        ExtFont(const char* lpFileName, int iHeight, int iWidth, bool bBold, bool bAntiAlias,
            int iMarginLeft, int iMarginRight, int iMarginBottom, int iLineSpacing, bool bDrawShadow,
            bool bOriginalAscii);

        ~ExtFont() = default;

        // 禁止拷贝（缓存与后端资源不可浅拷贝），允许移动
        ExtFont(const ExtFont&) = delete;
        ExtFont& operator=(const ExtFont&) = delete;
        ExtFont(ExtFont&& other) noexcept = default;
        ExtFont& operator=(ExtFont&& other) noexcept = default;

        /**
         * @brief 加载系统字体并初始化字形取模后端
         * @return 加载成功返回 true；DirectWrite 初始化失败时自动回退 GDI
         */
        bool Load(const char* fontName, int iHeight, int iWidth, bool bBold, bool bAntiAlias,
            int iMarginLeft, int iMarginRight, int iMarginBottom, int iLineSpacing, bool bDrawShadow,
            bool bOriginalAscii);

        /**
         * @brief GBK 区码/位码 → wchar_t 转换（带全局查表缓存，避免重复调用 MultiByteToWideChar）
         */
        static wchar_t GbkToWchar(uint8_t section, uint8_t position);

        /**
         * @brief 判断字符是否在 GBK 可表示范围（非 GBK 字符 → 跳过以避方块）
         */
        static bool IsGbkChar(wchar_t wch);

        /** @brief 初始化 GBK 字符集查表（插件 Init 时调用一次） */
        static void InitGbkTables();

        /**
         * @brief 获取字形的抗锯齿 alpha 掩膜（按需渲染 + 缓存）
         * @return GlyphWidth×EffectiveHeight 字节 alpha 数据指针，失败返回 nullptr
         */
        const uint8_t* GetGlyphAlpha(wchar_t ch);

        /**
         * @brief 获取单个字符的步进宽度（advance width，含左右边距）
         * @note 首次调用时测量并缓存
         */
        int GetAdvanceWidth(wchar_t ch);

    private:
        /**
         * @brief 字形位图分配器
         *
         * 按固定大小分片分配（每片 1 MB），地址稳定、摊销分配开销，
         * 避免每个字形单独堆分配导致的内存碎片。
         */
        class GlyphPool
        {
        public:
            void Init(std::size_t glyphBytes) { glyphBytes_ = glyphBytes; }

            /** @brief 分配一个字形大小的块；耗尽内存返回 nullptr */
            uint8_t* Allocate();

        private:
            static constexpr std::size_t ChunkBytes = 1u << 20; // 1 MB
            std::vector<std::unique_ptr<uint8_t[]>> chunks_;
            std::size_t used_ = 0;
            std::size_t glyphBytes_ = 0;
        };

        std::unique_ptr<IGlyphRasterizer> rasterizer_; ///< 字形取模后端（DirectWrite/GDI）
        GlyphPool                         pool_;       ///< 字形位图分配器
        std::unique_ptr<uint8_t*[]>       glyphCache_; ///< 64K 字形指针表（nullptr = 未缓存）
        std::unique_ptr<int16_t[]>        widthCache_; ///< 64K 字符宽度表（0 = 未测量）
    };

} // namespace H3FontExtension
