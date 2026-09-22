// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/evalhelpers/metadata.h"
#include "debugger/evaluation/walkers/walkers.h"
#include "debugger/evalhelpers.h"
#include "debugger/frames.h"
#include "debuginfo/debuginfo.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <list>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace dncdbg::EvalMetadataHelpers
{

// Note, could return S_CAN_EXIT for fast exit.
HRESULT ForEachFields(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const WalkFieldsCallback &cb)
{
    HRESULT Status = S_OK;
    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    while (SUCCEEDED(pMDImport->EnumFields(&hEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        if (FAILED(Status = cb(fieldDef)) ||
            Status == S_CAN_EXIT)
        {
            break;
        }
    }
    pMDImport->CloseEnum(hEnum);
    return Status;
}

// Note, could return S_CAN_EXIT for fast exit.
HRESULT ForEachProperties(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const WalkPropertiesCallback &cb)
{
    HRESULT Status = S_OK;
    mdProperty propertyDef = mdPropertyNil;
    ULONG numProperties = 0;
    HCORENUM propEnum = nullptr;
    while (SUCCEEDED(pMDImport->EnumProperties(&propEnum, currentTypeDef, &propertyDef, 1, &numProperties)) &&
           numProperties != 0)
    {
        if (FAILED(Status = cb(propertyDef)) ||
            Status == S_CAN_EXIT)
        {
            break;
        }
    }
    pMDImport->CloseEnum(propEnum);
    return Status;
}

HRESULT FindThisProxyFieldValue(IMetaDataImport *pMDImport, ICorDebugClass *pClass, mdTypeDef typeDef,
                                ICorDebugValue *pInputValue, ICorDebugValue **ppResultValue)
{
    HRESULT Status = S_OK;
    BOOL isNull = FALSE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));
    if (isNull == TRUE)
    {
        return E_INVALIDARG;
    }

    Status = ForEachFields(pMDImport, typeDef,
        [&](mdFieldDef fieldDef) -> HRESULT
        {
            ULONG nameLen = 0;
            IfFailRet(pMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen,
                                               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));

            WSTRING mdName(nameLen, '\0');
            if (SUCCEEDED(pMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr,
                                                   nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                // Remove null terminator that was included in the length
                if (!mdName.empty() && mdName.back() == '\0')
                {
                    mdName.pop_back();
                }

                const auto getValue = [&](ICorDebugValue **ppResultValue) -> HRESULT
                {
                    ToRelease<ICorDebugObjectValue> trObjValue;
                    IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
                    IfFailRet(trObjValue->GetFieldValue(pClass, fieldDef, ppResultValue));
                    return S_OK;
                };

                const MetadataHelpers::GeneratedNameKind generatedNameKind = MetadataHelpers::GetLocalOrFieldNameKind(mdName);
                if (generatedNameKind == MetadataHelpers::GeneratedNameKind::ThisProxyField)
                {
                    IfFailRet(getValue(ppResultValue));
                    return S_CAN_EXIT; // Fast exit from the loop.
                }
                else if (generatedNameKind == MetadataHelpers::GeneratedNameKind::DisplayClassLocalOrField)
                {
                    ToRelease<ICorDebugValue> trDisplayClassValue;
                    IfFailRet(getValue(&trDisplayClassValue));
                    ToRelease<ICorDebugClass> trDisplayClass;

                    ToRelease<ICorDebugValue2> trValue2;
                    IfFailRet(trDisplayClassValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
                    ToRelease<ICorDebugType> trType;
                    IfFailRet(trValue2->GetExactType(&trType));
                    IfFailRet(trType->GetClass(&trDisplayClass));
                    mdTypeDef displayClassTypeDef = mdTypeDefNil;
                    IfFailRet(trDisplayClass->GetToken(&displayClassTypeDef));

                    IfFailRet(FindThisProxyFieldValue(pMDImport, trDisplayClass, displayClassTypeDef, trDisplayClassValue, ppResultValue));
                    if (ppResultValue != nullptr)
                    {
                        return S_CAN_EXIT; // Fast exit from the loop.
                    }
                }
            }
            return S_OK; // Return success to continue walking.
        });

    // Note, ForEachFields() could return S_CAN_EXIT for fast exit.
    return SUCCEEDED(Status) ? S_OK : Status;
}

HRESULT GetFirstUserCodeEnclosingClass(IMetaDataImport *pMDImport, mdTypeDef typeDef, mdTypeDef &userTypeDef)
{
    HRESULT Status = S_OK;

    while (true)
    {
        ULONG nameLen = 0;
        IfFailRet(pMDImport->GetTypeDefProps(typeDef, nullptr, 0, &nameLen, nullptr, nullptr));

        WSTRING mdName(nameLen, '\0');
        IfFailRet(pMDImport->GetTypeDefProps(typeDef, mdName.data(), nameLen, nullptr, nullptr, nullptr));
        // Remove null terminator that was included in the length
        if (!mdName.empty() && mdName.back() == '\0')
        {
            mdName.pop_back();
        }

        if (!MetadataHelpers::IsSynthesizedLocalName(mdName))
        {
            userTypeDef = typeDef;
            break;
        }

        IfFailRet(pMDImport->GetNestedClassProps(typeDef, &typeDef));
    };

    return S_OK;
}

HRESULT GetFQDisplayTypeName(ICorDebugThread *pThread, FrameLevel frameLevel, std::string &displayTypeName, bool &haveThis)
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

    ULONG szMethodLen = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &szMethodLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));

    DWORD methodAttr = 0;
    WSTRING szMethod(szMethodLen, '\0');
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, szMethod.data(), szMethodLen, nullptr,
                                         &methodAttr, nullptr, nullptr, nullptr, nullptr));
    // Remove null terminator that was included in the length
    if (!szMethod.empty() && szMethod.back() == '\0')
    {
        szMethod.pop_back();
    }

    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trFunction->GetClass(&trClass));
    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&typeDef));
    // We are inside a method of this class; if typeDef is not a TypeDef token, something has definitely gone wrong.
    if (TypeFromToken(typeDef) != mdtTypeDef)
    {
        return E_FAIL;
    }

    std::list<std::string> args;
    MetadataHelpers::GetGenericArgs(trFrame, args);

    haveThis = ((methodAttr & mdStatic) == 0);
    // In case of a static method, this is definitely not an async/lambda case.
    if (!haveThis)
    {
        return MetadataHelpers::GetFQDisplayNameForToken(typeDef, trMDImport, displayTypeName, &args);
    }

    MetadataHelpers::GeneratedCodeKind generatedCodeKind = MetadataHelpers::GeneratedCodeKind::Normal;
    IfFailRet(MetadataHelpers::GetGeneratedCodeKind(trMDImport, szMethod, typeDef, generatedCodeKind));
    if (generatedCodeKind == MetadataHelpers::GeneratedCodeKind::Normal)
    {
        return MetadataHelpers::GetFQDisplayNameForToken(typeDef, trMDImport, displayTypeName, &args);
    }

    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));
    ToRelease<ICorDebugValue> trCurrentThis;
    IfFailRet(trILFrame->GetArgument(0, &trCurrentThis));

    // Check whether we have a real "this" value (it should be stored in ThisProxyField).
    ToRelease<ICorDebugValue> trUserThis;
    IfFailRet(FindThisProxyFieldValue(trMDImport, trClass, typeDef, trCurrentThis, &trUserThis));
    haveThis = (trUserThis != nullptr);

    // Find the first user code enclosing class, since the compiler adds async/lambda as a nested class.
    mdTypeDef userTypeDef = mdTypeDefNil;
    IfFailRet(GetFirstUserCodeEnclosingClass(trMDImport, typeDef, userTypeDef));

    return MetadataHelpers::GetFQDisplayNameForToken(userTypeDef, trMDImport, displayTypeName, &args);
}

