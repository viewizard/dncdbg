// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_EVALHELPERS_TYPEPROXY_H
#define DEBUGGER_EVALUATION_EVALHELPERS_TYPEPROXY_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

namespace dncdbg::TypeProxy
{

HRESULT GetDebuggerTypeProxyValue(ICorDebugThread *pThread, ICorDebugModule *pModule, ICorDebugValue *pFrontValue,
                                  ICorDebugType *pType, mdTypeDef currentTypeDef, ICorDebugValue **ppTypeProxyValue);

// Should be called by ICorDebugManagedCallback.
HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule);

// Cleans up the TypeProxy internal state. See ManagedDebugger::Cleanup().
void Cleanup();

} // namespace dncdbg::TypeProxy

#endif // DEBUGGER_EVALUATION_EVALHELPERS_TYPEPROXY_H
