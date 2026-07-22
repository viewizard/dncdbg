// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_FRAMES_H
#define DEBUGGER_FRAMES_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/protocol.h"
#include <functional>

namespace dncdbg
{

class DebugInfo;

HRESULT GetFrameAt(ICorDebugThread *pThread, FrameLevel level, DebugInfo *pDebugInfo, bool justMyCode, ICorDebugFrame **ppFrame);
HRESULT GetStackFrames(ICorDebugThread *pThread, ThreadId threadId, FrameLevel startFrame, unsigned maxFrames,
                       DebugInfo *pDebugInfo, bool justMyCode, std::vector<StackFrame> &stackFrames);

} // namespace dncdbg

#endif // DEBUGGER_FRAMES_H
