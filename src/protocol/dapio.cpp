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
std::ofstream DAPIO::m_protocolLog; // NOLINT(cert-err58-cpp)
uint64_t DAPIO::m_seqCounter = 1;

void to_json(json &j, const Source &s)
{
    j = json{{"name", s.name},
             {"path", s.path}};
}

void to_json(json &j, const Breakpoint &b)
{
    j = json{{"id",       b.id},
             {"line",     b.line},
             {"verified", b.verified}};
    if (!b.message.empty())
    {
        j["message"] = b.message;
    }
    if (b.verified)
    {
        j["endLine"] = b.endLine;
        if (!b.source.IsNull())
        {
            j["source"] = b.source;
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
        j["source"] = f.source;
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
        j["namedVariables"] = s.namedVariables;
        // j["indexedVariables"] = s.indexedVariables;
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
        j["namedVariables"] = v.namedVariables;
        // j["indexedVariables"] = v.indexedVariables;
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
    capabilities["supportsConfigurationDoneRequest"] = true;
    capabilities["supportsFunctionBreakpoints"] = true;
    capabilities["supportsConditionalBreakpoints"] = true;
    capabilities["supportTerminateDebuggee"] = true;
    capabilities["supportsSetVariable"] = true;
    capabilities["supportsSetExpression"] = true;
    capabilities["supportsTerminateRequest"] = true;
    capabilities["supportsCancelRequest"] = true;
    capabilities["supportsExceptionInfoRequest"] = true;
    capabilities["supportsExceptionFilterOptions"] = true;
    json excFilters = json::array();
    for (const auto &entry : GetExceptionFilters())
    {
        const json filter{{"filter", entry.first},
                          {"label",entry.first}};
        excFilters.push_back(filter);
    }
    capabilities["exceptionBreakpointFilters"] = excFilters;
    capabilities["supportsExceptionOptions"] = false; // TODO add implementation
}

void DAPIO::SetupProtocolLogging(const std::string &path)
{
    if (path.empty())
    {
        return;
    }

    m_protocolLog.open(path);
}

void DAPIO::ResetSeqCounter()
{
    m_seqCounter = 1;
}

void DAPIO::EmitProcessEvent(PID pid, const std::string &argv0)
{
    json body;

    body["name"] = argv0;
    body["systemProcessId"] = PID::ScalarType(pid);
    body["isLocalProcess"] = true;
    body["startMethod"] = "launch";

    EmitEvent("process", body);
}

void DAPIO::EmitStoppedEvent(const StoppedEvent &event)
{
    json body;

    switch (event.reason)
    {
    case StoppedEventReason::Step:
        body["reason"] = "step";
        break;
    case StoppedEventReason::Breakpoint:
        body["reason"] = "breakpoint";
        break;
    case StoppedEventReason::Exception:
        body["reason"] = "exception";
        break;
    case StoppedEventReason::Pause:
        body["reason"] = "pause";
        break;
    case StoppedEventReason::Entry:
        body["reason"] = "entry";
        break;
    }

    // Note, `description` not in use at this moment, provide `reason` only.

    if (!event.text.empty())
    {
        body["text"] = event.text;
    }

    body["threadId"] = static_cast<int>(event.threadId);
    body["allThreadsStopped"] = event.allThreadsStopped;

    // vsdbg shows additional info, but it is not a part of the protocol
    // body["line"] = event.frame.line;
    // body["column"] = event.frame.column;
    // body["source"] = event.frame.source;

    EmitEvent("stopped", body);
}

void DAPIO::EmitExitedEvent(const ExitedEvent &event)
{
    json body;
    body["exitCode"] = event.exitCode;
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
        body["threadId"] = static_cast<int>(threadId);
    }

    body["allThreadsContinued"] = true;
    EmitEvent("continued", body);
}

void DAPIO::EmitThreadEvent(const ThreadEvent &event)
{
    json body;

    switch (event.reason)
    {
    case ThreadEventReason::Started:
        body["reason"] = "started";
        break;
    case ThreadEventReason::Exited:
        body["reason"] = "exited";
        break;
    default:
        return;
    }

    body["threadId"] = static_cast<int>(event.threadId);

    EmitEvent("thread", body);
}

void DAPIO::EmitModuleEvent(const ModuleEvent &event)
{
    json body;

    switch (event.reason)
    {
    case ModuleEventReason::New:
        body["reason"] = "new";
        break;
    case ModuleEventReason::Changed:
        body["reason"] = "changed";
        break;
    case ModuleEventReason::Removed:
        body["reason"] = "removed";
        break;
    }

    json &module = body["module"];
    module["id"] = event.module.id;
    module["name"] = event.module.name;
    module["path"] = event.module.path;

    if (event.reason != ModuleEventReason::Removed)
    {
        switch (event.module.symbolStatus)
        {
        case SymbolStatus::Skipped:
            module["symbolStatus"] = "Skipped loading symbols.";
            break;
        case SymbolStatus::Loaded:
            module["symbolStatus"] = "Symbols loaded.";
            break;
        case SymbolStatus::NotFound:
            module["symbolStatus"] = "Symbols not found.";
            break;
        }
    }

    EmitEvent("module", body);
}

void DAPIO::EmitOutputEvent(const OutputEvent &event)
{
    json body;

    switch(event.category)
    {
        case OutputCategory::Console:
            body["category"] = "console";
            break;
        case OutputCategory::StdOut:
            body["category"] = "stdout";
            break;
        case OutputCategory::StdErr:
            body["category"] = "stderr";
            break;
    }

    if (!event.source.IsNull())
    {
        body["source"] = event.source;
        body["line"] = event.line;
        body["column"] = event.column;
    }

    body["output"] = event.output;

    EmitEvent("output", body);
}

void DAPIO::EmitBreakpointEvent(const BreakpointEvent &event)
{
    json body;

    switch (event.reason)
    {
    case BreakpointEventReason::New:
        body["reason"] = "new";
        break;
    case BreakpointEventReason::Changed:
        body["reason"] = "changed";
        break;
    case BreakpointEventReason::Removed:
        body["reason"] = "removed";
        break;
    }

    body["breakpoint"] = event.breakpoint;

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

    body["capabilities"] = capabilities;

    EmitEvent("capabilities", body);
}

// Caller must care about m_outMutex.
void DAPIO::EmitMessage(nlohmann::json &message, std::string &output)
{
    message["seq"] = m_seqCounter;
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
    message["type"] = "event";
    message["event"] = name;
    message["body"] = body;
    EmitMessageWithLog(LOG_EVENT, message);
}

// Caller must care about m_outMutex.
void DAPIO::LogInternal(const std::string_view &prefix, const std::string &text)
{
    if (!m_protocolLog.is_open())
    {
        return;
    }
    
    m_protocolLog << prefix << text << std::endl; // NOLINT(performance-avoid-endl)
}

void DAPIO::Log(const std::string_view &prefix, const std::string &text)
{
    const std::scoped_lock<std::mutex> lock(m_outMutex);
    LogInternal(prefix, text);
}

} // namespace dncdbg
