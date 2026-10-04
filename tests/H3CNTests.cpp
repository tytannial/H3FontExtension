//===----------------------------------------------------------------------===//
// 纯逻辑测试：GBK 编码判定/转换、RGB565 颜色转换
//
// 不依赖游戏进程，可在开发机直接运行；被测代码来自 ExtFont 静态方法与
// H3FontExtension.h 的内联工具（不构造 ExtFont 对象，不触发游戏内存访问）。
//===----------------------------------------------------------------------===//

#include "H3FontExtension.h"

#include <Windows.h>
#include <cstdio>

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

    void testIsSingleByte()
    {
        CHECK(H3FontExtension::isSingleByte(0x00));
        CHECK(H3FontExtension::isSingleByte(0x20));
        CHECK(H3FontExtension::isSingleByte(0x7E));
        CHECK(H3FontExtension::isSingleByte(0x80)); // CP936 单字节欧元符号
        CHECK(H3FontExtension::isSingleByte(0xFF)); // 填充值
        CHECK(!H3FontExtension::isSingleByte(0x81));
        CHECK(!H3FontExtension::isSingleByte(0xB0));
        CHECK(!H3FontExtension::isSingleByte(0xFE));
    }

    void testIsDBCSLeadByte()
    {
        CHECK(H3FontExtension::isDBCSLeadByte(0x81, 0x40));
        CHECK(H3FontExtension::isDBCSLeadByte(0xB0, 0xA1));
        CHECK(H3FontExtension::isDBCSLeadByte(0xFE, 0xFE));
        CHECK(!H3FontExtension::isDBCSLeadByte(0x80, 0xA1)); // 首字节 < 0x81
        CHECK(!H3FontExtension::isDBCSLeadByte(0xB0, 0x3F)); // 尾字节 < 0x40
        CHECK(!H3FontExtension::isDBCSLeadByte(0xB0, 0x7F)); // 0x7F 为非法码位
        CHECK(!H3FontExtension::isDBCSLeadByte(0xB0, 0xFF)); // 尾字节 > 0xFE
    }

    void testRgb888ToRgb565()
    {
        CHECK(H3FontExtension::rgb888ToRgb565(0x000000) == 0x0000);
        CHECK(H3FontExtension::rgb888ToRgb565(0xFF0000) == 0xF800);
        CHECK(H3FontExtension::rgb888ToRgb565(0x00FF00) == 0x07E0);
        CHECK(H3FontExtension::rgb888ToRgb565(0x0000FF) == 0x001F);
        CHECK(H3FontExtension::rgb888ToRgb565(0xFFFFFF) == 0xFFFF);
        CHECK(H3FontExtension::rgb888ToRgb565(0xF8FC00) == 0xFFE0);
    }

    void testGbkTables()
    {
        H3FontExtension::ExtFont::InitGbkTables();

        CHECK(H3FontExtension::ExtFont::IsGbkChar(L'A'));
        CHECK(H3FontExtension::ExtFont::IsGbkChar(L'中'));
        CHECK(!H3FontExtension::ExtFont::IsGbkChar(L'\uD55C')); // 韩文音节不在 GBK

        // GBK 编码往返：L'中' → MultiByteToWideChar 反查字节 → GbkToWchar 还原
        char gbk[2] = {};
        const wchar_t wch = L'中';
        const int len = ::WideCharToMultiByte(936, 0, &wch, 1, gbk, 2, nullptr, nullptr);
        CHECK(len == 2);
        if (len == 2)
        {
            const wchar_t back = H3FontExtension::ExtFont::GbkToWchar(
                static_cast<uint8_t>(gbk[0]), static_cast<uint8_t>(gbk[1]));
            CHECK(back == wch);
        }
    }

} // namespace

int main()
{
    testIsSingleByte();
    testIsDBCSLeadByte();
    testRgb888ToRgb565();
    testGbkTables();

    std::printf("H3CNTests: %d checks, %d failures\n", gChecks, gFailures);
    return (gFailures == 0) ? 0 : 1;
}
