// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/jmc.h"
#include "metadata/attributes.h"
#include "utils/torelease.h"
#include <array>
#include <iterator>
#include <vector>

namespace dncdbg
{

namespace
{

const std::vector<std::string_view> &GetTypeAttrNames()
{
    static const std::vector<std::string_view> typeAttrNames{
        DebuggerAttribute::NonUserCode,
        DebuggerAttribute::StepThrough
    };
    return typeAttrNames;
}

const std::vector<std::string_view> &GetMethodAttrNames()
{
    static const std::vector<std::string_view> methodAttrNames{
        DebuggerAttribute::NonUserCode,
        DebuggerAttribute::StepThrough,
        DebuggerAttribute::Hidden
    };
    return methodAttrNames;
}

HRESULT GetNonJMCMethodsForTypeDef(IMetaDataImport *pMDImport, mdTypeDef typeDef, std::vector<mdToken> &excludeMethods)
{
    ULONG numMethods = 0;
    HCORENUM fEnum = nullptr;
    mdMethodDef methodDef = mdMethodDefNil;
    while (SUCCEEDED(pMDImport->EnumMethods(&fEnum, typeDef, &methodDef, 1, &numMethods)) && numMethods != 0)
    {
        mdTypeDef memTypeDef = mdTypeDefNil;
        ULONG nameLen = 0;
        std::array<WCHAR, mdNameLen> szFunctionName{};

        if (FAILED(pMDImport->GetMethodProps(methodDef, &memTypeDef, szFunctionName.data(), mdNameLen, &nameLen,
                                             nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        if (HasAttribute(pMDImport, methodDef, GetMethodAttrNames()))
        {
            excludeMethods.push_back(methodDef);
        }
    }
    pMDImport->CloseEnum(fEnum);

    return S_OK;
}

HRESULT GetNonJMCClassesAndMethods(ICorDebugModule *pModule, std::vector<mdToken> &excludeTokens)
{
    HRESULT Status = S_OK;

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    ULONG numTypedefs = 0;
    HCORENUM fEnum = nullptr;
    mdTypeDef typeDef = mdTypeDefNil;
    while (SUCCEEDED(trMDImport->EnumTypeDefs(&fEnum, &typeDef, 1, &numTypedefs)) && numTypedefs != 0)
    {
        if (HasAttribute(trMDImport, typeDef, GetTypeAttrNames()))
        {
            excludeTokens.push_back(typeDef);
        }
        else
        {
            GetNonJMCMethodsForTypeDef(trMDImport, typeDef, excludeTokens);
        }
    }
    trMDImport->CloseEnum(fEnum);

    return S_OK;
}

void DisableJMCForTokenList(ICorDebugModule *pModule, const std::vector<mdToken> &excludeTokens)
{
    for (const mdToken token : excludeTokens)
    {
        if (TypeFromToken(token) == mdtMethodDef)
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
        else if (TypeFromToken(token) == mdtTypeDef)
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
    std::unordered_set<mdToken> excludeTypeTokens;

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    for (const mdMethodDef methodToken : methodTokens)
    {
        // Note, in case of method we need check class attributes first, since class also could have it.
        ToRelease<ICorDebugFunction> trFunction;
        IfFailRet(pModule->GetFunctionFromToken(methodToken, &trFunction));
        ToRelease<ICorDebugClass> trClass;
        IfFailRet(trFunction->GetClass(&trClass));
        mdToken typeToken = mdTokenNil;
        IfFailRet(trClass->GetToken(&typeToken));

        // In case class have "not user code" related attribute, no reason set JMC to false for each method, set it to class will be enough.
        if (HasAttribute(trMDImport, typeToken, GetTypeAttrNames()))
        {
            excludeTypeTokens.emplace(typeToken);
        }
        else if (HasAttribute(trMDImport, methodToken, GetMethodAttrNames()))
        {
            excludeTokens.push_back(methodToken);
        }
    }
    std::copy(excludeTypeTokens.begin(), excludeTypeTokens.end(), std::back_inserter(excludeTokens));

    DisableJMCForTokenList(pModule, excludeTokens);
    return S_OK;
}

} // namespace dncdbg
