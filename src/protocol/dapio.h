// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef PROTOCOL_DAPIO_H
#define PROTOCOL_DAPIO_H

#include "types/types.h"
#include "types/protocol.h"
#include <json/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

namespace dncdbg::DAPIO
{

void SetupProtocolLogging(const std::string &path);

void EmitProcessEvent(DWORD processId, const std::string &name, StartMethod startMethod);
void EmitStoppedEvent(const StoppedEvent &event);
void EmitExitedEvent(const ExitedEvent &event);
void EmitTerminatedEvent();
void EmitContinuedEvent(ThreadId threadId, bool singleThread);
void EmitThreadEvent(const ThreadEvent &event);
void EmitModuleEvent(const ModuleEvent &event);
void EmitLoadedSourceEvent(const LoadedSourceEvent &event);
void EmitOutputEvent(const OutputEvent &event);
void EmitBreakpointEvent(const BreakpointEvent &event);
void EmitInitializedEvent();
void EmitCapabilitiesEvent();

void EmitMessageWithLog(std::string_view message_prefix, nlohmann::json &message);
void Log(std::string_view prefix, const std::string &text);

} // namespace dncdbg::DAPIO

#endif // PROTOCOL_DAPIO_H
