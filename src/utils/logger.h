// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifndef UTILS_LOGGER_H
#define UTILS_LOGGER_H

#include <cstdint>
#include <string_view>
#include <functional>
#include <iomanip>

#ifdef _MSC_VER
#include <cstdio>
#endif

#ifndef __cplusplus
#error "This file applicable only in C++ source code, plain C not supported."
#endif

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

// All definitions in this namespace intendent only for internal usage.
namespace LogInternal
{

// This function computes file path (directory component) length at compile time.
constexpr size_t path_len(std::string_view path)
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

} // namespace LogInternal

using LoggerCallback = std::function<void(std::ostream &stream)>;

void log_print(LogPriority prio, const char *file, int line, const char *func, const LoggerCallback &cb);

// Following macros shouldn't be used directly, it is intendent for internal use.
#define LOG_(prio, cbLog) log_print(prio, &__FILE__[LogInternal::path_len(__FILE__)], __LINE__, __func__, [&](std::ostream &log){ cbLog; })

#ifdef DEBUG
#define LOGD(cbLog) LOG_(LogPriority::DBG, cbLog)
#else
#define LOGD(cbLog)
#endif

#define LOGI(cbLog) LOG_(LogPriority::INF, cbLog)
#define LOGW(cbLog) LOG_(LogPriority::WRN, cbLog)
#define LOGE(cbLog) LOG_(LogPriority::ERR, cbLog)
#define LOGF(cbLog) LOG_(LogPriority::FTL, cbLog)

constexpr uint32_t hexErrWidth = 8;

#endif // UTILS_LOGGER_H
