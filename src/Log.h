#pragma once

#include <Windows.h>

namespace H3FontExtension
{
    /**
     * @brief 初始化日志（定位插件 DLL 同目录的 H3CN.log）
     *
     * 必须在任何 logInfo/logError 调用前执行；初始化失败后日志调用变为空操作。
     * @param hModule 插件自身模块句柄（由 DllMain 传入）
     */
    void initLog(HMODULE hModule);

    /** @brief 写入一条信息日志（printf 风格，UTF-8 内容） */
    void logInfo(const char* format, ...);

    /** @brief 写入一条错误日志（printf 风格，UTF-8 内容） */
    void logError(const char* format, ...);

} // namespace H3FontExtension
