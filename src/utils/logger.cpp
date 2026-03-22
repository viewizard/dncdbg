// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#include "utils/logger.h"
#include "utils/limits.h" // NOLINT(misc-include-cleaner)
#include <mutex>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iomanip>

#ifdef _WIN32
#include <cassert>
#include <windows.h>
#endif

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace
{
constexpr long MAX_TIMESTAMP_SECONDS = 0x7fffff;
constexpr long NSEC_TO_MSEC = 1000000;

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
std::ostream &open_log_stream()
{
    const char *env = getenv("LOG_OUTPUT");
    if (env == nullptr)
    {
        static std::ostream null_stream(nullptr);
        return null_stream;
    }

    if (strcmp("stdout", env) == 0)
    {
        return std::cout;
    }

    if (strcmp("stderr", env) == 0)
    {
        return std::cerr;
    }

    static std::ofstream log_file;
    log_file.open(env, std::ios::app);
    if (!log_file.is_open())
    {
        static std::ostream null_stream(nullptr);
        return null_stream;
    }

    return log_file;
}

} // namespace

// Function should form output line like this:
//
// 1500636976.777 I(P 2293, T 2293): udev.c:64 uevent_control_cb() > Set udev monitor buffer size 131072
// ^              ^    ^       ^      ^     ^              ^          ^
// |              |    ` pid   ` tid  |     ` line number  |          ` user provided message
// |              ` log level         ` file name          ` function name
// `--- time sec.msec
//
void log_print(LogPriority prio, const char *file, int line, const char *func, const LoggerCallback &cb)
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

    static std::ostream &log_stream = open_log_stream();

    if (!log_stream.good())
    {
        return;
    }

    log_stream << (ts.tv_sec & MAX_TIMESTAMP_SECONDS) << '.'
               << std::dec << std::setfill('0') << std::setw(3)
               << (ts.tv_nsec / NSEC_TO_MSEC) << ' '
               << level << "(P" << std::setw(4) << get_pid()
               << ", T" << std::setw(4) << get_tid() << "): "
               << file << ":" << line << " " << func << "() > ";

    cb(log_stream);

    if (log_stream.fail())
    {
        return;
    }

    log_stream << '\n';
    log_stream.flush();
}
