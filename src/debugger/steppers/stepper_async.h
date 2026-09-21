// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_STEPPERS_STEPPER_ASYNC_H
#define DEBUGGER_STEPPERS_STEPPER_ASYNC_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"

namespace dncdbg::AsyncStepper
{

HRESULT SetupStep(ICorDebugThread *pThread, StepType stepType);

// Important! Callbacks related methods must control return for succeeded return code.
// Do not allow debugger API return succeeded (uncontrolled) return code.
// Bad :
//     return pThread->GetID(&threadId);
// Good:
//     IfFailRet(pThread->GetID(&threadId));
//     return S_OK;
HRESULT ManagedCallbackBreakpoint(ICorDebugThread *pThread);
HRESULT ManagedCallbackStepComplete();

HRESULT DisableAllSteppers();

// Cleans up the AsyncStepper internal state. See Steppers::Cleanup().
void Cleanup();

} // namespace dncdbg::AsyncStepper

#endif // DEBUGGER_STEPPERS_STEPPER_ASYNC_H
