// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "protocol/dapio.h"
#include "protocol/internal_helpers.h"
#include <fstream>
#include <iostream>
#include <mutex>

// for convenience
using nlohmann::json;

namespace dncdbg::DAPIO
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

void EmitEvent(const std::string &name, const nlohmann::json &body)
{
    json message;
    message.emplace("type", "event");
    message.emplace("event", name);
    message.emplace("body", body);
    EmitMessageWithLog(LOG_EVENT, message);
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

void SetupProtocolLogging(const std::string &path)
{
    if (path.empty())
    {
        return;
    }

    GetProtocolLog().open(path);
}

void EmitProcessEvent(DWORD processId, const std::string &name, StartMethod startMethod)
{
    json body;

    body.emplace("name", name);
    body.emplace("systemProcessId", processId);
    body.emplace("isLocalProcess", true);
    body.emplace("pointerSize", sizeof(void *) * CHAR_BIT);

    switch (startMethod)
    {
    case StartMethod::Attach:
        body.emplace("startMethod", "attach");
        break;
    case StartMethod::Launch:
        body.emplace("startMethod", "launch");
        break;
    default:
        assert(false);
        break;
    }

    EmitEvent("process", body);
}

void EmitStoppedEvent(const StoppedEvent &event)
{
    json body;

    switch (event.reason)
    {
    case StoppedEventReason::Step:
        body.emplace("reason", "step");
        break;
    case StoppedEventReason::Breakpoint:
        body.emplace("reason", "breakpoint");
        break;
    case StoppedEventReason::Exception:
        body.emplace("reason", "exception");
        break;
    case StoppedEventReason::Pause:
        body.emplace("reason", "pause");
        break;
    case StoppedEventReason::Entry:
        body.emplace("reason", "entry");
        break;
    case StoppedEventReason::Goto:
        body.emplace("reason", "goto");
        break;
    }

    // Note, `description` not in use at this moment, provide `reason` only.

    if (!event.text.empty())
    {
        body.emplace("text", event.text);
    }

    body.emplace("threadId", static_cast<int>(event.threadId));
    body.emplace("allThreadsStopped", event.allThreadsStopped);

    if (!event.hitBreakpointIds.empty())
    {
        body.emplace("hitBreakpointIds", event.hitBreakpointIds);
    }

    // vsdbg shows additional info, but it is not a part of the protocol
    // body.emplace("line", event.frame.line);
    // body.emplace("column", event.frame.column);
    // body.emplace("source", event.frame.source);

    EmitEvent("stopped", body);
}

void EmitExitedEvent(const ExitedEvent &event)
{
    json body;
    body.emplace("exitCode", event.exitCode);
    EmitEvent("exited", body);
}

void EmitTerminatedEvent()
{
    EmitEvent("terminated", json::object());
}

void EmitContinuedEvent(ThreadId threadId, bool singleThread)
{
    json body;

    if (threadId)
    {
        body.emplace("threadId", static_cast<int>(threadId));
    }

    body.emplace("allThreadsContinued", !singleThread);
    EmitEvent("continued", body);
}

void EmitThreadEvent(const ThreadEvent &event)
{
    json body;

    switch (event.reason)
    {
    case ThreadEventReason::Started:
        body.emplace("reason", "started");
        break;
    case ThreadEventReason::Exited:
        body.emplace("reason", "exited");
        break;
    default:
        assert(false);
        return;
    }

    body.emplace("threadId", static_cast<int>(event.threadId));

    EmitEvent("thread", body);
}

void EmitModuleEvent(const ModuleEvent &event)
{
    json body;

    switch (event.reason)
    {
    case ModuleEventReason::New:
        body.emplace("reason", "new");
        break;
    case ModuleEventReason::Changed:
        body.emplace("reason", "changed");
        break;
    case ModuleEventReason::Removed:
        body.emplace("reason", "removed");
        break;
    }

    body.emplace("module", event.module);

    EmitEvent("module", body);
}

void EmitLoadedSourceEvent(const LoadedSourceEvent &event)
{
    json body;

    switch (event.reason)
    {
    case LoadedSourceEventReason::New:
        body.emplace("reason", "new");
        break;
    case LoadedSourceEventReason::Changed:
        body.emplace("reason", "changed");
        break;
    case LoadedSourceEventReason::Removed:
        body.emplace("reason", "removed");
        break;
    }

    body.emplace("source", event.source);

    EmitEvent("loadedSource", body);
}

void EmitOutputEvent(const OutputEvent &event)
{
    json body;

    switch (event.category)
    {
        case OutputCategory::Console:
            body.emplace("category", "console");
            break;
        case OutputCategory::StdOut:
            body.emplace("category", "stdout");
            break;
        case OutputCategory::StdErr:
            body.emplace("category", "stderr");
            break;
    }

    if (!event.source.IsNull())
    {
        body.emplace("source", event.source);
        body.emplace("line", event.line);
        body.emplace("column", event.column);
    }

    body.emplace("output", event.output);

    EmitEvent("output", body);
}

void EmitBreakpointEvent(const BreakpointEvent &event)
{
    json body;

    switch (event.reason)
    {
    case BreakpointEventReason::New:
        body.emplace("reason", "new");
        break;
    case BreakpointEventReason::Changed:
        body.emplace("reason", "changed");
        break;
    case BreakpointEventReason::Removed:
        body.emplace("reason", "removed");
        break;
    }

    body.emplace("breakpoint", event.breakpoint);

    EmitEvent("breakpoint", body);
}

void EmitInitializedEvent()
{
    EmitEvent("initialized", json::object());
}

void EmitCapabilitiesEvent()
{
    json body = json::object();
    json capabilities = json::object();

    AddCapabilitiesTo(capabilities);

    body.emplace("capabilities", capabilities);

    EmitEvent("capabilities", body);
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

} // namespace dncdbg::DAPIO
