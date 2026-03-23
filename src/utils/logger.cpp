// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#include "utils/logger.h"
#include "utils/limits.h" // NOLINT(misc-include-cleaner)
#include <ctime>

#ifdef _WIN32
#include <cassert>
#include <windows.h>
#endif

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace dncdbg
{

std::mutex Logger::m_logMutex;

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

} // namespace

void Logger::OpenLogStream(const char *fileName)
{
    GetLogStream().open(fileName, std::ios::app);
}

// Function should form output line like this:
//
// 1500636976.777 I(P 2293, T 2293): udev.c:64 uevent_control_cb() > Set udev monitor buffer size 131072
// ^              ^    ^       ^      ^     ^              ^          ^
// |              |    ` pid   ` tid  |     ` line number  |          ` user provided message
// |              ` log level         ` file name          ` function name
// `--- time sec.msec
//
void Logger::LogPrint(LogPriority prio, const char *file, int line, const char *func, const LoggerCallback &cb)
{
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);

    const std::scoped_lock<std::mutex> lock(m_logMutex);

    if (prio == LogPriority::DEF)
    {
        prio = LogPriority::INF;
    }

    char level = 'I';
    if (prio >= LogPriority::DBG && prio <= LogPriority::FTL)
    {
        level = "DIWEF"[static_cast<uint8_t>(prio) - static_cast<uint8_t>(LogPriority::DBG)];
    }

    if (!GetLogStream().good())
    {
        return;
    }

    GetLogStream() << (ts.tv_sec & MAX_TIMESTAMP_SECONDS) << '.'
                   << std::dec << std::setfill('0') << std::setw(3)
                   << (ts.tv_nsec / NSEC_TO_MSEC) << ' '
                   << level << "(P" << std::setw(4) << get_pid()
                   << ", T" << std::setw(4) << get_tid() << "): "
                   << file << ":" << line << " " << func << "() > ";

    cb(GetLogStream());

    if (GetLogStream().fail())
    {
        return;
    }

    GetLogStream() << '\n';
    GetLogStream().flush();
}

} // namespace dncdbg
