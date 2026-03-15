// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#include "utils/logger.h"
#include "utils/limits.h" // NOLINT(misc-include-cleaner)
#include <array>
#include <cassert>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#endif

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace
{
std::array<char, static_cast<std::size_t>(2 * LINE_MAX)> log_buffer{};

// Implementation clock_gettime(CLOCK_MONOTONIC, ...) for Windows.
#ifdef _WIN32
enum
{
    CLOCK_MONOTONIC = 0
};

int clock_gettime(int tsrc, struct timespec *ts)
{
    (void)tsrc;
    assert(tsrc == CLOCK_MONOTONIC);

    static __int64 base = []() {
        __int64 t;
        GetSystemTimeAsFileTime((FILETIME *)&t);
        return t;
    }();

    __int64 cur;
    GetSystemTimeAsFileTime((FILETIME *)&cur);
    cur -= base;
    ts->tv_sec = time_t(cur / 10000000i64), ts->tv_nsec = long(cur % 10000000i64 * 100);
    return 0;
}
#endif

// Function returns thread identifier.
unsigned get_tid()
{
#ifdef _WIN32
    static const thread_local unsigned thread_id = static_cast<unsigned>(GetCurrentThreadId());
#else
    static const thread_local unsigned thread_id = syscall(SYS_gettid);
#endif

    return thread_id;
}

// Function returns process identifier.
unsigned get_pid()
{
#ifdef _WIN32
    static const unsigned process_id = static_cast<unsigned>(GetCurrentProcessId());
#else
    static const unsigned process_id = ::getpid();
#endif

    return process_id;
}

// This function opens log file, log file name is determined
// by contents of environment variable "LOG_OUTPUT".
FILE *open_log_file()
{
    const char *env = getenv("LOG_OUTPUT");
    if (env == nullptr)
    {
        return nullptr; // log disabled
    }

    if (strcmp("stdout", env) == 0)
    {
        return stdout;
    }

    if (strcmp("stderr", env) == 0)
    {
        return stderr;
    }

    FILE *result = fopen(env, "a"); // NOLINT(cppcoreguidelines-owning-memory)
    if (result == nullptr)
    {
        perror(env);
        return nullptr;
    }

    static_cast<void>(setvbuf(result, log_buffer.data(), _IOFBF, log_buffer.size()));
    return result;
}
} // namespace

// Function should form output line like this:
//
// 1500636976.777 I(P 2293, T 2293): udev.c: uevent_control_cb(62) > Set udev monitor buffer size 131072
// ^              ^    ^       ^      ^       ^                 ^     ^
// |              |    `pid    ` tid  |       ` function name   |     ` user provided message
// |              ` log level         ` file name               ` line number
// `--- time sec.msec
//
extern "C" int log_vprint(LogPriority prio, const char *fmt, va_list ap)
{
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);

    static std::mutex mutex;
    const std::scoped_lock<std::mutex> lock(mutex);

    if (prio == LogPriority::DEF)
    {
        prio = LogPriority::INF;
    }

    char level = 'I';
    if (prio >= LogPriority::DBG && prio <= LogPriority::FTL)
    {
        level = "DIWEF"[static_cast<uint8_t>(prio) - static_cast<uint8_t>(LogPriority::DBG)];
    }

    static FILE *log_file = open_log_file();
    if (log_file == nullptr || (ferror(log_file) != 0))
    {
        return LOG_ERROR_NOT_PERMITTED;
    }

    const int len = fprintf(log_file, "%li.%03i %c(P%4u, T%4u): ", static_cast<long>(ts.tv_sec & 0x7fffff),
                            static_cast<int>(ts.tv_nsec / 1000000), level, get_pid(), get_tid());

    const int r = vfprintf(log_file, fmt, ap);
    if (r < 0)
    {
        static_cast<void>(fputc('\n', log_file));
        return LOG_ERROR_INVALID_PARAMETER;
    }

    static_cast<void>(fputc('\n', log_file));
    return fflush(log_file) < 0 ? LOG_ERROR_NOT_PERMITTED : len + r + 1;
}

extern "C" int log_print(LogPriority prio, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_vprint(prio, fmt, args);
    va_end(args);
    return 0;
}
