// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/walkers/walkers.h"
#include "debugger/evalhelpers.h"
#include "debugger/evaluation/evalhelpers/evalexec.h"
#include "debugger/evaluation/evalhelpers/metadata.h"
#include "debugger/frames.h"
#include "debuginfo/debuginfo.h"
#include "metadata/helpers.h"
#include "metadata/sigparse.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace dncdbg::Walkers
{

namespace
{

HRESULT WalkPrimaryConstructorParameterFields(IMetaDataImport *pMDImport, ICorDebugClass *pClass, mdTypeDef typeDef,
                                              ICorDebugValue *pInputValue, std::unordered_set<WSTRING> &usedNames,
                                              const WalkStackVarsCallback &cb)
{
    HRESULT Status = S_OK;
    BOOL isNull = FALSE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));
    if (isNull == TRUE)
    {
        return S_OK;
    }

    return EvalMetadataHelpers::ForEachFields(pMDImport, typeDef, [&](mdFieldDef fieldDef) -> HRESULT
    {
        ULONG nameLen = 0;
        IfFailRet(pMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen,
                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));

        WSTRING mdName(nameLen, '\0');
        DWORD fieldAttr = 0;
        if (FAILED(pMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr,
                                            &fieldAttr, nullptr, nullptr, nullptr, nullptr, nullptr)) ||
            (fieldAttr & fdStatic) != 0 ||
            (fieldAttr & fdLiteral) != 0)
        {
            return S_OK; // Return success to continue walking.
        }
        // Remove null terminator that was included in the length
        if (!mdName.empty() && mdName.back() == '\0')
        {
            mdName.pop_back();
        }

        WSTRING wParameterName;
        if (MetadataHelpers::GetLocalOrFieldNameKind(mdName) != MetadataHelpers::GeneratedNameKind::PrimaryConstructorParameterField ||
            FAILED(MetadataHelpers::TryParseGeneratedName(mdName, wParameterName)) ||
            usedNames.find(wParameterName) != usedNames.cend())
        {
            return S_OK; // Return success to continue walking.
        }

        const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
        {
            trValue.Free();
            IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, nullptr));
            ToRelease<ICorDebugObjectValue> trObjValue;
            IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
            IfFailRet(trObjValue->GetFieldValue(pClass, fieldDef, ppResultValue));
            return S_OK;
        };

        IfFailRet(cb(to_utf8(wParameterName.c_str()), getValue));
        if (Status == S_CAN_EXIT)
        {
            return S_CAN_EXIT; // Fast exit from the loop.
        }
        usedNames.insert(wParameterName);

        return S_OK;
    });
}

} // unnamed namespace

