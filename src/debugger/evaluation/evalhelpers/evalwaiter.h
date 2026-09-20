// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_EVALWAITER_H
#define DEBUGGER_EVALUATION_EVALWAITER_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include <functional>

namespace dncdbg::EvalWaiter
{

using WaitEvalResultCallback = std::function<HRESULT(ICorDebugEval *)>;

// Cleans up the EvalWaiter internal state. See ManagedDebugger::Cleanup().
void Cleanup();

bool IsEvalRunning();
void CancelEvalRunning();

HRESULT WaitEvalResult(ICorDebugThread *pThread, ICorDebugValue **ppEvalResult, const WaitEvalResultCallback &cbSetupEval);

// Should be called by ICorDebugManagedCallback.
void NotifyEvalComplete(ICorDebugThread *pThread, ICorDebugEval *pEval);
HRESULT ManagedCallbackCustomNotification(ICorDebugThread *pThread);
HRESULT SetupCrossThreadDependencyNotificationClass(ICorDebugModule *pModule);

} // namespace dncdbg::EvalWaiter

#endif // DEBUGGER_EVALUATION_EVALWAITER_H
