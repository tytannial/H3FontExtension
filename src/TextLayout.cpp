//===----------------------------------------------------------------------===//
// 文本布局与测量：颜色码解析、拆行、布局/测量缓存、宽度与行数 Hook
//
// 像素混合与字符绘制见 TextRender.cpp，插件入口见 H3FontExtension.cpp。
//===----------------------------------------------------------------------===//

#include "H3FontExtensionInternal.h"

#include <algorithm>
#include <charconv>
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

    /** @brief 32 位色深：从调色板获取颜色（HD Mod 32 位调色板指针布局） */
    static DWORD __fastcall getColor32(const TPalette16& palette, int colorIdx)
    {
        return reinterpret_cast<const HD::Palette&>(palette).data32()[colorIdx];
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

    /**
     * @brief 遍历文本中所有"可见 token"，自动跳过颜色码 {~RRGGBB} / { } / }
     *
     * 颜色码格式：
     *   - {~#RRGGBB}  十六进制颜色（或 {~colorname} 命名颜色）
     *   - { / }        高亮切换（{ 进入高亮，} 恢复默认）
     *
     * 这些控制字符在遍历中被跳过，回调只接收实际可见字符。
     *
     * @param pFont   字体指针
     * @param szText  输入文本（以 '\0' 结尾）
     * @param fn      回调：fn(TokenType type, int width, int charBytes) -> bool
     *                返回 false 可提前终止遍历
     * @return        遍历结束时的指针位置（调用方可用于恢复 szText 推进）
     */
    template<typename Func>
    static const char* forEachVisibleChar(H3FontExt* pFont, LPCSTR szText, Func&& fn)
    {
        while (*szText)
        {
            uint8_t code = static_cast<uint8_t>(*szText);

            // -------- 跳过颜色码 --------
            if (code == '{' || code == '}')
            {
                if (code == '{' && *(szText + 1) == '~')
                {
                    szText += 2; // 跳过 "{~"
                    while (*szText && *szText != '}' && *szText != ' ' && *szText != '\n')
                        ++szText;
                    if (*szText == '}') // 跳过 '}'
                        ++szText;
                    continue;
                }
                ++szText;
                continue;
            }

            // -------- 换行 --------
            if (code == '\n')
            {
                if (!fn(TokenType::NEWLINE, 0, 1))
                    return szText;
                ++szText;
                continue;
            }

            // -------- 空格 --------
            if (code == ' ')
            {
                const int spaceW = charWidthPx(pFont, L' ');
                if (!fn(TokenType::SPACE, spaceW, 1))
                    return szText;
                ++szText;
                continue;
            }

            // -------- 单字节字符 --------
            if (isSingleByte(code))
            {
                const wchar_t wch = (wchar_t)code;
                const int charW = charWidthPx(pFont, wch);
                if (!fn(TokenType::SINGLE_BYTE, charW, 1))
                    return szText;
                ++szText;
                continue;
            }

            // -------- 双字节字符 --------
            uint8_t nc = static_cast<uint8_t>(*(szText + 1));
            if (isDBCSLeadByte(code, nc))
            {
                const wchar_t wch = ExtFont::GbkToWchar(code, nc);
                const int charW = charWidthPx(pFont, wch);
                if (!fn(TokenType::DOUBLE_BYTE, charW, 2))
                    return szText;
                szText += 2;
            }
            else
            {
                ++szText; // 无效的双字节首字节，跳过
            }
        }
        return szText;
    }

    /**
     * @brief 增量更新颜色状态（扫描追加到行缓冲的内容）
     *
     * 追踪 {~...} / { / } 的状态变化，维护"当前行末尾活跃的颜色状态"，
     * 供折行时向下一行继承。相比每次折行重扫整个行缓冲（O(n²)），
     * 增量扫描只处理新追加的片段，总复杂度 O(n)。
     *
     * @param p               扫描起点
     * @param end             扫描终点
     * @param activeTag       [in/out] 活跃的自定义颜色标签（含大括号）
     * @param highlightActive [in/out] 活跃的高亮状态
     */
    static void updateColorState(const char* p, const char* end, std::string& activeTag, bool& highlightActive)
    {
        while (p < end)
        {
            const uint8_t b = static_cast<uint8_t>(*p);

            // 跳过双字节 GBK 字符，避免第二字节碰巧为 0x7B/0x7D 造成误判
            if (!isSingleByte(b) && (p + 1 < end))
            {
                const uint8_t nb = static_cast<uint8_t>(*(p + 1));
                if (isDBCSLeadByte(b, nb))
                {
                    p += 2;
                    continue;
                }
            }

            if (*p == '{' && (p + 1 < end) && *(p + 1) == '~')
            {
                // 自定义颜色标签 {~ColorName}：颜色关闭时等同高亮 {
                const char* start = p;
                p += 2;
                while (p < end && *p != '}')
                    ++p;
                if (p < end && *p == '}')
                    ++p;
                if (gTextColorEnabled)
                {
                    activeTag.assign(start, p - start);
                    highlightActive = false;
                }
                else
                {
                    highlightActive = true;
                    activeTag.clear();
                }
            }
            else if (*p == '{')
            {
                highlightActive = true;
                activeTag.clear();
                ++p;
            }
            else if (*p == '}')
            {
                highlightActive = false;
                activeTag.clear();
                ++p;
            }
            else
            {
                ++p;
            }
        }
    }

    /**
     * @brief 将一行原始文本（含颜色码）预处理为 CleanLine
     *
     * 剥离所有颜色控制字符 {~...} / { / }，生成纯可见字符序列，
     * 同时将颜色变更信息记录为 ColorStop 列表。
     *
     * @param pFont          字体（用于测量每个字符的步进宽度）
     * @param src            原始拆行结果（含颜色码）
     * @param default_color   默认渲染颜色（当前色深格式）
     * @param highlight_color { } 切换的高亮颜色
     * @param is32bit        当前是否为 32 位色深模式
     * @param dst            [out] 预处理结果
     */
    static void preprocessLine(H3FontExt* pFont, const TextLineStruct& src, DWORD default_color, DWORD highlight_color, bool is32bit, CleanLine& dst)
    {
        dst.line_width = src.line_width;
        dst.text.clear();
        dst.stops.clear();
        dst.stops.reserve(4); // 典型行的颜色切换次数很少，避免小规模多次扩容
        dst.advances.clear();

        if (src.line_width == 0)
            return;

        const std::string& raw = src.text;
        const std::size_t len = raw.size();

        // 预分配：clean text 最多和 raw 一样长，通常更短
        dst.text.reserve(len);
        dst.advances.reserve(len);

        DWORD curColor = default_color;

        for (std::size_t i = 0; i < len; )
        {
            const uint8_t code = static_cast<uint8_t>(raw[i]);

            // ============ '{' —— 进入高亮或自定义颜色 ============
            if (code == '{')
            {
                DWORD newColor = highlight_color;
                ++i;

                if (i < len && raw[i] == '~')
                {
                    // 仅在颜色功能开启时解析自定义颜色值，否则跳过整个标签
                    if (gTextColorEnabled)
                    {
                        ++i;
                        const std::size_t codeStart = i;
                        while (i < len && raw[i] != '}')
                            ++i;

                        if (i > codeStart)
                        {
                            if (raw[codeStart] == '#')
                            {
                                DWORD c = 0;
                                auto [_, ec] = std::from_chars(
                                    raw.data() + codeStart + 1,
                                    raw.data() + i,
                                    c, 16);
                                if (ec == std::errc())
                                {
                                    // 十六进制颜色按当前色深转换（修复 16 位模式下颜色错误）
                                    newColor = is32bit ? c : static_cast<DWORD>(rgb888ToRgb565(c));
                                }
                                else
                                {
                                    newColor = default_color;
                                }
                            }
                            else
                            {
                                const std::string key(raw.data() + codeStart, i - codeStart);
                                const auto it = gTextColorMap.find(key);
                                if (it != gTextColorMap.end())
                                    newColor = is32bit ? it->second.rgb888 : it->second.rgb565;
                                else
                                    newColor = default_color;
                            }
                        }
                        else
                        {
                            newColor = default_color;
                        }
                    }
                    else
                    {
                        // 颜色功能关闭：跳到 '}' 之后，颜色保持 highlight_color（黄色）
                        ++i;
                        while (i < len && raw[i] != '}')
                            ++i;
                    }

                    // 跳过 '}'
                    if (i < len && raw[i] == '}')
                        ++i;
                }

                if (newColor != curColor)
                {
                    curColor = newColor;
                    dst.stops.push_back({ dst.text.size(), curColor });
                }
                continue;
            }

            // ============ '}' —— 恢复默认颜色 ============
            if (code == '}')
            {
                if (curColor != default_color)
                {
                    curColor = default_color;
                    dst.stops.push_back({ dst.text.size(), curColor });
                }
                ++i;
                continue;
            }

            // ============ 可见字符：拷贝到 clean text（同时预计算步进宽度） ============
            if (isSingleByte(code))
            {
                dst.text.push_back(static_cast<char>(code));
                dst.advances.push_back(static_cast<uint16_t>(
                    charWidthPx(pFont, static_cast<wchar_t>(code))));
                ++i;
            }
            else
            {
                // 双字节字符：先检查下一字节是否存在且有效
                if (i + 1 < len)
                {
                    const uint8_t nextCode = static_cast<uint8_t>(raw[i + 1]);
                    if (isDBCSLeadByte(code, nextCode))
                    {
                        dst.text.push_back(raw[i]);
                        dst.text.push_back(raw[i + 1]);
                        dst.advances.push_back(static_cast<uint16_t>(
                            charWidthPx(pFont, ExtFont::GbkToWchar(code, nextCode))));
                        i += 2;
                        continue;
                    }
                }
                // 无效双字节或尾部不完整，跳过首字节
                ++i;
            }
        }
    }

    /**
     * @brief 将文本按指定宽度拆分为多行
     *
     * 算法流程（按词为单位）：
     *   1. 跳过行首空白和换行符
     *   2. 用 forEachVisibleChar 确定下一个"词"的边界
     *   3. 若当前行放不下该词，输出当前行
     *   4. 若词本身超宽，逐字符强制拆行
     *   5. 将词追加到当前行
     *
     * 颜色状态在追加内容时增量维护，折行推送后 O(1) 继承到下一行。
     *
     * @param pFont     ASCII 字体指针
     * @param szText    输入文本（以 '\0' 结尾）
     * @param iBoxWidth 文本框像素宽度
     * @param lines     [out] 拆分结果（含颜色码的原始行）
     */
    static void __stdcall splitTextIntoLines(H3FontExt* pFont, LPCSTR szText, const int iBoxWidth,
        std::vector<TextLineStruct>& lines)
    {
        if (!*szText)
            return;

        // 缓存常用参数
        const int spaceWidth = charWidthPx(pFont, L' ');

        std::string lineBuf;
        int line_width = 0;

        // 当前行末尾的活跃颜色状态（随追加内容增量更新）
        std::string activeTag;
        bool highlightActive = false;

        // 推送当前行
        // inherit = true ：自动折行，颜色状态继承到下一行
        // inherit = false：显式换行符，颜色状态重置
        auto pushLine = [&](bool inherit)
        {
            lines.push_back({ std::move(lineBuf), line_width });
            lineBuf.clear();
            line_width = 0;

            if (inherit)
            {
                if (!activeTag.empty())
                    lineBuf = activeTag;
                else if (highlightActive)
                    lineBuf.push_back('{');
            }
            else
            {
                activeTag.clear();
                highlightActive = false;
            }
        };

        // ============ 主循环：按词为单位处理 ============
        while (*szText)
        {
            // ---- 阶段 1：处理行首空白和换行符 ----
            int blankCount = 0;
            int blankWidth = 0;

            while (true)
            {
                uint8_t ch = static_cast<uint8_t>(*szText);
                if (ch == ' ')
                {
                    blankWidth += spaceWidth;
                    ++blankCount;
                    ++szText;
                }
                else if (ch == '\n')
                {
                    // 换行：当前行强制输出，重置状态
                    pushLine(false);
                    blankCount = 0;
                    blankWidth = 0;
                    ++szText;
                }
                else
                {
                    break;
                }
            }

            if (!*szText)
                break;

            // ---- 阶段 2：取词——使用 forEachVisibleChar 确定词边界 ----
            int wordWidth = 0;
            const char* wordStart = szText;

            // realEnd 指向第一个 Space/Newline（触发停止处），或 '\0'
            const char* realEnd = forEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*charBytes*/)
                {
                    if (type == TokenType::SPACE || type == TokenType::NEWLINE)
                        return false;
                    wordWidth += width;
                    return true;
                });

            // 没有可见字符（如纯颜色码后跟空格/结尾）：安全推进 szText
            if (wordWidth == 0)
            {
                // 仍需更新颜色状态，保证跨行继承正确
                updateColorState(wordStart, realEnd, activeTag, highlightActive);
                szText = (realEnd > wordStart) ? realEnd : (wordStart + 1);
                continue;
            }

            // ---- 阶段 3：拆行判定 ----
            if (line_width + wordWidth + blankWidth > iBoxWidth)
            {
                if (line_width > 0)
                    pushLine(true);
                blankCount = 0;
                blankWidth = 0;

                // 如果词本身就超宽，逐字符拆行
                if (wordWidth > iBoxWidth)
                {
                    LPCSTR p = wordStart;
                    while (p < realEnd)
                    {
                        const uint8_t code = static_cast<uint8_t>(*p);

                        // ---- 颜色标记：保留到行缓冲但不计入宽度 ----
                        if (code == '{' || code == '}')
                        {
                            const char* tagStart = p;
                            if (code == '{' && (p + 1 < realEnd) && *(p + 1) == '~')
                            {
                                p += 2; // 跳过 "{~"
                                while (p < realEnd && *p != '}')
                                    ++p;
                                if (p < realEnd && *p == '}') // 跳过 '}'
                                    ++p;
                            }
                            else
                            {
                                // 单个 { 或 }
                                ++p;
                            }
                            lineBuf.append(tagStart, p - tagStart);
                            updateColorState(tagStart, p, activeTag, highlightActive);
                            continue;
                        }

                        // ---- 可见字符：统一通过测宽入口获取宽度 ----
                        int charW = 0;
                        int charBytes = 0;
                        if (isSingleByte(code))
                        {
                            charW = charWidthPx(pFont, (wchar_t)code);
                            charBytes = 1;
                        }
                        else
                        {
                            const uint8_t nextCode = (p + 1 < realEnd) ? static_cast<uint8_t>(*(p + 1)) : 0;
                            if (isDBCSLeadByte(code, nextCode))
                            {
                                charW = charWidthPx(pFont, ExtFont::GbkToWchar(code, nextCode));
                                charBytes = 2;
                            }
                            else
                            {
                                charW = 0;
                                charBytes = 1;
                            }
                        }

                        if (line_width + charW > iBoxWidth && line_width > 0)
                            pushLine(true);

                        lineBuf.append(p, charBytes);
                        line_width += charW;
                        p += charBytes;
                    }
                    szText = realEnd;
                    continue; // 词已处理完毕，跳过阶段 4
                }
            }

            // ---- 阶段 4：将词追加到当前行 ----
            if (blankCount > 0)
                lineBuf.append(blankCount, ' ');

            // 保留颜色标记原始字节（preprocessLine 将在渲染阶段剥离）
            lineBuf.append(wordStart, static_cast<std::size_t>(realEnd - wordStart));
            line_width += wordWidth + blankWidth;
            updateColorState(wordStart, realEnd, activeTag, highlightActive);
            szText = realEnd;
        }

        // 输出最后一行
        if (line_width > 0 || !lineBuf.empty())
        {
            lines.push_back({ std::move(lineBuf), line_width });
        }
    }
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
        const bool is32 = (gGamePalette != nullptr && isHDMod32Bit()) ||
            (*reinterpret_cast<const uint8_t*>(GameBitDepthAddress) == 4);
        const int newMode = is32 ? 1 : 0;
        if (gBitMode != newMode)
        {
            gBitMode = newMode;
            gGetColor = is32 ? getColor32 : getColor16;
            clearLayoutCache();
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
        splitTextIntoLines(pFont, szText, iBoxWidth, scratch);

        slot.lines.clear();
        slot.lines.reserve(scratch.size());
        for (const auto& rawLine : scratch)
        {
            slot.lines.emplace_back();
            preprocessLine(pFont, rawLine, default_color, highlight_color, is32bit, slot.lines.back());
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
        splitTextIntoLines(pFont, szText, iBoxWidth, scratch);
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

            forEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*bytes*/)
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

            forEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*bytes*/)
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

            forEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*bytes*/)
                {
                    if (type == TokenType::NEWLINE)
                    {
                        ++lineCount;
                        line_width = 0;
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
            forEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*bytes*/)
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

            forEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*bytes*/)
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
            forEachVisibleChar(pFont, szText,
                [&](TokenType /*type*/, int width, int /*bytes*/)
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