HRESULT WalkStackVars(ICorDebugThread *pThread, FrameLevel frameLevel, const WalkStackVarsCallback &cb)
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

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

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

    ToRelease<ICorDebugValueEnum> trLocalsEnum;
    IfFailRet(trILFrame->EnumerateLocalVariables(&trLocalsEnum));

    ULONG cLocals = 0;
    IfFailRet(trLocalsEnum->GetCount(&cLocals));

    ULONG cArguments = 0;
    ToRelease<ICorDebugValueEnum> trArgumentEnum;
    IfFailRet(trILFrame->EnumerateArguments(&trArgumentEnum));
    IfFailRet(trArgumentEnum->GetCount(&cArguments));

    // Note, we use the same order as vsdbg:
    // 1. "this" (real, or the "this" proxy field for async methods and lambdas).
    // 2. "real" arguments.
    // 3. "real" local variables.
    // 4. async/lambda object fields.

    ULONG szMethodLen = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &szMethodLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));

    DWORD methodAttr = 0;
    WSTRING szMethod(szMethodLen, '\0');
    PCCOR_SIGNATURE pSig = nullptr;
    ULONG cbSig = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, szMethod.data(), szMethodLen, nullptr,
                                         &methodAttr, &pSig, &cbSig, nullptr, nullptr));
    // Remove null terminator that was included in the length
    if (!szMethod.empty() && szMethod.back() == '\0')
    {
        szMethod.pop_back();
    }

    MetadataHelpers::GeneratedCodeKind generatedCodeKind = MetadataHelpers::GeneratedCodeKind::Normal;
    ToRelease<ICorDebugValue> trCurrentThis; // Current "this". Note, for an async method or lambda this is a special object (not the user's "this").
    ToRelease<ICorDebugValue> trUserThis;
    ToRelease<ICorDebugClass> trUserThisClass;
    mdTypeDef userThisTypeDef = mdTypeDefNil;
    // In case of a static method, this is definitely not an async/lambda case.
    if ((methodAttr & mdStatic) == 0)
    {
        ToRelease<ICorDebugClass> trClass;
        IfFailRet(trFunction->GetClass(&trClass));
        mdTypeDef typeDef = mdTypeDefNil;
        IfFailRet(trClass->GetToken(&typeDef));
        IfFailRet(MetadataHelpers::GetGeneratedCodeKind(trMDImport, szMethod, typeDef, generatedCodeKind));
        Status = trILFrame->GetArgument(0, &trCurrentThis);
        if (Status == CORDBG_E_IL_VAR_NOT_AVAILABLE)
        {
            const auto getValue = [&](ICorDebugValue **, std::string *pFallbackTypeName) -> HRESULT
            {
                if (pFallbackTypeName != nullptr)
                {
                    EvalMetadataHelpers::GetFQDisplayRealCodeTypeName(trFrame, *pFallbackTypeName);
                }
                return CORDBG_E_IL_VAR_NOT_AVAILABLE;
            };

            IfFailRet(cb("this", getValue));
            if (Status == S_CAN_EXIT)
            {
                return S_OK;
            }
            // Reset trFrame/trILFrame, since they could be neutered by the `cb` call; we need to track this case.
            trFrame.Free();
            trILFrame.Free();
        }
        else if (FAILED(Status))
        {
            return Status;
        }
        else
        {
            if (generatedCodeKind == MetadataHelpers::GeneratedCodeKind::Normal)
            {
                trCurrentThis->AddRef();
                trUserThis = trCurrentThis.GetPtr();
                trClass->AddRef();
                trUserThisClass = trClass.GetPtr();
                userThisTypeDef = typeDef;
            }
            else
            {
                // Check whether we have a real "this" value (it should be stored in ThisProxyField).
                IfFailRet(EvalMetadataHelpers::FindThisProxyFieldValue(trMDImport, trClass, typeDef, trCurrentThis, &trUserThis));
                if (trUserThis != nullptr)
                {
                    IfFailRet(EvalMetadataHelpers::GetFirstUserCodeEnclosingClass(trMDImport, typeDef, userThisTypeDef));
                    IfFailRet(trModule->GetClassFromToken(userThisTypeDef, &trUserThisClass));
                }
            }

            if (trUserThis != nullptr)
            {
                const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
                {
                    trUserThis->AddRef();
                    *ppResultValue = trUserThis;
                    return S_OK;
                };

                IfFailRet(cb("this", getValue));
                if (Status == S_CAN_EXIT)
                {
                    return S_OK;
                }
                // Reset trFrame/trILFrame, since they could be neutered by the `cb` call; we need to track this case.
                trFrame.Free();
                trILFrame.Free();
            }
        }
    }

    // A lambda can duplicate arguments into a display class local object. Make sure we call "cb" only once per unique name.
    // Note, usedNames is not used by the 'this' related code above, since it has "find first and return" logic.
    // At the same time, all code below ignores the 'this' argument/field check.
    std::unordered_set<WSTRING> usedNames;

    for (ULONG i = (methodAttr & mdStatic) == 0 ? 1 : 0; i < cArguments; i++)
    {
        // https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/metadata/imetadataimport-getparamformethodindex-method
        // The ordinal position in the parameter list where the requested parameter occurs. Parameters are numbered starting from one, with the method's return value in position zero.
        // Note, IMetaDataImport::GetParamForMethodIndex() doesn't include "this", but ICorDebugILFrame::GetArgument() does. This is why we have different logic here.
        ULONG paramNameLen = 0;
        mdParamDef paramDef = mdParamDefNil;
        const ULONG idx = ((methodAttr & mdStatic) == 0) ? i : (i + 1);
        if (FAILED(trMDImport->GetParamForMethodIndex(methodDef, idx, &paramDef)) ||
            FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, nullptr, 0, &paramNameLen,
                                             nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        WSTRING wParamName(paramNameLen, '\0');
        if (FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, wParamName.data(), paramNameLen,
                                             nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }
        // Remove null terminator that was included in the length
        if (!wParamName.empty() && wParamName.back() == '\0')
        {
            wParamName.pop_back();
        }

        const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *pFallbackTypeName) -> HRESULT
        {
            if (trFrame == nullptr) // Force trFrame/trILFrame update.
            {
                IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
                if (trFrame == nullptr)
                {
                    return E_FAIL;
                }
                IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));
            }

            Status = trILFrame->GetArgument(i, ppResultValue);
            if (Status == CORDBG_E_IL_VAR_NOT_AVAILABLE && pFallbackTypeName != nullptr)
            {
                SigElementType returnElementType;
                std::vector<SigElementType> argElementTypes;
                if (SUCCEEDED(ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType, argElementTypes, true)))
                {
                    const ULONG index = ((methodAttr & mdStatic) == 0) ? (i - 1) : i;
                    if (argElementTypes.size() > index)
                    {
                        *pFallbackTypeName = argElementTypes.at(index).metadataTypeName;
                    }
                }
            }
            return Status;
        };

        IfFailRet(cb(to_utf8(wParamName.c_str()), getValue));
        if (Status == S_CAN_EXIT)
        {
            return S_OK;
        }
        usedNames.insert(wParamName);
        // Reset trFrame/trILFrame, since they could be neutered by the `cb` call; we need to track this case.
        trFrame.Free();
        trILFrame.Free();
    }

    for (uint32_t i = 0; i < cLocals; i++)
    {
        WSTRING wLocalName;
        if (FAILED(DebugInfo::GetFrameNamedLocalVariable(trModule, methodDef, currentIlOffset, i, wLocalName)))
        {
            continue;
        }

        const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
        {
            if (trFrame == nullptr) // Force trFrame/trILFrame update.
            {
                IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
                if (trFrame == nullptr)
                {
                    return E_FAIL;
                }
                IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));
            }
            return trILFrame->GetLocalVariable(i, ppResultValue);
        };

        // Note, this method could have lambdas inside; display class local objects must also be checked,
        // since these objects could hold the current method's local variables too.
        if (MetadataHelpers::GetLocalOrFieldNameKind(wLocalName) == MetadataHelpers::GeneratedNameKind::DisplayClassLocalOrField)
        {
            ToRelease<ICorDebugValue> trDisplayClassValue;
            IfFailRet(getValue(&trDisplayClassValue, nullptr));
            IfFailRet(WalkGeneratedClassFields(trMDImport, trDisplayClassValue, currentIlOffset, usedNames, methodDef,
                                               trModule, cb));
            if (Status == S_CAN_EXIT)
            {
                return S_OK;
            }
            continue;
        }

        IfFailRet(cb(to_utf8(wLocalName.c_str()), getValue));
        if (Status == S_CAN_EXIT)
        {
            return S_OK;
        }
        usedNames.insert(wLocalName);
        // Reset trFrame/trILFrame, since they could be neutered by the `cb` call; we need to track this case.
        trFrame.Free();
        trILFrame.Free();
    }

    // Enumerate local constants (literals) from PDB
    {
        std::vector<PDB::LocalConstant> localConstants;
        if (SUCCEEDED(DebugInfo::GetLocalConstants(trModule, methodDef, currentIlOffset, localConstants)))
        {
            for (const auto &constant : localConstants)
            {
                if (usedNames.find(constant.name) != usedNames.cend())
                {
                    continue;
                }

                // Skip compiler-generated constants
                if (MetadataHelpers::IsSynthesizedLocalName(constant.name))
                {
                    continue;
                }

                const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *pFallbackTypeName) -> HRESULT
                {
                    PCCOR_SIGNATURE pSig = constant.signature.data();
                    PCCOR_SIGNATURE pSigEnd = pSig + constant.signature.size();
                    std::string realDisplayTypeName;
                    IfFailRet(EvalExec::CreateLiteralLocalValue(pThread, pSig, pSigEnd, ppResultValue, realDisplayTypeName));

                    if (pFallbackTypeName != nullptr)
                    {
                        *pFallbackTypeName = std::move(realDisplayTypeName);
                    }

                    return S_OK;
                };

                IfFailRet(cb(to_utf8(constant.name.c_str()), getValue));
                if (Status == S_CAN_EXIT)
                {
                    return S_OK;
                }
                usedNames.insert(constant.name);
            }
        }
    }

    if (generatedCodeKind != MetadataHelpers::GeneratedCodeKind::Normal && trCurrentThis != nullptr)
    {
        IfFailRet(WalkGeneratedClassFields(trMDImport, trCurrentThis, currentIlOffset, usedNames, methodDef, trModule, cb));
        if (Status == S_CAN_EXIT)
        {
            return S_OK;
        }
    }

    if (trUserThis != nullptr && trUserThisClass != nullptr && TypeFromToken(userThisTypeDef) == mdtTypeDef)
    {
        IfFailRet(WalkPrimaryConstructorParameterFields(trMDImport, trUserThisClass, userThisTypeDef, trUserThis, usedNames, cb));
        // Note: WalkPrimaryConstructorParameterFields() could return S_CAN_EXIT.
    }
    return S_OK;
}

} // namespace dncdbg::Walkers
