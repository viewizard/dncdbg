// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_WALKERS_H
#define DEBUGGER_EVALUATION_WALKERS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debugger/evaluation/walkers/types.h"
#include "types/types.h"
#include "utils/utf.h"
#include <unordered_set>

namespace dncdbg::Walkers
{

HRESULT WalkIndexers(ICorDebugType *pInputType, const WalkIndexersCallback &cb);

HRESULT WalkMembers(ICorDebugValue *pInputValue, ICorDebugThread *pThread, FrameLevel frameLevel,
                    bool provideSetterData, FormatSpecifier specifier, const WalkMembersCallback &cb);

HRESULT WalkMethods(ICorDebugValue *pInputTypeValue, bool walkBaseType, const WalkMethodsCallback &cb);
HRESULT WalkMethods(ICorDebugType *pInputType, bool walkBaseType, ICorDebugType **ppResultType,
                    const WalkMethodsCallback &cb);
HRESULT WalkExtensionMethods(ICorDebugType *pInputType, CorElementType elemType, const WalkMethodsCallback &cb);

HRESULT WalkGeneratedClassFields(IMetaDataImport *pMDImport, ICorDebugValue *pInputValue, uint32_t currentIlOffset,
                                 std::unordered_set<WSTRING> &usedNames, mdMethodDef methodDef,
                                 ICorDebugModule *pModule, const WalkStackVarsCallback &cb);

HRESULT WalkStackVars(ICorDebugThread *pThread, FrameLevel frameLevel, const WalkStackVarsCallback &cb);

// Should be called by ICorDebugManagedCallback.
HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule);
HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule);

// Cleans up the Walkers internal state. See ManagedDebugger::Cleanup().
void Cleanup();

} // namespace dncdbg::Walkers

#endif // DEBUGGER_EVALUATION_WALKERS_H
