// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "utils/logger.h"
#include <array>
#include <charconv>
#include <chrono>
#include <cstring>
#include <limits>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef FEATURE_PAL
#include <unistd.h>
#endif

#if (defined(__APPLE__) && defined(__MACH__))
#include <pthread.h>
#endif

namespace dncdbg
{

std::mutex Logger::m_logMutex;
std::atomic<LogLevel> Logger::m_logLevel{LogLevel::INF};

namespace
{
// Timestamp seconds are masked to 23 bits (0x7fffff), which keeps the seconds part of the header
// within seven decimal digits.
constexpr uint32_t MAX_TIMESTAMP_SECONDS = 0x7fffff;

// Milliseconds are always written with three digits, and process and thread identifiers with at least four
// digits. Seconds have no minimum width.
constexpr size_t SECONDS_MIN_DIGITS = 1;
constexpr size_t MSEC_DIGITS = 3;
constexpr size_t MIN_ID_DIGITS = 4;

// Buffer sizes for the decimal representation of a 32-bit unsigned value and of an int (including the sign).
constexpr size_t MAX_DECIMAL_DIGITS = static_cast<size_t>(std::numeric_limits<uint32_t>::digits10) + 1;
constexpr size_t MAX_LINE_NUMBER_DIGITS = static_cast<size_t>(std::numeric_limits<int>::digits10) + 2;

// The "<sec>.<msec> <L>(P<pid>, T<tid>): " header prefix consists of four decimal numbers and 11 fixed
// characters ('.', ' ', the level symbol, '(', 'P', ',', ' ', 'T', ')', ':', ' ').
constexpr size_t HEADER_PREFIX_NUMBER_COUNT = 4;
constexpr size_t HEADER_PREFIX_SEPARATOR_COUNT = 11;
constexpr size_t MAX_HEADER_PREFIX_LENGTH = (MAX_DECIMAL_DIGITS * HEADER_PREFIX_NUMBER_COUNT) + HEADER_PREFIX_SEPARATOR_COUNT;

// Suffix written after the function name and before the user-provided message.
constexpr std::string_view FUNC_SUFFIX("() > ");

// Function returns thread identifier.
unsigned get_tid()
{
#if (defined(__APPLE__) && defined(__MACH__))
    const auto getTID = []() -> unsigned
    {
        uint64_t tid = 0;
        pthread_threadid_np(nullptr, &tid);
        return static_cast<unsigned>(tid);
    };
    static const thread_local unsigned thread_id = getTID();
#elif defined(__linux__)
    static const thread_local unsigned thread_id = ::gettid();
#elif defined(_WIN32)
    static const thread_local unsigned thread_id = static_cast<unsigned>(GetCurrentThreadId());
#else
#error "Unsupported platform"
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

// Converts an uppercase ASCII letter to lowercase. Any other character is returned unchanged.
constexpr char ToLowerAscii(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

// Compares ASCII strings ignoring the case of letters. Unlike a comparison based on std::tolower(),
// it does not depend on the current C locale and does not allocate a temporary string.
constexpr bool EqualsAsciiCaseInsensitive(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (size_t i = 0; i < left.size(); ++i)
    {
        if (ToLowerAscii(left.at(i)) != ToLowerAscii(right.at(i)))
        {
            return false;
        }
    }

    return true;
}

} // unnamed namespace

void Logger::OpenLogStream(const char *fileName)
{
    if (fileName == nullptr)
    {
        return;
    }

    std::ofstream &logStream = GetLogStream();
    const std::scoped_lock lock(m_logMutex);

    // open() fails on a stream that is already open, so close it and reset the stream state first.
    if (logStream.is_open())
    {
        logStream.close();
    }
    logStream.clear();
    logStream.open(fileName, std::ios::app);
}

void Logger::SetLogLevel(const char *level)
{
    if (level == nullptr)
    {
        return;
    }

    if (std::strcmp(level, "0") == 0)
    {
        m_logLevel.store(LogLevel::DBG, std::memory_order_relaxed);
        return;
    }
    if (std::strcmp(level, "1") == 0)
    {
        m_logLevel.store(LogLevel::INF, std::memory_order_relaxed);
        return;
    }
    if (std::strcmp(level, "2") == 0)
    {
        m_logLevel.store(LogLevel::WRN, std::memory_order_relaxed);
        return;
    }
    if (std::strcmp(level, "3") == 0)
    {
        m_logLevel.store(LogLevel::ERR, std::memory_order_relaxed);
        return;
    }

    if (EqualsAsciiCaseInsensitive(level, "DEBUG"))
    {
        m_logLevel.store(LogLevel::DBG, std::memory_order_relaxed);
    }
    else if (EqualsAsciiCaseInsensitive(level, "INFO"))
    {
        m_logLevel.store(LogLevel::INF, std::memory_order_relaxed);
    }
    else if (EqualsAsciiCaseInsensitive(level, "WARNING"))
    {
        m_logLevel.store(LogLevel::WRN, std::memory_order_relaxed);
    }
    else if (EqualsAsciiCaseInsensitive(level, "ERROR"))
    {
        m_logLevel.store(LogLevel::ERR, std::memory_order_relaxed);
    }
    else
    {
        m_logLevel.store(LogLevel::INF, std::memory_order_relaxed);
        LOGW(log << "Unknown log level '" << level << "', using default INFO level");
    }
}

// The function forms an output line like this:
//
// 1500636976.777 I(P 2293, T 2293): udev.c:64 uevent_control_cb() > Set udev monitor buffer size 131072
// ^              ^    ^       ^      ^     ^              ^          ^
// |              |    ` pid   ` tid  |     ` line number  |          ` user provided message
// |              ` log level         ` file name          ` function name
// `--- time sec.msec
//
void Logger::LogPrint(LogLevel level, const char *file, int line, const char *func, const LoggerCallback &cb)
{
    if (!IsEnabled(level))
    {
        return;
    }

    std::ofstream &logStream = GetLogStream();

    // The timestamp is taken before the mutex is acquired, so waiting for other threads does not affect it.
    // std::chrono::steady_clock is monotonic on all supported platforms, so it is used on every platform.
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto nowSec = std::chrono::duration_cast<std::chrono::seconds>(now);
    const auto nowMsec = std::chrono::duration_cast<std::chrono::milliseconds>(now - nowSec);

    std::array<char, MAX_HEADER_PREFIX_LENGTH> header{};
    char *cursor = header.data();

    // Appends the decimal representation of the value, zero-padded to at least minWidth digits.
    const auto appendNumber = [&cursor](const uint32_t value, const size_t minWidth)
    {
        std::array<char, MAX_DECIMAL_DIGITS> digits{};
        const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), value);
        const auto length = static_cast<size_t>(result.ptr - digits.data());
        const size_t padding = (length < minWidth) ? (minWidth - length) : 0;
        std::memset(cursor, '0', padding);
        cursor += padding;
        std::memcpy(cursor, digits.data(), length);
        cursor += length;
    };

