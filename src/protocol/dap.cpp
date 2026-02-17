// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "managed/interop.h"
#include "protocol/dap.h"
#include "debugger/manageddebugger.h"
#include "utils/logger.h"
#include "utils/torelease.h"
#include <algorithm>
#include <exception>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// for convenience
using json = nlohmann::json;

namespace dncdbg
{

namespace
{

constexpr std::string_view TWO_CRLF("\r\n\r\n");
constexpr std::string_view CONTENT_LENGTH("Content-Length: ");

constexpr std::string_view LOG_COMMAND("-> (C) ");
constexpr std::string_view LOG_RESPONSE("<- (R) ");
constexpr std::string_view LOG_EVENT("<- (E) ");

const std::unordered_map<std::string, ExceptionBreakpointFilter> &GetExceptionFilters()
{
    static const std::unordered_map<std::string, ExceptionBreakpointFilter> exceptionFilters{
        {"all", ExceptionBreakpointFilter::THROW},
        {"user-unhandled", ExceptionBreakpointFilter::USER_UNHANDLED}
    };
    return exceptionFilters;
}

// Make sure we continue add new commands into queue only after current command execution is finished.
// Note, configurationDone: prevent deadlock in _dup() call during std::getline() from stdin in main thread.
const std::unordered_set<std::string> &GetSyncCommandExecutionSet()
{
    static const std::unordered_set<std::string> syncCommandExecutionSet{
        "configurationDone",
        "disconnect",
        "terminate"
    };
    return syncCommandExecutionSet;
}

// Commands, that trigger command queue canceling routine.
const std::unordered_set<std::string> &GetCancelCommandQueueSet()
{
    static const std::unordered_set<std::string> cancelCommandQueueSet{
        "disconnect",
        "terminate",
        "continue",
        "next",
        "stepIn",
        "stepOut"
    };
    return cancelCommandQueueSet;
}

// Don't cancel commands related to debugger configuration. For example, breakpoint setup could be done in any time
// (even if process don't attached at all).
const std::unordered_set<std::string> &GetDebuggerSetupCommandSet()
{
    static const std::unordered_set<std::string> debuggerSetupCommandSet{
        "initialize",
        "setExceptionBreakpoints",
        "setFunctionBreakpoints",
        "setBreakpoints",
        "configurationDone",
        "launch",
        "disconnect",
        "terminate",
        "attach"
    };
    return debuggerSetupCommandSet;
}

void AddCapabilitiesTo(json &capabilities)
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

std::string ReadData(std::istream &cin)
{
    // parse header (only content len) until empty line
    long content_len = -1;
    while (true)
    {
        std::string line;
        std::getline(cin, line);
        if (!cin.good())
        {
            if (cin.eof())
            {
                LOGI("EOF");
            }
            else
            {
                LOGE("input stream reading error");
            }
            return {};
        }

        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line.empty())
        {
            if (content_len < 0)
            {
                LOGE("protocol error: no 'Content Length:' field!");
                return {};
            }
            break; // header and content delimiter
        }

        LOGD("header: '%s'", line.c_str());

        if (line.size() > CONTENT_LENGTH.size() &&
            std::equal(CONTENT_LENGTH.begin(), CONTENT_LENGTH.end(), line.begin()))
        {
            if (content_len >= 0)
            {
                LOGW("protocol violation: duplicate '%s'", line.c_str());
            }

            char *p = nullptr; // NOLINT(misc-const-correctness)
            errno = 0;
            static constexpr int base = 10;
            content_len = static_cast<long>(strtoul(&line[CONTENT_LENGTH.size()], &p, base));
            if (errno == ERANGE || (*p != 0 && (isspace(*p) == 0)))
            {
                LOGE("protocol violation: '%s'", line.c_str());
                return {};
            }
        }
    }

    std::string result(content_len, 0);
    if (!cin.read(result.data(), content_len))
    {
        if (cin.eof())
        {
            LOGE("Unexpected EOF!");
        }
        else
        {
            LOGE("input stream reading error");
        }
        return {};
    }

