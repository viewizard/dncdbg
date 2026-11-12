// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "protocol/internal_helpers.h"
#include "config/config.h"
#include "utils/logger.h"
#include "utils/remote_server.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <ostream>
#include <string>
#include <utility>

// for convenience
using nlohmann::json;

namespace dncdbg::DAP
{

namespace
{

// Use a function-local static to avoid undefined behavior from a static std::ofstream
// member, whose constructor may throw.
std::ofstream &GetProtocolLog()
{
    static std::ofstream protocolLog;
    return protocolLog;
}

std::mutex &GetOutMutex()
{
    static std::mutex outMutex;
    return outMutex;
}

// Note: this counter must be protected by GetOutMutex().
uint64_t &GetSeqCounter()
{
    static uint64_t seqCounter = 1;
    return seqCounter;
}

// Number of decimal digits in the largest 64-bit unsigned value (18446744073709551615).
constexpr std::size_t gMaxSizeDigits = 20;

// Largest buffer capacity kept between messages. A bigger buffer (for example after a very large
// variables response) is released after use, so it does not stay allocated for the whole session.
constexpr std::size_t gMaxRetainedOutputCapacity = 64 * 1024;

// Serialization buffer shared by all messages. It is only accessed under GetOutMutex(), and its
// capacity is reused, so hot paths (stopped and output events, variables responses) don't
// reallocate per message.
std::string &GetOutputBuffer()
{
    static std::string outputBuffer;
    return outputBuffer;
}

// Clears the shared buffer when it goes out of scope (including on exception), and releases its
// memory if it grew beyond gMaxRetainedOutputCapacity. Caller must hold GetOutMutex().
class OutputBufferGuard
{
  public:
    explicit OutputBufferGuard(std::string &buffer) noexcept
        : m_buffer(buffer)
    {
    }

    ~OutputBufferGuard()
    {
        if (m_buffer.capacity() > gMaxRetainedOutputCapacity)
        {
            std::string().swap(m_buffer);
        }
        else
        {
            m_buffer.clear();
        }
    }

    OutputBufferGuard(const OutputBufferGuard &) = delete;
    OutputBufferGuard &operator=(const OutputBufferGuard &) = delete;
    OutputBufferGuard(OutputBufferGuard &&) = delete;
    OutputBufferGuard &operator=(OutputBufferGuard &&) = delete;

