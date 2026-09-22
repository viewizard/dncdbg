// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/walkers/walkers.h"
#include "debugger/evaluation/evalhelpers/metadata.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"
#include "utils/torelease.h"

namespace dncdbg::Walkers
{

HRESULT WalkIndexers(ICorDebugType *pInputType, const WalkIndexersCallback &cb)
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

        Status = EvalMetadataHelpers::ForEachProperties(trMDImport, currentTypeDef,
            [&](mdProperty propertyDef) -> HRESULT
            {
                mdMethodDef mdGetter = mdMethodDefNil;
                if (FAILED(trMDImport->GetPropertyProps(propertyDef, nullptr, nullptr, 0,
                                                        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                                        nullptr, nullptr, &mdGetter, nullptr, 0, nullptr)))
                {
                    return S_OK; // Return success to continue walking.
                }

                DWORD getterAttr = 0;
                PCCOR_SIGNATURE pSig = nullptr;
                ULONG cbSig = 0;
                if (FAILED(trMDImport->GetMethodProps(mdGetter, nullptr, nullptr, 0, nullptr, &getterAttr,
                                                      &pSig, &cbSig, nullptr, nullptr)))
                {
                    return S_OK; // Return success to continue walking.
                }

                // Note, indexers cannot be static in C#.
                if ((getterAttr & mdStatic) != 0U)
                {
                    return S_OK; // Return success to continue walking.
                }

                // A bit hacky, but a fast way to detect an indexer:
                // an instance property getter that takes arguments is an indexer for sure.
                uint32_t argCount = 0;
                if (FAILED(GetMethodArgCount(pSig, pSig + cbSig, argCount)) ||
                    argCount == 0)
                {
                    return S_OK; // Return success to continue walking.
                }

                SigElementType returnElementType;
                std::vector<SigElementType> argElementTypes;
                if (FAILED(ParseMethodSig(trMDImport, mdGetter, pSig, pSig + cbSig, returnElementType,
                                          argElementTypes, false, nullptr)))
                {
                    return S_OK; // Return success to continue walking.
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
                    return S_OK; // Return success to continue walking.
                }

                const auto getFunction = [&](ICorDebugFunction **ppResultFunction) -> HRESULT
                {
                    return trModule->GetFunctionFromToken(mdGetter, ppResultFunction);
                };

                return cb(argElementTypes, getFunction);
            });
        // Note: The code above was moved out of IfFailRet() due to MSVC error C2121.
        IfFailRet(Status);
        if (Status == S_CAN_EXIT)
        {
            return S_CAN_EXIT;
        }

        ToRelease<ICorDebugType> trBaseType;
        if (SUCCEEDED(trInputType->GetBase(&trBaseType)) && trBaseType != nullptr)
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
