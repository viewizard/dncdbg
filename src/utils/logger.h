// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifndef UTILS_LOGGER_H
#define UTILS_LOGGER_H

#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <string_view>

#ifdef _MSC_VER
#include <cstdio>
#endif

#ifndef __cplusplus
#error "This file applicable only in C++ source code, plain C not supported."
#endif

namespace dncdbg
{

constexpr uint32_t hexErrWidth = 8;

// Log levels.
enum class LogPriority : uint8_t
{
    UNK = 0, // UNKNOWN
    DEF,     // DEFAULT
    DBG,     // DEBUG
    INF,     // INFO
    WRN,     // WARNING
    ERR,     // ERROR
    FTL      // FATAL
};

class Logger
{
  public:

    using LoggerCallback = std::function<void(std::ostream &log)>;

    static void LogPrint(LogPriority prio, const char *file, int line, const char *func, const LoggerCallback &cb);
    static void OpenLogStream(const char *fileName);

  private:

    static std::mutex m_logMutex;

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
        if (path[pos - 1] == '/' || path[pos - 1] == '\\')
        {
            return pos;
        }
    }

    return 0;
}

} // namespace dncdbg

// Following macros shouldn't be used directly, it is intendent for internal use.
#define LOG_(prio, cbLog) dncdbg::Logger::LogPrint(prio, &__FILE__[dncdbg::PathLen(__FILE__)], __LINE__, __func__, [&](std::ostream &log){ cbLog; })

#ifdef DEBUG
#define LOGD(cbLog) LOG_(dncdbg::LogPriority::DBG, cbLog)
#else
#define LOGD(cbLog)
#endif

#define LOGI(cbLog) LOG_(dncdbg::LogPriority::INF, cbLog)
#define LOGW(cbLog) LOG_(dncdbg::LogPriority::WRN, cbLog)
#define LOGE(cbLog) LOG_(dncdbg::LogPriority::ERR, cbLog)
#define LOGF(cbLog) LOG_(dncdbg::LogPriority::FTL, cbLog)

#endif // UTILS_LOGGER_H
