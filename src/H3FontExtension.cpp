#include "H3FontExtension.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <exception>
#include <unordered_map>
#include <utility>

namespace H3FontExtension
{

    // ============================================================
    // 全局状态（仅本翻译单元可见）
    // ============================================================

    static Patcher* _P = nullptr;
    static PatcherInstance* _PI = nullptr;

    /** @brief 是否启用文本颜色功能 */
    static bool IsTextColorEnable = true;

    /** @brief 命名颜色条目（加载配置时预转换两种色深，渲染期零转换开销） */
    struct NamedColor
    {
        DWORD rgb888; ///< 32 位色深格式
        WORD  rgb565; ///< 16 位色深格式
    };

    /** @brief 命名颜色映射表（从配置文件加载；仅在布局缓存未命中的拆行/预处理阶段查询） */
    static std::unordered_map<std::string, NamedColor> TextColorMap;

    /** @brief 文本框最小宽度限制 */
    static int BoxWidthMin = 0;

    /** @brief 文本框最大宽度限制 */
    static int BoxWidthMax = 0;

    /** @brief 字体名 → 扩展字库映射表（key 为小写字体名，如 "medfont.fnt"） */
    static std::unordered_map<std::string, ExtFont> g_ExtFontTable;

    /** @brief 当前色深模式：-1 = 尚未判定，0 = 16 位，1 = 32 位 */
    static int BitMode = -1;

    /**
     * @brief 游戏位深标志地址
     *
     * 原版 H3BitMode 读取的字节：HD Mod 在 32 位模式下运行时将其立即数补丁为 4，
     * 16 位模式保持原值，据此判断色深模式
     */
    constexpr uintptr_t GameBitDepthAddress = 0x5FA22B;

    // ============================================================
    // Section 2: 渲染后端（色深适配 + 像素混合）
    // ============================================================
    //
    // 游戏支持 16 位（RGB565）和 32 位（RGB888）两种色深模式。
    // GetColor 在 DDraw 初始化时绑定对应实现；
    // 像素混合通过模板参数在绘制入口处一次性分派，内循环可完全内联。

    /** @brief 16 位色深：从调色板获取颜色 */
    static DWORD __fastcall GetColor16(const TPalette16& palette, int colorIdx)
    {
        return palette.Palette[colorIdx];
    }

    /** @brief 32 位色深：从调色板获取颜色（HD Mod 32 位调色板指针布局） */
    static DWORD __fastcall GetColor32(const TPalette16& palette, int colorIdx)
    {
        return reinterpret_cast<const HD::Palette&>(palette).data32()[colorIdx];
    }

    /** @brief 色深自适应：获取调色板颜色的函数指针（默认 16 位，DDraw 初始化后更新） */
    static DWORD(__fastcall* GetColor)(const TPalette16& palette, int colorIdx) = GetColor16;

    /**
     * @brief 精确除以 255（避免整数除法指令）
     *
     * 对 0 ≤ x ≤ 255×255 恒有 floor(x/255) == (x + (x>>8) + 1) >> 8。
     * 混合运算 fg*a + bg*(255-a) 是凸组合，最大值 255×255，在精确域内。
     */
    static inline uint8_t Div255(uint32_t x)
    {
        return static_cast<uint8_t>((x + (x >> 8) + 1) >> 8);
    }

