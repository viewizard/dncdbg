// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluator.h"
#include "config/config.h"
#include "debugger/evalhelpers.h"
#include "debugger/evaluation/evalhelpers/evalexec.h"
#include "debugger/evaluation/walkers/walkers.h"
#include "debugger/frames.h"
#include "debugger/valueprint.h"
#include "debuginfo/debuginfo.h"
#include "metadata/helpers.h"
#include "metadata/sigparse.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <vector>

namespace dncdbg::Evaluator
{

namespace
{

HRESULT FollowNestedFindType(ICorDebugThread *pThread, const std::string &displayTypeName,
                             const PDB::ImportsAndAliases &pdbImports,
                             const std::vector<std::string> &identifiers, ICorDebugType **ppResultType)
{
    HRESULT Status = S_OK;

    std::vector<std::string> classIdentifiers = MetadataHelpers::SplitFQDisplayTypeName(displayTypeName);

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(MetadataHelpers::FindTypeModule(classIdentifiers, pThread, pdbImports, &trModule));

    bool trim = false;
    while (!classIdentifiers.empty())
    {
        if (trim)
        {
            classIdentifiers.pop_back();
        }

        std::vector<std::string> fullpath = classIdentifiers;
        std::copy(identifiers.cbegin(), identifiers.cend(), std::back_inserter(fullpath));

        int nextClassIdentifier = 0;
        ToRelease<ICorDebugType> trType;
        if (FAILED(MetadataHelpers::FindType(fullpath, nextClassIdentifier, pThread, trModule, pdbImports, &trType)))
        {
            break;
        }

        assert(fullpath.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
        if (nextClassIdentifier == static_cast<int>(fullpath.size()))
        {
            *ppResultType = trType.Detach();
            return S_OK;
        }

        trim = true;
    }

    return E_FAIL;
}

HRESULT FollowFields(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pValue,
                     ValueKind valueKind, const std::vector<std::string> &identifiers, int nextIdentifier,
                     FormatSpecifier specifier, ICorDebugValue **ppResult, std::string *pRealDisplayTypeName,
                     std::unique_ptr<Walkers::SetterData> *pResultSetterData)
{
    HRESULT Status = S_OK;

    // Note: when (nextIdentifier == identifiers.size()), the result is pValue itself, so we are fine here.
    assert(identifiers.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
    if (nextIdentifier > static_cast<int>(identifiers.size()))
    {
        return E_FAIL;
    }

    pValue->AddRef();
    ToRelease<ICorDebugValue> trResultValue(pValue);
    for (int i = nextIdentifier; i < static_cast<int>(identifiers.size()); i++)
    {
        if (identifiers.at(i).empty())
        {
            return E_FAIL;
        }

        const ToRelease<ICorDebugValue> trClassValue(trResultValue.Detach());

        IfFailRet(Walkers::WalkMembers(trClassValue, pThread, frameLevel, (pResultSetterData != nullptr), specifier,
            [&](ICorDebugType */*pType*/, bool isStatic, const std::string &memberName,
                const Walkers::GetValueCallback &getValue, Walkers::SetterData *pSetterData, std::string *) -> HRESULT
            {
                if ((isStatic && valueKind == ValueKind::Variable) ||
                    (!isStatic && valueKind == ValueKind::Static) ||
                    memberName != identifiers.at(i))
                {
                    return S_OK;
                }

                if (FAILED(Status = getValue(&trResultValue, pRealDisplayTypeName)))
                {
                    if (pRealDisplayTypeName != nullptr)
                    {
                        pRealDisplayTypeName->clear();
                    }
                    return Status;
                }
                if (pSetterData != nullptr &&
                    pResultSetterData != nullptr)
                {
                    *pResultSetterData = std::make_unique<Walkers::SetterData>(*pSetterData);
                }

                return S_CAN_EXIT; // Fast exit from the loop.
            }));

        if (trResultValue == nullptr)
        {
            return E_FAIL;
        }

        valueKind = ValueKind::Variable; // We can only follow through instance fields.
    }

    *ppResult = trResultValue.Detach();
    return S_OK;
}

HRESULT FollowNestedFindValue(ICorDebugThread *pThread, FrameLevel frameLevel, const std::string &displayTypeName,
                              std::vector<std::string> &identifiers, FormatSpecifier specifier,
                              const PDB::ImportsAndAliases &pdbImports, ICorDebugValue **ppResult,
                              std::string *pRealDisplayTypeName, std::unique_ptr<Walkers::SetterData> *pResultSetterData)
{
    HRESULT Status = S_OK;

    std::vector<std::string> classIdentifiers = MetadataHelpers::SplitFQDisplayTypeName(displayTypeName);
    assert(identifiers.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
    const int identifiersNum = static_cast<int>(identifiers.size()) - 1;
    std::vector<std::string> fieldName{identifiers.back()};

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(MetadataHelpers::FindTypeModule(classIdentifiers, pThread, pdbImports, &trModule));

    bool trim = false;
    while (!classIdentifiers.empty())
    {
        if (trim)
        {
            classIdentifiers.pop_back();
        }

        std::vector<std::string> fullpath = classIdentifiers;
        std::copy(identifiers.cbegin(), identifiers.cbegin() + identifiersNum, std::back_inserter(fullpath));

        int nextClassIdentifier = 0;
        ToRelease<ICorDebugType> trType;
        if (FAILED(MetadataHelpers::FindType(fullpath, nextClassIdentifier, pThread, trModule, pdbImports, &trType)))
        {
            break;
        }

        assert(fullpath.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
        if (nextClassIdentifier < static_cast<int>(fullpath.size()))
        {
            // Look for non-static fields inside a static member.
            std::vector<std::string> staticName;
            for (int i = nextClassIdentifier; i < static_cast<int>(fullpath.size()); i++)
            {
                staticName.emplace_back(fullpath.at(i));
            }
            staticName.emplace_back(fieldName.at(0));
            ToRelease<ICorDebugValue> trTypeObject;
            if (TypeHasStaticMembers(trType) &&
                SUCCEEDED(EvalExec::CreateTypeObject(pThread, trType, &trTypeObject)) &&
                SUCCEEDED(FollowFields(pThread, frameLevel, trTypeObject, ValueKind::Static, staticName,
                                       0, specifier, ppResult, pRealDisplayTypeName, pResultSetterData)))
            {
                return S_OK;
            }
            trim = true;
            continue;
        }

        ToRelease<ICorDebugValue> trTypeObject;
        if (TypeHasStaticMembers(trType) &&
            SUCCEEDED(EvalExec::CreateTypeObject(pThread, trType, &trTypeObject)) &&
            SUCCEEDED(FollowFields(pThread, frameLevel, trTypeObject, ValueKind::Static, fieldName,
                                   0, specifier, ppResult, pRealDisplayTypeName, pResultSetterData)))
        {
            return S_OK;
        }

        trim = true;
    }

    return E_FAIL;
}

} // unnamed namespace

HRESULT CallOverriddenToString(ICorDebugThread *pThread, ICorDebugValue *pInputValue, FormatSpecifier specifier, std::string &output)
{
    if ((Config::GetEvalFlags() & Config::EVAL_NOTOSTRING) != 0U)
    {
        return CORDBG_E_DEBUGGING_DISABLED;
    }

    HRESULT Status = S_OK;

    ToRelease<ICorDebugValue2> trInputValue2;
    IfFailRet(pInputValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trInputValue2)));
    ToRelease<ICorDebugType> trInputType;
    IfFailRet(trInputValue2->GetExactType(&trInputType));

    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(Walkers::WalkMethods(trInputType, false, nullptr,
        [&](bool isStatic, const std::string &methodName, Walkers::ReturnElementType &,
            std::vector<SigElementType> &methodArgs, uint32_t /*methodGenParamCount*/,
            const Walkers::GetFunctionCallback &getFunction) -> HRESULT
        {
            if (isStatic || !methodArgs.empty() || methodName != "ToString")
            {
                return S_OK; // Return success to continue walking.
            }

            IfFailRet(getFunction(&trFunc));

            return S_CAN_EXIT; // Fast exit from the loop, since we already found trFunc.
        }));

    if (trFunc == nullptr)
    {
        return E_INVALIDARG;
    }

    ToRelease<ICorDebugValue> trRefValue;
    IfFailRet(EvalExec::CallFunction(pThread, trFunc, trInputType.GetPtr(), nullptr, &pInputValue,
                                     1, specifier, &trRefValue));
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(trRefValue, &trValue, nullptr));
    return PrintStringValue(trValue, output);
}

HRESULT ResolveIdentifiers(ICorDebugThread *pThread, FrameLevel frameLevel, ICorDebugValue *pForcedThisValue,
                           Walkers::SetterData *pInputSetterData, std::vector<std::string> &identifiers,
                           FormatSpecifier specifier, ICorDebugValue **ppResultValue, std::string *pRealDisplayTypeName,
                           std::unique_ptr<Walkers::SetterData> *pResultSetterData, ICorDebugType **ppResultType)
{
    if (pForcedThisValue != nullptr && identifiers.empty())
    {
        pForcedThisValue->AddRef();
        *ppResultValue = pForcedThisValue;
        if (pInputSetterData != nullptr && pResultSetterData != nullptr)
        {
            *pResultSetterData = std::make_unique<Walkers::SetterData>(*pInputSetterData);
        }
        return S_OK;
    }
    else if (pForcedThisValue != nullptr)
    {
        return FollowFields(pThread, frameLevel, pForcedThisValue, ValueKind::Variable, identifiers,
                            0, specifier, ppResultValue, pRealDisplayTypeName, pResultSetterData);
    }

    HRESULT Status = S_OK;
    int nextIdentifier = 0;
    ToRelease<ICorDebugValue> trResolvedValue;
    ToRelease<ICorDebugValue> trThisValue;

    if (identifiers.at(nextIdentifier) == "$exception")
    {
        IfFailRet(pThread->GetCurrentException(&trResolvedValue));
        if (trResolvedValue == nullptr)
        {
            return E_FAIL;
        }
    }
    else if (identifiers.at(nextIdentifier) == "$pid")
    {
        ToRelease<ICorDebugProcess> trProcess;
        IfFailRet(pThread->GetProcess(&trProcess));
        DWORD processId = 0;
        IfFailRet(trProcess->GetID(&processId));

        ToRelease<ICorDebugEval> trEval;
        IfFailRet(pThread->CreateEval(&trEval));
        IfFailRet(trEval->CreateValue(ELEMENT_TYPE_U4, nullptr, &trResolvedValue));

#ifdef DEBUG_INTERNAL_TESTS
        uint32_t cbSize = 0;
        IfFailRet(trResolvedValue->GetSize(&cbSize));
        assert(cbSize == 4);
#endif // DEBUG_INTERNAL_TESTS

        ToRelease<ICorDebugGenericValue> trGenericValue;
        IfFailRet(trResolvedValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
        IfFailRet(trGenericValue->SetValue(static_cast<void *>(&processId)));
    }
    else if (identifiers.at(nextIdentifier) == "$tid")
    {
        DWORD threadId = 0;
        IfFailRet(pThread->GetID(&threadId));

        ToRelease<ICorDebugEval> trEval;
        IfFailRet(pThread->CreateEval(&trEval));
        IfFailRet(trEval->CreateValue(ELEMENT_TYPE_U4, nullptr, &trResolvedValue));

#ifdef DEBUG_INTERNAL_TESTS
        uint32_t cbSize = 0;
        IfFailRet(trResolvedValue->GetSize(&cbSize));
        assert(cbSize == 4);
#endif // DEBUG_INTERNAL_TESTS

        ToRelease<ICorDebugGenericValue> trGenericValue;
        IfFailRet(trResolvedValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
        IfFailRet(trGenericValue->SetValue(static_cast<void *>(&threadId)));
    }
    else
    {
        IfFailRet(Walkers::WalkStackVars(pThread, frameLevel,
            [&](const std::string &name, const Walkers::GetValueCallback &getValue) -> HRESULT
            {
                if (name == "this")
                {
                    if (FAILED(getValue(&trThisValue, pRealDisplayTypeName)) || (trThisValue == nullptr))
                    {
                        if (pRealDisplayTypeName != nullptr)
                        {
                            pRealDisplayTypeName->clear();
                        }
                        return S_OK;
                    }

                    if (name == identifiers.at(nextIdentifier))
                    {
                        return S_CAN_EXIT; // Fast way to exit from stack vars walk routine.
                    }
                }
                else if (name == identifiers.at(nextIdentifier))
                {
                    if (FAILED(getValue(&trResolvedValue, pRealDisplayTypeName)) || (trResolvedValue == nullptr))
                    {
                        if (pRealDisplayTypeName != nullptr)
                        {
                            pRealDisplayTypeName->clear();
                        }
                        return S_OK;
                    }

                    return S_CAN_EXIT; // Fast way to exit from stack vars walk routine.
                }

                return S_OK;
            }));
    }

    if ((trResolvedValue == nullptr) && (trThisValue != nullptr)) // check this/this.*
    {
        if (identifiers.at(nextIdentifier) == "this")
        {
            nextIdentifier++; // skip first identifier with "this" (we have it in trThisValue), check rest
        }

        if (SUCCEEDED(FollowFields(pThread, frameLevel, trThisValue, ValueKind::Variable, identifiers,
                                   nextIdentifier, specifier, &trResolvedValue, pRealDisplayTypeName, pResultSetterData)))
        {
            *ppResultValue = trResolvedValue.Detach();
            return S_OK;
        }
    }

    PDB::ImportsAndAliases pdbImports;
    GetImportsAndAliases(pThread, frameLevel, pdbImports);

    if (trResolvedValue == nullptr) // check statics in nested classes
    {
        ToRelease<ICorDebugFrame> trFrame;
        IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
        if (trFrame == nullptr)
        {
            return E_FAIL;
        }

        std::string displayTypeName;
        MetadataHelpers::GetFQDisplayRealCodeTypeName(trFrame, displayTypeName);

        if (SUCCEEDED(FollowNestedFindValue(pThread, frameLevel, displayTypeName, identifiers, specifier,
                                            pdbImports, &trResolvedValue, pRealDisplayTypeName, pResultSetterData)))
        {
            *ppResultValue = trResolvedValue.Detach();
            return S_OK;
        }

        if (ppResultType != nullptr &&
            SUCCEEDED(FollowNestedFindType(pThread, displayTypeName, pdbImports, identifiers, ppResultType)))
        {
            return S_OK;
        }
    }

    ValueKind valueKind = ValueKind::Variable;
    if (trResolvedValue != nullptr)
    {
        nextIdentifier++;
        assert(identifiers.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
        if (nextIdentifier == static_cast<int>(identifiers.size()))
        {
            *ppResultValue = trResolvedValue.Detach();
            return S_OK;
        }
        valueKind = ValueKind::Variable;
    }
    else
    {
        ToRelease<ICorDebugType> trType;
        IfFailRet(MetadataHelpers::FindType(identifiers, nextIdentifier, pThread, nullptr, pdbImports, &trType));

        // Identifiers resolved into a type, not a value. If the type could be the result, provide the type directly as the result.
        // This way the caller will know that there is no object instance here (it should operate with static members/methods only).
        assert(identifiers.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
        if ((ppResultType != nullptr) && nextIdentifier == static_cast<int>(identifiers.size()))
        {
            *ppResultType = trType.Detach();
            return S_OK;
        }

        if (nextIdentifier == static_cast<int>(identifiers.size()) || // no more identifiers to resolve into members
            !TypeHasStaticMembers(trType) || // type doesn't have static members, nothing to explore here
            FAILED(EvalExec::CreateTypeObject(pThread, trType, &trResolvedValue)))
        {
            return E_INVALIDARG;
        }

        valueKind = ValueKind::Static;
    }

    ToRelease<ICorDebugValue> trResultValue;
    IfFailRet(FollowFields(pThread, frameLevel, trResolvedValue, valueKind, identifiers,
                           nextIdentifier, specifier, &trResultValue, pRealDisplayTypeName, pResultSetterData));

    *ppResultValue = trResultValue.Detach();
    return S_OK;
}

void GetImportsAndAliases(ICorDebugThread *pThread, FrameLevel frameLevel, PDB::ImportsAndAliases &pdbImports)
{
    const auto getImportsAndAliases = [&]() -> HRESULT
    {
        HRESULT Status = S_OK;
        ToRelease<ICorDebugFrame> trFrame;
        IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
        if (trFrame == nullptr)
        {
            return E_FAIL;
        }

        ToRelease<ICorDebugFunction> trFunction;
        IfFailRet(trFrame->GetFunction(&trFunction));

        ToRelease<ICorDebugModule> trModule;
        IfFailRet(trFunction->GetModule(&trModule));

        mdMethodDef methodDef = mdMethodDefNil;
        IfFailRet(trFunction->GetToken(&methodDef));

        ToRelease<ICorDebugILFrame> trILFrame;
        IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

        uint32_t currentIlOffset = 0;
        CorDebugMappingResult mappingResult = MAPPING_NO_INFO;
        IfFailRet(trILFrame->GetIP(&currentIlOffset, &mappingResult));
        if (mappingResult == MAPPING_UNMAPPED_ADDRESS ||
            mappingResult == MAPPING_NO_INFO)
        {
            return E_FAIL;
        }

        ToRelease<IUnknown> trUnknown;
        IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        IfFailRet(DebugInfo::GetImportsAndAliases(trModule, methodDef, currentIlOffset, pdbImports));

        const auto applyTokenName = [&trMDImport](std::vector<PDB::Imports> &alias) -> HRESULT
        {
            for (auto &entry : alias)
            {
                // For TypeSpec tokens, pre-resolve the generic type arguments from the signature
                // so that GetFQDisplayNameForToken() can substitute them into the display name.
                std::list<std::string> args;
                std::list<std::string> *pArgs = nullptr;
                if (TypeFromToken(entry.token) == mdtTypeSpec)
                {
                    PCCOR_SIGNATURE pSig = nullptr;
                    ULONG cbSig = 0;
                    SigElementType sigType;
                    if (FAILED(trMDImport->GetTypeSpecFromToken(entry.token, &pSig, &cbSig)) ||
                        FAILED(ParseElementType(trMDImport, pSig, pSig + cbSig, 0, sigType, &args, true)))
                    {
                        // Skip entries whose TypeSpec signature cannot be parsed.
                        continue;
                    }
                    pArgs = &args;
                }

                if (FAILED(MetadataHelpers::GetFQDisplayNameForToken(entry.token, trMDImport, entry.displayName, pArgs)))
                {
                    // Skip entries whose target type cannot be resolved.
                    continue;
                }
            }

            return S_OK;
        };

        const auto importType = pdbImports.find(PDB::ImportsKind::ImportType);
        if (importType != pdbImports.cend())
        {
            applyTokenName(importType->second);
        }

        const auto aliasType = pdbImports.find(PDB::ImportsKind::AliasType);
        if (aliasType != pdbImports.cend())
        {
            applyTokenName(aliasType->second);
        }

        return S_OK;
    };

    pdbImports.clear();
    getImportsAndAliases();

    // In case of failure (or no debug info for this code), add the default "System" namespace.
    auto &importNamespace = pdbImports[PDB::ImportsKind::ImportNamespace];
    if (importNamespace.empty())
    {
        importNamespace.emplace_back();
        importNamespace.back().targetNamespace = "System";
    }
}

} // namespace dncdbg::Evaluator
