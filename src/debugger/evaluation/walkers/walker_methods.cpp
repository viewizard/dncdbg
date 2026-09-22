// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/walkers/walkers.h"
#include "metadata/helpers.h"
#include "metadata/sigparse.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <vector>

namespace dncdbg::Walkers
{

HRESULT WalkMethods(ICorDebugValue *pInputTypeValue, bool walkBaseType, const WalkMethodsCallback &cb)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(pInputTypeValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    ToRelease<ICorDebugType> trResultType;

    return WalkMethods(trType, walkBaseType, &trResultType, cb);
}

HRESULT WalkMethods(ICorDebugType *pInputType, bool walkBaseType, ICorDebugType **ppResultType,
                    const WalkMethodsCallback &cb)
{
    HRESULT Status = S_OK;
    pInputType->AddRef();
    ToRelease<ICorDebugType> trInputType(pInputType);

    std::vector<SigElementType> genericTypeParameters;
    IfFailRet(MetadataHelpers::GetGenericTypeParameters(pInputType, genericTypeParameters));

    while (trInputType != nullptr)
    {
        ToRelease<ICorDebugClass> trClass;
        IfFailRet(trInputType->GetClass(&trClass));
        ToRelease<ICorDebugModule> trModule;
        IfFailRet(trClass->GetModule(&trModule));
        mdTypeDef currentTypeDef = mdTypeDefNil;
        IfFailRet(trClass->GetToken(&currentTypeDef));
        ToRelease<IUnknown> trUnknown;
        IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        ULONG numMethods = 0;
        HCORENUM fEnum = nullptr;
        mdMethodDef methodDef = mdMethodDefNil;
        while (SUCCEEDED(trMDImport->EnumMethods(&fEnum, currentTypeDef, &methodDef, 1, &numMethods)) && numMethods != 0)
        {
            ULONG nameLen = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &nameLen,
                                                  nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            std::vector<WCHAR> szFunctionName(nameLen, '\0');
            DWORD methodAttr = 0;
            PCCOR_SIGNATURE pSig = nullptr;
            ULONG cbSig = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, szFunctionName.data(), nameLen, nullptr,
                                                  &methodAttr, &pSig, &cbSig, nullptr, nullptr)))
            {
                continue;
            }

            SigElementType returnElementType;
            std::vector<SigElementType> argElementTypes;
            uint32_t methodGenParamCount = 0;
            if (FAILED(ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType,
                                      argElementTypes, false, &methodGenParamCount)))
            {
                continue;
            }

            if (FAILED(ApplyGenericTypeParameters(genericTypeParameters, returnElementType)))
            {
                continue;
            }

            bool applyFailed = false;
            for (auto &argType : argElementTypes)
            {
                if (FAILED(ApplyGenericTypeParameters(genericTypeParameters, argType)))
                {
                    applyFailed = true;
                }
            }
            if (applyFailed)
            {
                continue;
            }

            const bool isStatic = ((methodAttr & mdStatic) != 0U);

            const auto getFunction = [&](ICorDebugFunction **ppResultFunction) -> HRESULT
            {
                return trModule->GetFunctionFromToken(methodDef, ppResultFunction);
            };

            IfFailRet(cb(isStatic, to_utf8(szFunctionName.data()), returnElementType, argElementTypes, methodGenParamCount, getFunction));
            if (Status == S_CAN_EXIT)
            {
                if (ppResultType != nullptr)
                {
                    *ppResultType = trInputType.Detach();
                }
                trMDImport->CloseEnum(fEnum);
                return S_OK;
            }
        }
        trMDImport->CloseEnum(fEnum);

        ToRelease<ICorDebugType> trBaseType;
        if (walkBaseType &&
            SUCCEEDED(trInputType->GetBase(&trBaseType)) && trBaseType != nullptr)
        {
            trInputType = trBaseType.Detach();
        }
        else
        {
            trInputType.Free();
        }
    }

    return S_OK;
}

} // namespace dncdbg::Walkers
