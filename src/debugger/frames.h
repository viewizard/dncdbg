// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.
#pragma once

#include <cor.h>
#include <cordebug.h>

#include "interfaces/types.h"
#include <functional>

namespace dncdbg
{

enum FrameType
{
    FrameUnknown,
    FrameCLRNative,
    FrameCLRInternal,
    FrameCLRManaged
};

typedef std::function<HRESULT(FrameType, ICorDebugFrame *)> WalkFramesCallback;

HRESULT GetFrameAt(ICorDebugThread *pThread, FrameLevel level, ICorDebugFrame **ppFrame);
const char *GetInternalTypeName(CorDebugInternalFrameType frameType);
HRESULT WalkFrames(ICorDebugThread *pThread, const WalkFramesCallback &cb);

} // namespace dncdbg
