// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "protocol/internal_helpers.h"
#include "config/config.h"
#include <fstream>
#include <iostream>
#include <mutex>

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
    message.emplace("seq", GetSeqCounter());
    ++GetSeqCounter();
    output = message.dump();
    std::cout << CONTENT_LENGTH << output.size() << TWO_CRLF << output;
    std::cout.flush();
}

// Caller must hold GetOutMutex().
void LogInternal(std::string_view prefix, const std::string &text)
{
    if (!GetProtocolLog().is_open())
    {
        return;
    }

    GetProtocolLog() << prefix << text << std::endl; // NOLINT(performance-avoid-endl)
}

} // namespace

const std::unordered_map<std::string, ExceptionBreakpointFilter> &GetExceptionFilters()
{
    static const std::unordered_map<std::string, ExceptionBreakpointFilter> exceptionFilters{
        {"all", ExceptionBreakpointFilter::THROW},
        {"user-unhandled", ExceptionBreakpointFilter::USER_UNHANDLED}
    };
    return exceptionFilters;
}

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
    for (const auto &entry : GetExceptionFilters())
    {
        const json filter{{"filter", entry.first},
                          {"label", entry.first}};
        excFilters.push_back(filter);
    }
    capabilities.emplace("exceptionBreakpointFilters", excFilters);
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
}

void SetupProtocolLoggingInternal(const std::string &path)
{
    if (path.empty())
    {
        return;
    }

    GetProtocolLog().open(path);
}

void EmitEvent(const std::string &name, const nlohmann::json &body)
{
    json message;
    message.emplace("type", "event");
    message.emplace("event", name);
    message.emplace("body", body);
    EmitMessageWithLog(LOG_EVENT, message);
}

void EmitMessageWithLog(std::string_view message_prefix, nlohmann::json &message)
{
    const std::scoped_lock<std::mutex> lock(GetOutMutex());
    std::string output;
    EmitMessage(message, output);
    LogInternal(message_prefix, output);
}

void Log(std::string_view prefix, const std::string &text)
{
    const std::scoped_lock<std::mutex> lock(GetOutMutex());
    LogInternal(prefix, text);
}

} // namespace dncdbg::DAP
