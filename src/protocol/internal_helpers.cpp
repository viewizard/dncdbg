// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "protocol/internal_helpers.h"
#include "config/config.h"
#include "utils/remote_server.h"
#include <fstream>
#include <iostream>
#include <mutex>
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

// Caller must hold GetOutMutex().
void EmitMessage(nlohmann::json &message, std::string &output)
{
    // In server mode, the protocol is written to the loopback TCP connection (RemoteServer) instead of stdout.
    std::ostream &stream = Config::IsServerMode() ? RemoteServer::Instance().GetOutputStream() : std::cout;

    message.emplace("seq", GetSeqCounter());
    ++GetSeqCounter();
    output = message.dump();
    stream << CONTENT_LENGTH << output.size() << TWO_CRLF << output;
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
    std::string output;
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

} // namespace dncdbg::DAP
