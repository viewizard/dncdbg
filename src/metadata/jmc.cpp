// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/jmc.h"
#include "metadata/attributes.h"
#include "utils/torelease.h"
#include <iterator>
#include <string>
#include <vector>

namespace dncdbg
{

// https://docs.microsoft.com/en-us/dotnet/api/system.diagnostics.debuggernonusercodeattribute
// This attribute suppresses the display of these adjunct types and members in the debugger window and
// automatically steps through, rather than into, designer provided code.
const char DebuggerAttribute::NonUserCode[] = "System.Diagnostics.DebuggerNonUserCodeAttribute..ctor";
// Check `DebuggerStepThroughAttribute` for method and class.
// https://docs.microsoft.com/en-us/dotnet/api/system.diagnostics.debuggerstepthroughattribute
// Instructs the debugger to step through the code instead of stepping into the code.
const char DebuggerAttribute::StepThrough[] = "System.Diagnostics.DebuggerStepThroughAttribute..ctor";
// https://docs.microsoft.com/en-us/dotnet/api/system.diagnostics.debuggerhiddenattribute
// ... debugger does not stop in a method marked with this attribute and does not allow a breakpoint to be set in the method.
// https://docs.microsoft.com/en-us/dotnet/visual-basic/misc/bc40051
// System.Diagnostics.DebuggerHiddenAttribute does not affect 'Get' or 'Set' when applied to the Property definition.
// Apply the attribute directly to the 'Get' and 'Set' procedures as appropriate.
const char DebuggerAttribute::Hidden[] = "System.Diagnostics.DebuggerHiddenAttribute..ctor";

static std::vector<std::string> typeAttrNames{DebuggerAttribute::NonUserCode, DebuggerAttribute::StepThrough};
static std::vector<std::string> methodAttrNames{DebuggerAttribute::NonUserCode, DebuggerAttribute::StepThrough, DebuggerAttribute::Hidden};

static HRESULT GetNonJMCMethodsForTypeDef(IMetaDataImport *pMD, mdTypeDef typeDef, std::vector<mdToken> &excludeMethods)
{
    ULONG numMethods = 0;
    HCORENUM fEnum = NULL;
    mdMethodDef methodDef = mdMethodDefNil;
    while (SUCCEEDED(pMD->EnumMethods(&fEnum, typeDef, &methodDef, 1, &numMethods)) && numMethods != 0)
    {
        mdTypeDef memTypeDef = mdTypeDefNil;
        ULONG nameLen = 0;
        WCHAR szFunctionName[mdNameLen] = {0};

        if (FAILED(pMD->GetMethodProps(methodDef, &memTypeDef, szFunctionName, _countof(szFunctionName), &nameLen,
                                       nullptr, nullptr, nullptr, nullptr, nullptr)))
            continue;

        if (HasAttribute(pMD, methodDef, methodAttrNames))
            excludeMethods.push_back(methodDef);
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
    IfFailRet(pMDUnknown->QueryInterface(IID_IMetaDataImport, (LPVOID *)&pMD));

    ULONG numTypedefs = 0;
    HCORENUM fEnum = NULL;
    mdTypeDef typeDef = mdTypeDefNil;
    while (SUCCEEDED(pMD->EnumTypeDefs(&fEnum, &typeDef, 1, &numTypedefs)) && numTypedefs != 0)
    {
        if (HasAttribute(pMD, typeDef, typeAttrNames))
            excludeTokens.push_back(typeDef);
        else
            GetNonJMCMethodsForTypeDef(pMD, typeDef, excludeTokens);
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
                FAILED(pFunction->QueryInterface(IID_ICorDebugFunction2, (LPVOID *)&pFunction2)))
                continue;

            pFunction2->SetJMCStatus(FALSE);
        }
        else if (TypeFromToken(token) == mdtTypeDef)
        {
            ToRelease<ICorDebugClass> pClass;
            ToRelease<ICorDebugClass2> pClass2;
            if (FAILED(pModule->GetClassFromToken(token, &pClass)) ||
                FAILED(pClass->QueryInterface(IID_ICorDebugClass2, (LPVOID *)&pClass2)))
                continue;

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
    IfFailRet(pMDUnknown->QueryInterface(IID_IMetaDataImport, (LPVOID *)&pMD));

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
