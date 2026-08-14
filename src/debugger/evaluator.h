// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATOR_H
#define DEBUGGER_EVALUATOR_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debugger/evalhelpers.h"
#include "debuginfo/pdb.h"
#include "metadata/sigparse.h"
#include "types/types.h"
#include "utils/torelease.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace dncdbg
{

class DebugInfo;
class EvalExec;
class EvalStackMachine;
class TypeProxy;

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

        SetterData(SetterData &&) = delete;
        SetterData(const SetterData &) = delete;
        SetterData &operator=(SetterData &&) = delete;
        SetterData &operator=(const SetterData &) = delete;
        ~SetterData() = default;

        void Set(ICorDebugValue *pValue, ICorDebugType *pType, ICorDebugFunction *pFunction)
        {
            if (pValue != nullptr)
            {
                pValue->AddRef();
            }
            trThisValue = pValue;

            if (pType != nullptr)
            {
                pType->AddRef();
            }
            trPropertyType = pType;

            if (pFunction != nullptr)
            {
                pFunction->AddRef();
            }
            trSetterFunction = pFunction;
        }
    };

    using GetValueCallback = std::function<HRESULT(ICorDebugValue **, std::string *)>;
    using WalkMembersCallback = std::function<HRESULT(ICorDebugType *, bool, const std::string &, const GetValueCallback &,
                                                      SetterData *, std::string *)>;
    using WalkStackVarsCallback = std::function<HRESULT(const std::string &, const GetValueCallback &)>;
    using GetFunctionCallback = std::function<HRESULT(ICorDebugFunction **)>;
    using ReturnElementType = SigElementType;
    using WalkMethodsCallback = std::function<HRESULT(bool, const std::string &, ReturnElementType &,
                                                      std::vector<SigElementType> &, uint32_t, GetFunctionCallback)>;

    Evaluator(std::shared_ptr<DebugInfo> &sharedDebugInfo, std::shared_ptr<EvalExec> &sharedEvalExec);

    HRESULT ResolveIdentifiers(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pForcedThisValue,
                               SetterData *inputSetterData, std::vector<std::string> &identifiers,
                               FormatSpecifier specifier, ICorDebugValue **ppResultValue,
                               std::unique_ptr<SetterData> *resultSetterData, ICorDebugType **ppResultType);

    HRESULT GetStaticField(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugType *pType,
                           mdFieldDef fieldDef, ICorDebugValue **ppResultValue);
    HRESULT WalkMembers(ICorDebugValue *pInputValue, ICorDebugThread *pThread, FrameLevel frameLevel,
                        bool provideSetterData, FormatSpecifier specifier, const WalkMembersCallback &cb);

    static HRESULT WalkGeneratedClassFields(IMetaDataImport *pMDImport, ICorDebugValue *pInputValue, uint32_t currentIlOffset,
                                            std::unordered_set<WSTRING> &usedNames, mdMethodDef methodDef, DebugInfo *pDebugInfo,
                                            ICorDebugModule *pModule, const Evaluator::WalkStackVarsCallback &cb);

    HRESULT WalkStackVars(ICorDebugThread *pThread, FrameLevel frameLevel, const WalkStackVarsCallback &cb);

    // Get the fully-qualified metadata (FQMD) type name of the method's declaring type.
    HRESULT GetFQMDTypeName(ICorDebugThread *pThread, FrameLevel frameLevel, std::string &metadataTypeName, bool &haveThis);

    HRESULT FollowFields(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pValue, ValueKind valueKind,
                         const std::vector<std::string> &identifiers, int nextIdentifier, FormatSpecifier specifier,
                         ICorDebugValue **ppResult, std::unique_ptr<Evaluator::SetterData> *resultSetterData);

    HRESULT FollowNestedFindValue(ICorDebugThread *pThread, FrameLevel frameLevel, const std::string &displayTypeName,
                                  std::vector<std::string> &identifiers, FormatSpecifier specifier,
                                  const PDB::ImportsAndAliases &pdbImports,
                                  ICorDebugValue **ppResult, std::unique_ptr<Evaluator::SetterData> *resultSetterData);

    HRESULT CallOverriddenToString(ICorDebugThread *pThread, ICorDebugValue *pInputValue, FormatSpecifier specifier, std::string &output);

    static HRESULT GetElement(ICorDebugValue *pInputValue, std::vector<uint32_t> &indexes, ICorDebugValue **ppResultValue);

    static HRESULT WalkMethods(ICorDebugValue *pInputTypeValue, bool walkBaseType, const WalkMethodsCallback &cb);
    static HRESULT WalkMethods(ICorDebugType *pInputType, bool walkBaseType, ICorDebugType **ppResultType, const WalkMethodsCallback &cb);
    HRESULT WalkExtensionMethods(ICorDebugType *pInputType, CorElementType elemType, const Evaluator::WalkMethodsCallback &cb);

    HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule);
    HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule);

    void GetImportsAndAliases(ICorDebugThread *pThread, FrameLevel frameLevel, PDB::ImportsAndAliases &pdbImports);

    [[nodiscard]] bool IsJustMyCode() const
    {
        return m_justMyCode;
    }
    void SetJustMyCode(bool enable)
    {
        m_justMyCode = enable;
    }

    [[nodiscard]] uint32_t GetEvalFlags() const
    {
        return m_evalFlags;
    }
    void SetEvalFlags(uint32_t evalFlags)
    {
        m_evalFlags = evalFlags;
    }

  private:

    std::shared_ptr<DebugInfo> m_sharedDebugInfo;
    std::shared_ptr<EvalExec> m_sharedEvalExec;
    std::shared_ptr<TypeProxy> m_sharedTypeProxy;

    bool m_justMyCode{true};
    uint32_t m_evalFlags{defaultEvalFlags};

    // Extension methods related

    struct ModuleExtensionMethods
    {
        ToRelease<ICorDebugModule> trModule;
        std::vector<mdMethodDef> methodDefs;

        ModuleExtensionMethods(ICorDebugModule *pModule, std::vector<mdMethodDef> &&methodDefs_)
            : trModule(pModule),
              methodDefs(std::move(methodDefs_))
        {
        }
    };
    std::mutex m_extensionMethodsMutex;
    std::unordered_map<CORDB_ADDRESS, ModuleExtensionMethods> m_extensionMethodsCache;

    HRESULT FillModuleExtensionMethodsCache(ICorDebugModule *pModule);
};

} // namespace dncdbg

#endif // DEBUGGER_EVALUATOR_H
