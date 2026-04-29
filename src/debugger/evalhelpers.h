// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALHELPERS_H
#define DEBUGGER_EVALHELPERS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace dncdbg
{

HRESULT DereferenceAndUnboxValue(ICorDebugValue *pValue, ICorDebugValue **ppOutputValue, BOOL *pIsNull = nullptr);

template <typename T, typename = typename std::enable_if_t<std::is_integral_v<T>>>
HRESULT GetIntegralValue(ICorDebugValue *pInputValue, T &value)
{
    HRESULT Status = S_OK;

    BOOL isNull = TRUE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));

    if (isNull == TRUE)
    {
        return E_FAIL;
    }

    uint32_t cbSize = 0;
    IfFailRet(trValue->GetSize(&cbSize));
    if (cbSize != sizeof(value))
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    IfFailRet(trGenericValue->GetValue(&value));
    return S_OK;
}

class EvalWaiter;

class EvalHelpers
{
  public:

    explicit EvalHelpers(std::shared_ptr<EvalWaiter> &sharedEvalWaiter)
        : m_sharedEvalWaiter(sharedEvalWaiter)
    {
    }

    HRESULT CreateTypeObjectStaticConstructor(ICorDebugThread *pThread, ICorDebugType *pType,
                                              ICorDebugValue **ppTypeObjectResult = nullptr,
                                              bool DetectStaticMembers = true);

    HRESULT EvalFunction(ICorDebugThread *pThread, ICorDebugFunction *pFunc, ICorDebugType *pArgType,
                         std::vector<ToRelease<ICorDebugType>> *pTrMethodGenericTypes,
                         ICorDebugValue **ppArgsValue, uint32_t argsValueCount,
                         ICorDebugValue **ppEvalResult, bool ignoreEvalFlags = false);

    HRESULT CreateLiteralFieldValue(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                                    UVCP_CONSTANT pRawValue, ULONG rawValueLength, ICorDebugValue **ppLiteralValue);

    HRESULT CreateLiteralLocalValue(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                                    ICorDebugValue **ppLiteralValue);

    HRESULT CreateString(ICorDebugThread *pThread, const std::string &value, ICorDebugValue **ppNewString);

    static HRESULT FindMethodInModule(ICorDebugThread *pThread, const std::string &moduleName, const WSTRING &className,
                                      const WSTRING &methodName, ICorDebugFunction **ppFunction);

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
    // Note, we also use handles (results of eval) in var refs during break (cleared at 'Continue').
    // Warning! Since we use `std::prev(m_typeObjectCache.end())` without any check in code, make sure cache size is `2` or bigger.
    static constexpr size_t m_typeObjectCacheSize = 100;
    // The idea of the cache is not to hold all type objects, but to prevent creating the same type objects numerous times during eval.
    // At access, elements are moved to the front of the list; new elements are also added to the front. In this way, unused elements are displaced from the cache.
    std::list<type_object_t> m_typeObjectCache;

    HRESULT TryReuseTypeObjectFromCache(ICorDebugType *pType, ICorDebugValue **ppTypeObjectResult);
    HRESULT AddTypeObjectToCache(ICorDebugType *pType, ICorDebugValue *pTypeObject);
    HRESULT CreateLiteralValueImpl(ICorDebugThread *pThread, PCCOR_SIGNATURE pSig, PCCOR_SIGNATURE pSigEnd,
                                   CorElementType underlyingType, UVCP_CONSTANT pRawValue, ULONG rawValueLength,
                                   ICorDebugValue **ppLiteralValue);
};

} // namespace dncdbg

#endif // DEBUGGER_EVALHELPERS_H
