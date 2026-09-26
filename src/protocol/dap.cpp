// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "protocol/dap.h"
#include "config/config.h"
#include "debugger/manageddebugger.h"
#include "protocol/dap_events.h"
#include "protocol/internal_helpers.h"
#include "protocol/to_json.h" // NOLINT(misc-include-cleaner)
#include "types/protocol.h"
#include "types/types.h"
#include "utils/hresult.h"
#include "utils/logger.h"
#include <json/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <iterator>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// for convenience
using nlohmann::json;

namespace dncdbg::DAP
{

namespace
{

// Make sure we continue adding new commands to the queue only after the current command execution is finished.
// Note: configurationDone prevents a deadlock in the _dup() call during std::getline() from stdin in the main thread.
const std::unordered_set<std::string> &GetSyncCommandExecutionSet()
{
    static const std::unordered_set<std::string> syncCommandExecutionSet{
        "configurationDone",
        "disconnect",
        "terminate",
        "restart"
    };
    return syncCommandExecutionSet;
}

// Commands, that trigger command queue canceling routine.
const std::unordered_set<std::string> &GetCancelCommandQueueSet()
{
    static const std::unordered_set<std::string> cancelCommandQueueSet{
        "disconnect",
        "terminate",
        "restart",
        "continue",
        "next",
        "stepIn",
        "stepOut",
        "goto"
    };
    return cancelCommandQueueSet;
}

// Don't cancel commands related to debugger configuration. For example, breakpoint setup could be done at any time
// (even if process is not attached at all).
const std::unordered_set<std::string> &GetDebuggerSetupCommandSet()
{
    static const std::unordered_set<std::string> debuggerSetupCommandSet{
        "initialize",
        "setExceptionBreakpoints",
        "setFunctionBreakpoints",
        "setBreakpoints",
        "configurationDone",
        "launch",
        "attach",
        "disconnect",
        "terminate",
        "restart"
    };
    return debuggerSetupCommandSet;
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
                LOGI(log << "EOF");
            }
            else
            {
                LOGE(log << "input stream reading error");
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
                LOGE(log << "protocol error: no 'Content Length:' field!");
                return {};
            }
            break; // header and content delimiter
        }

        if (line.size() > CONTENT_LENGTH.size() &&
            std::equal(CONTENT_LENGTH.cbegin(), CONTENT_LENGTH.cend(), line.begin()))
        {
            if (content_len >= 0)
            {
                LOGW(log << "protocol violation: duplicate '" << line << "'");
            }

            char *p = nullptr;
            errno = 0;
            static constexpr int base = 10;
            content_len = static_cast<long>(strtoul(&line.at(CONTENT_LENGTH.size()), &p, base));
            if (errno == ERANGE || (*p != 0 && (isspace(*p) == 0)))
            {
                LOGE(log << "protocol violation: '" << line << "'");
                return {};
            }
        }
    }

    std::string result(content_len, 0);
    if (!cin.read(result.data(), content_len))
    {
        if (cin.eof())
        {
            LOGE(log << "Unexpected EOF!");
        }
        else
        {
            LOGE(log << "input stream reading error");
        }
        return {};
    }

    return result;
}

json FormJsonForExceptionDetails(const ExceptionDetails &details)
{
    std::list<const ExceptionDetails *> reverseExceptions;

    const ExceptionDetails *current = &details;
    while (current != nullptr)
    {
        reverseExceptions.push_front(current);
        current = current->innerException.get();
    }

    json result;
    while (!reverseExceptions.empty())
    {
        current = reverseExceptions.front();
        reverseExceptions.pop_front();

        json tmp{{"typeName", current->typeName},
                 {"fullTypeName", current->fullTypeName},
                 {"evaluateName", current->evaluateName},
                 {"stackTrace", current->stackTrace},
                 {"formattedDescription", current->formattedDescription},
                 {"source", current->source}};

        result.swap(tmp);

        if (!current->message.empty())
        {
            result.emplace("message", current->message);
        }

        if (current->innerException)
        {
            // Note, DAP has "innerException" field as array, but in reality we don't have an array with inner
            // exceptions here, since the exception object has only one exception object reference in the InnerException field.
            json arr = json::array();
            arr.push_back(tmp);
            result.emplace("innerException", arr);
        }
    }

    return result;
}

// Parse a DAP Source from JSON. When the Source itself has no `sourceReference`, `fallbackSourceReference`
// is used instead (the Source Request allows this field at the top level of the arguments).
// Returns E_INVALIDARG if neither `path`, nor `name`, nor `sourceReference` is provided.
HRESULT ParseSourceJson(const json &sourceJson, int32_t fallbackSourceReference, Source &source)
{
    const std::string sourcePath = sourceJson.value("path", std::string());
    const std::string sourceName = sourceJson.value("name", std::string());
    int32_t sourceReference = std::max(sourceJson.value("sourceReference", 0), 0);

    if (sourceReference == 0)
    {
        sourceReference = std::max(fallbackSourceReference, 0);
    }
    if (sourcePath.empty() && sourceName.empty() && sourceReference == 0)
    {
        return E_INVALIDARG;
    }

    source = Source(sourcePath.empty() ? sourceName : sourcePath, sourceReference);
    if (sourceJson.contains("checksums"))
    {
        std::transform(sourceJson.at("checksums").cbegin(), sourceJson.at("checksums").cend(),
                       std::back_inserter(source.checksums), [](const auto &c)
                       {
                           return Checksum(c.value("algorithm", std::string()),
                                           c.value("checksum", std::string()));
                       });
    }

    return S_OK;
}

