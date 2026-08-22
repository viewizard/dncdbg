// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_EVALEXEC_H
#define DEBUGGER_EVALUATION_EVALEXEC_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dncdbg
{

class EvalWaiter;

class EvalExec
{
  public:

    explicit EvalExec(std::shared_ptr<EvalWaiter> &sharedEvalWaiter)
        : m_sharedEvalWaiter(sharedEvalWaiter)
    {
    }

    HRESULT CallFunction(ICorDebugThread *pThread, ICorDebugFunction *pFunc, ICorDebugType *pArgType,
                         std::vector<ToRelease<ICorDebugType>> *pTrMethodGenericTypes,
                         ICorDebugValue **ppArgsValue, uint32_t argsValueCount,
                         FormatSpecifier specifier, ICorDebugValue **ppEvalResult);

    HRESULT CallConstructor(ICorDebugThread *pThread, ICorDebugFunction *pConstrFunc,
                            std::vector<ToRelease<ICorDebugType>> &trTypeParams,
                            ICorDebugValue **ppArgsValue, uint32_t argsValueCount,
                            ICorDebugValue **ppEvalResult);

    HRESULT CreateTypeObject(ICorDebugThread *pThread, ICorDebugType *pType, ICorDebugValue **ppTypeObjectResult = nullptr);

    HRESULT CreateArray(ICorDebugThread *pThread, ICorDebugType *pElementType,
                        std::vector<uint32_t> &dimensions, ICorDebugValue **ppEvalResult);

    HRESULT CreateLiteralFieldValue(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd, UVCP_CONSTANT pRawValue,
                                    ULONG rawValueLength, ICorDebugValue **ppLiteralValue, std::string &realDisplayTypeName);

    HRESULT CreateLiteralLocalValue(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                                    ICorDebugValue **ppLiteralValue, std::string &realDisplayTypeName);

    HRESULT CreateString(ICorDebugThread *pThread, const std::string &value, ICorDebugValue **ppNewString);

    HRESULT CreateValueType(ICorDebugThread *pThread, ICorDebugClass *pValueTypeClass, void *valueData, ICorDebugValue **ppValue);

    [[nodiscard]] uint32_t GetEvalFlags() const
    {
        return m_evalFlags;
    }
    void SetEvalFlags(uint32_t evalFlags)
    {
        m_evalFlags = evalFlags;
    }

    void Cleanup();

  private:

    std::shared_ptr<EvalWaiter> m_sharedEvalWaiter;
    uint32_t m_evalFlags{defaultEvalFlags};

    std::mutex m_trSuppressFinalizeMutex;
    ToRelease<ICorDebugFunction> m_trSuppressFinalize;

    struct type_object_t
    {
        COR_TYPEID m_TypeID;
        ToRelease<ICorDebugHandleValue> m_trTypeObject;
    };

    std::mutex m_typeObjectCacheMutex;
    // Because handles affect the performance of the garbage collector, the debugger should limit itself to a relatively
    // small number of handles (about 256) that are active at a time.
    // https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/debugging/icordebugheapvalue2-createhandle-method
    // Note: we also use handles (results of eval) in var refs during break (cleared at 'Continue').
    // Warning! Since we use `std::prev(m_typeObjectCache.end())` without any check in the code, make sure the cache size is `2` or bigger.
    static constexpr size_t m_typeObjectCacheSize = 100;
    // The idea of the cache is not to hold all type objects, but to prevent creating the same type objects numerous times during eval.
    // On access, elements are moved to the front of the list; new elements are also added to the front. In this way, unused elements are displaced from the cache.
    std::list<type_object_t> m_typeObjectCache;

    HRESULT TryReuseTypeObjectFromCache(ICorDebugType *pType, ICorDebugValue **ppTypeObjectResult);
    HRESULT AddTypeObjectToCache(ICorDebugType *pType, ICorDebugValue *pTypeObject);
    HRESULT CreateLiteralValueImpl(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                                   CorElementType underlyingType, UVCP_CONSTANT pRawValue, ULONG rawValueLength,
                                   ICorDebugValue **ppLiteralValue, std::string &realDisplayTypeName,
                                   bool valueInlineInSig = false);
};

} // namespace dncdbg

#endif // DEBUGGER_EVALUATION_EVALEXEC_H
