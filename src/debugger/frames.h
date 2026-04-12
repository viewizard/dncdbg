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

#include "types/types.h"
#include <functional>

namespace dncdbg
{

enum class FrameType : uint8_t
{
    Unknown,
    CLRNative,
    CLRInternal,
    CLRManaged
};

using WalkFramesCallback = std::function<HRESULT(FrameType, ICorDebugFrame *)>;

HRESULT GetFrameAt(ICorDebugThread *pThread, FrameLevel level, ICorDebugFrame **ppFrame);
const char *GetInternalTypeName(CorDebugInternalFrameType frameType);
HRESULT WalkFrames(ICorDebugThread *pThread, const WalkFramesCallback &cb);

} // namespace dncdbg

#endif // DEBUGGER_FRAMES_H