// Internal state of the DAP protocol layer (formerly the DAP class members).
std::atomic<bool> &GetExit()
{
    static std::atomic<bool> exit{false};
    return exit;
}

bool &GetInternalConsole()
{
    static bool internalConsole{false};
    return internalConsole;
}

struct CommandQueueEntry
{
    std::string command;
    nlohmann::json arguments;
    nlohmann::json response;
};

std::mutex &GetCommandsMutex()
{
    static std::mutex commandsMutex;
    return commandsMutex;
}

std::condition_variable &GetCommandsCV()
{
    static std::condition_variable commandsCV;
    return commandsCV;
}

std::condition_variable &GetCommandSyncCV()
{
    static std::condition_variable commandSyncCV;
    return commandSyncCV;
}

bool &GetCommandSyncFlag()
{
    static bool commandSyncFlag{false};
    return commandSyncFlag;
}

std::list<CommandQueueEntry> &GetCommandsQueue()
{
    static std::list<CommandQueueEntry> commandsQueue;
    return commandsQueue;
}

void ParseAndApplyDebugSessionOptions(const json &arguments)
{
    Config::SetJustMyCode(arguments.value("justMyCode", true)); // MS vsdbg has "justMyCode" enabled by default.
    Config::SetStepFiltering(arguments.value("enableStepFiltering", true)); // MS vsdbg has "enableStepFiltering" enabled by default.
    Config::SetStopAtEntry(arguments.value("stopAtEntry", false)); // MS vsdbg has "stopAtEntry" disabled by default.
    Config::SetSuppressJITOptimizations(arguments.value("suppressJITOptimizations", false)); // MS vsdbg has "suppressJITOptimizations" disabled by default.

    uint32_t evalFlags = Config::EVAL_DEFAULT;
    if (arguments.contains("expressionEvaluationOptions"))
    {
        // https://github.com/OmniSharp/omnisharp-vscode/issues/3173
        // https://github.com/dotnet/vscode-csharp/blob/627cb33704ba2a688904313e51b460c8324a34eb/package.nls.json#L456
        const bool allowFuncEval = arguments.at("expressionEvaluationOptions").value("allowImplicitFuncEval", true);
        evalFlags |= allowFuncEval ? 0 : Config::EVAL_NOFUNCEVAL;
        // https://github.com/dotnet/vscode-csharp/blob/627cb33704ba2a688904313e51b460c8324a34eb/package.nls.json#L462
        const bool allowToString = arguments.at("expressionEvaluationOptions").value("allowToString", true);
        evalFlags |= (allowToString && allowFuncEval) ? 0 : Config::EVAL_NOTOSTRING;
        // https://github.com/dotnet/vscode-csharp/blob/627cb33704ba2a688904313e51b460c8324a34eb/package.nls.json#L469
        const bool showRawValues = arguments.at("expressionEvaluationOptions").value("showRawValues", false);
        evalFlags |= showRawValues ? Config::EVAL_SHOWRAWVALUES : 0;
    }
    Config::SetEvalFlags(evalFlags);

    const auto findSourceFileMap = arguments.find("sourceFileMap");
    std::map<std::string, std::string> map;
    if (findSourceFileMap != arguments.cend())
    {
        try
        {
            // https://code.visualstudio.com/docs/csharp/debugger-settings#_source-file-map
            map = findSourceFileMap->get<std::map<std::string, std::string>>();
        }
        catch (const std::exception &ex)
        {
            LOGI(log << "sourceFileMap exception '" << ex.what() << "'");
        }
    }
    ManagedDebugger::SetSourceFileMap(std::move(map));
}

HRESULT ParseAndApplyLaunchOptions(const json &arguments, std::map<std::string, std::string> &env)
{
    const auto findProgram = arguments.find("program");
    if (findProgram == arguments.cend())
    {
        return E_INVALIDARG;
    }
    const std::string program = findProgram->get<std::string>();
    std::vector<std::string> args = arguments.value("args", std::vector<std::string>());

    const auto cwdIt = arguments.find("cwd");
    const std::string cwd = cwdIt != arguments.cend() ? cwdIt.value().get<std::string>() : std::string{};

    const auto findEnv = arguments.find("env");
    if (findEnv != arguments.cend())
    {
        try
        {
            env = findEnv->get<std::map<std::string, std::string>>();
        }
        catch (const std::exception &ex)
        {
            LOGI(log << "env exception '" << ex.what() << "'");
            // The read may have been interrupted mid-way and left the map in an inconsistent state; clear it to be safe.
            env.clear();
        }
    }

    const std::string dllSuffix = ".dll";
    if (program.size() >= dllSuffix.size() &&
        program.compare(program.size() - dllSuffix.size(), dllSuffix.size(), dllSuffix) == 0)
    {
        args.insert(args.begin(), program);
        return ManagedDebugger::Launch("dotnet", args, env, cwd);
    }
    else
    {
        // If we're not being asked to launch a DLL, assume that whatever we're given is an executable.
        return ManagedDebugger::Launch(program, args, env, cwd);
    }
}