    return result;
}

json FormJsonForExceptionDetails(const ExceptionDetails &details)
{
    json result{{"typeName", details.typeName},
                {"fullTypeName", details.fullTypeName},
                {"evaluateName", details.evaluateName},
                {"stackTrace", details.stackTrace},
                {"formattedDescription", details.formattedDescription},
                {"source", details.source}};

    if (!details.message.empty())
    {
        result["message"] = details.message;
    }

    if (details.innerException)
    {
        // Note, DAP have "innerException" field as array, but in real we don't have array with inner
        // exceptions here, since exception object have only one exeption object reference in InnerException field.
        json arr = json::array();
        arr.push_back(FormJsonForExceptionDetails(*details.innerException));
        result["innerException"] = arr;
    }

    return result;
}

} // unnamed namespace

static void to_json(json &j, const Source &s) // NOLINT(misc-use-anonymous-namespace)
{
    j = json{{"name", s.name},
             {"path", s.path}};
}

static void to_json(json &j, const Breakpoint &b) // NOLINT(misc-use-anonymous-namespace)
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

static void to_json(json &j, const StackFrame &f) // NOLINT(misc-use-anonymous-namespace)
{
    j = json{{"id",        static_cast<int>(f.id)},
             {"name",      f.methodName},
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

static void to_json(json &j, const Thread &t) // NOLINT(misc-use-anonymous-namespace)
{
    j = json{{"id", static_cast<int>(t.id)},
             {"name", t.name}};
          // {"running", t.running}
}

static void to_json(json &j, const Scope &s) // NOLINT(misc-use-anonymous-namespace)
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

static void to_json(json &j, const Variable &v) // NOLINT(misc-use-anonymous-namespace)
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

void DAP::EmitContinuedEvent(ThreadId threadId)
{
    LogFuncEntry();

    json body;

    if (threadId)
    {
        body["threadId"] = static_cast<int>(threadId);
    }

    body["allThreadsContinued"] = true;
    EmitEvent("continued", body);
}

void DAP::EmitStoppedEvent(const StoppedEvent &event)
{
    LogFuncEntry();

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

void DAP::EmitExitedEvent(const ExitedEvent &event)
{
    LogFuncEntry();
    json body;
    body["exitCode"] = event.exitCode;
    EmitEvent("exited", body);
}

void DAP::EmitTerminatedEvent()
{
    LogFuncEntry();
    EmitEvent("terminated", json::object());
}

void DAP::EmitThreadEvent(const ThreadEvent &event)
{
    LogFuncEntry();
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

void DAP::EmitModuleEvent(const ModuleEvent &event)
{
    LogFuncEntry();
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

void DAP::EmitOutputEvent(const OutputEvent &event)
{
    LogFuncEntry();
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

void DAP::EmitBreakpointEvent(const BreakpointEvent &event)
{
    LogFuncEntry();
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

void DAP::EmitInitializedEvent()
{
    LogFuncEntry();
    EmitEvent("initialized", json::object());
}

void DAP::EmitProcessEvent(PID pid, const std::string &argv0)
{
    json body;

    body["name"] = argv0;
    body["systemProcessId"] = PID::ScalarType(pid);
    body["isLocalProcess"] = true;
    body["startMethod"] = "launch";

    EmitEvent("process", body);
}

void DAP::EmitCapabilitiesEvent()
{
    LogFuncEntry();

    json body = json::object();
    json capabilities = json::object();

    AddCapabilitiesTo(capabilities);

    body["capabilities"] = capabilities;

    EmitEvent("capabilities", body);
}

// Caller must care about m_outMutex.
void DAP::EmitMessage(nlohmann::json &message, std::string &output)
{
    message["seq"] = m_seqCounter;
    ++m_seqCounter;
    output = message.dump();
    cout << CONTENT_LENGTH << output.size() << TWO_CRLF << output;
    cout.flush();
}

void DAP::EmitMessageWithLog(const std::string_view &message_prefix, nlohmann::json &message)
{
    const std::scoped_lock<std::mutex> lock(m_outMutex);
    std::string output;
    EmitMessage(message, output);
    Log(message_prefix, output);
}

void DAP::EmitEvent(const std::string &name, const nlohmann::json &body)
{
    json message;
    message["type"] = "event";
    message["event"] = name;
    message["body"] = body;
    EmitMessageWithLog(LOG_EVENT, message);
}

HRESULT DAP::HandleCommand(const std::string &command, const nlohmann::json &arguments, nlohmann::json &body)
{
    using CommandCallback = std::function<HRESULT(const json &arguments, json &body)>;
    static std::unordered_map<std::string, CommandCallback> commands{
        {"initialize", [&](const json &/*arguments*/, json &body)
            {
                m_sharedDebugger->Initialize();

                AddCapabilitiesTo(body);

                return S_OK;
            }},
        {"setExceptionBreakpoints", [&](const json &arguments, json &/*body*/)
            {
                const std::vector<std::string> filters = arguments.value("filters", std::vector<std::string>());
                std::vector<std::map<std::string, std::string>> filterOptions =
                    arguments.value("filterOptions", std::vector<std::map<std::string, std::string>>());

                // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_SetExceptionBreakpoints
                // The 'filter' and 'filterOptions' sets are additive.
                // Response to ‘setExceptionBreakpoints’ request:
                // ... The Breakpoint objects are in the same order as the elements of the ‘filters’, ‘filterOptions’,
                // ‘exceptionOptions’ arrays given as arguments.
                std::vector<ExceptionBreakpoint> exceptionBreakpoints;

                for (const auto &entry : filters)
                {
                    auto findFilter = GetExceptionFilters().find(entry);
                    if (findFilter == GetExceptionFilters().end())
                    {
                        return E_INVALIDARG;
                    }
                    // in case of DAP, we can't setup categoryHint during breakpoint setup, since this protocol
                    // don't provide such information
                    exceptionBreakpoints.emplace_back(ExceptionCategory::ANY, findFilter->second);
                }

                for (auto &entry : filterOptions)
                {
                    auto findId = entry.find("filterId");
                    if (findId == entry.end() || findId->second.empty())
                    {
                        return E_INVALIDARG;
                    }

                    auto findFilter = GetExceptionFilters().find(findId->second);
                    if (findFilter == GetExceptionFilters().end())
                    {
                        return E_INVALIDARG;
                    }
                    // in case of DAP, we can't setup categoryHint during breakpoint setup, since this protocol
                    // don't provide such information
                    exceptionBreakpoints.emplace_back(ExceptionCategory::ANY, findFilter->second);

                    auto findCondition = entry.find("condition");
                    if (findCondition == entry.end() || findCondition->second.empty())
                    {
                        continue;
                    }

                    if (findCondition->second[0] == '!')
                    {
                        if (findCondition->second.size() == 1)
                        {
                            continue;
                        }

                        findCondition->second[0] = ' ';
                        exceptionBreakpoints.back().negativeCondition = true;
                    }

                    std::replace(findCondition->second.begin(), findCondition->second.end(), ',', ' ');
                    std::stringstream ss(findCondition->second);
                    const std::istream_iterator<std::string> begin(ss);
                    const std::istream_iterator<std::string> end;
                    exceptionBreakpoints.back().condition = std::unordered_set<std::string>(begin, end);
                }

                HRESULT Status = S_OK;
                std::vector<Breakpoint> breakpoints;
                IfFailRet(m_sharedDebugger->SetExceptionBreakpoints(exceptionBreakpoints, breakpoints));

                // TODO form body with breakpoints (optional output, MS vsdbg don't provide it for VSCode IDE now)
                // body["breakpoints"] = breakpoints;

                return S_OK;
            }},
        {"configurationDone", [&](const json &/*arguments*/, json &/*body*/)
            {
                return m_sharedDebugger->ConfigurationDone(); 
            }},
        {"exceptionInfo", [&](const json &arguments, json &body)
            {
                HRESULT Status = S_OK;
                const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};
                ExceptionInfo exceptionInfo;
                IfFailRet(m_sharedDebugger->GetExceptionInfo(threadId, exceptionInfo));

                body["exceptionId"] = exceptionInfo.exceptionId;
                body["description"] = exceptionInfo.description;
                body["breakMode"] = exceptionInfo.breakMode;
                body["details"] = FormJsonForExceptionDetails(exceptionInfo.details);
                return S_OK;
            }},
        {"setBreakpoints", [&](const json &arguments, json &body)
            {
                HRESULT Status = S_OK;

                std::vector<LineBreakpoint> lineBreakpoints;
                for (const auto &b : arguments.at("breakpoints"))
                {
                    lineBreakpoints.emplace_back(b.at("line"), b.value("condition", std::string()));
                }

                std::vector<Breakpoint> breakpoints;
                IfFailRet(m_sharedDebugger->SetLineBreakpoints(arguments.at("source").at("path"), lineBreakpoints, breakpoints));

                body["breakpoints"] = breakpoints;

                return S_OK;
            }},
        {"launch", [&](const json &arguments, json &/*body*/)
            {
                auto cwdIt = arguments.find("cwd");
                const std::string cwd(cwdIt != arguments.end() ? cwdIt.value().get<std::string>() : std::string{});
                std::map<std::string, std::string> env;
                try
                {
                    env = arguments.at("env").get<std::map<std::string, std::string>>();
                }
                catch (std::exception &ex)
                {
                    LOGI("exception '%s'", ex.what());
                    // If we catch inconsistent state on the interrupted reading
                    env.clear();
                }

                m_sharedDebugger->SetJustMyCode(
                    arguments.value("justMyCode", true)); // MS vsdbg have "justMyCode" enabled by default.
                m_sharedDebugger->SetStepFiltering(arguments.value(
                    "enableStepFiltering", true)); // MS vsdbg have "enableStepFiltering" enabled by default.

                // https://github.com/OmniSharp/omnisharp-vscode/issues/3173
                uint32_t evalFlags = defaultEvalFlags;
                if (arguments.contains("expressionEvaluationOptions"))
                {
                    evalFlags |= arguments.at("expressionEvaluationOptions").value("allowImplicitFuncEval", true) ? 0 : EVAL_NOFUNCEVAL;
                }
                m_sharedDebugger->SetEvalFlags(evalFlags);

                if (!m_fileExec.empty())
                {
                    return m_sharedDebugger->Launch(m_fileExec, m_execArgs, env, cwd, arguments.value("stopAtEntry", false));
                }

                const std::string program = arguments.at("program").get<std::string>();
                std::vector<std::string> args = arguments.value("args", std::vector<std::string>());

                const std::string dllSuffix = ".dll";
                if (program.size() >= dllSuffix.size() &&
                    program.compare(program.size() - dllSuffix.size(), dllSuffix.size(), dllSuffix) == 0)
                {
                    args.insert(args.begin(), program);
                    return m_sharedDebugger->Launch("dotnet", args, env, cwd, arguments.value("stopAtEntry", false));
                }
                else
                {
                    // If we're not being asked to launch a dll, assume whatever we're given is an executable
                    return m_sharedDebugger->Launch(program, args, env, cwd, arguments.value("stopAtEntry", false));
                }
            }},
        {"threads", [&](const json &/*arguments*/, json &body)
            {
                HRESULT Status = S_OK;
                std::vector<Thread> threads;
                IfFailRet(m_sharedDebugger->GetThreads(threads));

                body["threads"] = threads;

                return S_OK;
            }},
        {"disconnect", [&](const json &arguments, json &/*body*/)
            {
                auto terminateArgIter = arguments.find("terminateDebuggee");
                DisconnectAction action = DisconnectAction::Default;
                if (terminateArgIter == arguments.end())
                {
                    action = DisconnectAction::Default;
                }
                else
                {
                    action = terminateArgIter.value().get<bool>() ? DisconnectAction::Terminate
                                                                  : DisconnectAction::Detach;
                }

                m_sharedDebugger->Disconnect(action);

                return S_OK;
            }},
        {"terminate", [&](const json &/*arguments*/, json &/*body*/)
            {
                m_sharedDebugger->Disconnect(DisconnectAction::Terminate);
                return S_OK;
            }},
        {"stackTrace", [&](const json &arguments, json &body)
            {
                HRESULT Status = S_OK;

                int totalFrames = 0;
                const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};

                std::vector<StackFrame> stackFrames;
                IfFailRet(m_sharedDebugger->GetStackTrace(threadId, FrameLevel{arguments.value("startFrame", 0)},
                                                        static_cast<unsigned>(arguments.value("levels", 0)), stackFrames, totalFrames));

                body["stackFrames"] = stackFrames;
                body["totalFrames"] = totalFrames;

                return S_OK;
            }},
        {"continue", [&](const json &arguments, json &body)
            {
                body["allThreadsContinued"] = true;

                const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};
                body["threadId"] = static_cast<int>(threadId);
                return m_sharedDebugger->Continue(threadId);
            }},
        {"pause", [&](const json &arguments, json &body)
            {
                const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};
                body["threadId"] = static_cast<int>(threadId);
                return m_sharedDebugger->Pause(threadId);
            }},
        {"next", [&](const json &arguments, json &/*body*/)
            {
                return m_sharedDebugger->StepCommand(ThreadId{static_cast<int>(arguments.at("threadId"))},
                                                   StepType::STEP_OVER);
            }},
        {"stepIn", [&](const json &arguments, json &/*body*/)
            {
                return m_sharedDebugger->StepCommand(ThreadId{static_cast<int>(arguments.at("threadId"))},
                                                   StepType::STEP_IN);
            }},
        {"stepOut", [&](const json &arguments, json &/*body*/)
            {
                return m_sharedDebugger->StepCommand(ThreadId{static_cast<int>(arguments.at("threadId"))},
                                                   StepType::STEP_OUT);
            }},
        {"scopes", [&](const json &arguments, json &body)
            {
                HRESULT Status = S_OK;
                std::vector<Scope> scopes;
                const FrameId frameId{static_cast<int>(arguments.at("frameId"))};
                IfFailRet(m_sharedDebugger->GetScopes(frameId, scopes));

                body["scopes"] = scopes;

                return S_OK;
            }},
        {"variables", [&](const json &arguments, json &body)
            {
                HRESULT Status = S_OK;
                const std::string filterName = arguments.value("filter", "");
                VariablesFilter filter = VariablesFilter::Both;
                if (filterName == "named")
                {
                    filter = VariablesFilter::Named;
                }
                else if (filterName == "indexed")
                {
                    filter = VariablesFilter::Indexed;
                }

                std::vector<Variable> variables;
                IfFailRet(m_sharedDebugger->GetVariables(arguments.at("variablesReference"), filter,
                                                       arguments.value("start", 0), arguments.value("count", 0),
                                                       variables));

                body["variables"] = variables;

                return S_OK;
            }},
        {"evaluate", [&](const json &arguments, json &body)
            {
                const std::string expression = arguments.at("expression");
                const FrameId frameId([&]()
                    {
                        auto frameIdIter = arguments.find("frameId");
                        if (frameIdIter == arguments.end())
                        {
                            const ThreadId threadId = m_sharedDebugger->GetLastStoppedThreadId();
                            return FrameId{threadId, FrameLevel{0}};
                        }
                        else
                        {
                            return FrameId{static_cast<int>(frameIdIter.value())};
                        }
                    }());

                HRESULT Status = S_OK;
                Variable variable;
                std::string output;
                if (FAILED(Status = m_sharedDebugger->Evaluate(frameId, expression, variable, output)))
                {
                    if (output.empty())
                    {
                        std::stringstream stream;
                        stream << "error: 0x" << std::hex << Status;
                        body["message"] = stream.str();
                    }
                    else
                    {
                        body["message"] = output;
                    }

                    return Status;
                }

                body["result"] = variable.value;
                body["type"] = variable.type;
                body["variablesReference"] = variable.variablesReference;
                if (variable.variablesReference > 0)
                {
                    body["namedVariables"] = variable.namedVariables;
                    // indexedVariables
                }
                return S_OK;
            }},
        {"setExpression", [&](const json &arguments, json &body)
            {
                const std::string expression = arguments.at("expression");
                const std::string value = arguments.at("value");
                const FrameId frameId([&]()
                    {
                        auto frameIdIter = arguments.find("frameId");
                        if (frameIdIter == arguments.end())
                        {
                            const ThreadId threadId = m_sharedDebugger->GetLastStoppedThreadId();
                            return FrameId{threadId, FrameLevel{0}};
                        }
                        else
                        {
                            return FrameId{static_cast<int>(frameIdIter.value())};
                        }
                    }());

                HRESULT Status = S_OK;
                std::string output;
                if (FAILED(Status = m_sharedDebugger->SetExpression(frameId, expression, value, output)))
                {
                    if (output.empty())
                    {
                        std::stringstream stream;
                        stream << "error: 0x" << std::hex << Status;
                        body["message"] = stream.str();
                    }
                    else
                    {
                        body["message"] = output;
                    }

                    return Status;
                }

                body["value"] = output;
                return S_OK;
            }},
        {"attach", [&](const json &arguments, json &/*body*/)
            {
                int processId = 0;

                const json &processIdArg = arguments.at("processId");
                if (processIdArg.is_string())
                {
                    processId = std::stoi(processIdArg.get<std::string>());
                }
                else if (processIdArg.is_number())
                {
                    processId = processIdArg;
                }
                else
                {
                    return E_INVALIDARG;
                }

                return m_sharedDebugger->Attach(processId);
            }},
        {"setVariable", [&](const json &arguments, json &body)
            {
                const std::string name = arguments.at("name");
                const std::string value = arguments.at("value");
                const int ref = arguments.at("variablesReference");

                HRESULT Status = S_OK;
                std::string output;
                if (FAILED(Status = m_sharedDebugger->SetVariable(name, value, ref, output)))
                {
                    body["message"] = output;
                    return Status;
                }

                body["value"] = output;

                return S_OK;
            }},
        {"setFunctionBreakpoints", [&](const json &arguments, json &body)
            {
                HRESULT Status = S_OK;

                std::vector<FuncBreakpoint> funcBreakpoints;
                for (const auto &b : arguments.at("breakpoints"))
                {
                    std::string params;
                    std::string name = b.at("name");

                    const std::size_t openBrace = name.find('(');
                    if (openBrace != std::string::npos)
                    {
                        const std::size_t closeBrace = name.find(')');

                        params = std::string(name, openBrace, closeBrace - openBrace + 1);
                        name.erase(openBrace, closeBrace);
                    }

                    funcBreakpoints.emplace_back(name, params, b.value("condition", std::string()));
                }

                std::vector<Breakpoint> breakpoints;
                IfFailRet(m_sharedDebugger->SetFuncBreakpoints(funcBreakpoints, breakpoints));

                body["breakpoints"] = breakpoints;

                return Status;
            }}};

    if (m_sharedDebugger == nullptr)
    {
        return CORDBG_E_DEBUGGING_DISABLED;
    }

    auto command_it = commands.find(command);
    if (command_it == commands.end())
    {
        return E_NOTIMPL;
    }

    return command_it->second(arguments, body);
}

HRESULT DAP::HandleCommandJSON(const std::string &command, const nlohmann::json &arguments, nlohmann::json &body)
{
    try
    {
        return HandleCommand(command, arguments, body);
    }
    catch (nlohmann::detail::exception &ex)
    {
        LOGE("JSON error: %s", ex.what());
        body["message"] = std::string("can't parse: ") + ex.what();
    }

    return E_FAIL;
}

void DAP::CommandsWorker()
{
    std::unique_lock<std::mutex> lockCommandsMutex(m_commandsMutex);

    while (true)
    {
        while (m_commandsQueue.empty())
        {
            // Note, during m_commandsCV.wait() (waiting for notify_one call with entry added into queue),
            // m_commandsMutex will be unlocked (see std::condition_variable for more info).
            m_commandsCV.wait(lockCommandsMutex);
        }

        CommandQueueEntry c = std::move(m_commandsQueue.front());
        m_commandsQueue.pop_front();
        lockCommandsMutex.unlock();

        // Check for ncdbg internal commands.
        if (c.command == "ncdbg_disconnect")
        {
            if (m_sharedDebugger != nullptr)
            {
                m_sharedDebugger->Disconnect();
            }
            break;
        }

        json body = json::object();
        std::future<HRESULT> future = std::async(std::launch::async, [&]()
            {
                return HandleCommandJSON(c.command, c.arguments, body);
            });
        HRESULT Status = S_OK;
        // Note, CommandsWorker() loop should never hangs, but even in case some command execution is timed out,
        // this could be not critical issue. Let IDE decide.

        // MSVS debugger use config file, for Visual Studio 2022 Community Edition located at
        // C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\Profiles\CSharp.vssettings
        // Visual Studio have timeout setup for each type of requests, for example:
        // LocalsTimeout = 1000
        // LongEvalTimeout = 10000
        // NormalEvalTimeout = 5000
        // QuickwatchTimeout = 15000
        // SetValueTimeout = 10000
        // ...
        // we use max default timeout (15000), one timeout for all requests.

        // TODO add timeout configuration feature
        const std::future_status timeoutStatus = future.wait_for(std::chrono::milliseconds(15000));
        if (timeoutStatus == std::future_status::timeout)
        {
            body["message"] = "Command execution timed out.";
            Status = COR_E_TIMEOUT;
        }
        else
        {
            Status = future.get();
        }

        if (SUCCEEDED(Status))
        {
            c.response["success"] = true;
            c.response["body"] = body;
        }
        else
        {
            if (body.find("message") == body.end())
            {
                static constexpr uint32_t hexNumberWidth = 8;
                std::ostringstream ss;
                ss << "Failed command '" << c.command << "' : "
                   << "0x" << std::setw(hexNumberWidth) << std::setfill('0') << std::hex << Status;
                c.response["message"] = ss.str();
            }
            else
            {
                c.response["message"] = body["message"];
            }

            c.response["success"] = false;
        }

        EmitMessageWithLog(LOG_RESPONSE, c.response);

        // Post command action.
        if (GetSyncCommandExecutionSet().find(c.command) != GetSyncCommandExecutionSet().end())
        {
            m_commandSyncCV.notify_one();
        }

        if (c.command == "disconnect")
        {
            break;
        }
        // The Debug Adapter Protocol specifies that `InitializedEvent` occurs after the `InitializeRequest` has returned:
        // https://microsoft.github.io/debug-adapter-protocol/specification#arrow_left-initialized-event
        else if (c.command == "initialize" && SUCCEEDED(Status))
        {
            EmitInitializedEvent();
        }

        lockCommandsMutex.lock();
    }

    m_exit = true;
}

// Caller must care about m_commandsMutex.
std::list<DAP::CommandQueueEntry>::iterator DAP::CancelCommand(const std::list<DAP::CommandQueueEntry>::iterator &iter)
{
    iter->response["success"] = false;
    iter->response["message"] =
        std::string("Error processing '") + iter->command + std::string("' request. The operation was canceled.");
    EmitMessageWithLog(LOG_RESPONSE, iter->response);
    return m_commandsQueue.erase(iter);
}

void DAP::CommandLoop()
{
#ifdef DEBUG
    // nlohmann/json have internal dump serializer and care about escaped characters, test it
    nlohmann::json body;
    body["test"] = std::string("te\023st\nte\023st\nte\023st\nte\023st\nte\023st234\n");
    const std::string expected(R"({"test":"te\u0013st\nte\u0013st\nte\u0013st\nte\u0013st\nte\u0013st234\n"})");
    assert(body.dump() == expected);
#endif // DEBUG

    CreateManagedDebugger();
    std::thread commandsWorker{&DAP::CommandsWorker, this};

    m_exit = false;

    while (!m_exit)
    {
        const std::string requestText = ReadData(cin);
        if (requestText.empty())
        {
            CommandQueueEntry queueEntry;
            queueEntry.command = "ncdbg_disconnect";
            const std::scoped_lock<std::mutex> guardCommandsMutex(m_commandsMutex);
            m_commandsQueue.clear();
            m_commandsQueue.emplace_back(std::move(queueEntry));
            m_commandsCV.notify_one(); // notify_one with lock
            break;
        }

        {
            const std::scoped_lock<std::mutex> lock(m_outMutex);
            Log(LOG_COMMAND, requestText);
        }

        struct bad_format : public std::invalid_argument
        {
            bad_format(const char *s) : invalid_argument(s)
            {
            }
        };

        CommandQueueEntry queueEntry;
        try
        {
            json request = json::parse(requestText);

            // Variable `resp' is used to construct response and assign it to `response'
            // variable in single step: `response' variable should always be in
            // consistent state (it must not have state when some fields is assigned and
            // some not assigned due to an exception) because `response' is used below
            // in exception handler.
            json resp;
            resp["type"] = "response";
            resp["request_seq"] = request.at("seq");
            queueEntry.response = resp;

            queueEntry.command = request.at("command");
            resp["command"] = queueEntry.command;
            queueEntry.response = resp;

            if (request["type"] != "request")
            {
                throw bad_format("wrong request type!");
            }

            auto argIter = request.find("arguments");
            queueEntry.arguments = (argIter == request.end() ? json::object() : argIter.value());

            // Pre command action.
            if (queueEntry.command == "initialize")
            {
                EmitCapabilitiesEvent();
            }
            else if (GetCancelCommandQueueSet().find(queueEntry.command) != GetCancelCommandQueueSet().end())
            {
                const std::scoped_lock<std::mutex> guardCommandsMutex(m_commandsMutex);
                if (m_sharedDebugger != nullptr)
                {
                    m_sharedDebugger->CancelEvalRunning();
                }

                for (auto iter = m_commandsQueue.begin(); iter != m_commandsQueue.end();)
                {
                    if (GetDebuggerSetupCommandSet().find(iter->command) != GetDebuggerSetupCommandSet().end())
                    {
                        ++iter;
                    }
                    else
                    {
                        iter = CancelCommand(iter);
                    }
                }
            }
            // Note, in case "cancel" this is command implementation itself.
            else if (queueEntry.command == "cancel")
            {
                auto requestId = queueEntry.arguments.at("requestId");
                std::unique_lock<std::mutex> lockCommandsMutex(m_commandsMutex);
                queueEntry.response["success"] = false;
                for (auto iter = m_commandsQueue.begin(); iter != m_commandsQueue.end(); ++iter)
                {
                    if (requestId != iter->response["request_seq"])
                    {
                        continue;
                    }

                    if (GetDebuggerSetupCommandSet().find(iter->command) != GetDebuggerSetupCommandSet().end())
                    {
                        break;
                    }

                    CancelCommand(iter);

                    queueEntry.response["success"] = true;
                    break;
                }
                lockCommandsMutex.unlock();

                if (!queueEntry.response["success"])
                {
                    queueEntry.response["message"] = "CancelRequest is not supported for requestId.";
                }

                EmitMessageWithLog(LOG_RESPONSE, queueEntry.response);
                continue;
            }

            std::unique_lock<std::mutex> lockCommandsMutex(m_commandsMutex);
            const bool isCommandNeedSync = GetSyncCommandExecutionSet().find(queueEntry.command) != GetSyncCommandExecutionSet().end();
            m_commandsQueue.emplace_back(std::move(queueEntry));
            m_commandsCV.notify_one(); // notify_one with lock

            if (isCommandNeedSync)
            {
                m_commandSyncCV.wait(lockCommandsMutex);
            }

            continue;
        }
        catch (nlohmann::detail::exception &ex)
        {
            LOGE("JSON error: %s", ex.what());
            queueEntry.response["type"] = "response";
            queueEntry.response["success"] = false;
            queueEntry.response["message"] = std::string("can't parse: ") + ex.what();
        }
        catch (bad_format &ex)
        {
            LOGE("JSON error: %s", ex.what());
            queueEntry.response["type"] = "response";
            queueEntry.response["success"] = false;
            queueEntry.response["message"] = std::string("can't parse: ") + ex.what();
        }

        EmitMessageWithLog(LOG_RESPONSE, queueEntry.response);
    }

    commandsWorker.join();
    Interop::Shutdown();
}

void DAP::SetupProtocolLogging(const std::string &path)
{
    if (path.empty())
    {
        return;
    }

    m_protocolLog.open(path);
}

// Caller must care about m_outMutex.
void DAP::Log(const std::string_view &prefix, const std::string &text)
{
    if (!m_protocolLog.is_open())
    {
        return;
    }
    
    m_protocolLog << prefix << text << std::endl; // NOLINT(performance-avoid-endl)
}

void DAP::CreateManagedDebugger()
{
    assert(m_sharedDebugger == nullptr);
    try
    {
        m_sharedDebugger = std::make_shared<ManagedDebugger>(this);
    }
    catch (const std::exception &e)
    {
        EmitOutputEvent(OutputEvent(OutputCategory::StdErr, e.what()));
    }
}

} // namespace dncdbg
