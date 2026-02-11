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

#include "types/types.h"
#include "utils/torelease.h"
#include <functional>
#include <vector>

namespace dncdbg
{

class Modules;
class EvalHelpers;
class EvalStackMachine;

class Evaluator
{
  public:

    struct ArgElementType
    {
        CorElementType corType = ELEMENT_TYPE_MAX;
        std::string typeName;

        ArgElementType()
            : corType(ELEMENT_TYPE_MAX)
        {}

        ArgElementType(CorElementType t, std::string n)
        {
            corType = t;
            typeName = n;
        }

        static bool isAlias(const CorElementType type1, const CorElementType type2, const std::string &name2);
        bool areEqual(const ArgElementType &arg) const;
        inline bool operator==(const ArgElementType &arg)
        {
            return areEqual(arg);
        }
        inline bool operator!=(const ArgElementType &arg)
        {
            return !areEqual(arg);
        }
    };

    using ReturnElementType = ArgElementType;

    struct SetterData
    {
        ToRelease<ICorDebugValue> thisValue;
        ToRelease<ICorDebugType> propertyType;
        ToRelease<ICorDebugFunction> setterFunction;

        SetterData(ICorDebugValue *pValue, ICorDebugType *pType, ICorDebugFunction *pFunction)
        {
            Set(pValue, pType, pFunction);
        };

        SetterData(SetterData &setterData)
        {
            Set(setterData.thisValue.GetPtr(), setterData.propertyType.GetPtr(), setterData.setterFunction.GetPtr());
        };

        void Set(ICorDebugValue *pValue, ICorDebugType *pType, ICorDebugFunction *pFunction)
        {
            if (pValue)
                pValue->AddRef();
            thisValue = pValue;

            if (pType)
                pType->AddRef();
            propertyType = pType;

            if (pFunction)
                pFunction->AddRef();
            setterFunction = pFunction;
        }
    };

    using GetValueCallback = std::function<HRESULT(ICorDebugValue **, int)>;
    using WalkMembersCallback = std::function<HRESULT(ICorDebugType *, bool, const std::string &, const GetValueCallback &, SetterData *)>;
    using WalkStackVarsCallback = std::function<HRESULT(const std::string &, const GetValueCallback &)>;
    using GetFunctionCallback = std::function<HRESULT(ICorDebugFunction **)>;
    using WalkMethodsCallback = std::function<HRESULT(bool, const std::string &, ReturnElementType &, std::vector<ArgElementType> &, GetFunctionCallback)>;

    Evaluator(std::shared_ptr<Modules> &sharedModules,
              std::shared_ptr<EvalHelpers> &sharedEvalHelpers,
              std::shared_ptr<EvalStackMachine> &sharedEvalStackMachine)
        : m_sharedModules(sharedModules),
          m_sharedEvalHelpers(sharedEvalHelpers),
          m_sharedEvalStackMachine(sharedEvalStackMachine)
    {}

    HRESULT ResolveIdentifiers(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pInputValue,
                               SetterData *inputSetterData, std::vector<std::string> &identifiers,
                               ICorDebugValue **ppResultValue, std::unique_ptr<SetterData> *resultSetterData,
                               ICorDebugType **ppResultType, uint32_t evalFlags);

    HRESULT WalkMembers(ICorDebugValue *pInputValue, ICorDebugThread *pThread, FrameLevel frameLevel,
                        ICorDebugType *pTypeCast, bool provideSetterData, WalkMembersCallback cb);

    HRESULT WalkStackVars(ICorDebugThread *pThread, FrameLevel frameLevel, const WalkStackVarsCallback &cb);

    static HRESULT GetMethodClass(ICorDebugThread *pThread, FrameLevel frameLevel, std::string &methodClass, bool &thisParam);

    HRESULT LookupExtensionMethods(ICorDebugType *pType, const std::string &methodName,
                                   std::vector<Evaluator::ArgElementType> &methodArgs,
                                   std::vector<Evaluator::ArgElementType> &methodGenerics,
                                   ICorDebugFunction **ppCorFunc);

    HRESULT FollowNestedFindType(ICorDebugThread *pThread, const std::string &methodClass,
                                 std::vector<std::string> &identifiers, ICorDebugType **ppResultType);

    HRESULT FollowFields(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pValue,
                         ValueKind valueKind, std::vector<std::string> &identifiers, int nextIdentifier,
                         ICorDebugValue **ppResult, std::unique_ptr<Evaluator::SetterData> *resultSetterData,
                         uint32_t evalFlags);

    HRESULT FollowNestedFindValue(ICorDebugThread *pThread, FrameLevel frameLevel, const std::string &methodClass,
                                  std::vector<std::string> &identifiers, ICorDebugValue **ppResult,
                                  std::unique_ptr<Evaluator::SetterData> *resultSetterData, uint32_t evalFlags);

    static HRESULT GetElement(ICorDebugValue *pInputValue, std::vector<uint32_t> &indexes, ICorDebugValue **ppResultValue);
    HRESULT WalkMethods(ICorDebugType *pInputType, ICorDebugType **ppResultType,
                        std::vector<Evaluator::ArgElementType> &methodGenerics, const WalkMethodsCallback &cb);
    HRESULT WalkMethods(ICorDebugValue *pInputTypeValue, const WalkMethodsCallback &cb);
    HRESULT SetValue(ICorDebugThread *pThread, FrameLevel frameLevel, ToRelease<ICorDebugValue> &iCorPrevValue,
                     const GetValueCallback *getValue, SetterData *setterData, const std::string &value, uint32_t evalFlags,
                     std::string &output);

    static ArgElementType GetElementTypeByTypeName(const std::string &typeName);

  private:

    std::shared_ptr<Modules> m_sharedModules;
    std::shared_ptr<EvalHelpers> m_sharedEvalHelpers;
    std::shared_ptr<EvalStackMachine> m_sharedEvalStackMachine;
};

} // namespace dncdbg