HRESULT HandleCommand(const std::string &command, const nlohmann::json &arguments, nlohmann::json &responseBody)
{
    using CommandCallback = std::function<HRESULT(const json &arguments, json &responseBody)>;
    static std::unordered_map<std::string, CommandCallback> commands{
        {"initialize", [](const json &/*arguments*/, json &responseBody)
            {
                // clientID, clientName, adapterID - not in use now

                // Note: supportsMemoryReferences is ignored, since memoryReference is always provided regardless of this capability.

                AddCapabilitiesTo(responseBody);

                return S_OK;
            }},
        {"setExceptionBreakpoints", [](const json &arguments, json &/*responseBody*/)
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
                    const auto findFilter = GetExceptionFilters().find(entry);
                    if (findFilter == GetExceptionFilters().cend())
                    {
                        return E_INVALIDARG;
                    }
                    // in case of DAP, we can't set up categoryHint during breakpoint setup,
                    // since this protocol doesn't provide such information
                    exceptionBreakpoints.emplace_back(ExceptionCategory::ANY, findFilter->second);
                }

                for (auto &entry : filterOptions)
                {
                    const auto findId = entry.find("filterId");
                    if (findId == entry.cend() || findId->second.empty())
                    {
                        return E_INVALIDARG;
                    }

                    const auto findFilter = GetExceptionFilters().find(findId->second);
                    if (findFilter == GetExceptionFilters().cend())
                    {
                        return E_INVALIDARG;
                    }
                    // in case of DAP, we can't set up categoryHint during breakpoint setup,
                    // since this protocol doesn't provide such information
                    exceptionBreakpoints.emplace_back(ExceptionCategory::ANY, findFilter->second);

                    const auto findCondition = entry.find("condition");
                    if (findCondition == entry.cend() || findCondition->second.empty())
                    {
                        continue;
                    }

                    if (findCondition->second.at(0) == '!')
                    {
                        if (findCondition->second.size() == 1)
                        {
                            continue;
                        }

                        findCondition->second.at(0) = ' ';
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
                IfFailRet(ManagedDebugger::SetExceptionBreakpoints(exceptionBreakpoints, breakpoints));

                // TODO form responseBody with breakpoints (optional output, MS vsdbg doesn't provide it for VS Code IDE now)
                // responseBody.emplace("breakpoints", breakpoints);

                return S_OK;
            }},
        {"configurationDone", [](const json &/*arguments*/, json &/*responseBody*/)
            {
                return ManagedDebugger::ConfigurationDone();
            }},
        {"exceptionInfo", [](const json &arguments, json &responseBody)
            {
                HRESULT Status = S_OK;
                const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};
                ExceptionInfo exceptionInfo;
                IfFailRet(ManagedDebugger::GetExceptionInfo(threadId, exceptionInfo));

                responseBody.emplace("exceptionId", exceptionInfo.exceptionId);
                responseBody.emplace("description", exceptionInfo.description);
                responseBody.emplace("breakMode", exceptionInfo.breakMode);
                responseBody.emplace("details", FormJsonForExceptionDetails(exceptionInfo.details));
                return S_OK;
            }},
        {"setBreakpoints", [](const json &arguments, json &responseBody)
            {
                HRESULT Status = S_OK;

                std::vector<SourceBreakpoint> sourceBreakpoints;
                std::transform(arguments.at("breakpoints").cbegin(), arguments.at("breakpoints").cend(),
                               std::back_inserter(sourceBreakpoints), [](const auto &b)
                               {
                                   return SourceBreakpoint(b.at("line"),
                                                           b.value("column", 0),
                                                           b.value("condition", std::string()),
                                                           b.value("hitCondition", std::string()),
                                                           b.value("logMessage", std::string()));
                               });

                Source source;
                IfFailRet(ParseSourceJson(arguments.at("source"), 0, source));

                std::vector<Breakpoint> breakpoints;
                IfFailRet(ManagedDebugger::SetSourceBreakpoints(source, sourceBreakpoints, breakpoints));

                responseBody.emplace("breakpoints", breakpoints);

                return S_OK;
            }},
        {"launch", [](const json &arguments, json &/*responseBody*/)
            {
                HRESULT Status = S_OK;
                std::map<std::string, std::string> env;
                IfFailRet(ParseAndApplyLaunchOptions(arguments, env));

                ParseAndApplyDebugSessionOptions(arguments);

                // https://aka.ms/VSCode-CS-LaunchJson-Console
                std::string console;
                const auto findConsole = env.find("DNCDBG_CONSOLE");
                if (findConsole != env.cend())
                {
                    console = findConsole->second;
                }
                else // fallback to `console` field
                {
                    const auto consoleIter = arguments.find("console");
                    if (consoleIter != arguments.cend())
                    {
                        console = consoleIter.value();
                    }
                }

                if (console == "internalConsole")
                {
                    GetInternalConsole() = true;
                }
                else if (console == "remoteConsole")
                {
                    constexpr int defaultPort = 22534;
                    int remoteConsolePort = defaultPort;
                    const auto findConsolePort = env.find("DNCDBG_REMOTECONSOLEPORT");
                    if (findConsolePort != env.cend())
                    {
                        try
                        {
                            remoteConsolePort = std::stoi(findConsolePort->second);
                        }
                        catch (const std::invalid_argument &ex)
                        {
                            LOGE(log << "DNCDBG_REMOTECONSOLEPORT not a number: " << ex.what());
                            return E_INVALIDARG;
                        }
                        catch (const std::out_of_range &ex)
                        {
                            LOGE(log << "DNCDBG_REMOTECONSOLEPORT number out of int range: " << ex.what());
                            return E_INVALIDARG;
                        }
                    }

                    if (!ManagedDebugger::InitializeRemoteConsoleServer(remoteConsolePort))
                    {
                        return INET_E_CANNOT_CONNECT;
                    }
                }
                else if (console == "externalTerminal")
                {
#ifdef _WIN32
                    if (SetEnvironmentVariableW(L"DNCDBG_CREATE_NEW_CONSOLE", L"1") == FALSE)
                    {
                        LOGE(log << "Failed to set DNCDBG_CREATE_NEW_CONSOLE environment variable, error: " << GetLastError());
                        return E_FAIL;
                    }
#else
                    LOGW(log << "externalTerminal console mode is not supported on this platform");
#endif // _WIN32
                }

                return S_OK;
            }},
        {"threads", [](const json &/*arguments*/, json &responseBody)
            {
                HRESULT Status = S_OK;
                std::vector<Thread> threads;
                IfFailRet(ManagedDebugger::GetThreads(threads));

                responseBody.emplace("threads", threads);

                return S_OK;
            }},
        {"disconnect", [](const json &arguments, json &/*responseBody*/)
            {
                const auto terminateArgIter = arguments.find("terminateDebuggee");
                ManagedDebugger::DisconnectAction action = ManagedDebugger::DisconnectAction::Default;
                if (terminateArgIter == arguments.cend())
                {
                    action = ManagedDebugger::DisconnectAction::Default;
                }
                else
                {
                    action = terminateArgIter.value().get<bool>() ? ManagedDebugger::DisconnectAction::Terminate
                                                                  : ManagedDebugger::DisconnectAction::Detach;
                }

                ManagedDebugger::Disconnect(action);

                return S_OK;
            }},
        {"terminate", [](const json &/*arguments*/, json &/*responseBody*/)
            {
                ManagedDebugger::Disconnect(ManagedDebugger::DisconnectAction::Terminate);
                return S_OK;
            }},
        {"stackTrace", [](const json &arguments, json &responseBody)
            {
                HRESULT Status = S_OK;

                const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};

                std::vector<StackFrame> stackFrames;
                IfFailRet(ManagedDebugger::GetStackTrace(threadId, FrameLevel{arguments.value("startFrame", 0)},
                                                         static_cast<unsigned>(arguments.value("levels", 0)), stackFrames));

                responseBody.emplace("stackFrames", stackFrames);
                responseBody.emplace("totalFrames", stackFrames.size());

                return S_OK;
            }},
        {"continue", [](const json &arguments, json &responseBody)
            {
                const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};
                const bool singleThread = arguments.value("singleThread", false);

                HRESULT Status = S_OK;
                IfFailRet(ManagedDebugger::Continue(threadId, singleThread));

                responseBody.emplace("allThreadsContinued", !singleThread);
                responseBody.emplace("threadId", static_cast<int>(threadId));
                return S_OK;
            }},
        {"pause", [](const json &arguments, json &/*responseBody*/)
            {
                const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};
                return ManagedDebugger::Pause(threadId);
            }},
        {"next", [](const json &arguments, json &/*responseBody*/)
            {
                const bool singleThread = arguments.value("singleThread", false);
                return ManagedDebugger::StepCommand(ThreadId{static_cast<int>(arguments.at("threadId"))},
                                                    StepType::STEP_OVER, singleThread);
            }},
        {"stepIn", [](const json &arguments, json &/*responseBody*/)
            {
                const bool singleThread = arguments.value("singleThread", false);
                return ManagedDebugger::StepCommand(ThreadId{static_cast<int>(arguments.at("threadId"))},
                                                    StepType::STEP_IN, singleThread);
            }},
        {"stepOut", [](const json &arguments, json &/*responseBody*/)
            {
                const bool singleThread = arguments.value("singleThread", false);
                return ManagedDebugger::StepCommand(ThreadId{static_cast<int>(arguments.at("threadId"))},
                                                    StepType::STEP_OUT, singleThread);
            }},
        {"scopes", [](const json &arguments, json &responseBody)
            {
                HRESULT Status = S_OK;
                std::vector<Scope> scopes;
                const FrameId frameId{static_cast<int>(arguments.at("frameId"))};
                IfFailRet(ManagedDebugger::GetScopes(frameId, scopes));

                responseBody.emplace("scopes", scopes);

                return S_OK;
            }},
        {"variables", [](const json &arguments, json &responseBody)
            {
                HRESULT Status = S_OK;
                std::vector<Variable> variables;
                IfFailRet(ManagedDebugger::GetVariables(arguments.at("variablesReference"), variables));

                responseBody.emplace("variables", variables);

                return S_OK;
            }},
        {"evaluate", [](const json &arguments, json &responseBody)
            {
                std::string expression = arguments.at("expression");
                const FrameId frameId([&]
                    {
                        const auto frameIdIter = arguments.find("frameId");
                        if (frameIdIter == arguments.cend())
                        {
                            const ThreadId threadId = ManagedDebugger::GetLastStoppedThreadId();
                            return FrameId{threadId, FrameLevel{0}};
                        }
                        else
                        {
                            return FrameId{static_cast<int>(frameIdIter.value())};
                        }
                    }());

                if (GetInternalConsole() && ManagedDebugger::IsProcessRunning())
                {
                    expression += '\n'; // User pressed "Enter".
                    ManagedDebugger::WriteStdin({expression.data(), expression.size()});
                    responseBody.emplace("message", "Text redirected to debuggee stdin.");
                    return S_OK;
                }

                HRESULT Status = S_OK;
                Variable variable;
                std::string output;
                if (FAILED(Status = ManagedDebugger::Evaluate(frameId, expression, variable, output)))
                {
                    if (output.empty())
                    {
                        std::stringstream stream;
                        stream << "error: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status;
                        responseBody.emplace("message", stream.str());
                    }
                    else
                    {
                        responseBody.emplace("message", output);
                    }

                    return Status;
                }

                responseBody.emplace("result", variable.value);
                responseBody.emplace("type", variable.type);
                responseBody.emplace("variablesReference", variable.variablesReference);
                if (!variable.memoryReference.empty())
                {
                    responseBody.emplace("memoryReference", variable.memoryReference);
                }
                return S_OK;
            }},
        {"setExpression", [](const json &arguments, json &responseBody)
            {
                const std::string expression = arguments.at("expression");
                const std::string value = arguments.at("value");
                const FrameId frameId([&]
                    {
                        const auto frameIdIter = arguments.find("frameId");
                        if (frameIdIter == arguments.cend())
                        {
                            const ThreadId threadId = ManagedDebugger::GetLastStoppedThreadId();
                            return FrameId{threadId, FrameLevel{0}};
                        }
                        else
                        {
                            return FrameId{static_cast<int>(frameIdIter.value())};
                        }
                    }());

                HRESULT Status = S_OK;
                std::string output;
                if (FAILED(Status = ManagedDebugger::SetExpression(frameId, expression, value, output)))
                {
                    if (output.empty())
                    {
                        std::stringstream stream;
                        stream << "error: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status;
                        responseBody.emplace("message", stream.str());
                    }
                    else
                    {
                        responseBody.emplace("message", output);
                    }

                    return Status;
                }
                // TODO: add `memoryReference`
                responseBody.emplace("value", output);
                return S_OK;
            }},
        {"attach", [](const json &arguments, json &/*responseBody*/)
            {
                HRESULT Status = S_OK;
                const DWORD processId = arguments.value("processId", 0);
                if (processId == 0)
                {
                    return E_INVALIDARG;
                }

                IfFailRet(ManagedDebugger::Attach(processId));

                ParseAndApplyDebugSessionOptions(arguments);
                return S_OK;
            }},
        {"setVariable", [](const json &arguments, json &responseBody)
            {
                const std::string name = arguments.at("name");
                const std::string value = arguments.at("value");
                const int ref = arguments.at("variablesReference");

                HRESULT Status = S_OK;
                std::string output;
                if (FAILED(Status = ManagedDebugger::SetVariable(name, value, ref, output)))
                {
                    responseBody.emplace("message", output);
                    return Status;
                }
                // TODO: add `type` and `memoryReference`
                responseBody.emplace("value", output);

                return S_OK;
            }},
        {"setFunctionBreakpoints", [](const json &arguments, json &responseBody)
            {
                HRESULT Status = S_OK;

                std::vector<FunctionBreakpoint> functionBreakpoints;
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

                    functionBreakpoints.emplace_back(name, params, b.value("condition", std::string()),
                                                     b.value("hitCondition", std::string()));
                }

                std::vector<Breakpoint> breakpoints;
                IfFailRet(ManagedDebugger::SetFunctionBreakpoints(functionBreakpoints, breakpoints));

                responseBody.emplace("breakpoints", breakpoints);

                return Status;
            }},
        {"modules", [](const json &arguments, json &responseBody)
            {
                size_t totalModules = 0;
                std::vector<Module> modules;
                ManagedDebugger::GetModules(arguments.value("startModule", 0), arguments.value("moduleCount", 0),
                                             modules, totalModules);

                responseBody.emplace("modules", modules);
                responseBody.emplace("totalModules", totalModules);

                return S_OK;
            }},
        {"gotoTargets", [](const json &arguments, json &responseBody)
            {
                HRESULT Status = S_OK;

                Source source;
                IfFailRet(ParseSourceJson(arguments.at("source"), 0, source));

                const int32_t line = arguments.at("line");
                const int32_t column = arguments.value("column", 0);

                std::vector<GotoTarget> targets;
                std::string output;
                if (FAILED(Status = ManagedDebugger::GetGotoTarget(source, line, column, targets, output)))
                {
                    if (!output.empty())
                    {
                        responseBody.emplace("message", output);
                    }
                    return Status;
                }

                responseBody.emplace("targets", targets);

                return S_OK;
            }},
        {"goto", [](const json &arguments, json &responseBody)
            {
                const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};
                const uint32_t targetId = arguments.at("targetId");

                HRESULT Status = S_OK;
                std::string output;
                if (FAILED(Status = ManagedDebugger::Goto(threadId, targetId, output)))
                {
                    if (!output.empty())
                    {
                        responseBody.emplace("message", output);
                    }
                    return Status;
                }

                return S_OK;
            }},
        {"source", [](const json &arguments, json &responseBody)
            {
                HRESULT Status = S_OK;

                // Note, per DAP spec. `source` is optional, but `sourceReference` is required.
                // The `sourceReference` at the top level of the arguments is used when
                // `source` is not provided, or provides no `sourceReference` itself.
                const int32_t sourceReference = arguments.at("sourceReference");

                Source source;
                if (arguments.contains("source"))
                {
                    IfFailRet(ParseSourceJson(arguments.at("source"), sourceReference, source));
                }
                else
                {
                    source = Source(std::string(), sourceReference);
                }

                std::string sourceContent;
                if (FAILED(Status = ManagedDebugger::GetSourceContent(source, sourceContent)))
                {
                    if (!sourceContent.empty())
                    {
                        responseBody.emplace("message", sourceContent);
                    }
                    return Status;
                }

                responseBody.emplace("content", sourceContent);

                return S_OK;
            }},
        {"loadedSources", [](const json &/*arguments*/, json &responseBody)
            {
                std::vector<Source> sources;
                ManagedDebugger::GetLoadedSources(sources);

                responseBody.emplace("sources", sources);

                return S_OK;
            }},
        {"breakpointLocations", [](const json &arguments, json &responseBody)
            {
                HRESULT Status = S_OK;

                Source source;
                IfFailRet(ParseSourceJson(arguments.at("source"), 0, source));

                BreakpointLocation rangeToSearch;
                rangeToSearch.line = arguments.at("line");
                rangeToSearch.column = arguments.value("column", 0);
                rangeToSearch.endLine = arguments.value("endLine", 0);
                rangeToSearch.endColumn = arguments.value("endColumn", 0);

                // Note, `column`, `endLine` and `endColumn` are optional; zero means the field is not provided.
                if (rangeToSearch.line <= 0 || rangeToSearch.column < 0 ||
                    rangeToSearch.endLine < 0 || rangeToSearch.endColumn < 0)
                {
                    return E_INVALIDARG;
                }

                std::vector<BreakpointLocation> locations;
                IfFailRet(ManagedDebugger::GetBreakpointLocations(source, rangeToSearch, locations));

                // DAP requires the 'breakpointLocations' response to be a sorted set of possible breakpoint locations.
                std::sort(locations.begin(), locations.end(), [](const BreakpointLocation &a, const BreakpointLocation &b)
                {
                    return a.line < b.line || (a.line == b.line && a.column < b.column);
                });

                responseBody.emplace("breakpoints", locations);

                return S_OK;
            }},
        {"restart", [](const json &arguments, json &/*responseBody*/)
            {
                HRESULT Status = S_OK;

                if (ManagedDebugger::HaveDebugProcess())
                {
                    IfFailRet(ManagedDebugger::Disconnect(ManagedDebugger::DisconnectAction::Default));
                }

                // Note, the optional `arguments` field carries the (possibly updated) original launch or attach
                // arguments, so the restart target may differ from the one used initially.
                const auto restartArgumentsIt = arguments.find("arguments");
                if (restartArgumentsIt != arguments.cend())
                {
                    const json &restartArguments = restartArgumentsIt.value();

                    const DWORD processId = restartArguments.value("processId", 0);
                    if (processId != 0)
                    {
                        IfFailRet(ManagedDebugger::Attach(processId));
                    }
                    else
                    {
                        std::map<std::string, std::string> env;
                        IfFailRet(ParseAndApplyLaunchOptions(restartArguments, env));
                    }

                    ParseAndApplyDebugSessionOptions(restartArguments);
                }

                return ManagedDebugger::ConfigurationDone();
            }}};

    const auto command_it = commands.find(command);
    if (command_it == commands.cend())
    {
        responseBody.emplace("message", "Request '" + command + "' is not supported.");
        return E_NOTIMPL;
    }

    return command_it->second(arguments, responseBody);
}

