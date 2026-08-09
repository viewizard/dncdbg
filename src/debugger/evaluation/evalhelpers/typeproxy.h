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

#include "utils/torelease.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace dncdbg
{

class EvalExec;

class TypeProxy
{
  public:

    TypeProxy(std::shared_ptr<EvalExec> &sharedEvalExec)
        : m_sharedEvalExec(sharedEvalExec)
    {
    }

    HRESULT GetDebuggerTypeProxyValue(ICorDebugThread *pThread, ICorDebugModule *pModule, ICorDebugValue *pFrontValue,
                                      ICorDebugType *pType, mdTypeDef currentTypeDef, ICorDebugValue **ppTypeProxyValue);

    HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule);

  private:

    std::shared_ptr<EvalExec> m_sharedEvalExec;

    std::mutex m_debuggerTypeProxyMutex;
    std::unordered_map<CORDB_ADDRESS, std::unordered_set<mdTypeDef>> m_debuggerTypeProxyCheckedTypes;
    struct DebuggerTypeProxyCache
    {
        CORDB_ADDRESS modAddress{0};
        mdMethodDef methodDef{mdMethodDefNil};
        uint32_t enclosingTypesParamCount{0};
    };
    std::unordered_map<CORDB_ADDRESS, std::unordered_map<mdTypeDef, DebuggerTypeProxyCache>> m_debuggerTypeProxyCache;
    std::unordered_map<CORDB_ADDRESS, ToRelease<ICorDebugModule>> m_debuggerTypeProxyModuleCache;

    HRESULT GetDebuggerTypeProxyValue(ICorDebugThread *pThread, ICorDebugModule *pModule, ICorDebugModule *pAttrModule,
                                      ICorDebugValue *pFrontValue, ICorDebugType *pType, mdTypeDef currentTypeDef,
                                      mdTypeDef proxyAttrTypeDef, const std::string &proxyTypeName, ICorDebugValue **ppTypeProxyValue);
    HRESULT GetCachedDebuggerTypeProxyValue(ICorDebugThread *pThread, ICorDebugModule *pModule, ICorDebugValue *pFrontValue, ICorDebugType *pType,
                                            mdTypeDef currentTypeDef, bool &typeChecked, ICorDebugValue **ppTypeProxyValue);
};

} // namespace dncdbg

#endif // DEBUGGER_EVALUATION_EVALHELPERS_TYPEPROXY_H
