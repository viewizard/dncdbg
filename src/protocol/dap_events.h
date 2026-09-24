// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef PROTOCOL_DAP_EVENTS_H
#define PROTOCOL_DAP_EVENTS_H

#include "types/types.h"
#include "types/protocol.h"
#include <string>

namespace dncdbg::DAP
{

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

} // namespace dncdbg::DAP

#endif // PROTOCOL_DAP_EVENTS_H