HRESULT HandleCommandJSON(const std::string &command, const nlohmann::json &arguments, nlohmann::json &responseBody)
{
    try
    {
        return HandleCommand(command, arguments, responseBody);
    }
    catch (nlohmann::detail::exception &ex)
    {
        LOGE(log << "JSON error: " << ex.what());
        responseBody.emplace("message", std::string("can't parse: ") + ex.what());
    }

    return E_FAIL;
}

void CommandsWorker()
{
    std::mutex &commandsMutex = GetCommandsMutex();
    std::condition_variable &commandsCV = GetCommandsCV();
    std::condition_variable &commandSyncCV = GetCommandSyncCV();
    bool &commandSyncFlag = GetCommandSyncFlag();
    std::list<CommandQueueEntry> &commandsQueue = GetCommandsQueue();

    std::unique_lock<std::mutex> lockCommandsMutex(commandsMutex);

    while (true)
    {
        while (commandsQueue.empty())
        {
            // Note, during commandsCV.wait() (waiting for a notify_one call with an entry added to the queue),
            // commandsMutex will be unlocked (see std::condition_variable for more info).
            commandsCV.wait(lockCommandsMutex);
        }

        CommandQueueEntry c = std::move(commandsQueue.front());
        commandsQueue.pop_front();
        lockCommandsMutex.unlock();

        // Check for dncdbg internal commands.
        if (c.command == "dncdbg_disconnect")
        {
            ManagedDebugger::Disconnect();
            break;
        }

        json responseBody = json::object();
        std::future<HRESULT> future = std::async(std::launch::async, [&]
            {
                return HandleCommandJSON(c.command, c.arguments, responseBody);
            });
        HRESULT Status = S_OK;
        // Note, the CommandsWorker() loop should never hang, but even if some command execution times out,
        // this may not be a critical issue. Let the IDE decide.

        // The MSVS debugger uses a config file; for Visual Studio 2022 Community Edition it is located at
        // C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\Profiles\CSharp.vssettings
        // Visual Studio has a timeout setting for each type of request, for example:
        // LocalsTimeout = 1000
        // LongEvalTimeout = 10000
        // NormalEvalTimeout = 5000
        // QuickwatchTimeout = 15000
        // SetValueTimeout = 10000
        // ...
        // we use the maximum default timeout (15000 ms), one timeout for all requests.

        // TODO add timeout configuration feature
        const std::future_status timeoutStatus = future.wait_for(std::chrono::milliseconds(15000));
        if (timeoutStatus == std::future_status::timeout)
        {
            responseBody.emplace("message", "Command execution timed out.");
            Status = COR_E_TIMEOUT;
        }
        else
        {
            Status = future.get();
        }

        if (SUCCEEDED(Status))
        {
            c.response.emplace("success", true);
            if (!responseBody.empty())
            {
                c.response.emplace("body", responseBody);
            }
        }
        else
        {
            if (!responseBody.contains("message"))
            {
                std::ostringstream ss;
                ss << "Failed command '" << c.command << "' : "
                   << "0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status;
                c.response.emplace("message", ss.str());
            }
            else
            {
                c.response.emplace("message", responseBody.at("message"));
            }

            c.response.emplace("success", false);
        }

        DAP::EmitMessageWithLog(LOG_RESPONSE, c.response);

        // Post command action.
        if (GetSyncCommandExecutionSet().find(c.command) != GetSyncCommandExecutionSet().cend())
        {
            {
                const std::scoped_lock<std::mutex> guardCommandsMutex(commandsMutex);
                commandSyncFlag = false;
            }
            commandSyncCV.notify_one();
        }

        if (c.command == "disconnect")
        {
            break;
        }
        // The Debug Adapter Protocol specifies that `InitializedEvent` occurs after the `InitializeRequest` has returned:
        // https://microsoft.github.io/debug-adapter-protocol/specification#arrow_left-initialized-event
        else if (c.command == "initialize" && SUCCEEDED(Status))
        {
            DAP::EmitInitializedEvent();
        }
        // Emit StoppedEvent, since with Goto command IP was changed without real process execution.
        else if (c.command == "goto" && SUCCEEDED(Status))
        {
            DAP::EmitStoppedEvent(StoppedEvent(StoppedEventReason::Goto, ManagedDebugger::GetLastStoppedThreadId()));
        }
        // Emit StoppedEvent after the response is sent.
        else if (c.command == "pause" && SUCCEEDED(Status))
        {
            DAP::EmitStoppedEvent(StoppedEvent(StoppedEventReason::Pause, ManagedDebugger::GetLastStoppedThreadId()));
        }

        lockCommandsMutex.lock();
    }

    GetExit() = true;
}