  private:
    std::string &m_buffer;
};

// Serializes `message` into `output`, reusing its capacity. This uses nlohmann's internal
// serializer with an output adapter bound to `output`, which is what json::dump() does, except
// that dump() always returns a newly allocated string.
// Caller must hold GetOutMutex().
void SerializeInto(const nlohmann::json &message, std::string &output)
{
    output.clear();
    nlohmann::detail::serializer<nlohmann::json> writer(
        nlohmann::detail::output_adapter<char, std::string>(output), ' ');
    writer.dump(message, false, false, 0);
}

// Caller must hold GetOutMutex(). `output` is the reusable serialization buffer.
void EmitMessage(nlohmann::json &message, std::string &output)
{
    // In server mode, the protocol is written to the loopback TCP connection (RemoteServer) instead of stdout.
    std::ostream &stream = Config::IsServerMode() ? RemoteServer::Instance().GetOutputStream() : std::cout;

    message.emplace("seq", GetSeqCounter());
    ++GetSeqCounter();
    SerializeInto(message, output);

    // Build "Content-Length: <size>\r\n\r\n" on the stack (no heap, no stream formatting).
    std::array<char, CONTENT_LENGTH.size() + gMaxSizeDigits + TWO_CRLF.size()> header{};
    char *cursor = std::copy(CONTENT_LENGTH.begin(), CONTENT_LENGTH.end(), header.data());
    cursor = std::to_chars(cursor, header.data() + header.size(), output.size()).ptr;
    cursor = std::copy(TWO_CRLF.begin(), TWO_CRLF.end(), cursor);

    stream.write(header.data(), static_cast<std::streamsize>(cursor - header.data()));
    stream.write(output.data(), static_cast<std::streamsize>(output.size()));
    stream.flush();
}

// Caller must hold GetOutMutex().
void LogInternal(std::string_view prefix, const std::string &text)
{
    if (!GetProtocolLog().is_open())
    {
        return;
    }

    GetProtocolLog() << prefix << text << '\n' << std::flush;
}

} // namespace

void AddCapabilitiesTo(json &capabilities)
{
    capabilities.emplace("supportsConfigurationDoneRequest", true);
    capabilities.emplace("supportsFunctionBreakpoints", true);
    capabilities.emplace("supportsConditionalBreakpoints", true);
    capabilities.emplace("supportTerminateDebuggee", true);
    capabilities.emplace("supportsSetVariable", true);
    capabilities.emplace("supportsSetExpression", true);
    capabilities.emplace("supportsTerminateRequest", true);
    capabilities.emplace("supportsCancelRequest", true);
    capabilities.emplace("supportsExceptionInfoRequest", true);
    capabilities.emplace("supportsExceptionFilterOptions", true);
    json excFilters = json::array();
    for (const auto &entry : gExceptionFilters)
    {
        json excFilter = json::object();
        excFilter.emplace("filter", entry.name);
        excFilter.emplace("label", entry.name);
        excFilters.push_back(std::move(excFilter));
    }
    capabilities.emplace("exceptionBreakpointFilters", std::move(excFilters));
    capabilities.emplace("supportsExceptionOptions", false); // TODO add implementation
    capabilities.emplace("supportsHitConditionalBreakpoints", true);
    capabilities.emplace("supportsModulesRequest", true);
    capabilities.emplace("supportsLogPoints", true);
    capabilities.emplace("supportsGotoTargetsRequest", true);
    capabilities.emplace("supportsSingleThreadExecutionRequests", true);
    capabilities.emplace("supportsLoadedSourcesRequest", true);
    capabilities.emplace("supportsBreakpointLocationsRequest", true);
    // Note: The VS Code IDE doesn't support restarting a debug session, only restarting the entire debugger.
    if (!Config::IsRunningViaVsDbgUI())
    {
        capabilities.emplace("supportsRestartRequest", true);
    }
    capabilities.emplace("supportsReadMemoryRequest", true);
    capabilities.emplace("supportsWriteMemoryRequest", true);
}

void SetupProtocolLoggingInternal(const std::string &path)
{
    if (path.empty())
    {
        return;
    }

    GetProtocolLog().open(path);
}

void EmitEvent(std::string_view name, nlohmann::json &&body)
{
    json message;
    message.emplace("type", "event");
    message.emplace("event", name);
    message.emplace("body", std::move(body));
    EmitMessageWithLog(LOG_EVENT, message);
}

void EmitMessageWithLog(std::string_view messagePrefix, nlohmann::json &message)
{
    const std::scoped_lock<std::mutex> lock(GetOutMutex());
    std::string &output = GetOutputBuffer();
    const OutputBufferGuard bufferGuard(output);
    EmitMessage(message, output);
    LogInternal(messagePrefix, output);
}

void Log(std::string_view prefix, const std::string &text)
{
    const std::scoped_lock<std::mutex> lock(GetOutMutex());
    LogInternal(prefix, text);
}

// Zero (or less) means an unknown location; the DAP specification requires such zero line/column
// values to be ignored by the client, so the sentinel is returned unchanged.
int32_t ToClientLine(int32_t line)
{
    return (line <= 0 || Config::GetLinesStartAt1()) ? line : line - 1;
}

int32_t ToClientColumn(int32_t column)
{
    return (column <= 0 || Config::GetColumnsStartAt1()) ? column : column - 1;
}

// For a 0-based client, a provided value shifts by one into the internal 1-based numbering.
int32_t ToInternalLine(int32_t line)
{
    return Config::GetLinesStartAt1() ? line : line + 1;
}

// DAP session state gate shared by the command loop (dap.cpp) and the request handlers
// (dap_commands.cpp). Set to true by the `initialize` command and reset to false once `launch`
// or `attach` completes, marking the end of the initialization sequence.
std::atomic<bool> &GetInitialized()
{
    static std::atomic<bool> initialized{false};
    return initialized;
}

std::atomic<bool> &GetConfigurationDone()
{
    static std::atomic<bool> configurationDone{false};
    return configurationDone;
}

std::atomic<bool> &GetProcessSetupComplete()
{
    static std::atomic<bool> processSetupComplete{false};
    return processSetupComplete;
}

// Format an HRESULT as a zero-padded hexadecimal string, e.g. "0x80070057".
std::string FormatHexErrorCode(HRESULT status)
{
    constexpr auto hexDigitCount = static_cast<std::size_t>(hexErrWidth);
    std::array<char, hexDigitCount> digits{};
    const auto toCharsResult = std::to_chars(digits.data(), digits.data() + digits.size(),
                                             static_cast<uint32_t>(status), 16);
    assert(toCharsResult.ec == std::errc{}); // a 32-bit value always fits into 8 hex digits

    const auto writtenDigitCount = static_cast<std::size_t>(toCharsResult.ptr - digits.data());
    std::string result("0x");
    result.append(hexDigitCount - writtenDigitCount, '0');
    result.append(digits.data(), writtenDigitCount);
    return result;
}

} // namespace dncdbg::DAP
