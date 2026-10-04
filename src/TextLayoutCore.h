//===----------------------------------------------------------------------===//
// 布局核心算法（模板，与游戏字体解耦）
//
// 生产环境由 TextLayout.cpp 注入基于 ExtFont 的测宽与颜色解析实现；
// 测试通过注入固定实现直接验证拆行与颜色码解析逻辑。
//===----------------------------------------------------------------------===//

#pragma once

#include "H3FontExtension.h"

#include <string>
#include <utility>
#include <vector>

namespace H3FontExtension
{
    /** @brief 不受 locale 影响的 ASCII 小写转换 */
    inline char asciiToLower(char c)
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    /** @brief 大小写不敏感的 ASCII 定长比较 */
    inline bool equalsIgnoreCaseAscii(const char* lhs, const char* rhs, std::size_t len)
    {
        for (std::size_t i = 0; i < len; ++i)
        {
            if (asciiToLower(lhs[i]) != asciiToLower(rhs[i]))
                return false;
        }
        return true;
    }

    /**
     * @brief 解析十六进制颜色码（支持 6 位 RRGGBB / 8 位 RRGGBBAA，可带 '#' 前缀）
     *
     * ERA/原版语法中 8 位写法为 RRGGBBAA（如 {~FF000080}），本函数仅取 RGB 通道，
     * 忽略 alpha（16 位色深渲染无透明度概念）。
     *
     * @param code 颜色码文本（不含 '~'）
     * @param len  文本长度
     * @param rgb  [out] 解析出的 0xRRGGBB
     * @return 是否为合法的十六进制颜色码
     */
    inline bool parseHexColorCode(const char* code, std::size_t len, DWORD& rgb)
    {
        if (len > 0 && code[0] == '#')
        {
            ++code;
            --len;
        }

        if (len != 6 && len != 8)
            return false;

        DWORD value = 0;
        for (std::size_t i = 0; i < len; ++i)
        {
            const char c = code[i];
            DWORD digit;
            if (c >= '0' && c <= '9')
                digit = static_cast<DWORD>(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = static_cast<DWORD>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                digit = static_cast<DWORD>(c - 'A' + 10);
            else
                return false;
            value = (value << 4) | digit;
        }

        rgb = (len == 8) ? (value >> 8) : value; // RRGGBBAA → RRGGBB
        return true;
    }

    /**
     * @brief 在 EML 属性串中查找指定键的值
     *
     * 属性语法：key=value 或 key="quoted value"，以空格分隔。
     * 例：{"color=Red align=center"} 中查找 "color" 得到 "Red"。
     *
     * @param attrs    属性串起始（标签名之后）
     * @param len      属性串长度
     * @param key      要查找的键（ASCII，大小写不敏感）
     * @param keyLen   键长度
     * @param value    [out] 值起始指针（不含引号）
     * @param valueLen [out] 值长度
     * @return 是否找到该键
     */
    inline bool findEmlAttr(const char* attrs, std::size_t len,
        const char* key, std::size_t keyLen,
        const char*& value, std::size_t& valueLen)
    {
        std::size_t i = 0;
        while (i < len)
        {
            while (i < len && attrs[i] == ' ')
                ++i;
            if (i >= len)
                break;

            const std::size_t keyStart = i;
            while (i < len && attrs[i] != '=' && attrs[i] != ' ')
                ++i;

            if (i >= len || attrs[i] != '=')
            {
                // 无 '=' 的裸词：已由内层循环推进，防呆兜底
                if (i == keyStart)
                    ++i;
                continue;
            }

            const std::size_t curKeyLen = i - keyStart;
            ++i; // 跳过 '='

            const char* val;
            std::size_t vLen;
            if (i < len && attrs[i] == '"')
            {
                ++i;
                val = attrs + i;
                while (i < len && attrs[i] != '"')
                    ++i;
                vLen = static_cast<std::size_t>(attrs + i - val);
                if (i < len)
                    ++i; // 跳过收尾引号
            }
            else
            {
                val = attrs + i;
                while (i < len && attrs[i] != ' ')
                    ++i;
                vLen = static_cast<std::size_t>(attrs + i - val);
            }

            if (curKeyLen == keyLen && equalsIgnoreCaseAscii(attrs + keyStart, key, keyLen))
            {
                value = val;
                valueLen = vLen;
                return true;
            }
        }
        return false;
    }

    /** @brief 判断颜色码文本是否为 EML 复合标签 {~text ...} */
    inline bool isEmlTextTag(const char* code, std::size_t len)
    {
        return len >= 4 && equalsIgnoreCaseAscii(code, "text", 4) &&
            (len == 4 || code[4] == ' ');
    }

    /**
     * @brief 遍历文本中所有"可见 token"，自动跳过颜色码与 EML 标签
     *
     * 控制标记格式：
     *   - {~ColorName} / {~#RRGGBB} / {~RRGGBBAA}  颜色标签
     *   - {~text color=... align=...}              EML 复合标签（属性含空格，需整体跳过）
     *   - {~>defname:frame:group ...}              ERA 内嵌图片标签（本插件不渲染，按零宽度跳过）
     *   - { / }                                    高亮切换（{ 进入高亮，} 恢复默认）
     *
     * 这些控制字符在遍历中被跳过，回调只接收实际可见字符。
     *
     * @param pFont   字体指针
     * @param szText  输入文本（以 '\0' 结尾）
     * @param fn      回调：fn(TokenType type, int width, int charBytes) -> bool
     *                返回 false 可提前终止遍历
     * @return        遍历结束时的指针位置（调用方可用于恢复 szText 推进）
     */
    template<typename WidthOf, typename Func>
    const char* forEachVisibleChar(const WidthOf& widthOf, LPCSTR szText, Func&& fn)
    {
        while (*szText)
        {
            uint8_t code = static_cast<uint8_t>(*szText);

            // -------- 跳过颜色码 / EML 标签 --------
            if (code == '{' || code == '}')
            {
                if (code == '{' && *(szText + 1) == '~')
                {
                    szText += 2; // 跳过 "{~"
                    // 标签一律以 '}' 结尾；换行作为兜底终止，避免损坏的标签吞噬后续文本。
                    // 注意：EML 属性（{~text color=Red}）含空格，不能以空格作为终止条件
                    while (*szText && *szText != '}' && *szText != '\n')
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
                const int spaceW = widthOf(L' ');
                if (!fn(TokenType::SPACE, spaceW, 1))
                    return szText;
                ++szText;
                continue;
            }

            // -------- 单字节字符 --------
            if (isSingleByte(code))
            {
                const wchar_t wch = (wchar_t)code;
                const int charW = widthOf(wch);
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
                const int charW = widthOf(wch);
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
    inline void updateColorState(bool colorEnabled, const char* p, const char* end, std::string& activeTag, bool& highlightActive)
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
                if (colorEnabled)
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
    template<typename WidthOf, typename ColorResolver>
    void preprocessLine(const WidthOf& widthOf, const ColorResolver& resolveColor, bool colorEnabled, const TextLineStruct& src, DWORD default_color, DWORD highlight_color, bool is32bit, CleanLine& dst)
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
                    if (colorEnabled)
                    {
                        ++i;
                        const std::size_t codeStart = i;
                        while (i < len && raw[i] != '}')
                            ++i;

                        // 颜色码解析交由注入实现（生产：命名颜色表 / #RRGGBB；测试：固定表）
                        newColor = resolveColor(raw.data() + codeStart, i - codeStart, default_color, is32bit);
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
                    widthOf(static_cast<wchar_t>(code))));
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
                            widthOf(ExtFont::GbkToWchar(code, nextCode))));
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
    template<typename WidthOf>
    void splitTextIntoLines(const WidthOf& widthOf, bool colorEnabled, LPCSTR szText, const int iBoxWidth,
        std::vector<TextLineStruct>& lines)
    {
        if (!*szText)
            return;

        // 缓存常用参数
        const int spaceWidth = widthOf(L' ');

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
            const char* realEnd = forEachVisibleChar(widthOf, szText,
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
                updateColorState(colorEnabled, wordStart, realEnd, activeTag, highlightActive);
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
                            updateColorState(colorEnabled, tagStart, p, activeTag, highlightActive);
                            continue;
                        }

                        // ---- 可见字符：统一通过测宽入口获取宽度 ----
                        int charW = 0;
                        int charBytes = 0;
                        if (isSingleByte(code))
                        {
                            charW = widthOf((wchar_t)code);
                            charBytes = 1;
                        }
                        else
                        {
                            const uint8_t nextCode = (p + 1 < realEnd) ? static_cast<uint8_t>(*(p + 1)) : 0;
                            if (isDBCSLeadByte(code, nextCode))
                            {
                                charW = widthOf(ExtFont::GbkToWchar(code, nextCode));
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
            updateColorState(colorEnabled, wordStart, realEnd, activeTag, highlightActive);
            szText = realEnd;
        }

        // 输出最后一行
        if (line_width > 0 || !lineBuf.empty())
        {
            lines.push_back({ std::move(lineBuf), line_width });
        }
    }
} // namespace H3FontExtension