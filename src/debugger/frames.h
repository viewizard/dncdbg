// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.
#pragma once

#pragma warning(disable : 4068) // Visual Studio should ignore GCC pragmas
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <cor.h>
#include <cordebug.h>
#pragma GCC diagnostic pop

#include "interfaces/types.h"
#include <functional>

namespace dncdbg
{

enum FrameType
{
    FrameUnknown,
    FrameNative,
    FrameCLRNative,
    FrameCLRInternal,
    FrameCLRManaged
};

struct NativeFrame
{
    std::uintptr_t addr = 0;
    bool unknownFrameAddr = false;
    std::string libName;
    std::string procName;
    std::string fullSourcePath;
    int lineNum = 0;
};

typedef std::function<HRESULT(FrameType, std::uintptr_t, ICorDebugFrame *, NativeFrame *)> WalkFramesCallback;

struct Thread;

HRESULT GetFrameAt(ICorDebugThread *pThread, FrameLevel level, ICorDebugFrame **ppFrame);
const char *GetInternalTypeName(CorDebugInternalFrameType frameType);
HRESULT WalkFrames(ICorDebugThread *pThread, WalkFramesCallback cb);

} // namespace dncdbg
