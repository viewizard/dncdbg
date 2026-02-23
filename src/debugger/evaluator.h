// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "metadata/sigparse.h"
#include "types/types.h"
#include "utils/torelease.h"
#include <functional>
#include <memory>
#include <vector>

namespace dncdbg
{

class DebugInfo;
class EvalHelpers;
class EvalStackMachine;

class Evaluator
{
  public:

    struct SetterData
    {
        ToRelease<ICorDebugValue> trThisValue;
        ToRelease<ICorDebugType> trPropertyType;
        ToRelease<ICorDebugFunction> trSetterFunction;

        SetterData(ICorDebugValue *pValue, ICorDebugType *pType, ICorDebugFunction *pFunction)
        {
            Set(pValue, pType, pFunction);
        };

        SetterData(SetterData &setterData)
        {
            Set(setterData.trThisValue.GetPtr(), setterData.trPropertyType.GetPtr(), setterData.trSetterFunction.GetPtr());
        };

        void Set(ICorDebugValue *pValue, ICorDebugType *pType, ICorDebugFunction *pFunction)
        {
            if (pValue)
                pValue->AddRef();
            trThisValue = pValue;

            if (pType)
                pType->AddRef();
            trPropertyType = pType;

            if (pFunction)
                pFunction->AddRef();
            trSetterFunction = pFunction;
        }
    };

    using GetValueCallback = std::function<HRESULT(ICorDebugValue **, bool)>;
    using WalkMembersCallback = std::function<HRESULT(ICorDebugType *, bool, const std::string &, const GetValueCallback &, SetterData *)>;
    using WalkStackVarsCallback = std::function<HRESULT(const std::string &, const GetValueCallback &)>;
    using GetFunctionCallback = std::function<HRESULT(ICorDebugFunction **)>;
    using ReturnElementType = SigElementType;
    using WalkMethodsCallback = std::function<HRESULT(bool, const std::string &, ReturnElementType &, std::vector<SigElementType> &, GetFunctionCallback)>;

    Evaluator(std::shared_ptr<DebugInfo> &sharedDebugInfo,
              std::shared_ptr<EvalHelpers> &sharedEvalHelpers,
              std::shared_ptr<EvalStackMachine> &sharedEvalStackMachine)
        : m_sharedDebugInfo(sharedDebugInfo),
          m_sharedEvalHelpers(sharedEvalHelpers),
          m_sharedEvalStackMachine(sharedEvalStackMachine)
    {}

    HRESULT ResolveIdentifiers(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pInputValue,
                               SetterData *inputSetterData, std::vector<std::string> &identifiers,
                               ICorDebugValue **ppResultValue, std::unique_ptr<SetterData> *resultSetterData,
                               ICorDebugType **ppResultType);

    HRESULT WalkMembers(ICorDebugValue *pInputValue, ICorDebugThread *pThread, FrameLevel frameLevel,
                        ICorDebugType *pTypeCast, bool provideSetterData, WalkMembersCallback cb);

    HRESULT WalkStackVars(ICorDebugThread *pThread, FrameLevel frameLevel, const WalkStackVarsCallback &cb);

    static HRESULT GetMethodClass(ICorDebugThread *pThread, FrameLevel frameLevel, std::string &methodClass, bool &haveThis);

    HRESULT LookupExtensionMethods(ICorDebugType *pType, const std::string &methodName,
                                   std::vector<SigElementType> &methodArgs,
                                   std::vector<SigElementType> &methodGenerics,
                                   ICorDebugFunction **ppCorFunc);

    HRESULT FollowNestedFindType(ICorDebugThread *pThread, const std::string &methodClass,
                                 std::vector<std::string> &identifiers, ICorDebugType **ppResultType);

    HRESULT FollowFields(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pValue,
                         ValueKind valueKind, std::vector<std::string> &identifiers, int nextIdentifier,
                         ICorDebugValue **ppResult, std::unique_ptr<Evaluator::SetterData> *resultSetterData);

    HRESULT FollowNestedFindValue(ICorDebugThread *pThread, FrameLevel frameLevel, const std::string &methodClass,
                                  std::vector<std::string> &identifiers, ICorDebugValue **ppResult,
                                  std::unique_ptr<Evaluator::SetterData> *resultSetterData);

    static HRESULT GetElement(ICorDebugValue *pInputValue, std::vector<uint32_t> &indexes, ICorDebugValue **ppResultValue);
    HRESULT WalkMethods(ICorDebugType *pInputType, ICorDebugType **ppResultType,
                        std::vector<SigElementType> &methodGenerics, const WalkMethodsCallback &cb);
    HRESULT WalkMethods(ICorDebugValue *pInputTypeValue, const WalkMethodsCallback &cb);
    HRESULT SetValue(ICorDebugThread *pThread, FrameLevel frameLevel, ToRelease<ICorDebugValue> &trPrevValue,
                     const GetValueCallback *getValue, SetterData *setterData, const std::string &value,
                     std::string &output);

    static SigElementType GetElementTypeByTypeName(const std::string &typeName);

  private:

    std::shared_ptr<DebugInfo> m_sharedDebugInfo;
    std::shared_ptr<EvalHelpers> m_sharedEvalHelpers;
    std::shared_ptr<EvalStackMachine> m_sharedEvalStackMachine;
};

} // namespace dncdbg
