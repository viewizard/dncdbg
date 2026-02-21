// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include "types/types.h"
#include "types/protocol.h"
#include <json/json.hpp>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <list>
#include <mutex>
#include <string>
#include <string_view>

namespace dncdbg
{

class ManagedDebugger;

class DAP
{
  public:

    DAP()
        : m_exit(false),
          m_sharedDebugger(nullptr)
    {
    }

    void CreateManagedDebugger();
    void SetLaunchCommand(const std::string &fileExec, const std::vector<std::string> &args)
    {
        m_fileExec = fileExec;
        m_execArgs = args;
    }
    void CommandLoop();

    HRESULT HandleCommand(const std::string &command, const nlohmann::json &arguments, nlohmann::json &body);
    HRESULT HandleCommandJSON(const std::string &command, const nlohmann::json &arguments, nlohmann::json &body);

  private:

    std::atomic<bool> m_exit;
    std::shared_ptr<ManagedDebugger> m_sharedDebugger;

    std::string m_fileExec;
    std::vector<std::string> m_execArgs;

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
