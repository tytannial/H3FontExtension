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

    /** @brief 解析简单十进制整数（可带负号）；失败时 value 不变并返回 false */
    inline bool parseIntValue(const char*& p, const char* end, int& value)
    {
        bool negative = false;
        if (p < end && *p == '-')
        {
            negative = true;
            ++p;
        }
        if (p >= end || *p < '0' || *p > '9')
            return false;

        int v = 0;
        while (p < end && *p >= '0' && *p <= '9')
        {
            v = v * 10 + (*p - '0');
            ++p;
        }
        value = negative ? -v : v;
        return true;
    }

    /**
     * @brief 检测 EML 属性串中是否存在指定键/裸词标志（如 block、mirror）
     *
     * ERA 语义：键存在即为真（无论是否带 =value）。
     */
    inline bool hasEmlFlag(const char* attrs, std::size_t len, const char* flag, std::size_t flagLen)
    {
        std::size_t i = 0;
        while (i < len)
        {
            while (i < len && attrs[i] == ' ')
                ++i;
            if (i >= len)
                break;

            const std::size_t start = i;
            while (i < len && attrs[i] != ' ' && attrs[i] != '=')
                ++i;

            if (i - start == flagLen && equalsIgnoreCaseAscii(attrs + start, flag, flagLen))
                return true;

            // 跳过该属性的值
            if (i < len && attrs[i] == '=')
            {
                ++i;
                if (i < len && attrs[i] == '"')
                {
                    ++i;
                    while (i < len && attrs[i] != '"')
                        ++i;
                    if (i < len)
                        ++i;
                }
                else
                {
                    while (i < len && attrs[i] != ' ')
                        ++i;
                }
            }
        }
        return false;
    }

    /**
     * @brief 解析 EML 内嵌图片标签（{~>...} 中 '>' 起始的内容）
     *
     * 语法（与 ERA Rainbow 一致）：
     *   DefName[:frame] / DefName[:group:frame] [block] [mirror]
     *   [valign=top|middle|bottom] [from-y=N] [height=N] [offset-y=N]
     *
     * @param code 标签内容（首个字符必须是 '>'）
     * @param len  长度
     * @param spec [out] 解析结果
     * @return 解析是否成功（DEF 名非空）
     */
    inline bool parseInlineImageTag(const char* code, std::size_t len, InlineImageSpec& spec)
    {
        if (len < 2 || code[0] != '>')
            return false;

        const char* p = code + 1;
        const char* const end = code + len;

        // ---- DEF 名：引号包裹或读到 ':' / ' ' / 结束 ----
        std::string name;
        if (p < end && *p == '"')
        {
            ++p;
            while (p < end && *p != '"')
                name.push_back(*p++);
            if (p < end)
                ++p; // 跳过收尾引号
        }
        else
        {
            while (p < end && *p != ':' && *p != ' ')
                name.push_back(*p++);
        }
        if (name.empty())
            return false;
        spec.defName = std::move(name);

        // ---- 可选 :frame 或 :group:frame ----
        int first = -1;
        int second = -1;
        if (p < end && *p == ':')
        {
            ++p;
            parseIntValue(p, end, first);
            if (p < end && *p == ':')
            {
                ++p;
                parseIntValue(p, end, second);
            }
        }
        if (second >= 0)
        {
            spec.group = (first >= 0) ? first : 0;
            spec.frame = second;
        }
        else if (first >= 0)
        {
            spec.frame = first;
        }

        // ---- 属性 ----
        const char* attrs = p;
        const std::size_t attrsLen = static_cast<std::size_t>(end - p);

        spec.isBlock = hasEmlFlag(attrs, attrsLen, "block", 5);
        spec.mirror = hasEmlFlag(attrs, attrsLen, "mirror", 6);

        const char* value = nullptr;
        std::size_t valueLen = 0;

        if (findEmlAttr(attrs, attrsLen, "from-y", 6, value, valueLen))
        {
            const char* v = value;
            parseIntValue(v, value + valueLen, spec.fromY);
        }
        if (findEmlAttr(attrs, attrsLen, "height", 6, value, valueLen))
        {
            const char* v = value;
            int h = -1;
            if (parseIntValue(v, value + valueLen, h))
                spec.sliceHeight = h;
        }
        if (findEmlAttr(attrs, attrsLen, "offset-y", 8, value, valueLen))
        {
            const char* v = value;
            if (parseIntValue(v, value + valueLen, spec.offsetY))
                spec.hasOffsetY = true;
        }
        if (findEmlAttr(attrs, attrsLen, "valign", 6, value, valueLen))
        {
            if (valueLen == 3 && equalsIgnoreCaseAscii(value, "top", 3))
                spec.valign = ImageVAlign::Top;
            else if (valueLen == 6 && equalsIgnoreCaseAscii(value, "bottom", 6))
                spec.valign = ImageVAlign::Bottom;
            else
                spec.valign = ImageVAlign::Middle;
        }

        return true;
    }

    /**
     * @brief 遍历文本中所有"可见 token"，自动跳过颜色码与 EML 标签
     *
     * 控制标记格式：
     *   - {~ColorName} / {~#RRGGBB} / {~RRGGBBAA}  颜色标签
     *   - {~text color=... align=...}              EML 复合标签
     *   - {~>defname[:group:frame] ...}            EML 内嵌 DEF 图片标签
     *     通过 imageWidth 注入实现计算占位宽度，回调收到 TokenType::IMAGE
     *   - { / }                                    高亮切换（{ 进入高亮，} 恢复默认）
     *
     * 这些控制字符在遍历中被跳过，回调只接收实际可见字符与图片 token。
     *
     * @param widthOf   单字符测宽实现
     * @param imageWidth 图片度量实现：imageWidth(code, len, out)（code 以 '>' 起始，失败返回 false）
     * @param szText    输入文本（以 '\0' 结尾）
     * @param fn        回调：fn(TokenType type, int width, int charBytes, const char* code, std::size_t codeLen) -> bool
     *                  图片 token 的 code/codeLen 为标签内容（'>' 起始），其他 token 为 nullptr/0；
     *                  返回 false 可提前终止遍历（图片 token 中断时会先消费 '}' 再返回）
     * @return          遍历结束时的指针位置（调用方可用于恢复 szText 推进）
     */
    template<typename WidthOf, typename ImageWidth, typename Func>
    const char* forEachVisibleChar(const WidthOf& widthOf, const ImageWidth& imageWidth, LPCSTR szText, Func&& fn)
    {
        while (*szText)
        {
            uint8_t code = static_cast<uint8_t>(*szText);

            // -------- 跳过颜色码 / EML 标签 --------
            if (code == '{' || code == '}')
            {
                if (code == '{' && *(szText + 1) == '~')
                {
                    const char* const tagStart = szText;
                    szText += 2; // 跳过 "{~"
                    // 标签一律以 '}' 结尾；换行作为兜底终止，避免损坏的标签吞噬后续文本。
                    // 注意：EML 属性（{~text color=Red}）含空格，不能以空格作为终止条件
                    while (*szText && *szText != '}' && *szText != '\n')
                        ++szText;

                    const std::size_t codeLen = static_cast<std::size_t>(szText - tagStart - 2);
                    if (codeLen > 0 && tagStart[2] == '>')
                    {
                        // 内嵌图片：宽度按占位计算，交回调处理（不可分割 token）
                        ImageTokenMetrics info;
                        if (imageWidth(tagStart + 2, codeLen, info))
                        {
                            const int charBytes = static_cast<int>((*szText == '}') ? (szText - tagStart + 1)
                                                                                     : (szText - tagStart));
                            if (!fn(TokenType::IMAGE, info.width, charBytes, tagStart + 2, codeLen))
                            {
                                // 回调要求中断（如块级图片独立成词）：先消费 '}' 再返回
                                if (*szText == '}')
                                    ++szText;
                                return szText;
                            }
                        }
                    }

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
                if (!fn(TokenType::NEWLINE, 0, 1, nullptr, 0))
                    return szText;
                ++szText;
                continue;
            }

            // -------- 空格 --------
            if (code == ' ')
            {
                const int spaceW = widthOf(L' ');
                if (!fn(TokenType::SPACE, spaceW, 1, nullptr, 0))
                    return szText;
                ++szText;
                continue;
            }

            // -------- 单字节字符 --------
            if (isSingleByte(code))
            {
                const wchar_t wch = (wchar_t)code;
                const int charW = widthOf(wch);
                if (!fn(TokenType::SINGLE_BYTE, charW, 1, nullptr, 0))
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
                if (!fn(TokenType::DOUBLE_BYTE, charW, 2, nullptr, 0))
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

                // 内嵌图片标签 {~>...}：不影响颜色状态
                const bool isImage = (p - start > 2) && start[2] == '>';
                if (isImage)
                {
                    // 保持当前颜色状态
                }
                else if (colorEnabled)
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
     * 同时将颜色变更信息记录为 ColorStop 列表、内嵌图片记录为 ImageAnchor 列表。
     *
     * @param widthOf       字体（用于测量每个字符的步进宽度）
     * @param resolveColor   颜色解析实现
     * @param collectImage   内嵌图片锚点收集实现：collectImage(code, len, anchor) -> bool
     *                       （code 以 '>' 起始；返回 false 表示解析/加载失败，按零宽跳过）
     * @param colorEnabled   颜色功能开关
     * @param src            原始拆行结果（含颜色码）
     * @param default_color   默认渲染颜色（当前色深格式）
     * @param highlight_color { } 切换的高亮颜色
     * @param is32bit        当前是否为 32 位色深模式
     * @param dst            [out] 预处理结果
     */
    template<typename WidthOf, typename ColorResolver, typename ImageCollector>
    void preprocessLine(const WidthOf& widthOf, const ColorResolver& resolveColor, const ImageCollector& collectImage,
        bool colorEnabled, const TextLineStruct& src, DWORD default_color, DWORD highlight_color, bool is32bit, CleanLine& dst)
    {
        dst.line_width = src.line_width;
        dst.text.clear();
        dst.stops.clear();
        dst.stops.reserve(4); // 典型行的颜色切换次数很少，避免小规模多次扩容
        dst.advances.clear();
        dst.images.clear();

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

            // ============ '{' —— 进入高亮、自定义颜色或内嵌图片 ============
            if (code == '{')
            {
                DWORD newColor = highlight_color;
                ++i;

                if (i < len && raw[i] == '~')
                {
                    ++i;
                    const std::size_t codeStart = i;
                    while (i < len && raw[i] != '}')
                        ++i;

                    const std::size_t codeLen = i - codeStart;

                    // 内嵌图片标签 {~>...}：记录锚点，不产生字符、不改变颜色状态
                    if (codeLen > 0 && raw[codeStart] == '>')
                    {
                        ImageAnchor anchor;
                        anchor.bytePos = dst.text.size();
                        if (collectImage(raw.data() + codeStart, codeLen, anchor))
                            dst.images.push_back(std::move(anchor));

                        if (i < len && raw[i] == '}')
                            ++i;
                        continue;
                    }

                    // 仅在颜色功能开启时解析自定义颜色值，否则等同高亮 {
                    if (colorEnabled)
                    {
                        // 颜色码解析交由注入实现（生产：命名颜色表 / #RRGGBB；测试：固定表）
                        newColor = resolveColor(raw.data() + codeStart, codeLen, default_color, is32bit);
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
     *   2. 用 forEachVisibleChar 确定下一个"词"的边界（内嵌图片按占位宽度计入词宽）
     *   3. 若当前行放不下该词，输出当前行
     *   4. 若词本身超宽，逐字符强制拆行（图片作为不可分割单元整体换行）
     *   5. 块级图片独占行：前后强制换行，并按切片高度保留额外占位行
     *   6. 将词追加到当前行
     *
     * 颜色状态在追加内容时增量维护，折行推送后 O(1) 继承到下一行。
     *
     * @param widthOf    字体测宽实现
     * @param imageWidth 图片占位宽度实现（见 forEachVisibleChar）
     * @param szText     输入文本（以 '\0' 结尾）
     * @param iBoxWidth  文本框像素宽度
     * @param lines      [out] 拆分结果（含颜色码/图片标签的原始行）
     */
    template<typename WidthOf, typename ImageWidth>
    void splitTextIntoLines(const WidthOf& widthOf, const ImageWidth& imageWidth, bool colorEnabled, LPCSTR szText,
        const int iBoxWidth, std::vector<TextLineStruct>& lines)
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
            bool blockBreak = false;                 // 词中出现块级图片：图片独占行
            const char* blockTagStart = nullptr;     // 块级图片标签起点（"{~"）
            int blockImageWidth = 0;
            int blockImageLines = 1;

            // realEnd 指向第一个 Space/Newline（触发停止处），或 '\0'
            const char* realEnd = forEachVisibleChar(widthOf, imageWidth, szText,
                [&](TokenType type, int width, int /*charBytes*/, const char* code, std::size_t codeLen)
                {
                    if (type == TokenType::SPACE || type == TokenType::NEWLINE)
                        return false;

                    if (type == TokenType::IMAGE)
                    {
                        ImageTokenMetrics info;
                        if (imageWidth(code, codeLen, info) && info.isBlock)
                        {
                            // 块级图片：中断取词（图片前的片段作为独立词先处理）
                            blockBreak = true;
                            blockTagStart = code - 2; // 指向 "{~"
                            blockImageWidth = info.width;
                            blockImageLines = info.numLines;
                            return false;
                        }
                    }

                    wordWidth += width;
                    return true;
                });

            // ---- 块级图片：图片前片段 + 独占行 + 额外占位行 ----
            if (blockBreak)
            {
                // 图片前的词片段（若图片不在词首）
                const std::size_t prefixLen = static_cast<std::size_t>(blockTagStart - wordStart);
                if (prefixLen > 0)
                {
                    if (line_width + wordWidth + blankWidth > iBoxWidth)
                    {
                        if (line_width > 0)
                            pushLine(true);
                        blankCount = 0;
                        blankWidth = 0;
                    }

                    // 行首空格丢弃；词间空格保留并计入行宽
                    if (blankCount > 0 && (line_width > 0 || !lineBuf.empty()))
                    {
                        lineBuf.append(blankCount, ' ');
                        line_width += blankWidth;
                    }

                    lineBuf.append(wordStart, prefixLen);
                    line_width += wordWidth;
                    updateColorState(colorEnabled, wordStart, blockTagStart, activeTag, highlightActive);
                }

                // 图片独占行
                if (line_width > 0 || !lineBuf.empty())
                    pushLine(false);

                lineBuf.append(blockTagStart, static_cast<std::size_t>(realEnd - blockTagStart));
                line_width = blockImageWidth;
                pushLine(false);

                // 切片高度跨多个行高：为后续切片追加占位行（无锚点，仅推进行号）
                for (int extra = 1; extra < blockImageLines; ++extra)
                    pushLine(false);

                szText = realEnd;
                continue;
            }

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

                        // ---- 颜色标记/图片标签：保留到行缓冲；图片占位计入行宽 ----
                        if (code == '{' || code == '}')
                        {
                            const char* tagStart = p;
                            if (code == '{' && (p + 1 < realEnd) && *(p + 1) == '~')
                            {
                                p += 2; // 跳过 "{~"
                                while (p < realEnd && *p != '}')
                                    ++p;

                                const bool isImage = (p - tagStart > 2) && tagStart[2] == '>';
                                if (isImage)
                                {
                                    // 图片为不可分割单元：放不下先换行，再整体输出
                                    ImageTokenMetrics info;
                                    const int imgW = imageWidth(tagStart + 2,
                                        static_cast<std::size_t>(p - tagStart - 2), info) ? info.width : 0;
                                    if (line_width + imgW > iBoxWidth && line_width > 0)
                                        pushLine(true);

                                    line_width += imgW;
                                    if (p < realEnd && *p == '}') // 跳过 '}'
                                        ++p;
                                    lineBuf.append(tagStart, p - tagStart);
                                    continue; // 图片不影响颜色状态
                                }

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
            // 行首空格丢弃；词间空格保留并计入行宽
            if (blankCount > 0 && (line_width > 0 || !lineBuf.empty()))
            {
                lineBuf.append(blankCount, ' ');
                line_width += blankWidth;
            }

            // 保留颜色标记原始字节（preprocessLine 将在渲染阶段剥离）
            lineBuf.append(wordStart, static_cast<std::size_t>(realEnd - wordStart));
            line_width += wordWidth;
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