    /** @brief alpha 混合前景色（Is32=true: RGB888，false: RGB565） */
    template<bool Is32>
    static inline void BlendPixel(uint8_t* rowBuf, int col, DWORD fgColor, uint8_t alpha)
    {
        if constexpr (Is32)
        {
            DWORD* dst = reinterpret_cast<DWORD*>(rowBuf) + col;
            const DWORD bg = *dst;
            const uint32_t inv = 255u - alpha;
            const uint32_t r = Div255(((fgColor >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * inv);
            const uint32_t g = Div255(((fgColor >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * inv);
            const uint32_t b = Div255((fgColor & 0xFF) * alpha + (bg & 0xFF) * inv);
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
            const uint32_t r = Div255(fr * alpha + br * inv);
            const uint32_t g = Div255(fgg * alpha + bgg * inv);
            const uint32_t b = Div255(fb * alpha + bb * inv);
            *dst = static_cast<WORD>((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        }
    }

    /** @brief alpha 混合阴影色（Is32=true: RGB888 混合，false: 直写保持原风格） */
    template<bool Is32>
    static inline void BlendShadow(uint8_t* rowBuf, int col, DWORD shadowColor, uint8_t alpha)
    {
        if constexpr (Is32)
        {
            DWORD* dst = reinterpret_cast<DWORD*>(rowBuf) + col;
            const DWORD bg = *dst;
            const uint32_t inv = 255u - alpha;
            const uint32_t r = Div255(((shadowColor >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * inv);
            const uint32_t g = Div255(((shadowColor >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * inv);
            const uint32_t b = Div255((shadowColor & 0xFF) * alpha + (bg & 0xFF) * inv);
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
    static inline void WritePixel(uint8_t* rowBuf, int col, DWORD color)
    {
        if constexpr (Is32)
            *(reinterpret_cast<DWORD*>(rowBuf) + col) = color;
        else
            *(reinterpret_cast<WORD*>(rowBuf) + col) = static_cast<WORD>(color);
    }

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
    static inline int CharWidthPx(const H3FontExt* pFont, wchar_t ch)
    {
        if (pFont->ExtData->UsesOriginalAscii(ch))
        {
            const auto& spacing = pFont->fr.abc[static_cast<uint8_t>(ch)];
            return spacing.abcA + spacing.abcB + spacing.abcC;
        }
        return pFont->ExtData->GetAdvanceWidth(ch);
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
    static const char* ForEachVisibleChar(H3FontExt* pFont, LPCSTR szText, Func&& fn)
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
                if (!fn(TokenType::Newline, 0, 1))
                    return szText;
                ++szText;
                continue;
            }

            // -------- 空格 --------
            if (code == ' ')
            {
                const int spaceW = CharWidthPx(pFont, L' ');
                if (!fn(TokenType::Space, spaceW, 1))
                    return szText;
                ++szText;
                continue;
            }

            // -------- 单字节字符 --------
            if (IsSingleByte(code))
            {
                const wchar_t wch = (wchar_t)code;
                const int charW = CharWidthPx(pFont, wch);
                if (!fn(TokenType::SingleByte, charW, 1))
                    return szText;
                ++szText;
                continue;
            }

            // -------- 双字节字符 --------
            uint8_t nc = static_cast<uint8_t>(*(szText + 1));
            if (IsDBCSLeadByte(code, nc))
            {
                const wchar_t wch = ExtFont::GbkToWchar(code, nc);
                const int charW = CharWidthPx(pFont, wch);
                if (!fn(TokenType::DoubleByte, charW, 2))
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
    static void UpdateColorState(const char* p, const char* end, std::string& activeTag, bool& highlightActive)
    {
        while (p < end)
        {
            const uint8_t b = static_cast<uint8_t>(*p);

            // 跳过双字节 GBK 字符，避免第二字节碰巧为 0x7B/0x7D 造成误判
            if (!IsSingleByte(b) && (p + 1 < end))
            {
                const uint8_t nb = static_cast<uint8_t>(*(p + 1));
                if (IsDBCSLeadByte(b, nb))
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
                if (IsTextColorEnable)
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
     * @param defaultColor   默认渲染颜色（当前色深格式）
     * @param highlightColor { } 切换的高亮颜色
     * @param is32bit        当前是否为 32 位色深模式
     * @param dst            [out] 预处理结果
     */
    static void PreprocessLine(H3FontExt* pFont, const TextLineStruct& src, DWORD defaultColor, DWORD highlightColor, bool is32bit, CleanLine& dst)
    {
        dst.lineWidth = src.lineWidth;
        dst.text.clear();
        dst.stops.clear();
        dst.stops.reserve(4); // 典型行的颜色切换次数很少，避免小规模多次扩容
        dst.advances.clear();

        if (src.lineWidth == 0)
            return;

        const std::string& raw = src.Text;
        const std::size_t len = raw.size();

        // 预分配：clean text 最多和 raw 一样长，通常更短
        dst.text.reserve(len);
        dst.advances.reserve(len);

        DWORD curColor = defaultColor;

        for (std::size_t i = 0; i < len; )
        {
            const uint8_t code = static_cast<uint8_t>(raw[i]);

            // ============ '{' —— 进入高亮或自定义颜色 ============
            if (code == '{')
            {
                DWORD newColor = highlightColor;
                ++i;

                if (i < len && raw[i] == '~')
                {
                    // 仅在颜色功能开启时解析自定义颜色值，否则跳过整个标签
                    if (IsTextColorEnable)
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
                                    newColor = is32bit ? c : static_cast<DWORD>(RGB888toRGB565(c));
                                }
                                else
                                {
                                    newColor = defaultColor;
                                }
                            }
                            else
                            {
                                const std::string key(raw.data() + codeStart, i - codeStart);
                                const auto it = TextColorMap.find(key);
                                if (it != TextColorMap.end())
                                    newColor = is32bit ? it->second.rgb888 : it->second.rgb565;
                                else
                                    newColor = defaultColor;
                            }
                        }
                        else
                        {
                            newColor = defaultColor;
                        }
                    }
                    else
                    {
                        // 颜色功能关闭：跳到 '}' 之后，颜色保持 highlightColor（黄色）
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
                if (curColor != defaultColor)
                {
                    curColor = defaultColor;
                    dst.stops.push_back({ dst.text.size(), curColor });
                }
                ++i;
                continue;
            }

            // ============ 可见字符：拷贝到 clean text（同时预计算步进宽度） ============
            if (IsSingleByte(code))
            {
                dst.text.push_back(static_cast<char>(code));
                dst.advances.push_back(static_cast<uint16_t>(
                    CharWidthPx(pFont, static_cast<wchar_t>(code))));
                ++i;
            }
            else
            {
                // 双字节字符：先检查下一字节是否存在且有效
                if (i + 1 < len)
                {
                    const uint8_t nextCode = static_cast<uint8_t>(raw[i + 1]);
                    if (IsDBCSLeadByte(code, nextCode))
                    {
                        dst.text.push_back(raw[i]);
                        dst.text.push_back(raw[i + 1]);
                        dst.advances.push_back(static_cast<uint16_t>(
                            CharWidthPx(pFont, ExtFont::GbkToWchar(code, nextCode))));
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
     *   2. 用 ForEachVisibleChar 确定下一个"词"的边界
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
    static void __stdcall SplitTextIntoLines(H3FontExt* pFont, LPCSTR szText, const int iBoxWidth,
        std::vector<TextLineStruct>& lines)
    {
        if (!*szText)
            return;

        // 缓存常用参数
        const int spaceWidth = CharWidthPx(pFont, L' ');

        std::string lineBuf;
        int lineWidth = 0;

        // 当前行末尾的活跃颜色状态（随追加内容增量更新）
        std::string activeTag;
        bool highlightActive = false;

        // 推送当前行
        // inherit = true ：自动折行，颜色状态继承到下一行
        // inherit = false：显式换行符，颜色状态重置
        auto pushLine = [&](bool inherit)
        {
            lines.push_back({ std::move(lineBuf), lineWidth });
            lineBuf.clear();
            lineWidth = 0;

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

            // ---- 阶段 2：取词——使用 ForEachVisibleChar 确定词边界 ----
            int wordWidth = 0;
            const char* wordStart = szText;

            // realEnd 指向第一个 Space/Newline（触发停止处），或 '\0'
            const char* realEnd = ForEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*charBytes*/)
                {
                    if (type == TokenType::Space || type == TokenType::Newline)
                        return false;
                    wordWidth += width;
                    return true;
                });

            // 没有可见字符（如纯颜色码后跟空格/结尾）：安全推进 szText
            if (wordWidth == 0)
            {
                // 仍需更新颜色状态，保证跨行继承正确
                UpdateColorState(wordStart, realEnd, activeTag, highlightActive);
                szText = (realEnd > wordStart) ? realEnd : (wordStart + 1);
                continue;
            }

            // ---- 阶段 3：拆行判定 ----
            if (lineWidth + wordWidth + blankWidth > iBoxWidth)
            {
                if (lineWidth > 0)
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
                            UpdateColorState(tagStart, p, activeTag, highlightActive);
                            continue;
                        }

                        // ---- 可见字符：统一通过测宽入口获取宽度 ----
                        int charW = 0;
                        int charBytes = 0;
                        if (IsSingleByte(code))
                        {
                            charW = CharWidthPx(pFont, (wchar_t)code);
                            charBytes = 1;
                        }
                        else
                        {
                            const uint8_t nextCode = (p + 1 < realEnd) ? static_cast<uint8_t>(*(p + 1)) : 0;
                            if (IsDBCSLeadByte(code, nextCode))
                            {
                                charW = CharWidthPx(pFont, ExtFont::GbkToWchar(code, nextCode));
                                charBytes = 2;
                            }
                            else
                            {
                                charW = 0;
                                charBytes = 1;
                            }
                        }

                        if (lineWidth + charW > iBoxWidth && lineWidth > 0)
                            pushLine(true);

                        lineBuf.append(p, charBytes);
                        lineWidth += charW;
                        p += charBytes;
                    }
                    szText = realEnd;
                    continue; // 词已处理完毕，跳过阶段 4
                }
            }

            // ---- 阶段 4：将词追加到当前行 ----
            if (blankCount > 0)
                lineBuf.append(blankCount, ' ');

            // 保留颜色标记原始字节（PreprocessLine 将在渲染阶段剥离）
            lineBuf.append(wordStart, static_cast<std::size_t>(realEnd - wordStart));
            lineWidth += wordWidth + blankWidth;
            UpdateColorState(wordStart, realEnd, activeTag, highlightActive);
            szText = realEnd;
        }

        // 输出最后一行
        if (lineWidth > 0 || !lineBuf.empty())
        {
            lines.push_back({ std::move(lineBuf), lineWidth });
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
        int boxWidth = 0;              ///< 拆行宽度
        DWORD defaultColor = 0;        ///< 默认颜色（当前色深格式）
        DWORD highlightColor = 0;      ///< 高亮颜色
        bool is32bit = false;          ///< 创建时的色深
        std::vector<CleanLine> lines;  ///< 预处理结果
    };

    static constexpr std::size_t LayoutCacheSize = 128;
    static LayoutEntry g_LayoutCache[LayoutCacheSize];

    /** @brief FNV-1a 64 位字符串哈希 */
    static uint64_t Fnv1aHash(const char* s)
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
    static void ClearLayoutCache()
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
    static void UpdateBitMode()
    {
        const bool is32 = (gGamePalette != nullptr && isHDMod32Bit()) ||
            (*reinterpret_cast<const uint8_t*>(GameBitDepthAddress) == 4);
        const int newMode = is32 ? 1 : 0;
        if (BitMode != newMode)
        {
            BitMode = newMode;
            GetColor = is32 ? GetColor32 : GetColor16;
            ClearLayoutCache();
        }
    }

    /**
     * @brief 获取文本的预处理布局（缓存命中则零解析，未命中则构建并填充缓存）
     */
    static const std::vector<CleanLine>& GetOrCreateLayout(H3FontExt* pFont, LPCSTR szText, int iBoxWidth,
        DWORD defaultColor, DWORD highlightColor, bool is32bit)
    {
        auto* extFont = pFont->ExtData;
        const uint64_t hash = Fnv1aHash(szText);
        LayoutEntry& slot = g_LayoutCache[hash % LayoutCacheSize];

        if (slot.hash == hash
            && slot.font == extFont
            && slot.boxWidth == iBoxWidth
            && slot.defaultColor == defaultColor
            && slot.highlightColor == highlightColor
            && slot.is32bit == is32bit
            && slot.text == szText)
        {
            return slot.lines;
        }

        // 未命中：重建该槽位
        slot.hash = hash;
        slot.text = szText;
        slot.font = extFont;
        slot.boxWidth = iBoxWidth;
        slot.defaultColor = defaultColor;
        slot.highlightColor = highlightColor;
        slot.is32bit = is32bit;

        // 函数级复用缓冲（跨调用保留容量，避免每帧堆分配；不与其他入口共享）
        static std::vector<TextLineStruct> scratch;
        scratch.clear();
        SplitTextIntoLines(pFont, szText, iBoxWidth, scratch);

        slot.lines.clear();
        slot.lines.reserve(scratch.size());
        for (const auto& rawLine : scratch)
        {
            slot.lines.emplace_back();
            PreprocessLine(pFont, rawLine, defaultColor, highlightColor, is32bit, slot.lines.back());
        }
        return slot.lines;
    }

    // ============================================================
    // Section 5: 字符 & 文本渲染
    // ============================================================

    /** @brief 位图行起始指针（对应旧 H3LoadedPcx16::GetRow，调用方负责边界检查） */
    static inline uint8_t* GetPcxRow(Bitmap16Bit* pcx, int row)
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
    static bool H3Font_DrawCharBitmap(H3FontExt* pFont, Bitmap16Bit* pOutputPcx, wchar_t ch,
        int iX, int iY, DWORD uFontColor, DWORD shadowColor)
    {
        const auto& spacing = pFont->fr.abc[static_cast<uint8_t>(ch)];
        const int span = spacing.abcB;
        const int rows = pFont->oriHeight;
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

        uint8_t* rowBuf = GetPcxRow(pOutputPcx, iY + rowStart);
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
                WritePixel<Is32>(rowBuf, startX + colIdx, color);
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
    static bool H3Font_DrawChar(H3FontExt* pFont, Bitmap16Bit* pOutputPcx, wchar_t wch,
        int iX, int iY, DWORD uFontColor, DWORD shadowColor)
    {
        auto cFont = pFont->ExtData;

        // 原版 ASCII 位图路径（字体缓冲区缺失时回退 GDI）
        if (cFont->UsesOriginalAscii(wch) && pFont->Data)
            return H3Font_DrawCharBitmap<Is32>(pFont, pOutputPcx, wch, iX, iY, uFontColor, shadowColor);

        // 非 GBK 字符 → 跳过（避免 GDI 渲染为方块）
        if (!ExtFont::IsGbkChar(wch))
            return false;

        const uint8_t* glyph = cFont->GetGlyphAlpha(wch);
        if (!glyph)
            return false;

        const int glyphW = cFont->GlyphWidth;
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

        const bool drawShadow = cFont->DrawShadow;

        uint8_t* rowBuf = GetPcxRow(pOutputPcx, iY + rowStart);
        const uint8_t* glyphRow = glyph + static_cast<size_t>(rowStart) * glyphW;

        for (int rowIdx = rowStart; rowIdx < rowEnd; ++rowIdx)
        {
            // 阴影行（下移 1 像素），越界则不绘制
            uint8_t* shadowRowBuf = nullptr;
            if (drawShadow && iY + rowIdx + 1 < pcxH)
                shadowRowBuf = GetPcxRow(pOutputPcx, iY + rowIdx + 1);

            for (int colIdx = colStart; colIdx < colEnd; ++colIdx)
            {
                const uint8_t alpha = glyphRow[colIdx];
                if (alpha == 0)
                    continue;

                const int px = iX + colIdx;

                // Alpha 混合前景色（读 bg → 混合 fgColor → 写入）
                BlendPixel<Is32>(rowBuf, px, uFontColor, alpha);

                // Alpha 混合阴影（右下偏移 1 像素）
                // 若目标位置会被字形自身像素覆盖则跳过，避免黑色融入字体内部
                if (shadowRowBuf && px + 1 < pcxW)
                {
                    const bool overlap = (rowIdx + 1 < glyphH && colIdx + 1 < glyphW)
                        && glyphRow[glyphW + colIdx + 1] > 0;
                    if (!overlap)
                        BlendShadow<Is32>(shadowRowBuf, px + 1, shadowColor, alpha);
                }
            }

            rowBuf += pOutputPcx->Pitch;
            glyphRow += glyphW;
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
     * @param defaultColor 默认渲染颜色
     * @param shift       字形在行高内的垂直居中偏移
     * @param fontHeight  行高
     * @param cursorPos   输入光标位置（可见字符索引，-1 = 无光标；每行从 0 起算）
     */
    template<bool Is32>
    static void DrawLines(H3FontExt* pFont, Bitmap16Bit* pPcx, const std::vector<CleanLine>& textLines,
        int iX, int iY, int startY, int iBoxWidth, int iBoxHeight, uint32_t uAlignFlags,
        DWORD defaultColor, DWORD shadowColor, int shift, int fontHeight, int cursorPos)
    {
        auto* extFont = pFont->ExtData;
        const int lineCount = static_cast<int>(textLines.size());
        const int bottomBound = iY + iBoxHeight;
        // 原版位图 ASCII 位置：行内居中 + 原版基线偏移（对应原版 DrawStringExecute 的 y + fr.baseyoffset）
        const int asciiShift = (fontHeight - pFont->oriHeight) / 2 + pFont->fr.baseyoffset;

        for (int rowIdx = 0; rowIdx < lineCount; ++rowIdx)
        {
            const CleanLine& line = textLines[rowIdx];
            if (line.lineWidth == 0)
                continue;

            // 行超出文本框底部时停止（与原版 DrawStringExecute 按 fr.height 的行裁剪一致）
            if (iY + startY + (rowIdx + 1) * fontHeight > bottomBound)
                break;

            // 水平对齐
            int startX = 0;
            switch (uAlignFlags)
            {
            case font::CENTER_JUSTIFIED: startX = (iBoxWidth - line.lineWidth) / 2; break; // 居中
            case font::RIGHT_JUSTIFIED:  startX = iBoxWidth - line.lineWidth;        break; // 右对齐
            }

            int curX = iX + startX;
            const char* const textData = line.text.data();
            const char* p = textData;
            const char* const end = p + line.text.size();

            // 颜色跟踪
            DWORD curColor = defaultColor;
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
                const std::size_t bytePos = static_cast<std::size_t>(p - textData);
                while (stopIdx < stopCount && line.stops[stopIdx].bytePos == bytePos)
                {
                    curColor = line.stops[stopIdx].color;
                    ++stopIdx;
                }

                const uint8_t code = static_cast<uint8_t>(*p);

                if (IsSingleByte(code))
                {
                    H3Font_DrawChar<Is32>(pFont, pPcx, (wchar_t)code,
                        curX, glyphY((wchar_t)code), curColor, shadowColor);
                    if (cursorPos == charIdx)
                        H3Font_DrawChar<Is32>(pFont, pPcx, L'_', curX, glyphY(L'_'), defaultColor, shadowColor);
                    curX += line.advances[charIdx]; // 布局阶段预计算，绘制期零测量
                    ++p;
                    ++charIdx;
                }
                else
                {
                    uint8_t nextCode = (end - p >= 2) ? static_cast<uint8_t>(p[1]) : 0;
                    if (IsDBCSLeadByte(code, nextCode))
                    {
                        const wchar_t wch = ExtFont::GbkToWchar(code, nextCode);
                        H3Font_DrawChar<Is32>(pFont, pPcx, wch,
                            curX, glyphY(wch), curColor, shadowColor);
                        if (cursorPos == charIdx)
                            H3Font_DrawChar<Is32>(pFont, pPcx, L'_', curX, glyphY(L'_'), defaultColor, shadowColor);
                        curX += line.advances[charIdx]; // 布局阶段预计算，绘制期零测量
                        p += 2;
                        ++charIdx;
                    }
                    else
                    {
                        // 无效双字节，当单字节处理
                        H3Font_DrawChar<Is32>(pFont, pPcx, (wchar_t)code,
                            curX, glyphY((wchar_t)code), curColor, shadowColor);
                        if (cursorPos == charIdx)
                            H3Font_DrawChar<Is32>(pFont, pPcx, L'_', curX, glyphY(L'_'), defaultColor, shadowColor);
                        curX += line.advances[charIdx]; // 布局阶段预计算，绘制期零测量
                        ++p;
                        ++charIdx;
                    }
                }
            }

            // 行尾光标（cursorPos 等于本行可见字符数）
            if (cursorPos != -1 && cursorPos == charIdx)
                H3Font_DrawChar<Is32>(pFont, pPcx, L'_', curX, glyphY(L'_'), curColor, shadowColor);
        }
    }

    /**
     * @brief 在指定矩形区域内绘制多行文本
     *
     * 完整渲染管线：
     *   1. GetOrCreateLayout → 拆行 + 颜色解析（布局缓存命中时零开销）
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
    static void __stdcall H3Font_DrawText(HiHook* /*h*/, H3FontExt* pFont, char* szText, Bitmap16Bit* pPcx,
        int iX, int iY, int iBoxWidth, int iBoxHeight,
        uint32_t uColorIdx, uint32_t uAlignFlags, int iFontStyle)
    {
        if (!szText || iBoxWidth == 0 || !pPcx || !pFont->ExtData)
            return;

        // 绘制前重新判定色深：此时 HD Mod 已完全初始化，判定最可靠，
        // 且能感知运行中的色深切换
        UpdateBitMode();

        // ========== 阶段一：解析颜色与字体度量 ==========
        const uint32_t ci = (uColorIdx & 0x100) ? (uColorIdx & 0xFE) : (uColorIdx + 9);
        const DWORD defaultColor = GetColor(pFont->p16, ci);
        const DWORD highlightColor = GetColor(pFont->p16, ci + 1);
        const DWORD shadowColor = GetColor(pFont->p16, ShadowColorIndex);
        const bool is32bit = BitMode != 0;

        auto* extFont = pFont->ExtData;
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
                    ? (fontHeight - pFont->oriHeight) / 2 + pFont->fr.baseyoffset : shift);

                if (is32bit)
                    H3Font_DrawChar<true>(pFont, pPcx, L'_', iX, cursorY, defaultColor, shadowColor);
                else
                    H3Font_DrawChar<false>(pFont, pPcx, L'_', iX, cursorY, defaultColor, shadowColor);
            }
            return;
        }

        // ========== 阶段二：获取布局（缓存命中 → 跳过拆行与颜色解析）==========
        const std::vector<CleanLine>& textLines =
            GetOrCreateLayout(pFont, szText, iBoxWidth, defaultColor, highlightColor, is32bit);
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
            DrawLines<true>(pFont, pPcx, textLines, iX, iY, startY, iBoxWidth, iBoxHeight,
                uAlignFlags, defaultColor, shadowColor, shift, fontHeight, cursorPos);
        }
        else
        {
            DrawLines<false>(pFont, pPcx, textLines, iX, iY, startY, iBoxWidth, iBoxHeight,
                uAlignFlags, defaultColor, shadowColor, shift, fontHeight, cursorPos);
        }
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
        int boxWidth = 0;              ///< 拆行宽度（不涉及的测量恒为 0）
        int value = 0;                 ///< 测量结果
    };

    static constexpr std::size_t MeasureCacheSize = 64;

    /**
     * @brief 带缓存的测量入口（直接映射缓存，键 = 文本 + 字体 + 宽度）
     */
    template <typename ComputeFunc>
    static int CachedMeasure(MeasureCacheEntry(&cache)[MeasureCacheSize],
        H3FontExt* pFont, const char* text, int boxWidth, ComputeFunc&& compute)
    {
        const uint64_t hash = Fnv1aHash(text);
        MeasureCacheEntry& slot = cache[hash % MeasureCacheSize];

        if (slot.hash == hash
            && slot.font == pFont->ExtData
            && slot.boxWidth == boxWidth
            && slot.text == text)
        {
            return slot.value;
        }

        slot.hash = hash;
        slot.text = text;
        slot.font = pFont->ExtData;
        slot.boxWidth = boxWidth;
        slot.value = compute();
        return slot.value;
    }

    /**
     * @brief Hook: 将文本拆分为多行（返回 exe_vector<exe_string>）
     *
     * 替换游戏原始的文本拆分逻辑，支持双字节字符和颜色码。
     */
    static void __stdcall H3Font_SplitTextIntoLines(HiHook* /*h*/, H3FontExt* pFont, char* szText, const int iBoxWidth,
        exe_vector<exe_string>& lines)
    {
        lines.clear();

        if (!szText || !*szText || !pFont->ExtData)
            return;

        // 独立复用缓冲（不与布局缓存的拆行缓冲共享，避免潜在重入相互踩踏）
        static std::vector<TextLineStruct> scratch;
        scratch.clear();
        SplitTextIntoLines(pFont, szText, iBoxWidth, scratch);
        for (const auto& line : scratch)
        {
            lines.emplace_back(line.Text.c_str());
        }
    }

    /**
     * @brief Hook: 计算文本中最长单词的宽度
     *
     * 用于消息框等需要根据内容自适应宽度的场景。
     * 结果受 BoxWidthMin / BoxWidthMax 约束。
     */
    static int __stdcall H3Font_GetWordWidth(HiHook* /*h*/, H3FontExt* pFont, char* szText)
    {
        if (!szText || !*szText || !pFont->ExtData)
            return 0;

        static MeasureCacheEntry cache[MeasureCacheSize];
        return CachedMeasure(cache, pFont, szText, 0, [&]()
        {
            int maxWidth = 0;
            int wordWidth = 0;

            ForEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*bytes*/)
                {
                    if (type == TokenType::Newline || type == TokenType::Space || type == TokenType::DoubleByte)
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
            return std::clamp(maxWidth, BoxWidthMin, BoxWidthMax);
        });
    }

    /**
     * @brief Hook: 计算文本在指定宽度下的实际换行后最大行宽
     *
     * 模拟换行行为，返回所有行中最宽一行的像素宽度。
     */
    static int __stdcall H3Font_GetLineWrapWidth(HiHook* /*h*/, H3FontExt* pFont, char* szText, int iBoxWidth)
    {
        if (!szText || !*szText || !pFont->ExtData)
            return 0;

        static MeasureCacheEntry cache[MeasureCacheSize];
        return CachedMeasure(cache, pFont, szText, iBoxWidth, [&]()
        {
            int maxWidth = 0;
            int lineWidth = 0;

            ForEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*bytes*/)
                {
                    if (type == TokenType::Newline)
                    {
                        if (lineWidth > maxWidth)
                            maxWidth = lineWidth;
                        lineWidth = 0;
                    }
                    else
                    {
                        if (lineWidth + width > iBoxWidth)
                        {
                            if (lineWidth > maxWidth)
                                maxWidth = lineWidth;
                            lineWidth = 0;
                        }
                        lineWidth += width;
                    }
                    return true;
                });

            if (lineWidth > maxWidth)
                maxWidth = lineWidth;
            return maxWidth;
        });
    }

    /**
     * @brief Hook: 计算文本在指定宽度下的总行数
     */
    static int __stdcall H3Font_GetLineCount(HiHook* /*h*/, H3FontExt* pFont, char* szText, int iBoxWidth)
    {
        if (!szText || !*szText || !pFont->ExtData)
            return 0;

        static MeasureCacheEntry cache[MeasureCacheSize];
        return CachedMeasure(cache, pFont, szText, iBoxWidth, [&]()
        {
            int lineCount = 1;
            int lineWidth = 0;

            ForEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*bytes*/)
                {
                    if (type == TokenType::Newline)
                    {
                        ++lineCount;
                        lineWidth = 0;
                    }
                    else
                    {
                        if (lineWidth + width > iBoxWidth)
                        {
                            ++lineCount;
                            lineWidth = 0; // 当前字符归入新行
                        }
                        lineWidth += width;
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
    static int __stdcall H3Font_LineWidth(HiHook* /*h*/, H3FontExt* pFont, char* szText)
    {
        if (!szText || !*szText || !pFont->ExtData)
            return 0;

        static MeasureCacheEntry cache[MeasureCacheSize];
        return CachedMeasure(cache, pFont, szText, 0, [&]()
        {
            int lineWidth = 0;
            ForEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*bytes*/)
                {
                    if (type == TokenType::Newline)
                        return false;
                    lineWidth += width;
                    return true;
                });
            return lineWidth;
        });
    }

    /**
     * @brief Hook: 计算文本中最长行的像素宽度（不换行）
     */
    static int __stdcall H3Font_GetLineWidth(HiHook* /*h*/, H3FontExt* pFont, char* szText)
    {
        if (!szText || !*szText || !pFont->ExtData)
            return 0;

        static MeasureCacheEntry cache[MeasureCacheSize];
        return CachedMeasure(cache, pFont, szText, 0, [&]()
        {
            int maxWidth = 0;
            int lineWidth = 0;

            ForEachVisibleChar(pFont, szText,
                [&](TokenType type, int width, int /*bytes*/)
                {
                    if (type == TokenType::Newline)
                    {
                        if (lineWidth > maxWidth)
                            maxWidth = lineWidth;
                        lineWidth = 0;
                    }
                    else
                    {
                        lineWidth += width;
                    }
                    return true;
                });

            if (lineWidth > maxWidth)
                maxWidth = lineWidth;
            return maxWidth;
        });
    }

    // ============================================================
    // Section 7: 生命周期 Hook
    // ============================================================

    /**
     * @brief Hook: 字体加载后注入扩展字库
     *
     * 在游戏加载字体后，将对应的 ExtFont 挂载到 H3FontExt 上，
     * 并根据扩展字库高度调整字体行高。
     *
     * 字体名匹配规则：小写字体名精确匹配 → 回退到 "medfont.fnt"。
     */
    static font* __stdcall H3Font_Load_Hook(HiHook* h, char* name)
    {
        // 复制到局部缓冲区再小写化，避免原地修改游戏内存中的字体名
        // （若指针指向只读内存将导致访问违例）
        char fntName[64];
        strncpy_s(fntName, name, sizeof(fntName) - 1);
        _strlwr(fntName);

        auto pFont = FASTCALL_1(H3FontExt*, h->GetDefaultFunc(), fntName);
        if (!pFont)
            return pFont;

        // 精确匹配 → 回退默认字体；不使用 operator[] 以免为未知字体插入空条目
        auto it = g_ExtFontTable.find(fntName);
        if (it == g_ExtFontTable.end())
            it = g_ExtFontTable.find("medfont.fnt");
        if (it == g_ExtFontTable.end())
            return pFont;

        pFont->ExtData = &it->second;
        pFont->oriHeight = pFont->fr.height;
        // 行高 = max(原始高度, 有效字形高度 + 行间距)
        pFont->fr.height = static_cast<uint8_t>(std::max(static_cast<int>(pFont->fr.height),
            pFont->ExtData->EffectiveHeight() + pFont->ExtData->LineSpacing));
        return pFont;
    }

    /**
     * @brief Hook: DirectDraw 初始化后配置渲染后端
     *
     * 根据游戏色深模式（16/32 位）绑定对应的 GetColor 实现。
     * 同时根据屏幕分辨率计算文本框宽度限制，并清空布局缓存
     * （分辨率/色深变化后旧缓存不再有意义）。
     */
    static void __stdcall Main_DirectDrawInit_Hook(HiHook* h)
    {
        FASTCALL_0(void, h->GetDefaultFunc());

        // 根据游戏的图像模式初始化图像渲染函数指针
        UpdateBitMode();

        const auto maxWidth = getScreenWidth(_P) - 64 * 2;
        if (BoxWidthMax < 0)
            BoxWidthMax = maxWidth;
        else
            BoxWidthMax = std::clamp(BoxWidthMax, 256, maxWidth);

        if (BoxWidthMin < 0)
            BoxWidthMin = maxWidth;
        else
            BoxWidthMin = std::clamp(BoxWidthMin, 256, BoxWidthMax);
    }

    // ============================================================
    // Section 8: 插件入口
    // ============================================================

    /**
     * @brief 插件初始化入口
     *
     * 执行流程：
     *   1. 获取 Patcher 实例
     *   2. 加载 H3CN.toml 配置文件（字体映射、颜色表、宽度限制）
     *   3. 注册所有 Hook（字体加载、文本渲染、布局计算）
     */
    bool Init()
    {
#ifndef NDEBUG
        MessageBoxW(hwndApp, L"注入成功", L"调试中", 0);
#endif

        // ---------- 初始化 GBK 查表 ----------
        ExtFont::InitGbkTables();

        // ---------- 获取 Patcher 实例 ----------
        _P = GetPatcher();
        _PI = _P->GetInstance("HD.Plugin.H3FontExtension");
        if (!_PI)
        {
            _PI = _P->CreateInstance("HD.Plugin.H3FontExtension");
        }
        else
        {
            _PI->UndoAll();
        }

        // ---------- 加载配置文件 ----------
        try
        {
            auto config = toml::parse_file("H3CN.toml");

            // 字体映射：[Fonts] 节，每个条目定义游戏字体名与扩展字库的对应关系
            if (const auto fontsNode = config["Fonts"]; fontsNode.is_array())
            {
                for (const auto& item : *fontsNode.as_array())
                {
                    const auto* fontCfg = item.as_table();
                    if (!fontCfg)
                        continue;

                    std::string name = (*fontCfg)["Name"].value_or(std::string{});
                    const std::string extFontName = (*fontCfg)["ExtFont"].value_or(std::string{});
                    const int height = (*fontCfg)["Height"].value_or(0);
                    const int width = (*fontCfg)["Width"].value_or(0);

                    // 字体名小写化（匹配规则）；跳过无效条目
                    for (char& c : name)
                        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                    if (name.empty() || height <= 0 || width <= 0)
                        continue;

                    g_ExtFontTable.insert_or_assign(name,
                        ExtFont(
                            extFontName.c_str()
                            , height
                            , width
                            , (*fontCfg)["Bold"].value_or(false)
                            , (*fontCfg)["AntiAlias"].value_or(true)
                            , (*fontCfg)["MarginLeft"].value_or(1)
                            , (*fontCfg)["MarginRight"].value_or(0)
                            , (*fontCfg)["MarginBottom"].value_or(0)
                             , (*fontCfg)["LineSpacing"].value_or(0)
                             , (*fontCfg)["DrawShadow"].value_or(true)
                             , (*fontCfg)["OriginalAscii"].value_or(true)
                         ));
                }
            }

            // 文本颜色：[General].TextColor 开关 + [TextColor] 命名颜色表
            IsTextColorEnable = config["General"]["TextColor"].value_or(true);
            TextColorMap.clear();
            if (IsTextColorEnable)
            {
                if (const auto colorNode = config["TextColor"]; colorNode.is_table())
                {
                    for (const auto& [key, value] : *colorNode.as_table())
                    {
                        // 预转换两种色深格式，渲染期直接取用
                        if (const auto c = value.value<int32_t>())
                        {
                            const DWORD rgb = static_cast<DWORD>(*c) & 0xFFFFFF;
                            TextColorMap.emplace(std::string(key), NamedColor{ rgb, RGB888toRGB565(rgb) });
                        }
                    }
                }
            }

            // 消息框宽度限制：[MessageBox]
            BoxWidthMin = config["MessageBox"]["BoxWidthMin"].value_or(0);
            BoxWidthMax = config["MessageBox"]["BoxWidthMax"].value_or(-1);
        }
        catch (const std::exception&)
        {
            MessageBoxW(hwndApp, L"配置文件加载失败", L"错误", 0);
        }

        // 字体表可能已重建：清空布局缓存避免引用过期字库
        ClearLayoutCache();

        // ---------- 注册 Hook ----------

        // DDraw 初始化时检测色深模式
        _PI->WriteHiHook(0x601AB0, SPLICE_, EXTENDED_, FASTCALL_, Main_DirectDrawInit_Hook);

        // 扩展字体内存分配（font → H3FontExt，+8 字节）
        _PI->WriteDword(0x55B9CE + 1, sizeof(H3FontExt));

        // 字体加载后挂载扩展字库
        _PI->WriteHiHook(0x55BAE0, SPLICE_, EXTENDED_, THISCALL_, H3Font_Load_Hook);

        // 文本渲染与布局 Hook
        _PI->WriteHiHook(0x4B51F0, SPLICE_, EXTENDED_, THISCALL_, H3Font_DrawText);           // 文本绘制
        _PI->WriteHiHook(0x4B5580, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetLineCount);       // 计算文本行数
        _PI->WriteHiHook(0x4B5680, SPLICE_, EXTENDED_, THISCALL_, H3Font_LineWidth);          // 首行文本像素宽度
        _PI->WriteHiHook(0x4B56F0, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetLineWidth);       // 最长文本行宽度
        _PI->WriteHiHook(0x4B5770, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetWordWidth);       // 最长单词宽度
        _PI->WriteHiHook(0x4B57E0, SPLICE_, EXTENDED_, THISCALL_, H3Font_GetLineWrapWidth);   // 最长换行后宽度
        _PI->WriteHiHook(0x4B58F0, SPLICE_, EXTENDED_, THISCALL_, H3Font_SplitTextIntoLines); // 拆分文本行

        return true;
    }

} // namespace H3FontExtension
