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

static const std::vector<std::string_view> typeAttrNames{DebuggerAttribute::NonUserCode, DebuggerAttribute::StepThrough};
static const std::vector<std::string_view> methodAttrNames{DebuggerAttribute::NonUserCode, DebuggerAttribute::StepThrough, DebuggerAttribute::Hidden};

static HRESULT GetNonJMCMethodsForTypeDef(IMetaDataImport *pMD, mdTypeDef typeDef, std::vector<mdToken> &excludeMethods)
{
    ULONG numMethods = 0;
    HCORENUM fEnum = nullptr;
    mdMethodDef methodDef = mdMethodDefNil;
    while (SUCCEEDED(pMD->EnumMethods(&fEnum, typeDef, &methodDef, 1, &numMethods)) && numMethods != 0)
    {
        mdTypeDef memTypeDef = mdTypeDefNil;
        ULONG nameLen = 0;
        std::array<WCHAR, mdNameLen> szFunctionName{};

        if (FAILED(pMD->GetMethodProps(methodDef, &memTypeDef, szFunctionName.data(), mdNameLen, &nameLen,
                                       nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        if (HasAttribute(pMD, methodDef, methodAttrNames))
        {
            excludeMethods.push_back(methodDef);
        }
    }
    pMD->CloseEnum(fEnum);

    return S_OK;
}

static HRESULT GetNonJMCClassesAndMethods(ICorDebugModule *pModule, std::vector<mdToken> &excludeTokens)
{
    HRESULT Status = S_OK;

    ToRelease<IUnknown> pMDUnknown;
    ToRelease<IMetaDataImport> pMD;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &pMDUnknown));
    IfFailRet(pMDUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&pMD)));

    ULONG numTypedefs = 0;
    HCORENUM fEnum = nullptr;
    mdTypeDef typeDef = mdTypeDefNil;
    while (SUCCEEDED(pMD->EnumTypeDefs(&fEnum, &typeDef, 1, &numTypedefs)) && numTypedefs != 0)
    {
        if (HasAttribute(pMD, typeDef, typeAttrNames))
        {
            excludeTokens.push_back(typeDef);
        }
        else
        {
            GetNonJMCMethodsForTypeDef(pMD, typeDef, excludeTokens);
        }
    }
    pMD->CloseEnum(fEnum);

    return S_OK;
}

static void DisableJMCForTokenList(ICorDebugModule *pModule, const std::vector<mdToken> &excludeTokens)
{
    for (const mdToken token : excludeTokens)
    {
        if (TypeFromToken(token) == mdtMethodDef)
        {
            ToRelease<ICorDebugFunction> pFunction;
            ToRelease<ICorDebugFunction2> pFunction2;
            if (FAILED(pModule->GetFunctionFromToken(token, &pFunction)) ||
                FAILED(pFunction->QueryInterface(IID_ICorDebugFunction2, reinterpret_cast<void **>(&pFunction2))))
            {
                continue;
            }

            pFunction2->SetJMCStatus(FALSE);
        }
        else if (TypeFromToken(token) == mdtTypeDef)
        {
            ToRelease<ICorDebugClass> pClass;
            ToRelease<ICorDebugClass2> pClass2;
            if (FAILED(pModule->GetClassFromToken(token, &pClass)) ||
                FAILED(pClass->QueryInterface(IID_ICorDebugClass2, reinterpret_cast<void **>(&pClass2))))
            {
                continue;
            }

            pClass2->SetJMCStatus(FALSE);
        }
    }
}

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

    ToRelease<IUnknown> pMDUnknown;
    ToRelease<IMetaDataImport> pMD;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &pMDUnknown));
    IfFailRet(pMDUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&pMD)));

    for (const mdMethodDef methodToken : methodTokens)
    {
        // Note, in case of method we need check class attributes first, since class also could have it.
        ToRelease<ICorDebugFunction> pFunction;
        IfFailRet(pModule->GetFunctionFromToken(methodToken, &pFunction));
        ToRelease<ICorDebugClass> pClass;
        IfFailRet(pFunction->GetClass(&pClass));
        mdToken typeToken = mdTokenNil;
        IfFailRet(pClass->GetToken(&typeToken));

        // In case class have "not user code" related attribute, no reason set JMC to false for each method, set it to class will be enough.
        if (HasAttribute(pMD, typeToken, typeAttrNames))
        {
            excludeTypeTokens.emplace(typeToken);
        }
        else if (HasAttribute(pMD, methodToken, methodAttrNames))
        {
            excludeTokens.push_back(methodToken);
        }
    }
    std::copy(excludeTypeTokens.begin(), excludeTypeTokens.end(), std::back_inserter(excludeTokens));

    DisableJMCForTokenList(pModule, excludeTokens);
    return S_OK;
}

} // namespace dncdbg
