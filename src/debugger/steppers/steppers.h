// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_STEPPERS_STEPPERS_H
#define DEBUGGER_STEPPERS_STEPPERS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"

// Facade for the stepping functionality. Hides all stepping-related implementation
// details (SimpleStepper, AsyncStepper, etc.) from the rest of the debugger code.

namespace dncdbg::Steppers
{

HRESULT SetupStep(ICorDebugThread *pThread, StepType stepType);

// Important! Callbacks related methods must control return for succeeded return code.
// Do not allow debugger API return succeeded (uncontrolled) return code.
// Bad :
//     return pThread->GetID(&threadId);
// Good:
//     IfFailRet(pThread->GetID(&threadId));
//     return S_OK;
HRESULT ManagedCallbackBreakpoint(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread);
HRESULT ManagedCallbackStepComplete(ICorDebugThread *pThread, CorDebugStepReason reason);

HRESULT DisableAll(ICorDebugProcess *pProcess);
HRESULT DisableAll(ICorDebugAppDomain *pAppDomain);
HRESULT DisableAllSimpleSteppers(ICorDebugProcess *pProcess);

void SetJustMyCode(bool enable);
void SetStepFiltering(bool enable);

// Cleans up the Steppers internal state. See ManagedDebugger::Cleanup().
void Cleanup();

} // namespace dncdbg::Steppers

#endif // DEBUGGER_STEPPERS_STEPPERS_H
