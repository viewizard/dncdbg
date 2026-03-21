// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifndef UTILS_LOGGER_H
#define UTILS_LOGGER_H

#include <stdarg.h>
#include <cstddef>
#include <cstring>
#include <cstdint>

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

// this function writes log message with given priority
extern "C" int log_print(LogPriority prio, const char *fmt, ...)
#ifndef _MSC_VER
    __attribute__((format(printf, 2, 3))) // check printf arguments (GCC/Clang only)
#endif
    ;

// Possible results of log_print() function call.
constexpr int LOG_ERROR_INVALID_PARAMETER = -1;
constexpr int LOG_ERROR_NOT_PERMITTED  = -2;

// All definitions in this namespace intendent only for internal usage.
namespace LogInternal
{

// This function computes file path (directory component) length at compile time.
template <size_t N> constexpr size_t path_len(const char (&path)[N], size_t pos = N - 1) // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
{
    if (path[pos] == '/' || path[pos] == '\\')
    {
        return pos + 1;
    }
    else if (pos != 0)
    {
        return path_len(path, pos - 1);
    }

    return 0;
}

// This function computes length of function name only for given function signature.
template <size_t N> constexpr size_t funcname_len(const char (&sig)[N], size_t pos = 0) // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
{
    return (sig[pos] >= 'A' && sig[pos] <= 'Z') || (sig[pos] >= 'a' && sig[pos] <= 'z') ||
                   (sig[pos] >= '0' && sig[pos] <= '9') || sig[pos] == '_' || sig[pos] == '$' || sig[pos] == ':'
               ? funcname_len(sig, pos + 1)
               : pos;
}

#ifndef _MSC_VER
inline int __attribute__((format(printf, 1, 2))) check_args(const char */*fmt*/, ...) // NOLINT(cert-dcl50-cpp,modernize-avoid-variadic-functions)
{
    return 0;
}
#endif

} // namespace LogInternal

// Following macros shouldn't be used directly, it is intendent for internal use.
#define LOG_S_(str) #str
#define LOG_S(str) LOG_S_(str)

// With Visual Studio's compiler arguments checking performed via (eliminated from code) call to printf.
#ifdef _MSC_VER
#define LOG_CHECK_ARGS_(fmt, ...) (false ? printf(fmt, ##__VA_ARGS__) : 0)
#else
#define LOG_CHECK_ARGS_(fmt, ...) (false ? LogInternal::check_args(fmt, ##__VA_ARGS__) : 0)
#endif

// Following macros shouldn't be used directly, it is intendent for internal use.
#define LOG_(prio, fmt, ...) \
        (LOG_CHECK_ARGS_(fmt, ##__VA_ARGS__), \
        log_print(prio, "%.*s: %.*s(%.*s) > " fmt, \
            static_cast<int>(sizeof(__FILE__) - LogInternal::path_len(__FILE__)), &__FILE__[LogInternal::path_len(__FILE__)], \
            static_cast<int>(LogInternal::funcname_len(__func__)), __func__, /* NOLINT(bugprone-lambda-function-name) */ \
            static_cast<int>(sizeof(LOG_S(__LINE))), LOG_S(__LINE__), \
            ##__VA_ARGS__))

#ifdef DEBUG
#define LOGD(fmt, ...) LOG_(LogPriority::DBG, fmt, ##__VA_ARGS__)
#else
#define LOGD(fmt, ...) LOG_CHECK_ARGS_(fmt, ##__VA_ARGS__)
#endif

#define LOGI(fmt, ...) LOG_(LogPriority::INF, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG_(LogPriority::WRN, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LOG_(LogPriority::ERR, fmt, ##__VA_ARGS__)
#define LOGF(fmt, ...) LOG_(LogPriority::FTL, fmt, ##__VA_ARGS__)

#endif // UTILS_LOGGER_H
