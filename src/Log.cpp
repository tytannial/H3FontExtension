//===----------------------------------------------------------------------===//
// 日志：写入插件 DLL 同目录的 H3CN.log（UTF-8）
//
// 仅保留最后一次运行的日志：initLog 时截断文件；
// 每次写入独立开关文件，调用频率低（初始化/错误路径），无需持有句柄。
//===----------------------------------------------------------------------===//

#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace H3FontExtension
{
    /** @brief 日志文件完整路径（initLog 成功后有效） */
    static wchar_t gLogPath[MAX_PATH] = {};

    /** @brief 日志是否可用（定位 / 拼接路径成功） */
    static bool gLogReady = false;

    /** @brief 实际写入（带时间戳与级别；gLogReady 为 false 时为空操作） */
    static void writeLog(const char* level, const char* format, va_list args)
    {
        if (!gLogReady)
            return;

        char message[1024] = {};
        std::vsnprintf(message, sizeof(message), format, args);

        SYSTEMTIME st = {};
        ::GetLocalTime(&st);

        char line[1200] = {};
        const int lineLen = std::snprintf(line, sizeof(line),
            "[%04u-%02u-%02u %02u:%02u:%02u] [%s] %s\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            level, message);
        if (lineLen <= 0)
            return;

        const HANDLE file = ::CreateFileW(gLogPath, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;

        const DWORD toWrite = static_cast<DWORD>(
            (lineLen < static_cast<int>(sizeof(line))) ? lineLen : static_cast<int>(sizeof(line)) - 1);
        DWORD written = 0;
        ::WriteFile(file, line, toWrite, &written, nullptr);
        ::CloseHandle(file);
    }

    void initLog(HMODULE hModule)
    {
        wchar_t path[MAX_PATH] = {};
        const DWORD len = ::GetModuleFileNameW(hModule, path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            return;

        wchar_t* fileName = ::wcsrchr(path, L'\\');
        if (!fileName)
            return;
        *(fileName + 1) = L'\0';
        if (::wcscat_s(path, L"H3CN.log") != 0)
            return;

        ::wcscpy_s(gLogPath, path);

        // 仅保留最后一次运行的日志：启动时截断旧内容
        const HANDLE file = ::CreateFileW(gLogPath, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
            ::CloseHandle(file);

        gLogReady = true;
        logInfo("=== H3FontExtension 日志开始 ===");
    }

    void logInfo(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        writeLog("INFO", format, args);
        va_end(args);
    }

    void logError(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        writeLog("ERROR", format, args);
        va_end(args);
    }

} // namespace H3FontExtension