    const auto sec = static_cast<uint32_t>(nowSec.count()) & MAX_TIMESTAMP_SECONDS;
    appendNumber(sec, SECONDS_MIN_DIGITS);
    *cursor++ = '.';
    appendNumber(static_cast<uint32_t>(nowMsec.count()), MSEC_DIGITS);
    *cursor++ = ' ';

    char levelSymb = 'I';
    static constexpr std::string_view levelSymbol("DIWE");
    if (level >= LogLevel::DBG && level <= LogLevel::ERR)
    {
        levelSymb = levelSymbol.at(static_cast<uint8_t>(level));
    }

    *cursor++ = levelSymb;
    *cursor++ = '(';
    *cursor++ = 'P';
    appendNumber(get_pid(), MIN_ID_DIGITS);
    *cursor++ = ',';
    *cursor++ = ' ';
    *cursor++ = 'T';
    appendNumber(get_tid(), MIN_ID_DIGITS);
    *cursor++ = ')';
    *cursor++ = ':';
    *cursor++ = ' ';

    const std::scoped_lock lock(m_logMutex);

    logStream.write(header.data(), static_cast<std::streamsize>(cursor - header.data()));
    logStream.write(file, static_cast<std::streamsize>(std::strlen(file)));
    logStream.put(':');

    std::array<char, MAX_LINE_NUMBER_DIGITS> lineText{};
    const auto lineResult = std::to_chars(lineText.data(), lineText.data() + lineText.size(), line);
    logStream.write(lineText.data(), static_cast<std::streamsize>(lineResult.ptr - lineText.data()));
    logStream.put(' ');

    logStream.write(func, static_cast<std::streamsize>(std::strlen(func)));
    logStream.write(FUNC_SUFFIX.data(), static_cast<std::streamsize>(FUNC_SUFFIX.size()));

    cb(logStream);

    if (logStream.fail())
    {
        return;
    }

    logStream.put('\n');
    logStream.flush();
}

} // namespace dncdbg