// Caller must hold the commands mutex.
std::list<CommandQueueEntry>::iterator CancelCommand(const std::list<CommandQueueEntry>::iterator &iter)
{
    iter->response.emplace("success", false);
    iter->response.emplace("message", std::string("Error processing '") + iter->command + std::string("' request. The operation was canceled."));
    DAP::EmitMessageWithLog(LOG_RESPONSE, iter->response);
    return GetCommandsQueue().erase(iter);
}

} // unnamed namespace

void CommandLoop()
{
    std::mutex &commandsMutex = GetCommandsMutex();
    std::condition_variable &commandsCV = GetCommandsCV();
    std::condition_variable &commandSyncCV = GetCommandSyncCV();
    std::list<CommandQueueEntry> &commandsQueue = GetCommandsQueue();
    bool &commandSyncFlag = GetCommandSyncFlag();

    GetExit() = false;

    std::thread commandsWorker{CommandsWorker};

    while (!GetExit())
    {
        const std::string requestText = ReadData(std::cin);
        if (requestText.empty())
        {
            // Input read failed for some reason, initiate forced disconnect.
            CommandQueueEntry queueEntry;
            queueEntry.command = "dncdbg_disconnect";
            const std::scoped_lock<std::mutex> guardCommandsMutex(commandsMutex);
            commandsQueue.clear();
            commandsQueue.emplace_back(std::move(queueEntry));
            commandsCV.notify_one(); // notify_one with lock
            break;
        }

        DAP::Log(LOG_COMMAND, requestText);

        struct bad_format : public std::invalid_argument
        {
            explicit bad_format(const char *s)
                : invalid_argument(s)
            {
            }
        };

        CommandQueueEntry queueEntry;
        try
        {
            json request = json::parse(requestText);

            // Variable `resp' is used to construct response and assign it to `response'
            // variable in single step: `response' variable should always be in
            // consistent state (it must not have state when some fields are assigned and
            // some not assigned due to an exception) because `response' is used below
            // in exception handler.
            json resp;
            resp.emplace("type", "response");
            resp.emplace("request_seq", request.at("seq"));
            queueEntry.response = resp;

            queueEntry.command = request.at("command");
            resp.emplace("command", queueEntry.command);
            queueEntry.response = resp;

            if (request.at("type") != "request")
            {
                throw bad_format("wrong request type!");
            }

            const auto argIter = request.find("arguments");
            queueEntry.arguments = (argIter == request.cend() ? json::object() : argIter.value());

            // Pre command action.
            if (queueEntry.command == "initialize")
            {
                DAP::EmitCapabilitiesEvent();
            }
            else if (GetCancelCommandQueueSet().find(queueEntry.command) != GetCancelCommandQueueSet().cend())
            {
                const std::scoped_lock<std::mutex> guardCommandsMutex(commandsMutex);
                ManagedDebugger::CancelEvalRunning();

                for (auto iter = commandsQueue.begin(); iter != commandsQueue.end();)
                {
                    if (GetDebuggerSetupCommandSet().find(iter->command) != GetDebuggerSetupCommandSet().cend())
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
                if (!queueEntry.arguments.contains("requestId"))
                {
                    queueEntry.response.emplace("success", false);
                    queueEntry.response.emplace("message", "CancelRequest don't have requestId.");
                    DAP::EmitMessageWithLog(LOG_RESPONSE, queueEntry.response);
                    continue;
                }

                const auto requestId = queueEntry.arguments.at("requestId");
                std::unique_lock<std::mutex> lockCommandsMutex(commandsMutex);
                queueEntry.response.emplace("success", false);
                for (auto iter = commandsQueue.begin(); iter != commandsQueue.end(); ++iter)
                {
                    if (requestId != iter->response.at("request_seq"))
                    {
                        continue;
                    }

                    if (GetDebuggerSetupCommandSet().find(iter->command) != GetDebuggerSetupCommandSet().cend())
                    {
                        break;
                    }

                    CancelCommand(iter);

                    queueEntry.response.at("success") = true;
                    break;
                }
                lockCommandsMutex.unlock();

                if (!queueEntry.response.at("success"))
                {
                    queueEntry.response.emplace("message", "CancelRequest is not supported for requestId.");
                }

                DAP::EmitMessageWithLog(LOG_RESPONSE, queueEntry.response);
                continue;
            }

            std::unique_lock<std::mutex> lockCommandsMutex(commandsMutex);
            commandSyncFlag = GetSyncCommandExecutionSet().find(queueEntry.command) != GetSyncCommandExecutionSet().cend();
            commandsQueue.emplace_back(std::move(queueEntry));
            commandsCV.notify_one(); // notify_one with lock

            if (commandSyncFlag)
            {
                commandSyncCV.wait(lockCommandsMutex, [&commandSyncFlag] { return !commandSyncFlag; });
            }

            continue;
        }
        catch (nlohmann::detail::exception &ex)
        {
            LOGE(log << "JSON error: " << ex.what());
            queueEntry.response.emplace("type", "response");
            queueEntry.response.emplace("success", false);
            queueEntry.response.emplace("message", std::string("can't parse: ") + ex.what());
        }
        catch (bad_format &ex)
        {
            LOGE(log << "JSON error: " << ex.what());
            queueEntry.response.emplace("type", "response");
            queueEntry.response.emplace("success", false);
            queueEntry.response.emplace("message", std::string("can't parse: ") + ex.what());
        }

        DAP::EmitMessageWithLog(LOG_RESPONSE, queueEntry.response);
    }

    commandsWorker.join();
}

void SetupProtocolLogging(const std::string &path)
{
    SetupProtocolLoggingInternal(path);
}

} // namespace dncdbg::DAP
