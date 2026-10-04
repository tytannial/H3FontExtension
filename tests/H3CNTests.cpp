//===----------------------------------------------------------------------===//
// 纯逻辑测试：GBK 编码判定/转换、RGB565 颜色转换、拆行与颜色码解析
//
// 不依赖游戏进程，可在开发机直接运行；布局核心算法通过注入固定测宽/
// 颜色实现验证（TextLayoutCore.h 模板）。
//===----------------------------------------------------------------------===//

#include "H3FontExtension.h"
#include "TextLayoutCore.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace H3FontExtension;

namespace
{
    int gChecks = 0;
    int gFailures = 0;

#define CHECK(cond)                                                          \
    do                                                                       \
    {                                                                        \
        ++gChecks;                                                           \
        if (!(cond))                                                         \
        {                                                                    \
            ++gFailures;                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

    /** @brief 测试测宽：ASCII 1px，其余（双字节字符）2px */
    struct test_width
    {
        int operator()(wchar_t ch) const
        {
            return (ch >= 0x20 && ch <= 0x7E) ? 1 : 2;
        }
    };

    /** @brief 测试颜色表：red / g，其余回退 */
    struct test_colors
    {
        DWORD operator()(const char* code, std::size_t len, DWORD fallback, bool /*is32bit*/) const
        {
            if (len == 3 && std::memcmp(code, "red", 3) == 0)
                return 0x00FF0000;
            if (len == 1 && code[0] == 'g')
                return 0x0000FF00;
            return fallback;
        }
    };

    /** @brief 测试颜色解析：模拟生产 color_resolver 的 ERA 兼容路径 */
    struct test_colors_eml
    {
        DWORD operator()(const char* code, std::size_t len, DWORD fallback, bool is32bit) const
        {
            if (len == 0)
                return fallback;

            if (isEmlTextTag(code, len))
            {
                const char* value = nullptr;
                std::size_t valueLen = 0;
                if (!findEmlAttr(code + 4, len - 4, "color", 5, value, valueLen))
                    return fallback;
                code = value;
                len = valueLen;
            }

            DWORD rgb = 0;
            if (parseHexColorCode(code, len, rgb))
                return is32bit ? rgb : static_cast<DWORD>(rgb888ToRgb565(rgb));

            if (len == 3 && equalsIgnoreCaseAscii(code, "red", 3))
                return 0x00FF0000;
            return fallback;
        }
    };

    void testIsSingleByte()
    {
        CHECK(isSingleByte(0x00));
        CHECK(isSingleByte(0x20));
        CHECK(isSingleByte(0x7E));
        CHECK(isSingleByte(0x80)); // CP936 单字节欧元符号
        CHECK(isSingleByte(0xFF)); // 填充值
        CHECK(!isSingleByte(0x81));
        CHECK(!isSingleByte(0xB0));
        CHECK(!isSingleByte(0xFE));
    }

    void testIsDBCSLeadByte()
    {
        CHECK(isDBCSLeadByte(0x81, 0x40));
        CHECK(isDBCSLeadByte(0xB0, 0xA1));
        CHECK(isDBCSLeadByte(0xFE, 0xFE));
        CHECK(!isDBCSLeadByte(0x80, 0xA1)); // 首字节 < 0x81
        CHECK(!isDBCSLeadByte(0xB0, 0x3F)); // 尾字节 < 0x40
        CHECK(!isDBCSLeadByte(0xB0, 0x7F)); // 0x7F 为非法码位
        CHECK(!isDBCSLeadByte(0xB0, 0xFF)); // 尾字节 > 0xFE
    }

    void testRgb888ToRgb565()
    {
        CHECK(rgb888ToRgb565(0x000000) == 0x0000);
        CHECK(rgb888ToRgb565(0xFF0000) == 0xF800);
        CHECK(rgb888ToRgb565(0x00FF00) == 0x07E0);
        CHECK(rgb888ToRgb565(0x0000FF) == 0x001F);
        CHECK(rgb888ToRgb565(0xFFFFFF) == 0xFFFF);
        CHECK(rgb888ToRgb565(0xF8FC00) == 0xFFE0);
    }

    void testGbkTables()
    {
        ExtFont::InitGbkTables();

        CHECK(ExtFont::IsGbkChar(L'A'));
        CHECK(ExtFont::IsGbkChar(L'中'));
        CHECK(!ExtFont::IsGbkChar(L'\uD55C')); // 韩文音节不在 GBK

        // GBK 编码往返：L'中' → WideCharToMultiByte 反查字节 → GbkToWchar 还原
        char gbk[2] = {};
        const wchar_t wch = L'中';
        const int len = ::WideCharToMultiByte(936, 0, &wch, 1, gbk, 2, nullptr, nullptr);
        CHECK(len == 2);
        if (len == 2)
        {
            const wchar_t back = ExtFont::GbkToWchar(
                static_cast<uint8_t>(gbk[0]), static_cast<uint8_t>(gbk[1]));
            CHECK(back == wch);
        }
    }

    void testSplitWords()
    {
        // "ab cd"：box=4 → "ab" 与 "cd" 分两行（空格不落行首）
        std::vector<TextLineStruct> lines;
        splitTextIntoLines(test_width{}, true, "ab cd", 4, lines);
        CHECK(lines.size() == 2);
        if (lines.size() == 2)
        {
            CHECK(lines[0].text == "ab" && lines[0].line_width == 2);
            CHECK(lines[1].text == "cd" && lines[1].line_width == 2);
        }
    }

    void testSplitCJK()
    {
        // 三个汉字（各 2px），box=4 → 每行两个 / 一个，超宽词逐字符拆行
        const char text[] = "\xD6\xD0\xCE\xC4\xD7\xD6"; // 中文字（GBK）
        std::vector<TextLineStruct> lines;
        splitTextIntoLines(test_width{}, true, text, 4, lines);
        CHECK(lines.size() == 2);
        if (lines.size() == 2)
        {
            CHECK(lines[0].text.size() == 4 && lines[0].line_width == 4);
            CHECK(lines[1].text.size() == 2 && lines[1].line_width == 2);
        }
    }

    void testSplitExplicitNewline()
    {
        std::vector<TextLineStruct> lines;
        splitTextIntoLines(test_width{}, true, "a\nb", 10, lines);
        CHECK(lines.size() == 2);
        if (lines.size() == 2)
        {
            CHECK(lines[0].text == "a");
            CHECK(lines[1].text == "b");
        }
    }

    void testSplitColorCodeWidth()
    {
        // 颜色码不参与宽度："{~red}ab" 宽 2，且原始标记保留在行缓冲中
        std::vector<TextLineStruct> lines;
        splitTextIntoLines(test_width{}, true, "{~red}ab", 10, lines);
        CHECK(lines.size() == 1);
        if (lines.size() == 1)
        {
            CHECK(lines[0].line_width == 2);
            CHECK(lines[0].text == "{~red}ab");
        }
    }

    void testPreprocessColors()
    {
        TextLineStruct src;
        src.text = "a{~red}b}c";
        src.line_width = 3;

        CleanLine clean;
        preprocessLine(test_width{}, test_colors{}, true, src, 0x00111111, 0x00222222, true, clean);

        CHECK(clean.text == "abc");
        CHECK(clean.line_width == 3);
        CHECK(clean.advances.size() == 3);
        CHECK(clean.stops.size() == 2);
        if (clean.stops.size() == 2)
        {
            CHECK(clean.stops[0].byte_pos == 1 && clean.stops[0].color == 0x00FF0000);
            CHECK(clean.stops[1].byte_pos == 2 && clean.stops[1].color == 0x00111111);
        }
    }

    void testColorDisabled()
    {
        // 颜色功能关闭：{~red} 等同高亮 {，使用高亮色
        TextLineStruct src;
        src.text = "{~red}a";
        src.line_width = 1;

        CleanLine clean;
        preprocessLine(test_width{}, test_colors{}, false, src, 0x00111111, 0x00222222, true, clean);

        CHECK(clean.text == "a");
        CHECK(clean.stops.size() == 1);
        if (clean.stops.size() == 1)
        {
            CHECK(clean.stops[0].byte_pos == 0);
            CHECK(clean.stops[0].color == 0x00222222);
        }
    }

    void testUpdateColorState()
    {
        std::string tag;
        bool highlight = false;

        const char seg1[] = "{~red}";
        updateColorState(true, seg1, seg1 + 6, tag, highlight);
        CHECK(tag == "{~red}");
        CHECK(!highlight);

        const char seg2[] = "}";
        updateColorState(true, seg2, seg2 + 1, tag, highlight);
        CHECK(tag.empty());
        CHECK(!highlight);

        const char seg3[] = "{";
        updateColorState(true, seg3, seg3 + 1, tag, highlight);
        CHECK(tag.empty());
        CHECK(highlight);

        // 颜色关闭：{~red} 视为高亮
        tag = "stale";
        highlight = false;
        const char seg4[] = "{~red}";
        updateColorState(false, seg4, seg4 + 6, tag, highlight);
        CHECK(tag.empty());
        CHECK(highlight);
    }

    void testParseHexColorCode()
    {
        DWORD rgb = 0;
        CHECK(parseHexColorCode("#FF8000", 7, rgb) && rgb == 0xFF8000);
        CHECK(parseHexColorCode("FF8000", 6, rgb) && rgb == 0xFF8000);
        CHECK(parseHexColorCode("ff8000", 6, rgb) && rgb == 0xFF8000);
        // 8 位 RRGGBBAA：忽略 alpha，仅取 RGB
        CHECK(parseHexColorCode("FF800080", 8, rgb) && rgb == 0xFF8000);
        CHECK(parseHexColorCode("#FF800080", 9, rgb) && rgb == 0xFF8000);
        // 非法输入
        CHECK(!parseHexColorCode("FF800", 5, rgb));
        CHECK(!parseHexColorCode("FF80000", 7, rgb));
        CHECK(!parseHexColorCode("GG8000", 6, rgb));
        CHECK(!parseHexColorCode("", 0, rgb));
        CHECK(!parseHexColorCode("#", 1, rgb));
    }

    void testFindEmlAttr()
    {
        const char attrs[] = "color=Red align=\"center\" from-y=10";
        constexpr std::size_t attrsLen = sizeof(attrs) - 1;

        const char* value = nullptr;
        std::size_t valueLen = 0;

        CHECK(findEmlAttr(attrs, attrsLen, "color", 5, value, valueLen));
        CHECK(valueLen == 3 && std::memcmp(value, "Red", 3) == 0);

        CHECK(findEmlAttr(attrs, attrsLen, "align", 5, value, valueLen));
        CHECK(valueLen == 6 && std::memcmp(value, "center", 6) == 0);

        CHECK(findEmlAttr(attrs, attrsLen, "from-y", 6, value, valueLen));
        CHECK(valueLen == 2 && std::memcmp(value, "10", 2) == 0);

        // 键名大小写不敏感
        CHECK(findEmlAttr(attrs, attrsLen, "COLOR", 5, value, valueLen));

        CHECK(!findEmlAttr(attrs, attrsLen, "missing", 7, value, valueLen));
    }

    void testEmlTagSkipping()
    {
        // EML 复合标签含空格，必须整体跳过（不能把属性文本当作可见字符）
        std::vector<TextLineStruct> lines;
        splitTextIntoLines(test_width{}, true, "{~text color=Red}ab", 10, lines);
        CHECK(lines.size() == 1);
        if (lines.size() == 1)
        {
            CHECK(lines[0].line_width == 2);
            CHECK(lines[0].text == "{~text color=Red}ab");
        }

        // ERA 内嵌图片标签按零宽度跳过
        lines.clear();
        splitTextIntoLines(test_width{}, true, "{~>image:1:2 block}ab", 10, lines);
        CHECK(lines.size() == 1);
        if (lines.size() == 1)
        {
            CHECK(lines[0].line_width == 2);
        }
    }

    void testPreprocessEml()
    {
        TextLineStruct src;
        src.text = "{~text color=#00FF00}x";
        src.line_width = 1;

        CleanLine clean;
        preprocessLine(test_width{}, test_colors_eml{}, true, src, 0x00111111, 0x00222222, true, clean);

        CHECK(clean.text == "x");
        CHECK(clean.stops.size() == 1);
        if (clean.stops.size() == 1)
        {
            CHECK(clean.stops[0].byte_pos == 0);
            CHECK(clean.stops[0].color == 0x0000FF00);
        }

        // 无 # 的 6 位十六进制与 8 位 RRGGBBAA（32 位与 16 位两条路径）
        CHECK(test_colors_eml{}(("FF0000"), 6, 0xBAD, true) == 0x00FF0000);
        CHECK(test_colors_eml{}(("FF000080"), 8, 0xBAD, true) == 0x00FF0000);
        CHECK(test_colors_eml{}(("FF0000"), 6, 0xBAD, false) == rgb888ToRgb565(0xFF0000));

        // 大小写不敏感命名颜色
        CHECK(test_colors_eml{}(("RED"), 3, 0xBAD, true) == 0x00FF0000);

        // EML 标签无 color 属性 → 回退
        const char noColor[] = "text align=center";
        CHECK(test_colors_eml{}(noColor, sizeof(noColor) - 1, 0xBAD, true) == 0xBAD);
    }

} // namespace

int main()
{
    testIsSingleByte();
    testIsDBCSLeadByte();
    testRgb888ToRgb565();
    testGbkTables();
    testSplitWords();
    testSplitCJK();
    testSplitExplicitNewline();
    testSplitColorCodeWidth();
    testPreprocessColors();
    testColorDisabled();
    testUpdateColorState();
    testParseHexColorCode();
    testFindEmlAttr();
    testEmlTagSkipping();
    testPreprocessEml();

    std::printf("H3CNTests: %d checks, %d failures\n", gChecks, gFailures);
    return (gFailures == 0) ? 0 : 1;
}
