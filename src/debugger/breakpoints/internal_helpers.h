// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_BREAKPOINTS_HELPERS_H
#define DEBUGGER_BREAKPOINTS_HELPERS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "utils/torelease.h"
#include <string>
#include <vector>

namespace dncdbg::BreakpointHelpers
{

// Note, this is the internal implementation of the breakpoints module.
// Do not use it outside of the breakpoints folder.

HRESULT IsSameFunctionBreakpoint(ICorDebugFunctionBreakpoint *pBreakpoint1, ICorDebugFunctionBreakpoint *pBreakpoint2);
HRESULT GetFunctionBreakpointModAddress(ICorDebugFunctionBreakpoint *pBreakpoint, CORDB_ADDRESS &modAddress);
HRESULT IsEnableByCondition(ICorDebugThread *pThread, const std::string &condition, std::string &output);
HRESULT SkipBreakpoint(ICorDebugModule *pModule, mdMethodDef methodToken);
HRESULT GetBreakpointNativeAddress(ICorDebugFunctionBreakpoint *pBreakpoint, CORDB_ADDRESS &nativeAddress);

// Shared registry of managed breakpoints (ICorDebugFunctionBreakpoint), keyed by
// "fully-qualified IL offset" (module address + method token + IL offset).
// Multiple breakpoints can point to the same location; in this case, the same
// ICorDebugFunctionBreakpoint object is shared (with reference counting).
HRESULT ActivateManagedBreakpoint(CORDB_ADDRESS modAddress, uint32_t methodToken, uint32_t ilOffset,
                                  ICorDebugModule *pModule, ICorDebugFunctionBreakpoint **ppFuncBreakpoint);
HRESULT DeactivateManagedBreakpoint(ToRelease<ICorDebugFunctionBreakpoint> &trFuncBreakpoint);

// Cleans up the BreakpointHelpers internal state. See Breakpoints::Cleanup().
void Cleanup();

} // namespace dncdbg::BreakpointHelpers

#endif // DEBUGGER_BREAKPOINTS_HELPERS_H
