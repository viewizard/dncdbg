// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include "types/types.h"
#include "types/protocol.h"
#include <json/json.hpp>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace dncdbg
{

constexpr std::string_view TWO_CRLF("\r\n\r\n");
constexpr std::string_view CONTENT_LENGTH("Content-Length: ");
constexpr std::string_view LOG_COMMAND("-> (C) ");
constexpr std::string_view LOG_RESPONSE("<- (R) ");
constexpr std::string_view LOG_EVENT("<- (E) ");

void to_json(nlohmann::json &j, const Source &s);
void to_json(nlohmann::json &j, const Breakpoint &b);
void to_json(nlohmann::json &j, const StackFrame &f);
void to_json(nlohmann::json &j, const Thread &t);
void to_json(nlohmann::json &j, const Scope &s);
void to_json(nlohmann::json &j, const Variable &v);

class DAPIO
{
  public:

    static void SetupProtocolLogging(const std::string &path);

    static const std::unordered_map<std::string, ExceptionBreakpointFilter> &GetExceptionFilters();
    static void AddCapabilitiesTo(nlohmann::json &capabilities);

    static void EmitProcessEvent(PID, const std::string &argv0);
    static void EmitStoppedEvent(const StoppedEvent &event);
    static void EmitExitedEvent(const ExitedEvent &event);
    static void EmitTerminatedEvent();
    static void EmitContinuedEvent(ThreadId threadId);
    static void EmitThreadEvent(const ThreadEvent &event);
    static void EmitModuleEvent(const ModuleEvent &event);
    static void EmitOutputEvent(const OutputEvent &event);
    static void EmitBreakpointEvent(const BreakpointEvent &event);
    static void EmitInitializedEvent();
    static void EmitCapabilitiesEvent();

    static void EmitMessageWithLog(const std::string_view &message_prefix, nlohmann::json &message);
    static void Log(const std::string_view &prefix, const std::string &text);

  private:

    static std::mutex m_outMutex;
    static std::ofstream m_protocolLog;
    static uint64_t m_seqCounter; // Note, this counter must be covered by m_outMutex.

    static void EmitMessage(nlohmann::json &message, std::string &output);
    static void EmitEvent(const std::string &name, const nlohmann::json &body);
    static void LogInternal(const std::string_view &prefix, const std::string &text);
};

} // namespace dncdbg
