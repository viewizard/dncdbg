// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/evalhelpers/evalresolver.h"
#include "debugger/evalhelpers.h"
#include "debugger/evaluation/evalhelpers/debuginfo.h"
#include "debugger/evaluation/evalhelpers/evalexec.h"
#include "debugger/evaluation/walkers/walkers.h"
#include "debugger/frames.h"
#include "debuginfo/pdb.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>

namespace dncdbg::EvalResolver
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
    EvalDebugInfoHelpers::GetImportsAndAliases(pThread, frameLevel, pdbImports);

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

} // namespace dncdbg::EvalResolver
