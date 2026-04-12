// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_BREAKPOINTS_BREAKPOINTUTILS_H
#define DEBUGGER_BREAKPOINTS_BREAKPOINTUTILS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include <string>

namespace dncdbg
{

class Variables;

namespace BreakpointUtils
{
HRESULT IsSameFunctionBreakpoint(ICorDebugFunctionBreakpoint *pBreakpoint1, ICorDebugFunctionBreakpoint *pBreakpoint2);
HRESULT GetFunctionBreakpointModAddress(ICorDebugFunctionBreakpoint *pBreakpoint, CORDB_ADDRESS &modAddress);
HRESULT IsEnableByCondition(const std::string &condition, Variables *pVariables, ICorDebugThread *pThread, std::string &output);
HRESULT SkipBreakpoint(ICorDebugModule *pModule, mdMethodDef methodToken, bool justMyCode);
} // namespace BreakpointUtils

} // namespace dncdbg

#endif // DEBUGGER_BREAKPOINTS_BREAKPOINTUTILS_H
