#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ExtFont.h"

#include <nh3api/core.hpp>
#include <nh3api/hd_mod.hpp>

namespace H3FontExtension
{
    /* 双字节码表定义
     * GBK范围
     * 0x8140-A0FE，收录 CJK 汉字 6080 个
     * 0xA1A1-A9FE，除 GB 2312 的符号外，还增补了其它符号
     * 0xAA40-A0FE，收录 CJK 汉字和增补的汉字 8160 个
     * 0xA840-A9A0，扩除非汉字区
     * 0xB0A1-F7FE，收录 GB 2312 汉字 6763 个，按原序排列
     * BIG5范围
     * 0xA140-0xA3BF：标点符号、希腊字母及特殊符号
     * 0xA440-0xC67E：常用汉字，先按笔划再按部首排序
     */

    // ============================================================
    // 常量定义
    // ============================================================

    /** @brief 阴影色在调色板中的索引（与原版 font::DrawCharacter 使用的索引 32 一致） */
    constexpr int ShadowColorIndex = 32;

    // ============================================================
    // 枚举
    // ============================================================

    /**
     * @brief 可见字符类型，用于文本扫描遍历
     */
    enum class TokenType : uint8_t {
        NEWLINE,    ///< 换行符 '\n'
        SPACE,      ///< 空格 ' '
        SINGLE_BYTE, ///< ASCII 单字节可见字符
        DOUBLE_BYTE  ///< 双字节字符（GBK 汉字等）
    };

    // ============================================================
    // 数据结构
    // ============================================================

    /**
     * @brief 原始文本行（含颜色码，拆分后未预处理）
     */
    struct TextLineStruct
    {
        std::string text;    ///< 行文本内容（含颜色码 {~...} / { / }）
        int line_width;      ///< 该行可见字符的总像素宽度
    };

    /**
     * @brief 扩展后的游戏字体结构体（比原始 font 多 8 字节）
     *
     * 通过修改游戏内存分配大小实现，额外字段存储原始高度和扩展字库指针。
     */
    struct H3FontExt : font
    {
        int      ori_height; ///< 原始字体高度（扩展前的 fr.height 值）
        ExtFont* ext_data;   ///< 关联的扩展字库指针
    };

    /**
     * @brief 颜色变更点：记录纯文本中某个字节偏移处的颜色切换
     */
    struct ColorStop
    {
        std::size_t byte_pos; ///< 在 cleanText 中的字节偏移
        DWORD  color;   ///< 切换后的渲染颜色（已转换为当前色深格式）
    };

    /**
     * @brief 预处理后的文本行
     *
     * 颜色码已从 text 中剥离，颜色信息独立存储在 stops 中。
     * 渲染时逐字节遍历 text，遇到 stop 点切换颜色。
     */
    struct CleanLine
    {
        std::string text;               ///< 纯可见字符（无颜色码、无控制字符）
        int         line_width;          ///< 该行可见字符的总像素宽度
        std::vector<ColorStop> stops;   ///< 按 byte_pos 升序排列的颜色变更点
        std::vector<uint16_t> advances; ///< 每个可见字符的步进宽度（布局阶段预计算，绘制期零测量）
    };

    // ============================================================
    // 内联工具函数
    // ============================================================

    /**
     * @brief 判断当前字节是否为合法的双字节字符首字节（GBK/DBCS）
     * @param code     当前字节
     * @param nextCode 下一字节
     * @return true 表示 code/nextCode 构成一个合法的双字节字符
     */
    inline static bool isDBCSLeadByte(uint8_t code, uint8_t nextCode)
    {
        // GBK 位码有效范围 0x40..0xFE，其中 0x7F 为非法码位
        return code >= DBCS_SECTION && nextCode >= DBCS_POSITION && nextCode <= 0xFE && nextCode != 0x7F;
    }

    /**
     * @brief 判断当前字节是否为单字节字符（ASCII 或 0xFF 填充）
     * @param code 当前字节
     * @return true 表示该字节为单字节字符
     */
    inline static bool isSingleByte(uint8_t code)
    {
        return code < DBCS_SECTION || code == 0xFF;
    }

    /**
     * @brief 将 24 位 RGB888 颜色值转换为 16 位 RGB565 格式
     * @param color 32 位颜色值（仅低 24 位有效）
     * @return RGB565 格式的 16 位颜色值
     */
    constexpr inline static WORD rgb888ToRgb565(DWORD color)
    {
        const DWORD r = (color >> 16) & 0xFF;
        const DWORD g = (color >> 8) & 0xFF;
        const DWORD b = color & 0xFF;
        return static_cast<WORD>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    // ============================================================
    // 公共 API
    // ============================================================

    /**
     * @brief 插件初始化入口
     *
     * 加载配置文件（位于插件 DLL 同目录，避免依赖进程工作目录），
     * 注册所有 Hook 函数。
     * @param hModule 插件自身模块句柄（由 DllMain 传入，用于定位配置文件）
     * @param gameEntryPoint 游戏可执行文件 PE 入口点（版本标识，仅用于日志）
     * @return 初始化是否成功
     */
    bool init(HMODULE hModule, uint32_t gameEntryPoint);

} // namespace H3FontExtension
