// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#pragma once

#include <stdarg.h>
#include <cstddef>
#include <cstring>

#ifdef _MSC_VER
#include <cstdio>
#endif

#ifndef __cplusplus
#error "This file applicable only in C++ source code, plain C not supported."
#endif

// Log levels as defined in Tizen.
enum log_priority
{
    DLOG_UNKNOWN = 0,
    DLOG_DEFAULT,
    DLOG_DEBUG,
    DLOG_INFO,
    DLOG_WARN,
    DLOG_ERROR,
    DLOG_FATAL
};

// this function writes log message with given priority
extern "C" int dlog_print(log_priority prio, const char *fmt, ...)
#ifndef _MSC_VER
    __attribute__((format(printf, 2, 3))) // check printf arguments (GCC/Clang only)
#endif
    ;

// Possible results of dlog_print() function call.
constexpr int DLOG_ERROR_INVALID_PARAMETER = -1;
constexpr int DLOG_ERROR_NOT_PERMITTED  = -2;

// All definitions in this namespace intendent only for internal usage.
namespace DLogInternal
{

// This function computes file path (directory component) length at compile time.
template <size_t N> constexpr size_t path_len(const char (&path)[N], size_t pos = N - 1)
{
    return (path[pos] == '/' || path[pos] == '\\') ? pos + 1 : pos ? path_len(path, pos - 1) : 0;
}

// This function computes length of function name only for given function signature.
template <size_t N> constexpr size_t funcname_len(const char (&sig)[N], size_t pos = 0)
{
    return (sig[pos] >= 'A' && sig[pos] <= 'Z') || (sig[pos] >= 'a' && sig[pos] <= 'z') ||
                   (sig[pos] >= '0' && sig[pos] <= '9') || sig[pos] == '_' || sig[pos] == '$' || sig[pos] == ':'
               ? funcname_len(sig, pos + 1)
               : pos;
}

#ifndef _MSC_VER
inline int __attribute__((format(printf, 1, 2))) check_args(const char *, ...)
{
    return 0;
}
#endif

} // namespace DLogInternal

// Following macros shouldn't be used directly, it is intendent for internal use.
#define LOG_S__(str) #str
#define LOG_S_(str) LOG_S__(str)

// With Visual Studio's compiler arguments checking performed via (eliminated from code) call to printf.
#ifdef _MSC_VER
#define LOG_CHECK_ARGS_(fmt, ...) (false ? printf(fmt, ##__VA_ARGS__) : 0)
#else
#define LOG_CHECK_ARGS_(fmt, ...) (false ? DLogInternal::check_args(fmt, ##__VA_ARGS__) : 0)
#endif

// Following macros shouldn't be used directly, it is intendent for internal use.
#define LOG_(prio, fmt, ...) \
        (LOG_CHECK_ARGS_(fmt, ##__VA_ARGS__), \
        dlog_print(prio, "%.*s: %.*s(%.*s) > " fmt, \
            static_cast<int>(sizeof(__FILE__) - DLogInternal::path_len(__FILE__)), &__FILE__[DLogInternal::path_len(__FILE__)], \
            static_cast<int>(DLogInternal::funcname_len(__func__)), __func__, /* NOLINT(bugprone-lambda-function-name) */ \
            static_cast<int>(sizeof(LOG_S_(__LINE))), LOG_S_(__LINE__), \
            ##__VA_ARGS__))

#ifdef DEBUG
#define LOGD(fmt, ...) LOG_(DLOG_DEBUG, fmt, ##__VA_ARGS__)
#else
#define LOGD(fmt, ...) LOG_CHECK_ARGS_(fmt, ##__VA_ARGS__)
#endif

#define LOGI(fmt, ...) LOG_(DLOG_INFO, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG_(DLOG_WARN, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LOG_(DLOG_ERROR, fmt, ##__VA_ARGS__)
#define LOGF(fmt, ...) LOG_(DLOG_FATAL, fmt, ##__VA_ARGS__)
