// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/walkers/walkers.h"
#include "metadata/enumerations.h"
#include "metadata/helpers.h"
#include "metadata/metadataname.h"
#include "metadata/sigparse.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
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
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&trMDImport)));

        IfFailRet(MetadataEnumerations::ForEachMethod(trMDImport, currentTypeDef,
            [&](mdMethodDef methodDef) -> HRESULT
            {
                DWORD methodAttr = 0;
                PCCOR_SIGNATURE pSig = nullptr;
                ULONG cbSig = 0;
                const auto readMethodName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
                {
                    return trMDImport->GetMethodProps(methodDef, nullptr, buf, size, pNeeded,
                                                      &methodAttr, &pSig, &cbSig, nullptr, nullptr);
                };
                std::string funcName;
                if (FAILED(MetadataName::ReadUtf8(readMethodName, funcName)))
                {
                    return S_OK;
                }

                SigElementType returnElementType;
                std::vector<SigElementType> argElementTypes;
                uint32_t methodGenParamCount = 0;
                if (FAILED(ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType,
                                          argElementTypes, false, &methodGenParamCount)))
                {
                    return S_OK;
                }

                if (FAILED(ApplyGenericTypeParameters(genericTypeParameters, returnElementType)))
                {
                    return S_OK;
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
                    return S_OK;
                }

                const bool isStatic = ((methodAttr & mdStatic) != 0U);

                const auto getFunction = [&](ICorDebugFunction **ppResultFunction) -> HRESULT
                {
                    return trModule->GetFunctionFromToken(methodDef, ppResultFunction);
                };

                return cb(isStatic, funcName, returnElementType, argElementTypes,
                          methodGenParamCount, getFunction);
            }));

        if (Status == S_CAN_EXIT)
        {
            if (ppResultType != nullptr)
            {
                *ppResultType = trInputType.Detach();
            }
            return S_OK;
        }

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
