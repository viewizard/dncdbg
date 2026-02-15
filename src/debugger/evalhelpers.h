// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <list>
#include <memory>
#include <mutex>
#include <string>

namespace dncdbg
{

class Modules;
class EvalWaiter;

class EvalHelpers
{
  public:

    EvalHelpers(std::shared_ptr<Modules> &sharedModules, std::shared_ptr<EvalWaiter> &sharedEvalWaiter)
        : m_sharedModules(sharedModules),
          m_sharedEvalWaiter(sharedEvalWaiter),
          m_evalFlags(defaultEvalFlags)
    {}

    HRESULT CreatTypeObjectStaticConstructor(ICorDebugThread *pThread, ICorDebugType *pType,
                                             ICorDebugValue **ppTypeObjectResult = nullptr,
                                             bool DetectStaticMembers = true);

    HRESULT EvalFunction(ICorDebugThread *pThread, ICorDebugFunction *pFunc, ICorDebugType **ppArgsType,
                         uint32_t ArgsTypeCount, ICorDebugValue **ppArgsValue, uint32_t ArgsValueCount,
                         ICorDebugValue **ppEvalResult, bool ignoreEvalFlags = false);

    HRESULT EvalGenericFunction(ICorDebugThread *pThread, ICorDebugFunction *pFunc, ICorDebugType **ppArgsType,
                                uint32_t ArgsTypeCount, ICorDebugValue **ppArgsValue, uint32_t ArgsValueCount,
                                ICorDebugValue **ppEvalResult);

    HRESULT GetLiteralValue(ICorDebugThread *pThread, ICorDebugType *pType, ICorDebugModule *pModule,
                            PCCOR_SIGNATURE pSignatureBlob, ULONG sigBlobLength, UVCP_CONSTANT pRawValue,
                            ULONG rawValueLength, ICorDebugValue **ppLiteralValue);

    HRESULT CreateString(ICorDebugThread *pThread, const std::string &value, ICorDebugValue **ppNewString);

    HRESULT FindMethodInModule(const std::string &moduleName, const WSTRING &className, const WSTRING &methodName,
                               ICorDebugFunction **ppFunction);

    void SetEvalFlags(uint32_t evalFlags)
    {
        m_evalFlags = evalFlags;
    }

    void Cleanup();

  private:

    std::shared_ptr<Modules> m_sharedModules;
    std::shared_ptr<EvalWaiter> m_sharedEvalWaiter;
    uint32_t m_evalFlags;

    std::mutex m_pSuppressFinalizeMutex;
    ToRelease<ICorDebugFunction> m_pSuppressFinalize;

    struct type_object_t
    {
        COR_TYPEID id;
        ToRelease<ICorDebugHandleValue> typeObject;
    };

    std::mutex m_typeObjectCacheMutex;
    // Because handles affect the performance of the garbage collector, the debugger should limit itself to a relatively
    // small number of handles (about 256) that are active at a time.
    // https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/debugging/icordebugheapvalue2-createhandle-method
    // Note, we also use handles (results of eval) in var refs during brake (cleared at 'Continue').
    // Warning! Since we use `std::prev(m_typeObjectCache.end())` without any check in code, make sure cache size is `2` or bigger.
    static constexpr size_t m_typeObjectCacheSize = 100;
    // The idea of cache is not hold all type objects, but prevent numerous times same type objects creation during eval.
    // At access, element moved to front of list, new element also add to front. In this way, not used elements displaced from cache.
    std::list<type_object_t> m_typeObjectCache;

    HRESULT TryReuseTypeObjectFromCache(ICorDebugType *pType, ICorDebugValue **ppTypeObjectResult);
    HRESULT AddTypeObjectToCache(ICorDebugType *pType, ICorDebugValue *pTypeObject);
};

} // namespace dncdbg
