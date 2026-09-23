// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef PROTOCOL_DAPIO_H
#define PROTOCOL_DAPIO_H

#include "types/types.h"
#include "types/protocol.h"
#include <json/json.hpp>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace dncdbg
{

class DAPIO
{
  public:

    static void SetupProtocolLogging(const std::string &path);

    static const std::unordered_map<std::string, ExceptionBreakpointFilter> &GetExceptionFilters();
    static void AddCapabilitiesTo(nlohmann::json &capabilities);

    static void EmitProcessEvent(DWORD processId, const std::string &name, StartMethod startMethod);
    static void EmitStoppedEvent(const StoppedEvent &event);
    static void EmitExitedEvent(const ExitedEvent &event);
    static void EmitTerminatedEvent();
    static void EmitContinuedEvent(ThreadId threadId, bool singleThread);
    static void EmitThreadEvent(const ThreadEvent &event);
    static void EmitModuleEvent(const ModuleEvent &event);
    static void EmitLoadedSourceEvent(const LoadedSourceEvent &event);
    static void EmitOutputEvent(const OutputEvent &event);
    static void EmitBreakpointEvent(const BreakpointEvent &event);
    static void EmitInitializedEvent();
    static void EmitCapabilitiesEvent();

    static void EmitMessageWithLog(std::string_view message_prefix, nlohmann::json &message);
    static void Log(std::string_view prefix, const std::string &text);

  private:

    // Prevent undefined behavior with static std::ofstream field usage, since it can throw in constructor.
    static std::ofstream &GetProtocolLog()
    {
        static std::ofstream protocolLog;
        return protocolLog;
    }

    static std::mutex m_outMutex;
    static uint64_t m_seqCounter; // Note, this counter must be covered by m_outMutex.

    static void EmitMessage(nlohmann::json &message, std::string &output);
    static void EmitEvent(const std::string &name, const nlohmann::json &body);
    static void LogInternal(std::string_view prefix, const std::string &text);
};

} // namespace dncdbg

#endif // PROTOCOL_DAPIO_H
