// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef UTILS_LOGGER_H
#define UTILS_LOGGER_H

#include "utils/function_ref.h"
#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string_view>

namespace dncdbg
{

constexpr uint32_t hexErrWidth = 8;

enum class LogLevel : uint8_t
{
    DBG,     // DEBUG
    INF,     // INFO
    WRN,     // WARNING
    ERR      // ERROR
};

class Logger
{
  public:

    using LoggerCallback = FunctionRef<void(std::ostream &log)>;

    // Fast check used by the LOG* macros, so that a disabled log message does not call LogPrint()
    // at all. LogPrint() repeats the check on its own, since it is part of the public interface.
    static bool IsEnabled(LogLevel level) noexcept
    {
        const std::ofstream &logStream = GetLogStream();
        return level >= m_logLevel.load(std::memory_order_relaxed) && logStream.is_open() && logStream.good();
    }

    static void LogPrint(LogLevel level, const char *file, int line, const char *func, const LoggerCallback &cb);
    static void OpenLogStream(const char *fileName);
    static void SetLogLevel(const char *level);

  private:

    static std::mutex m_logMutex;
    // The log level is read by all logging threads, so it is stored atomically.
    static std::atomic<LogLevel> m_logLevel;

    static std::ofstream &GetLogStream()
    {
        static std::ofstream logStream;
        return logStream;
    }
};

// This function computes file path (directory component) length at compile time.
constexpr size_t PathLen(std::string_view path)
{
    for (size_t pos = path.size(); pos > 0; --pos)
    {
        // Note: std::string_view::at() is not constexpr in C++17 (it became constexpr in C++20)
        if (path[pos - 1] == '/' || path[pos - 1] == '\\') // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        {
            return pos;
        }
    }

    return 0;
}

} // namespace dncdbg

// Following macros shouldn't be used directly, they are intended for internal use.
#define LOG_(prio, cbLog) do { if (dncdbg::Logger::IsEnabled(prio)) { dncdbg::Logger::LogPrint(prio, &__FILE__[dncdbg::PathLen(__FILE__)], __LINE__, __func__, [&](std::ostream &log){ cbLog; }); } } while (0) // NOLINT(cppcoreguidelines-macro-usage,cppcoreguidelines-pro-bounds-constant-array-index)

#ifdef DEBUG
#define LOGD(cbLog) LOG_(dncdbg::LogLevel::DBG, cbLog) // NOLINT(cppcoreguidelines-macro-usage)
#else
#define LOGD(cbLog)
#endif

#define LOGI(cbLog) LOG_(dncdbg::LogLevel::INF, cbLog) // NOLINT(cppcoreguidelines-macro-usage)
#define LOGW(cbLog) LOG_(dncdbg::LogLevel::WRN, cbLog) // NOLINT(cppcoreguidelines-macro-usage)
#define LOGE(cbLog) LOG_(dncdbg::LogLevel::ERR, cbLog) // NOLINT(cppcoreguidelines-macro-usage)

#endif // UTILS_LOGGER_H
