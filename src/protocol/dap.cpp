// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "protocol/dap.h"
#include "config/config.h"
#include "debugger/manageddebugger.h"
#include "protocol/dap_commands.h"
#include "protocol/dap_events.h"
#include "protocol/internal_helpers.h"
#include "types/protocol.h"
#include "utils/logger.h"
#include "utils/remote_server.h"
#include <json/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

// for convenience
using nlohmann::json;

namespace dncdbg::DAP
{

namespace
{

// An invalid DAP request structure (e.g. a wrong `type` field).
struct bad_format : public std::invalid_argument
{
    explicit bad_format(const char *s)
        : std::invalid_argument(s)
    {
    }
};

// The protocol loop exit flag: set by CommandsWorker() when it leaves its loop, read by CommandLoop().
std::atomic<bool> &GetExit()
{
    static std::atomic<bool> exit{false};
    return exit;
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

std::deque<CommandQueueEntry> &GetCommandsQueue()
{
    static std::deque<CommandQueueEntry> commandsQueue;
    return commandsQueue;
}

// Pending `launch`/`attach` commands received before `configurationDone`.
// They are held here until `configurationDone` is processed, then moved into
// the commandsQueue so the real attach/launch status is returned at the proper time.
std::deque<CommandQueueEntry> &GetPendingLaunchAttachQueue()
{
    static std::deque<CommandQueueEntry> pendingLaunchAttachQueue;
    return pendingLaunchAttachQueue;
}

// Make sure we continue adding new commands to the queue only after the current command execution is finished.
// Note: restart, launch and attach prevent a deadlock in the _dup() call during std::getline() from stdin in the main thread.
// Sorted by name (ASCII) for binary-search lookup (see ContainsSortedName()).
constexpr std::array<std::string_view, 7> gSyncCommandExecutionSet{{
    "attach",
    "configurationDone",
    "detach",
    "disconnect",
    "launch",
    "restart",
    "terminate"
}};

// Commands that trigger the command queue cancellation routine.
constexpr std::array<std::string_view, 9> gCancelCommandQueueSet{{
    "continue",
    "detach",
    "disconnect",
    "goto",
    "next",
    "restart",
    "stepIn",
    "stepOut",
    "terminate"
}};

// Don't cancel commands related to debugger configuration. For example, breakpoint setup could be done at any time
// (even if process is not attached at all).
constexpr std::array<std::string_view, 11> gDebuggerSetupCommandSet{{
    "attach",
    "configurationDone",
    "detach",
    "disconnect",
    "initialize",
    "launch",
    "restart",
    "setBreakpoints",
    "setExceptionBreakpoints",
    "setFunctionBreakpoints",
    "terminate"
}};

std::string ReadData(std::istream &stream)
{
    // Parse the header (only the content length) until an empty line.
    std::size_t contentLen = 0;
    bool contentLenFound = false;
    std::string line;
    while (true)
    {
        std::getline(stream, line);
        if (!stream.good())
        {
            if (stream.eof())
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
            if (!contentLenFound)
            {
                LOGE(log << "protocol error: no 'Content Length:' field!");
                return {};
            }
            break; // header and content delimiter
        }

        if (line.size() > CONTENT_LENGTH.size() &&
            line.compare(0, CONTENT_LENGTH.size(), CONTENT_LENGTH) == 0)
        {
            if (contentLenFound)
            {
                LOGW(log << "protocol violation: duplicate '" << line << "'");
            }

            // Parse the length with std::from_chars: it is locale-independent, does not touch the global
            // state (errno), and reports an overflow as an error instead of silently wrapping around.
            std::size_t parsedContentLen = 0;
            const char *const parseBegin = line.data() + CONTENT_LENGTH.size();
            const char *const parseEnd = line.data() + line.size();
            const char *digitsBegin = parseBegin;
            while (digitsBegin < parseEnd && std::isspace(static_cast<unsigned char>(*digitsBegin)) != 0)
            {
                ++digitsBegin; // tolerate leading spaces after the field name
            }

            const auto [numberEnd, parseErr] = std::from_chars(digitsBegin, parseEnd, parsedContentLen);
            // Trailing spaces are tolerated, but any other character after the number is a
            // protocol violation. A negative length is rejected as well: from_chars() does not
            // accept a sign for an unsigned type, so it is reported as an error.
            const bool hasNonSpaceTail = std::any_of(numberEnd, parseEnd,
                                                     [](unsigned char ch) { return std::isspace(ch) == 0; });
            if (parseErr != std::errc{} || hasNonSpaceTail)
            {
                LOGE(log << "protocol violation: '" << line << "'");
                return {};
            }

            contentLen = parsedContentLen;
            contentLenFound = true;
        }
    }

    // The content is read in bounded chunks, so the buffer grows only as data actually arrives. A hostile
    // or malformed client may announce an absurd 'Content-Length' and then send less data (or close the
    // stream): no memory is allocated or zero-filled for the announced size, and the read fails cleanly.
    static constexpr std::size_t readChunkSize = 64 * 1024;
    std::string result;
    std::size_t receivedLen = 0;
    try
    {
        while (receivedLen < contentLen)
        {
            const std::size_t chunkLen = std::min(readChunkSize, contentLen - receivedLen);
            result.resize(receivedLen + chunkLen);
            if (!stream.read(result.data() + receivedLen, static_cast<std::streamsize>(chunkLen)))
            {
                if (stream.eof())
                {
                    LOGE(log << "Unexpected EOF!");
                }
                else
                {
                    LOGE(log << "input stream reading error");
                }
                return {};
            }
            receivedLen += chunkLen;
        }
    }
    catch (const std::exception &ex)
    {
        // An allocation failure must lead to a clean disconnect (an empty result) instead of a crash.
        LOGE(log << "can't allocate the request content: " << ex.what());
        return {};
    }

    return result;
}

HRESULT HandleCommandJSON(const std::string &command, const nlohmann::json &arguments, nlohmann::json &responseBody)
{
    try
    {
        return HandleCommand(command, arguments, responseBody);
    }
    catch (const std::exception &ex)
    {
        LOGE(log << "JSON error: " << ex.what());
        responseBody.emplace("message", std::string("can't parse: ") + ex.what());
    }

    return E_FAIL;
}

// Only one command is in flight at a time: the caller must call WaitIdle() before submitting the next
// one. A command that outlives its timeout keeps running on this thread and delays every subsequent
// command until it finishes.
class CommandExecutor
{
  public:

    CommandExecutor()
        : m_thread(&CommandExecutor::Run, this)
    {
    }

    ~CommandExecutor()
    {
        {
            const std::scoped_lock<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_jobCV.notify_one();
        m_thread.join();
    }

    CommandExecutor(const CommandExecutor &) = delete;
    CommandExecutor &operator=(const CommandExecutor &) = delete;
    CommandExecutor(CommandExecutor &&) = delete;
    CommandExecutor &operator=(CommandExecutor &&) = delete;

    // Takes a non-owning pointer to the entry, which must stay alive until WaitIdle() returns.
    // Submit() must be called only when idle.
    void Submit(const CommandQueueEntry &entry)
    {
        {
            const std::scoped_lock<std::mutex> lock(m_mutex);
            m_job = &entry;
            m_done = false;
        }
        m_jobCV.notify_one();
    }

    // Returns false if the last submitted command did not finish within `timeout`.
    bool WaitFor(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_doneCV.wait_for(lock, timeout, [this] { return m_done; });
    }

    // Blocks until the last submitted command (if any) has finished.
    void WaitIdle()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_doneCV.wait(lock, [this] { return m_done; });
    }

    // Returns the result of the last submitted command. Must be called only after WaitFor() returned
    // true or after WaitIdle(). Rethrows an exception that escaped the command (the same behavior as
    // future::get()).
    std::pair<HRESULT, json> TakeResult()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const std::exception_ptr error = std::exchange(m_error, nullptr);
        std::pair<HRESULT, json> result = std::move(m_result);
        lock.unlock();

        if (error)
        {
            std::rethrow_exception(error);
        }
        return result;
    }

  private:

    void Run()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (true)
        {
            m_jobCV.wait(lock, [this] { return m_job != nullptr || m_stop; });
            if (m_job == nullptr)
            {
                return; // stop requested and no command is pending
            }

            const CommandQueueEntry *entry = std::exchange(m_job, nullptr);
            lock.unlock();

            std::pair<HRESULT, json> result{S_OK, json::object()};
            std::exception_ptr error;
            try
            {
                result.first = HandleCommandJSON(entry->command, entry->arguments, result.second);
            }
            catch (...)
            {
                error = std::current_exception();
            }

            lock.lock();
            m_result = std::move(result);
            m_error = std::move(error);
            m_done = true;
            m_doneCV.notify_all();
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_jobCV;
    std::condition_variable m_doneCV;
    const CommandQueueEntry *m_job{nullptr};
    bool m_done{true};
    bool m_stop{false};
    std::pair<HRESULT, json> m_result;
    std::exception_ptr m_error;
    std::thread m_thread; // declared last, so the thread starts after the other members are initialized
};

void CommandsWorker()
{
    CommandExecutor executor;
    std::mutex &commandsMutex = GetCommandsMutex();
    std::condition_variable &commandsCV = GetCommandsCV();
    std::condition_variable &commandSyncCV = GetCommandSyncCV();
    bool &commandSyncFlag = GetCommandSyncFlag();
    std::deque<CommandQueueEntry> &commandsQueue = GetCommandsQueue();

    std::unique_lock<std::mutex> lockCommandsMutex(commandsMutex);

    while (true)
    {
        // Note, during commandsCV.wait() (waiting for a notify_one call with an entry added to the queue),
        // commandsMutex will be unlocked (see std::condition_variable for more info).
        commandsCV.wait(lockCommandsMutex, [&commandsQueue] { return !commandsQueue.empty(); });

        CommandQueueEntry c = std::move(commandsQueue.front());
        commandsQueue.pop_front();
        lockCommandsMutex.unlock();

        // Check for dncdbg internal commands.
        if (c.command == "dncdbg_disconnect")
        {
            ManagedDebugger::Disconnect();
            break;
        }

        // The command runs on the persistent executor thread, which owns its response body until the command
        // completes. A command that outlives the request timeout keeps writing into that executor-owned body
        // only and cannot race with the response assembly below. `c` stays alive until WaitIdle() is called
        // at the end of this iteration, so the executor never reads a destroyed entry.
        executor.Submit(c);

        json responseBody;
        HRESULT Status = S_OK;
        // Note, the CommandsWorker() loop should never hang, but even if some command execution times out,
        // this may not be a critical issue. Let the IDE decide.
        if (!executor.WaitFor(std::chrono::milliseconds(Config::GetDapRequestTimeoutMs())))
        {
            responseBody.emplace("message", "Command execution timed out.");
            Status = COR_E_TIMEOUT;
        }
        else
        {
            // Move the executor-owned response body out of the executor.
            auto [taskStatus, taskResponseBody] = executor.TakeResult();
            Status = taskStatus;
            responseBody = std::move(taskResponseBody);
        }

        if (SUCCEEDED(Status))
        {
            c.response.emplace("success", true);
            if (!responseBody.empty())
            {
                c.response.emplace("body", std::move(responseBody));
            }
        }
        else
        {
            if (!responseBody.contains("message"))
            {
                c.response.emplace("message", "Failed command '" + c.command + "' : " + FormatHexErrorCode(Status));
            }
            else
            {
                c.response.emplace("message", std::move(responseBody.at("message")));
            }

            c.response.emplace("success", false);
        }

        DAP::EmitMessageWithLog(LOG_RESPONSE, c.response);

        // Post command action.
        if (ContainsSortedName(gSyncCommandExecutionSet, c.command))
        {
            {
                const std::scoped_lock<std::mutex> guardCommandsMutex(commandsMutex);
                commandSyncFlag = false;
            }
            commandSyncCV.notify_one();
        }

        if (c.command == "disconnect")
        {
            // The disconnect command may still be running on the executor thread after a timeout, so
            // wait for it before `c` goes out of scope.
            executor.WaitIdle();
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

        // Wait for a timed-out command to finish before `c` goes out of scope and before the next command
        // is submitted. Like the former std::future destructor, this blocks while commandsMutex is held, so
        // it only waits when the command has not finished yet (i.e. after a timeout).
        executor.WaitIdle();
    }

    GetExit() = true;
}

// Caller must hold the commands mutex.
std::deque<CommandQueueEntry>::iterator CancelCommand(const std::deque<CommandQueueEntry>::iterator &iter)
{
    iter->response.emplace("success", false);
    iter->response.emplace("message", std::string("Error processing '") + iter->command + "' request. The operation was canceled.");
    DAP::EmitMessageWithLog(LOG_RESPONSE, iter->response);
    return GetCommandsQueue().erase(iter);
}

} // unnamed namespace

void CommandLoop()
{
    std::mutex &commandsMutex = GetCommandsMutex();
    std::condition_variable &commandsCV = GetCommandsCV();
    std::condition_variable &commandSyncCV = GetCommandSyncCV();
    std::deque<CommandQueueEntry> &commandsQueue = GetCommandsQueue();
    bool &commandSyncFlag = GetCommandSyncFlag();

    GetExit() = false;

    std::thread commandsWorker{CommandsWorker};

    // In server mode, the protocol is read from the loopback TCP connection (RemoteServer) instead of stdin.
    std::istream &input = Config::IsServerMode() ? RemoteServer::Instance().GetInputStream() : std::cin;

    while (!GetExit())
    {
        const std::string requestText = ReadData(input);
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

        CommandQueueEntry queueEntry;
        try
        {
            json request = json::parse(requestText);

            // Note: `queueEntry.response` is also read by the exception handlers below, so it must
            // stay consistent at every step. Fields are emplaced in the order the request is parsed,
            // so a failed parse leaves a partially filled but still valid response. Also note,
            // emplace() never overwrites an existing key, so the handlers below only add missing fields.
            queueEntry.response.emplace("type", "response");
            queueEntry.response.emplace("request_seq", request.at("seq"));
            // Note, the command name is moved out of the parsed request (it is not read again),
            // so no string copy is made.
            queueEntry.command = std::move(request.at("command").get_ref<std::string &>());
            queueEntry.response.emplace("command", queueEntry.command);

            const json &requestType = request.at("type");
            if (!requestType.is_string() || requestType.get_ref<const std::string &>() != "request")
            {
                throw bad_format("wrong request type!");
            }

            // The request is not used after this point, so the arguments are moved instead of copied.
            const auto argIter = request.find("arguments");
            if (argIter != request.cend())
            {
                queueEntry.arguments = std::move(argIter.value());
            }
            else
            {
                queueEntry.arguments = json::object();
            }

            // Pre command action.
            if (queueEntry.command == "initialize")
            {
                DAP::EmitCapabilitiesEvent();
            }
            else if (ContainsSortedName(gCancelCommandQueueSet, queueEntry.command))
            {
                const std::scoped_lock<std::mutex> guardCommandsMutex(commandsMutex);
                ManagedDebugger::CancelEvalRunning();

                for (auto iter = commandsQueue.begin(); iter != commandsQueue.end();)
                {
                    if (ContainsSortedName(gDebuggerSetupCommandSet, iter->command))
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

                // A reference avoids copying the json value; `queueEntry.arguments` is not modified here.
                const auto &requestId = queueEntry.arguments.at("requestId");
                std::unique_lock<std::mutex> lockCommandsMutex(commandsMutex);
                queueEntry.response.emplace("success", false);
                for (auto iter = commandsQueue.begin(); iter != commandsQueue.end(); ++iter)
                {
                    if (requestId != iter->response.at("request_seq"))
                    {
                        continue;
                    }

                    if (ContainsSortedName(gDebuggerSetupCommandSet, iter->command))
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

            // https://microsoft.github.io/debug-adapter-protocol/overview.html
            //
            // Initialization happens first, and the debug adapter must respond to the `initialize` request
            // with any capabilities before any further communication can take place. At any point after the
            // client receives the capabilities, it sends a `launch` or `attach` request.
            // ...
            // After the response to `configurationDone` is sent, the debug adapter may respond to the `launch`
            // or `attach` request, and then the debug session has started.

            // DNCDbg DAP initialization sequence:
            // `launch` and `attach` must be added to the commandsQueue only after `configurationDone`.
            // If they arrive after `initialize` but before `configurationDone`, hold them pending until
            // `configurationDone` is processed. This ensures the real attach/launch status is returned
            // at the proper time. Commands sent before `initialize` are a protocol violation and are
            // not held here; they fall through to the handler, which rejects them.
            const bool isLaunchOrAttach = (queueEntry.command == "launch" || queueEntry.command == "attach");
            if (isLaunchOrAttach && GetInitialized() && !GetConfigurationDone())
            {
                // Hold the command until `configurationDone` is processed. It will be flushed
                // into the commandsQueue by the `configurationDone` handling further below.
                const std::scoped_lock<std::mutex> guardCommandsMutex(commandsMutex);
                GetPendingLaunchAttachQueue().emplace_back(std::move(queueEntry));
                continue;
            }

            std::unique_lock<std::mutex> lockCommandsMutex(commandsMutex);
            commandSyncFlag = ContainsSortedName(gSyncCommandExecutionSet, queueEntry.command);
            commandsQueue.emplace_back(std::move(queueEntry));
            commandsCV.notify_one(); // notify_one with lock

            if (commandSyncFlag)
            {
                commandSyncCV.wait(lockCommandsMutex, [&commandSyncFlag] { return !commandSyncFlag; });
            }

            // After `configurationDone` is processed, flush pending `launch`/`attach`
            // commands one by one. Each is a sync command, so we wait for it to finish
            // before submitting the next one. This preserves the deadlock protection of
            // the sync execution mechanism (see the gSyncCommandExecutionSet comment)
            // and guarantees that the real attach/launch status is returned at the proper time.
            if (GetConfigurationDone())
            {
                std::deque<CommandQueueEntry> &pendingLaunchAttachQueue = GetPendingLaunchAttachQueue();
                while (!pendingLaunchAttachQueue.empty())
                {
                    CommandQueueEntry pending = std::move(pendingLaunchAttachQueue.front());
                    pendingLaunchAttachQueue.pop_front();

                    commandSyncFlag = true;
                    commandsQueue.emplace_back(std::move(pending));
                    commandsCV.notify_one(); // notify_one with lock

                    commandSyncCV.wait(lockCommandsMutex, [&commandSyncFlag] { return !commandSyncFlag; });
                }
            }

            continue;
        }
        // nlohmann::detail::exception (JSON parse/type errors), bad_format and any other
        // std::exception-derived error are converted into a failed response.
        catch (const std::exception &ex)
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
