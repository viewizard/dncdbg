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

#include "debuginfo/pdb.h"
#include "types/types.h"
#include <functional>

namespace dncdbg
{

class DebugInfo;

enum class FrameType : uint8_t
{
    Unknown,
    CLRNative,
    CLRInternal,
    CLRManaged,
    CLRManagedException,
    CLRManagedExceptionUser
};

using WalkFramesCallback = std::function<HRESULT(FrameType, ICorDebugFrame *, const PDB::SequencePoint *,
                                                 const std::string *, const std::string *)>;

HRESULT GetFrameAt(ICorDebugThread *pThread, FrameLevel level, DebugInfo *pDebugInfo, ICorDebugFrame **ppFrame);
const char *GetInternalTypeName(CorDebugInternalFrameType frameType);
HRESULT WalkFrames(ICorDebugThread *pThread, DebugInfo *pDebugInfo, const WalkFramesCallback &cb);

} // namespace dncdbg

#endif // DEBUGGER_FRAMES_H
