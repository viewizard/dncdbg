// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.
#pragma once

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <list>
#include <mutex>
#include <string>

#include "interfaces/types.h"
#include "utils/string_view.h"
#include "json/json.hpp"

namespace dncdbg
{

class ManagedDebugger;

class DAP
{
  public:

    DAP(std::istream &input, std::ostream &output)
        : m_exit(false),
          m_sharedDebugger(nullptr),
          cin(input),
          cout(output),
          m_protocolMessagesOutput(LogNone),
          m_seqCounter(1)
    {
    }
    void SetDebugger(std::shared_ptr<ManagedDebugger> &sharedDebugger)
    {
        m_sharedDebugger = sharedDebugger;
    }
    void ProtocolMessages(const std::string &path);
    void SetLaunchCommand(const std::string &fileExec, const std::vector<std::string> &args)
    {
        m_fileExec = fileExec;
        m_execArgs = args;
    }

    void EmitExecEvent(PID, const std::string &argv0);
    void EmitStoppedEvent(const StoppedEvent &event);
    void EmitExitedEvent(const ExitedEvent &event);
    void EmitTerminatedEvent();
    void EmitContinuedEvent(ThreadId threadId);
    void EmitThreadEvent(const ThreadEvent &event);
    void EmitModuleEvent(const ModuleEvent &event);
    void EmitOutputEvent(OutputCategory category, const Utility::string_view &output, DWORD threadId = 0);
    void EmitBreakpointEvent(const BreakpointEvent &event);
    void Cleanup();
    void CommandLoop();

    void EmitInitializedEvent();
    void EmitCapabilitiesEvent();

  private:

    std::atomic<bool> m_exit;
    std::shared_ptr<ManagedDebugger> m_sharedDebugger;

    // File streams used to read commands and write responses.
    std::istream &cin;
    std::ostream &cout;

    std::mutex m_outMutex;
    enum
    {
        LogNone,
        LogConsole,
        LogFile
    } m_protocolMessagesOutput;
    std::ofstream m_protocolMessagesLog;
    uint64_t m_seqCounter; // Note, this counter must be covered by m_outMutex.

    std::string m_fileExec;
    std::vector<std::string> m_execArgs;

    void EmitMessage(nlohmann::json &message, std::string &output);
    void EmitMessageWithLog(const std::string &message_prefix, nlohmann::json &message);
    void EmitEvent(const std::string &name, const nlohmann::json &body);

    void Log(const std::string &prefix, const std::string &text);

    struct CommandQueueEntry
    {
        std::string command;
        nlohmann::json arguments;
        nlohmann::json response;
    };

    std::mutex m_commandsMutex;
    std::condition_variable m_commandsCV;
    std::condition_variable m_commandSyncCV;
    std::list<CommandQueueEntry> m_commandsQueue;

    void CommandsWorker();
    std::list<CommandQueueEntry>::iterator CancelCommand(const std::list<CommandQueueEntry>::iterator &iter);
};

} // namespace dncdbg
