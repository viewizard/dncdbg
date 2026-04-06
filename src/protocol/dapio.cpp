// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "protocol/dapio.h"
#include <iostream>

// for convenience
using json = nlohmann::json;

namespace dncdbg
{

std::mutex DAPIO::m_outMutex;
uint64_t DAPIO::m_seqCounter = 1;

void to_json(json &j, const Source &s)
{
    j = json{{"name", s.name},
             {"path", s.path}};
}

void to_json(json &j, const Breakpoint &b)
{
    j = json{{"id",       b.id},
             {"verified", b.verified}};

    if (b.line != 0)
    {
        j.emplace("line", b.line);
    }

    if (!b.message.empty())
    {
        j.emplace("message", b.message);
    }
    if (b.verified)
    {
        if (b.endLine != 0)
        {
            j.emplace("endLine", b.endLine);
        }
        if (!b.source.IsNull())
        {
            j.emplace("source", b.source);
        }
    }
}

void to_json(json &j, const StackFrame &f)
{
    j = json{{"id",        static_cast<int>(f.id)},
             {"name",      f.name},
             {"line",      f.line},
             {"column",    f.column},
             {"endLine",   f.endLine},
             {"endColumn", f.endColumn},
             {"moduleId",  f.moduleId}};
    if (!f.source.IsNull())
    {
        j.emplace("source", f.source);
    }
}

void to_json(json &j, const Thread &t)
{
    j = json{{"id", static_cast<int>(t.id)},
             {"name", t.name}};
          // {"running", t.running}
}

void to_json(json &j, const Scope &s)
{
    j = json{{"name", s.name},
             {"variablesReference", s.variablesReference},
             {"expensive", false}};

    if (s.variablesReference > 0)
    {
        j.emplace("namedVariables", s.namedVariables);
        // j.emplace("indexedVariables", s.indexedVariables);
    }
}

void to_json(json &j, const Variable &v)
{
    j = json{{"name", v.name},
             {"value", v.value},
             {"type", v.type},
             {"evaluateName", v.evaluateName},
             {"variablesReference", v.variablesReference}};

    if (v.variablesReference > 0)
    {
        j.emplace("namedVariables", v.namedVariables);
        // j.emplace("indexedVariables", v.indexedVariables);
    }
}

void to_json(json &j, const Module &m)
{
    j = json{{"id", m.id},
             {"name", m.name},
             {"path", m.path},
             {"isOptimized", m.isOptimized},
             {"isUserCode", m.isUserCode}};

    if (!m.symbolFilePath.empty())
    {
        j.emplace("symbolFilePath", m.symbolFilePath);
    }

    if (!m.addressRange.empty())
    {
        j.emplace("addressRange", m.addressRange);
    }

    switch (m.symbolStatus)
    {
    case SymbolStatus::Skipped:
        j.emplace("symbolStatus", "Skipped loading symbols.");
        break;
    case SymbolStatus::Loaded:
        j.emplace("symbolStatus", "Symbols loaded.");
        break;
    case SymbolStatus::NotFound:
        j.emplace("symbolStatus", "Symbols not found.");
        break;
    }
}

const std::unordered_map<std::string, ExceptionBreakpointFilter> &DAPIO::GetExceptionFilters()
{
    static const std::unordered_map<std::string, ExceptionBreakpointFilter> exceptionFilters{
        {"all", ExceptionBreakpointFilter::THROW},
        {"user-unhandled", ExceptionBreakpointFilter::USER_UNHANDLED}
    };
    return exceptionFilters;
}

void DAPIO::AddCapabilitiesTo(json &capabilities)
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
                          {"label",entry.first}};
        excFilters.push_back(filter);
    }
    capabilities.emplace("exceptionBreakpointFilters", excFilters);
    capabilities.emplace("supportsExceptionOptions", false); // TODO add implementation
    capabilities.emplace("supportsHitConditionalBreakpoints", true);
    capabilities.emplace("supportsModulesRequest", true);
}

void DAPIO::SetupProtocolLogging(const std::string &path)
{
    if (path.empty())
    {
        return;
    }

    GetProtocolLog().open(path);
}

void DAPIO::EmitProcessEvent(DWORD processId, const std::string &name, StartMethod startMethod)
{
    json body;

    body.emplace("name", name);
    body.emplace("systemProcessId", processId);
    body.emplace("isLocalProcess", true);
    body.emplace("pointerSize", sizeof(void *) * CHAR_BIT);

    assert(startMethod != StartMethod::None);

    switch (startMethod)
    {
    case StartMethod::Attach:
        body.emplace("startMethod", "attach");
        break;
    case StartMethod::Launch:
    default:
        body.emplace("startMethod", "launch");
        break;
    }

    EmitEvent("process", body);
}

void DAPIO::EmitStoppedEvent(const StoppedEvent &event)
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

void DAPIO::EmitExitedEvent(const ExitedEvent &event)
{
    json body;
    body.emplace("exitCode", event.exitCode);
    EmitEvent("exited", body);
}

void DAPIO::EmitTerminatedEvent()
{
    EmitEvent("terminated", json::object());
}

void DAPIO::EmitContinuedEvent(ThreadId threadId)
{
    json body;

    if (threadId)
    {
        body.emplace("threadId", static_cast<int>(threadId));
    }

    body.emplace("allThreadsContinued", true);
    EmitEvent("continued", body);
}

void DAPIO::EmitThreadEvent(const ThreadEvent &event)
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
        return;
    }

    body.emplace("threadId", static_cast<int>(event.threadId));

    EmitEvent("thread", body);
}

void DAPIO::EmitModuleEvent(const ModuleEvent &event)
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

void DAPIO::EmitOutputEvent(const OutputEvent &event)
{
    json body;

    switch(event.category)
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

void DAPIO::EmitBreakpointEvent(const BreakpointEvent &event)
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

void DAPIO::EmitInitializedEvent()
{
    EmitEvent("initialized", json::object());
}

void DAPIO::EmitCapabilitiesEvent()
{
    json body = json::object();
    json capabilities = json::object();

    AddCapabilitiesTo(capabilities);

    body.emplace("capabilities", capabilities);

    EmitEvent("capabilities", body);
}

// Caller must care about m_outMutex.
void DAPIO::EmitMessage(nlohmann::json &message, std::string &output)
{
    message.emplace("seq", m_seqCounter);
    ++m_seqCounter;
    output = message.dump();
    std::cout << CONTENT_LENGTH << output.size() << TWO_CRLF << output;
    std::cout.flush();
}

void DAPIO::EmitMessageWithLog(const std::string_view &message_prefix, nlohmann::json &message)
{
    const std::scoped_lock<std::mutex> lock(m_outMutex);
    std::string output;
    EmitMessage(message, output);
    LogInternal(message_prefix, output);
}

void DAPIO::EmitEvent(const std::string &name, const nlohmann::json &body)
{
    json message;
    message.emplace("type", "event");
    message.emplace("event", name);
    message.emplace("body", body);
    EmitMessageWithLog(LOG_EVENT, message);
}

// Caller must care about m_outMutex.
void DAPIO::LogInternal(const std::string_view &prefix, const std::string &text)
{
    if (!GetProtocolLog().is_open())
    {
        return;
    }

    GetProtocolLog() << prefix << text << std::endl; // NOLINT(performance-avoid-endl)
}

void DAPIO::Log(const std::string_view &prefix, const std::string &text)
{
    const std::scoped_lock<std::mutex> lock(m_outMutex);
    LogInternal(prefix, text);
}

} // namespace dncdbg
