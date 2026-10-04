//===----------------------------------------------------------------------===//
// 文本布局与测量：颜色码解析、拆行、布局/测量缓存、宽度与行数 Hook
//
// 像素混合与字符绘制见 TextRender.cpp，插件入口见 H3FontExtension.cpp。
//===----------------------------------------------------------------------===//

#include "H3FontExtensionInternal.h"

#include "DefImage.h"
#include "Log.h"
#include "TextLayoutCore.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace H3FontExtension
{
    // ============================================================
    // 全局状态（跨翻译单元共享，声明见 H3FontExtensionInternal.h）
    // ============================================================

    /** @brief 是否启用文本颜色功能 */
    bool gTextColorEnabled = true;

    /** @brief 命名颜色映射表（从配置文件加载；仅在布局缓存未命中的拆行/预处理阶段查询） */
    std::unordered_map<std::string, NamedColor> gTextColorMap;

    /** @brief 文本框最小宽度限制 */
    int gBoxWidthMin = 0;

    /** @brief 文本框最大宽度限制 */
    int gBoxWidthMax = 0;

    /** @brief 当前色深模式：-1 = 尚未判定，0 = 16 位，1 = 32 位 */
    int gBitMode = -1;

    /**
     * @brief 游戏位深标志地址
     *
     * 原版 H3BitMode 读取的字节：HD Mod 在 32 位模式下运行时将其立即数补丁为 4，
     * 16 位模式保持原值，据此判断色深模式
     */
    constexpr uintptr_t GameBitDepthAddress = 0x5FA22B;

    // ============================================================
    // Section 2: 色深适配（布局/渲染共用的调色板取色）
    // ============================================================
    /** @brief 16 位色深：从调色板获取颜色 */
    static DWORD __fastcall getColor16(const TPalette16& palette, int colorIdx)
    {
        return palette.Palette[colorIdx];
    }

    /** @brief RGB565 → RGB888 扩位（32 位色深下回退取色用） */
    static inline DWORD rgb565ToRgb888(WORD c)
    {
        const DWORD r = (c >> 11) & 0x1F;
        const DWORD g = (c >> 5) & 0x3F;
        const DWORD b = c & 0x1F;
        return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
    }

    /**
     * @brief 32 位色深：从 HD Mod 32 位调色板获取颜色
     *
     * 注意：字体结构缓存的调色板指针（pFont->p16）可能仍指向 16 位调色板对象。
     * ERA/WoG 在 HD Mod 替换全局调色板之前加载字体，此时该对象的 +0x218 处是
     * 调色板最后两个颜色（RGB565 数据），而 HD::Palette 会把它当 pal32 指针
     * 解引用（如 0xFFFF07FF），导致访问违规（ERA 环境下实测崩溃）。
     *
     * 因此优先使用全局 32 位调色板；两个候选对象都不具备 HD 32 位资源类型时，
     * 回退到 16 位取色并扩位到 RGB888，保证 32 位渲染路径下颜色格式正确。
     */
    static DWORD __fastcall getColor32(const TPalette16& palette, int colorIdx)
    {
        const TPalette16* src = (gGamePalette != nullptr && isHDMod32Bit(gGamePalette))
            ? gGamePalette
            : &palette;

        if (isHDMod32Bit(src))
            return reinterpret_cast<const HD::Palette*>(src)->data32()[colorIdx];

        return rgb565ToRgb888(palette.Palette[colorIdx]);
    }

    /** @brief 色深自适应：获取调色板颜色的函数指针（默认 16 位，DDraw 初始化后更新） */
    DWORD(__fastcall* gGetColor)(const TPalette16& palette, int colorIdx) = getColor16;
    // ============================================================
    // Section 3: 文本扫描与处理
    // ============================================================

    /**
     * @brief 统一字符测宽入口（按字体配置分派原版位图 / GDI 两条路径）
     *
     * 原版 ASCII 模式下可打印 ASCII（0x20-0x7E）的宽度取自游戏字体的
     * 间距表（leftMargin + span + rightMargin），与原版位图绘制一致；
     * 其余字符（GBK 汉字或全 GDI 模式的 ASCII）走 ExtFont 的 GDI 测量缓存。
     */
    static inline int charWidthPx(const H3FontExt* pFont, wchar_t ch)
    {
        if (pFont->ext_data->UsesOriginalAscii(ch))
        {
            const auto& spacing = pFont->fr.abc[static_cast<uint8_t>(ch)];
            return spacing.abcA + spacing.abcB + spacing.abcC;
        }
        return pFont->ext_data->GetAdvanceWidth(ch);
    }


    /** @brief 布局核心的生产测宽实现（绑定 H3FontExt 字形/间距表） */
    struct font_width
    {
        const H3FontExt* font;
        int operator()(wchar_t ch) const { return charWidthPx(font, ch); }
    };

    /**
     * @brief 布局核心的生产颜色解析实现（绑定命名颜色表与当前色深）
     *
     * 支持的颜色码：
     *   - 命名颜色（{~Red}，大小写不敏感，查 H3CN.toml 的 [TextColor] 表）
     *   - 十六进制（{~#RRGGBB} / {~RRGGBB} / {~RRGGBBAA}，可省略 #）
     *   - EML 复合标签（{~text color=Red align=center}，提取 color 属性）
     *
     * @param code     颜色码文本（{~ 与 } 之间的内容）
     * @param fallback 未命中时的回退颜色（默认色）
     * @param is32bit  是否 32 位色深（决定是否做 888→565 转换）
     */
    struct color_resolver
    {
        DWORD operator()(const char* code, std::size_t len, DWORD fallback, bool is32bit) const
        {
            if (len == 0)
                return fallback;

            // EML 复合标签 {~text color=X align=Y}：提取 color 属性值后按普通颜色码解析
            if (isEmlTextTag(code, len))
            {
                const char* value = nullptr;
                std::size_t valueLen = 0;
                if (!findEmlAttr(code + 4, len - 4, "color", 5, value, valueLen))
                    return fallback;
                return resolveValue(value, valueLen, fallback, is32bit);
            }

            return resolveValue(code, len, fallback, is32bit);
        }

        /** @brief 解析颜色值：十六进制（6/8 位，可带 #）或命名颜色（大小写不敏感） */
        DWORD resolveValue(const char* code, std::size_t len, DWORD fallback, bool is32bit) const
        {
            DWORD rgb = 0;
            if (parseHexColorCode(code, len, rgb))
                return is32bit ? rgb : static_cast<DWORD>(rgb888ToRgb565(rgb));

            std::string name(code, len);
            for (char& c : name)
                c = asciiToLower(c);
            const auto it = gTextColorMap.find(name);
            if (it == gTextColorMap.end())
                return fallback;
            return is32bit ? it->second.rgb888 : it->second.rgb565;
        }
    };

    /**
     * @brief 解析并准备内嵌图片锚点公共数据（生产实现）
     *
     * 流程：解析 {~>...} 标签 → 加载 DEF（带缓存）→ 查询帧尺寸 →
     * 按 NBSP 占位宽度（fillChars = ceil(帧宽 / NBSP 宽)）填充锚点。
     *
     * @return 解析或资源加载失败返回 false（调用方按零宽跳过）
     */
    static bool prepareImageAnchor(const H3FontExt* pFont, const char* code, std::size_t len, ImageAnchor& anchor)
    {
        InlineImageSpec spec;
        if (!parseInlineImageTag(code, len, spec))
            return false;

        CSprite* sprite = loadSpriteCached(spec.defName);
        if (!sprite)
            return false;

        int frameW = 0;
        int frameH = 0;
        if (!getFrameMetrics(sprite, spec.group, spec.frame, frameW, frameH))
            return false;

        const int nbsp = nbspWidth(*pFont);
        const int fillChars = (frameW + nbsp - 1) / nbsp;
        const int fontHeight = std::max(1, static_cast<int>(pFont->fr.height));

        anchor.sprite = sprite;
        anchor.group = spec.group;
        anchor.frame = spec.frame;
        anchor.width = fillChars * nbsp;
        anchor.height = frameH;
        anchor.offsetX = (anchor.width - frameW) / 2;
        anchor.isBlock = spec.isBlock;
        anchor.mirror = spec.mirror;
        anchor.valign = spec.valign;

        if (spec.isBlock)
        {
            // 块级图片：height 属性限制切片总高，按字体行高拆分为多行切片
            const int sliceTotalH = (spec.sliceHeight > 0) ? std::min(spec.sliceHeight, frameH) : frameH;
            const int numLines = std::max(1, (sliceTotalH + fontHeight - 1) / fontHeight);

            int offY = 0;
            switch (spec.valign)
            {
            case ImageVAlign::Bottom: offY = numLines * fontHeight - sliceTotalH; break;
            case ImageVAlign::Middle: offY = (numLines * fontHeight - sliceTotalH) / 2; break;
            case ImageVAlign::Top:
            default:                  offY = 0; break;
            }
            if (spec.hasOffsetY)
                offY = spec.offsetY;

            anchor.numLines = numLines;
            anchor.sliceStartY = 0; // 与 ERA 行重建公式一致（from-y 暂不支持）
            anchor.sliceHeight = sliceTotalH;
            anchor.offsetY = offY;
        }
        else
        {
            // 行内图片：完整帧，垂直对齐在绘制时按行高计算
            anchor.numLines = 1;
            anchor.sliceStartY = 0;
            anchor.sliceHeight = frameH;
            anchor.offsetY = 0;
        }
        return true;
    }

    /** @brief 生产图片度量实现（供折行/测量使用） */
    struct def_image_metrics
    {
        const H3FontExt* font;

        bool operator()(const char* code, std::size_t len, ImageTokenMetrics& out) const
        {
            ImageAnchor anchor;
            if (!prepareImageAnchor(font, code, len, anchor))
                return false;

            out.width = anchor.width;
            out.isBlock = anchor.isBlock;
            out.numLines = anchor.numLines;
            return true;
        }
    };

    /** @brief 生产图片锚点收集实现（供预处理阶段使用） */
    struct def_image_collector
    {
        const H3FontExt* font;

        bool operator()(const char* code, std::size_t len, ImageAnchor& anchor) const
        {
            return prepareImageAnchor(font, code, len, anchor);
        }
    };
    // ============================================================
    // Section 4: 布局缓存
    // ============================================================
    //
    // UI 文本（按钮、状态栏、对话框等）每帧重绘但内容大多不变。
    // 缓存"拆行 + 颜色码剥离"的预处理结果，命中时跳过全部解析开销。
    // 键为 (文本内容 + 拆行宽度 + 字体 + 颜色 + 色深)，覆盖所有影响输出的输入。

    /** @brief 布局缓存条目 */
    struct LayoutEntry
    {
        uint64_t hash = 0;             ///< 文本 FNV-1a 哈希（0 = 空槽位）
        std::string text;              ///< 原始文本（精确比较，防哈希碰撞误命中）
        ExtFont* font = nullptr;       ///< 扩展字库
        int box_width = 0;              ///< 拆行宽度
        DWORD default_color = 0;        ///< 默认颜色（当前色深格式）
        DWORD highlight_color = 0;      ///< 高亮颜色
        bool is32bit = false;          ///< 创建时的色深
        std::vector<CleanLine> lines;  ///< 预处理结果
    };

    static constexpr std::size_t LayoutCacheSize = 128;
    static LayoutEntry g_LayoutCache[LayoutCacheSize];

    /** @brief FNV-1a 64 位字符串哈希 */
    static uint64_t fnv1aHash(const char* s)
    {
        uint64_t h = 0xcbf29ce484222325ULL;
        while (*s)
        {
            h ^= static_cast<uint8_t>(*s++);
            h *= 0x100000001b3ULL;
        }
        return h;
    }

    /** @brief 清空布局缓存（分辨率/色深切换、字体表重建时调用） */
    void clearLayoutCache()
    {
        for (auto& slot : g_LayoutCache)
        {
            slot.hash = 0;
            slot.font = nullptr;
            slot.text.clear();
            slot.lines.clear();
        }
    }

    /**
     * @brief 检测并更新色深模式（16/32 位）
     *
     * 判定依据（任一成立即为 32 位）：
     *   1. NH3API 的 HD Mod 兼容判定：全局调色板资源已被替换为 32 位调色板
     *      （isHDMod32Bit 检查资源的 RType_palette32 类型）
     *   2. 游戏位深标志：HD Mod 在 32 位模式下运行时将 0x5FA22B 处的位深立即数补丁为 4
     *
     * DDraw 初始化 hook 中调用一次，此后每次文本绘制前重新调用：
     * 绘制时机足够晚（HD Mod 已完全初始化），判定最可靠，并能感知运行中的色深切换。
     */
    void updateBitMode()
    {
        const TPalette16* pal = gGamePalette;
        const bool hd32 = (pal != nullptr) && isHDMod32Bit(pal);
        const uint8_t depthFlag = *reinterpret_cast<const uint8_t*>(GameBitDepthAddress);
        const bool is32 = hd32 || (depthFlag == 4);
        const int newMode = is32 ? 1 : 0;
        if (gBitMode != newMode)
        {
            gBitMode = newMode;
            gGetColor = is32 ? getColor32 : getColor16;
            clearLayoutCache();
            logInfo("色深模式：%s（全局调色板 0x%08X，resType=%d，HD32=%d，位深标志=%u）",
                is32 ? "32 位" : "16 位",
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(pal)),
                pal ? static_cast<int>(pal->resType) : -1,
                hd32 ? 1 : 0,
                static_cast<unsigned>(depthFlag));
        }
    }

    /**
     * @brief 获取文本的预处理布局（缓存命中则零解析，未命中则构建并填充缓存）
     */
    const std::vector<CleanLine>& getOrCreateLayout(H3FontExt* pFont, LPCSTR szText, int iBoxWidth,
        DWORD default_color, DWORD highlight_color, bool is32bit)
    {
        auto* extFont = pFont->ext_data;
        const uint64_t hash = fnv1aHash(szText);
        LayoutEntry& slot = g_LayoutCache[hash % LayoutCacheSize];

        if (slot.hash == hash
            && slot.font == extFont
            && slot.box_width == iBoxWidth
            && slot.default_color == default_color
            && slot.highlight_color == highlight_color
            && slot.is32bit == is32bit
            && slot.text == szText)
        {
            return slot.lines;
        }

        // 未命中：重建该槽位
        slot.hash = hash;
        slot.text = szText;
        slot.font = extFont;
        slot.box_width = iBoxWidth;
        slot.default_color = default_color;
        slot.highlight_color = highlight_color;
        slot.is32bit = is32bit;

        // 函数级复用缓冲（跨调用保留容量，避免每帧堆分配；不与其他入口共享）
        static std::vector<TextLineStruct> scratch;
        scratch.clear();
        splitTextIntoLines(font_width{pFont}, def_image_metrics{pFont}, gTextColorEnabled, szText, iBoxWidth, scratch);

        slot.lines.clear();
        slot.lines.reserve(scratch.size());
        for (const auto& rawLine : scratch)
        {
            slot.lines.emplace_back();
            preprocessLine(font_width{pFont}, color_resolver{}, def_image_collector{pFont}, gTextColorEnabled, rawLine, default_color, highlight_color, is32bit, slot.lines.back());
        }
        return slot.lines;
    }
    // ============================================================
    // Section 6: Hook 包装函数
    // ============================================================
    //
    // 以下函数是对游戏引擎原始字体接口的 Hook 替换。
    // 签名与原始函数一致（__stdcall，首个参数为 HiHook*），
    // 内部使用 H3FontExt 扩展字体实现中文支持。

    // ---- 测量结果缓存 ----
    // 输入框绘制、对话框尺寸计算会频繁调用测量类 Hook；
    // 文本不变时直接复用结果，避免重复遍历解析。

    /** @brief 测量缓存条目 */
    struct MeasureCacheEntry
    {
        uint64_t hash = 0;             ///< 文本 FNV-1a 哈希（0 = 空槽位）
        std::string text;              ///< 原始文本（精确比较，防哈希碰撞误命中）
        const ExtFont* font = nullptr; ///< 创建时的扩展字库
        int box_width = 0;              ///< 拆行宽度（不涉及的测量恒为 0）
        int value = 0;                 ///< 测量结果
    };

    static constexpr std::size_t MeasureCacheSize = 64;

    /**
     * @brief 带缓存的测量入口（直接映射缓存，键 = 文本 + 字体 + 宽度）
     */
    template <typename ComputeFunc>
    static int cachedMeasure(MeasureCacheEntry(&cache)[MeasureCacheSize],
        H3FontExt* pFont, const char* text, int box_width, ComputeFunc&& compute)
    {
        const uint64_t hash = fnv1aHash(text);
        MeasureCacheEntry& slot = cache[hash % MeasureCacheSize];

        if (slot.hash == hash
            && slot.font == pFont->ext_data
            && slot.box_width == box_width
            && slot.text == text)
        {
            return slot.value;
        }

        slot.hash = hash;
        slot.text = text;
        slot.font = pFont->ext_data;
        slot.box_width = box_width;
        slot.value = compute();
        return slot.value;
    }

    /**
     * @brief Hook: 将文本拆分为多行（返回 exe_vector<exe_string>）
     *
     * 替换游戏原始的文本拆分逻辑，支持双字节字符和颜色码。
     */
    void __stdcall H3Font_SplitTextIntoLines(HiHook* /*h*/, H3FontExt* pFont, char* szText, const int iBoxWidth,
        exe_vector<exe_string>& lines)
    {
        lines.clear();

        if (!szText || !*szText || !pFont->ext_data)
            return;

        // 独立复用缓冲（不与布局缓存的拆行缓冲共享，避免潜在重入相互踩踏）
        static std::vector<TextLineStruct> scratch;
        scratch.clear();
        splitTextIntoLines(font_width{pFont}, def_image_metrics{pFont}, gTextColorEnabled, szText, iBoxWidth, scratch);
        for (const auto& line : scratch)
        {
            lines.emplace_back(line.text.c_str());
        }
    }

    /**
     * @brief Hook: 计算文本中最长单词的宽度
     *
     * 用于消息框等需要根据内容自适应宽度的场景。
     * 结果受 gBoxWidthMin / gBoxWidthMax 约束。
     */
    int __stdcall H3Font_GetWordWidth(HiHook* /*h*/, H3FontExt* pFont, char* szText)
    {
        if (!szText || !*szText || !pFont->ext_data)
            return 0;

        static MeasureCacheEntry cache[MeasureCacheSize];
        return cachedMeasure(cache, pFont, szText, 0, [&]()
        {
            int maxWidth = 0;
            int wordWidth = 0;

            forEachVisibleChar(font_width{pFont}, def_image_metrics{pFont}, szText,
                [&](TokenType type, int width, int /*bytes*/, const char* /*code*/, std::size_t /*codeLen*/)
                {
                    if (type == TokenType::NEWLINE || type == TokenType::SPACE || type == TokenType::DOUBLE_BYTE)
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
            return std::clamp(maxWidth, gBoxWidthMin, gBoxWidthMax);
        });
    }

    /**
     * @brief Hook: 计算文本在指定宽度下的实际换行后最大行宽
     *
     * 模拟换行行为，返回所有行中最宽一行的像素宽度。
     */
    int __stdcall H3Font_GetLineWrapWidth(HiHook* /*h*/, H3FontExt* pFont, char* szText, int iBoxWidth)
    {
        if (!szText || !*szText || !pFont->ext_data)
            return 0;

        static MeasureCacheEntry cache[MeasureCacheSize];
        return cachedMeasure(cache, pFont, szText, iBoxWidth, [&]()
        {
            int maxWidth = 0;
            int line_width = 0;

            forEachVisibleChar(font_width{pFont}, def_image_metrics{pFont}, szText,
                [&](TokenType type, int width, int /*bytes*/, const char* /*code*/, std::size_t /*codeLen*/)
                {
                    if (type == TokenType::NEWLINE)
                    {
                        if (line_width > maxWidth)
                            maxWidth = line_width;
                        line_width = 0;
                    }
                    else
                    {
                        if (line_width + width > iBoxWidth)
                        {
                            if (line_width > maxWidth)
                                maxWidth = line_width;
                            line_width = 0;
                        }
                        line_width += width;
                    }
                    return true;
                });

            if (line_width > maxWidth)
                maxWidth = line_width;
            return maxWidth;
        });
    }

    /**
     * @brief Hook: 计算文本在指定宽度下的总行数
     */
    int __stdcall H3Font_GetLineCount(HiHook* /*h*/, H3FontExt* pFont, char* szText, int iBoxWidth)
    {
        if (!szText || !*szText || !pFont->ext_data)
            return 0;

        static MeasureCacheEntry cache[MeasureCacheSize];
        return cachedMeasure(cache, pFont, szText, iBoxWidth, [&]()
        {
            int lineCount = 1;
            int line_width = 0;

            forEachVisibleChar(font_width{pFont}, def_image_metrics{pFont}, szText,
                [&](TokenType type, int width, int /*bytes*/, const char* code, std::size_t codeLen)
                {
                    if (type == TokenType::NEWLINE)
                    {
                        ++lineCount;
                        line_width = 0;
                    }
                    else if (type == TokenType::IMAGE)
                    {
                        ImageTokenMetrics info;
                        if (!def_image_metrics{pFont}(code, codeLen, info))
                            return true; // 解析失败：零宽跳过

                        if (info.isBlock)
                        {
                            // 块级图片：前后强制换行 + 额外占位行（与 splitTextIntoLines 一致）
                            if (line_width > 0)
                                ++lineCount;
                            lineCount += info.numLines - 1;
                            line_width = 0;
                            return true;
                        }

                        // 行内图片按占位宽度参与折行计算
                        if (line_width + info.width > iBoxWidth)
                        {
                            ++lineCount;
                            line_width = 0;
                        }
                        line_width += info.width;
                    }
                    else
                    {
                        if (line_width + width > iBoxWidth)
                        {
                            ++lineCount;
                            line_width = 0; // 当前字符归入新行
                        }
                        line_width += width;
                    }
                    return true;
                });

            return lineCount;
        });
    }

    /**
     * @brief Hook: 计算文本首行的像素宽度（遇换行符停止）
     *
     * 原始引擎实现按字节查 abc 表，对 GBK 双字节字符宽度计算错误。
     * 该函数被消息框特殊文本宽度计算（DoNormalDialog）、文本输入框
     * 裁剪/滚动偏移（textEntryWidget）等使用，替换后中文字符宽度准确。
     */
    int __stdcall H3Font_LineWidth(HiHook* /*h*/, H3FontExt* pFont, char* szText)
    {
        if (!szText || !*szText || !pFont->ext_data)
            return 0;

        static MeasureCacheEntry cache[MeasureCacheSize];
        return cachedMeasure(cache, pFont, szText, 0, [&]()
        {
            int line_width = 0;
            forEachVisibleChar(font_width{pFont}, def_image_metrics{pFont}, szText,
                [&](TokenType type, int width, int /*bytes*/, const char* /*code*/, std::size_t /*codeLen*/)
                {
                    if (type == TokenType::NEWLINE)
                        return false;
                    line_width += width;
                    return true;
                });
            return line_width;
        });
    }

    /**
     * @brief Hook: 计算文本中最长行的像素宽度（不换行）
     */
    int __stdcall H3Font_GetLineWidth(HiHook* /*h*/, H3FontExt* pFont, char* szText)
    {
        if (!szText || !*szText || !pFont->ext_data)
            return 0;

        static MeasureCacheEntry cache[MeasureCacheSize];
        return cachedMeasure(cache, pFont, szText, 0, [&]()
        {
            int maxWidth = 0;
            int line_width = 0;

            forEachVisibleChar(font_width{pFont}, def_image_metrics{pFont}, szText,
                [&](TokenType type, int width, int /*bytes*/, const char* /*code*/, std::size_t /*codeLen*/)
                {
                    if (type == TokenType::NEWLINE)
                    {
                        if (line_width > maxWidth)
                            maxWidth = line_width;
                        line_width = 0;
                    }
                    else
                    {
                        line_width += width;
                    }
                    return true;
                });

            if (line_width > maxWidth)
                maxWidth = line_width;
            return maxWidth;
        });
    }

    /**
     * @brief Hook: 计算字符串的总像素宽度（单行累计）
     *
     * 原版按单字节查 abc 表，对 GBK 文本宽度计算错误。该函数被
     * TBuyBuildWindow::set_prerequisite_text（先决条件文本折行位置判断）与
     * type_map_creation_bar（地图加载进度条文本控件宽度）调用。
     */
    int __stdcall H3Font_GetStringWidth(HiHook* /*h*/, H3FontExt* pFont, char* szText)
    {
        if (!szText || !*szText || !pFont->ext_data)
            return 0;

        static MeasureCacheEntry cache[MeasureCacheSize];
        return cachedMeasure(cache, pFont, szText, 0, [&]()
        {
            int totalWidth = 0;
            forEachVisibleChar(font_width{pFont}, def_image_metrics{pFont}, szText,
                [&](TokenType /*type*/, int width, int /*bytes*/, const char* /*code*/, std::size_t /*codeLen*/)
                {
                    totalWidth += width;
                    return true;
                });
            return totalWidth;
        });
    }

    /**
     * @brief Hook: 单字节字符宽度查询（GBK 感知状态机）
     *
     * 原版实现按单字节查 abc 表；唯一调用者 type_dialog_icon::set 会逐字节
     * 顺序遍历字符串并累加宽度。本实现以"待配对首字节"状态处理双字节字符：
     * 首字节挂起并返回 0，配对到合法尾字节时返回扩展字形的步进宽度；
     * ASCII 与原版一致。
     *
     * 字符编码判定统一走 isSingleByte/isDBCSLeadByte/GbkToWchar，
     * 后续扩展其他 DBCS 代码页时只需替换这些辅助实现。
     */
    int __stdcall H3Font_GetCharacterWidth(HiHook* /*h*/, H3FontExt* pFont, uint8_t currChar)
    {
        // 待配对的 DBCS 首字节（0 = 无）；调用者顺序遍历保证配对顺序
        static uint8_t pendingLead = 0;

        const uint8_t lead = pendingLead;
        pendingLead = 0;

        // 尾字节：配对成功，返回整个双字节字符的宽度
        if (lead != 0 && isDBCSLeadByte(lead, currChar))
        {
            return pFont->ext_data
                ? pFont->ext_data->GetAdvanceWidth(ExtFont::GbkToWchar(lead, currChar))
                : 0;
        }

        // DBCS 首字节：挂起，宽度延迟到尾字节返回
        if (!isSingleByte(currChar))
        {
            pendingLead = currChar;
            return 0;
        }

        const auto& spacing = pFont->fr.abc[currChar];
        return spacing.abcA + spacing.abcB + spacing.abcC;
    }

} // namespace H3FontExtension