HRESULT GetFQDisplayRealCodeMethodName(ICorDebugFrame *pFrame, std::string &displayName)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pFrame->GetFunction(&trFunction));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));
    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(trFunction->GetToken(&methodToken));

    mdMethodDef methodDef = mdMethodDefNil;
    bool asyncMethod = true;
    if (FAILED(DebugInfo::GetStateMachineKickoffMethod(trModule, methodToken, methodDef)) &&
        FAILED(MetadataHelpers::GetStateMachineKickoffMethod(trModule, methodToken, methodDef)))
    {
        methodDef = methodToken;
        asyncMethod = false;
    }

    std::ostringstream ss;
    std::string displayTypeName;
    std::string displayMethodName;
    IfFailRet(MetadataHelpers::GetDisplayTypeAndMethodName(pFrame, methodDef, displayTypeName, displayMethodName));

    if (!displayTypeName.empty())
    {
        ss << displayTypeName << ".";
    }
    ss << displayMethodName << "(";

    const auto addMethodParameters = [&]() -> HRESULT
    {
        ToRelease<IUnknown> trUnknown;
        IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        ToRelease<ICorDebugILFrame> trILFrame;
        IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

        DWORD methodAttr = 0;
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, nullptr,
                                             &methodAttr, &pSig, &cbSig, nullptr, nullptr));

        SigElementType returnElementType;
        std::vector<SigElementType> argElementTypes;
        // Ignore failed return code here; we need all we could parse from the sig.
        ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType, argElementTypes, true);

        ULONG cArguments = 0;
        std::unordered_map<std::string, ToRelease<ICorDebugValue>> asyncMethodParams;
        if (!asyncMethod)
        {
            ToRelease<ICorDebugValueEnum> trArgumentEnum;
            IfFailRet(trILFrame->EnumerateArguments(&trArgumentEnum));
            IfFailRet(trArgumentEnum->GetCount(&cArguments));
            // Decrement argument count to exclude `this` for instance methods.
            if ((methodAttr & mdStatic) == 0)
            {
                cArguments--;
            }
        }
        else
        {
            ToRelease<ICorDebugValue> trCurrentThis;
            if (SUCCEEDED(trILFrame->GetArgument(0, &trCurrentThis)))
            {
                std::unordered_set<WSTRING> usedNames;
                Walkers::WalkGeneratedClassFields(trMDImport, trCurrentThis, 0, usedNames, methodDef, trModule,
                    [&](const std::string &name, const Walkers::GetValueCallback &getValue) -> HRESULT
                    {
                        ToRelease<ICorDebugValue> trValue;
                        if (FAILED(getValue(&trValue, nullptr)))
                        {
                            return S_OK;
                        }

                        asyncMethodParams.emplace(name, trValue.Detach());
                        cArguments++;
                        return S_OK;
                    });
            }
        }

        for (ULONG i = 0; i < cArguments; i++)
        {
            // https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/metadata/imetadataimport-getparamformethodindex-method
            // The ordinal position in the parameter list where the requested parameter occurs. Parameters are numbered starting from one, with the method's return value in position zero.
            // Note: IMetaDataImport::GetParamForMethodIndex() doesn't include "this", but ICorDebugILFrame::GetArgument() does. This is why we have different logic here.
            const ULONG idx = i + 1;
            mdParamDef paramDef = mdParamDefNil;
            ULONG paramNameLen = 0;
            if (FAILED(trMDImport->GetParamForMethodIndex(methodDef, idx, &paramDef)) ||
                FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, nullptr, 0,
                                                 &paramNameLen, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            std::vector<WCHAR> wParamName(paramNameLen, '\0');
            if (FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, wParamName.data(), paramNameLen,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            if (i != 0)
            {
                ss << ", ";
            }

            if (argElementTypes.size() > i && !argElementTypes.at(i).parameterModifier.empty())
            {
                ss << argElementTypes.at(i).parameterModifier << " ";
            }

            const std::string paramName = to_utf8(wParamName.data());
            const auto asyncParam = asyncMethodParams.find(paramName);

            std::string displayTypeName;
            ToRelease<ICorDebugValue> trValue;
            if ((asyncMethod && asyncParam != asyncMethodParams.cend() &&
                 SUCCEEDED(MetadataHelpers::GetFQDisplayTypeName(asyncParam->second, displayTypeName))) ||
                (!asyncMethod &&
                 SUCCEEDED(Status = trILFrame->GetArgument((methodAttr & mdStatic) == 0 ? i + 1 : i, &trValue)) &&
                 SUCCEEDED(MetadataHelpers::GetFQDisplayTypeName(trValue, displayTypeName))))
            {
                ss << displayTypeName << " ";
            }
            else if (argElementTypes.size() > i && !argElementTypes.at(i).metadataTypeName.empty() &&
                     // TODO: replace with proper type and method generic parameters
                     argElementTypes.at(i).genericElemType != ELEMENT_TYPE_VAR &&
                     argElementTypes.at(i).genericElemType != ELEMENT_TYPE_MVAR)
            {
                ss << MetadataHelpers::ConvertMetadataToDisplayName(argElementTypes.at(i).metadataTypeName, nullptr) << " ";
            }
            // else
            //    in case of failure, ignore the parameter type and print only the parameter name

            ss << paramName;
        }
        return S_OK;
    };
    addMethodParameters();

    ss << ")";
    displayName = ss.str();
    return S_OK;
}

} // namespace dncdbg::EvalMetadataHelpers
