// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/jmc.h"
#include "metadata/attributes.h"
#include "metadata/enumerations.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace dncdbg
{

namespace
{

// Upper bound for the initial bucket count of the per-class cache, so a big token set does not preallocate a large table.
constexpr std::size_t maxTypeCacheHint = 256;

const std::vector<WSTRING> &GetTypeAttrNames()
{
    static const std::vector<WSTRING> typeAttrNames{
        DebuggerAttribute::GetNonUserCode(),
        DebuggerAttribute::GetStepThrough()
    };
    return typeAttrNames;
}

const std::vector<WSTRING> &GetMethodAttrNames()
{
    static const std::vector<WSTRING> methodAttrNames{
        DebuggerAttribute::GetNonUserCode(),
        DebuggerAttribute::GetStepThrough(),
        DebuggerAttribute::GetHidden()
    };
    return methodAttrNames;
}

HRESULT GetNonJMCMethodsForTypeDef(IMetaDataImport *pMDImport, mdTypeDef typeDef, std::vector<mdToken> &excludeMethods)
{
    HRESULT Status = S_OK;

    IfFailRet(MetadataEnumerations::ForEachMethod(pMDImport, typeDef,
        [&](mdMethodDef methodDef) -> HRESULT
        {
            if (HasAttribute(pMDImport, methodDef, GetMethodAttrNames()))
            {
                excludeMethods.push_back(methodDef);
            }
            return S_OK;
        }));

    return S_OK;
}

HRESULT GetNonJMCClassesAndMethods(ICorDebugModule *pModule, std::vector<mdToken> &excludeTokens)
{
    HRESULT Status = S_OK;

    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&trMDImport)));

    IfFailRet(MetadataEnumerations::ForEachTypeDef(trMDImport,
        [&](mdTypeDef typeDef) -> HRESULT
        {
            if (HasAttribute(trMDImport, typeDef, GetTypeAttrNames()))
            {
                excludeTokens.push_back(typeDef);
            }
            else
            {
                GetNonJMCMethodsForTypeDef(trMDImport, typeDef, excludeTokens);
            }
            return S_OK;
        }));

    return S_OK;
}

void DisableJMCForTokenList(ICorDebugModule *pModule, const std::vector<mdToken> &excludeTokens)
{
    for (const mdToken token : excludeTokens)
    {
        const DWORD tokenType = TypeFromToken(token);
        if (tokenType == mdtMethodDef)
        {
            ToRelease<ICorDebugFunction> trFunction;
            ToRelease<ICorDebugFunction2> trFunction2;
            if (FAILED(pModule->GetFunctionFromToken(token, &trFunction)) ||
                FAILED(trFunction->QueryInterface(IID_ICorDebugFunction2, reinterpret_cast<void **>(&trFunction2))))
            {
                continue;
            }

            trFunction2->SetJMCStatus(FALSE);
        }
        else if (tokenType == mdtTypeDef)
        {
            ToRelease<ICorDebugClass> trClass;
            ToRelease<ICorDebugClass2> trClass2;
            if (FAILED(pModule->GetClassFromToken(token, &trClass)) ||
                FAILED(trClass->QueryInterface(IID_ICorDebugClass2, reinterpret_cast<void **>(&trClass2))))
            {
                continue;
            }

            trClass2->SetJMCStatus(FALSE);
        }
    }
}

} // unnamed namespace

HRESULT DisableJMCByAttributes(ICorDebugModule *pModule)
{
    HRESULT Status = S_OK;
    std::vector<mdToken> excludeTokens;
    IfFailRet(GetNonJMCClassesAndMethods(pModule, excludeTokens));

    DisableJMCForTokenList(pModule, excludeTokens);
    return S_OK;
}

HRESULT DisableJMCByAttributes(ICorDebugModule *pModule, const std::unordered_set<mdMethodDef> &methodTokens)
{
    HRESULT Status = S_OK;
    std::vector<mdToken> excludeTokens;
    excludeTokens.reserve(methodTokens.size());
    // Cache of the class-level attribute check: methods usually share a declaring class,
    // so the class attributes are looked up only once per class.
    std::unordered_map<mdToken, bool> typeIsNonUserCode;
    typeIsNonUserCode.reserve(std::min<std::size_t>(methodTokens.size(), maxTypeCacheHint));

    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&trMDImport)));

    for (const mdMethodDef methodToken : methodTokens)
    {
        // Note, in case of a method, we need to check the class attributes first, since the class could also have them.
        mdToken typeToken = mdTokenNil;
        IfFailRet(trMDImport->GetMethodProps(methodToken, &typeToken, nullptr, 0, nullptr,
                                             nullptr, nullptr, nullptr, nullptr, nullptr));

        auto [typeIt, isNewType] = typeIsNonUserCode.try_emplace(typeToken, false);
        if (isNewType)
        {
            // In case the class has a "non-user code" related attribute, there is no reason to set JMC to false for each method;
            // setting it on the class will be enough.
            typeIt->second = HasAttribute(trMDImport, typeToken, GetTypeAttrNames());
            if (typeIt->second)
            {
                excludeTokens.push_back(typeToken);
            }
        }

        if (!typeIt->second && HasAttribute(trMDImport, methodToken, GetMethodAttrNames()))
        {
            excludeTokens.push_back(methodToken);
        }
    }

    DisableJMCForTokenList(pModule, excludeTokens);
    return S_OK;
}

} // namespace dncdbg
