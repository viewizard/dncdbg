// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/walkers/walkers.h"
#include "debugger/evaluation/evalhelpers/metadata.h"
#include "debugger/evalhelpers.h"
#include "debuginfo/debuginfo.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <cstdint>
#include <unordered_set>

namespace dncdbg::Walkers
{

// Note, could return S_CAN_EXIT for fast exit.
HRESULT WalkGeneratedClassFields(IMetaDataImport *pMDImport, ICorDebugValue *pInputValue, uint32_t currentIlOffset,
                                 std::unordered_set<WSTRING> &usedNames, mdMethodDef methodDef,
                                 ICorDebugModule *pModule, const WalkStackVarsCallback &cb)
{
    HRESULT Status = S_OK;
    BOOL isNull = FALSE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));
    if (isNull == TRUE)
    {
        return S_OK;
    }

    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trType->GetClass(&trClass));
    mdTypeDef currentTypeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&currentTypeDef));

    return EvalMetadataHelpers::ForEachFields(pMDImport, currentTypeDef,
        [&](mdFieldDef fieldDef) -> HRESULT
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

            const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
            {
                // Get the value again, since it could be neutered by an eval call in `cb` on a previous iteration.
                trValue.Free();
                IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));
                ToRelease<ICorDebugObjectValue> trObjValue;
                IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
                IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppResultValue));
                return S_OK;
            };

            const MetadataHelpers::GeneratedNameKind generatedNameKind = MetadataHelpers::GetLocalOrFieldNameKind(mdName);
            if (generatedNameKind == MetadataHelpers::GeneratedNameKind::DisplayClassLocalOrField)
            {
                ToRelease<ICorDebugValue> trDisplayClassValue;
                IfFailRet(getValue(&trDisplayClassValue, nullptr));
                IfFailRet(WalkGeneratedClassFields(pMDImport, trDisplayClassValue, currentIlOffset, usedNames, methodDef,
                                                   pModule, cb));
                if (Status == S_CAN_EXIT)
                {
                    return S_CAN_EXIT; // Fast exit from the loop.
                }
            }
            else if (generatedNameKind == MetadataHelpers::GeneratedNameKind::HoistedLocalField)
            {
                // Check that the hoisted local is in scope.
                // Note: if this check fails for any reason, ignore it and show the variable anyway, since it is not a fatal error.
                int32_t index = 0;
                if (SUCCEEDED(MetadataHelpers::TryParseSlotIndex(mdName, index)) && index >= 0 &&
                    !DebugInfo::IsHoistedLocalInScope(pModule, methodDef, currentIlOffset, static_cast<uint32_t>(index)))
                {
                    return S_OK; // Return success to continue walking.
                }

                if (usedNames.find(mdName) != usedNames.cend())
                {
                    return S_OK; // Return success to continue walking.
                }

                WSTRING wLocalName;
                if (FAILED(MetadataHelpers::TryParseGeneratedName(mdName, wLocalName)))
                {
                    return S_OK; // Return success to continue walking.
                }

                IfFailRet(cb(to_utf8(wLocalName.c_str()), getValue));
                if (Status == S_CAN_EXIT)
                {
                    return S_CAN_EXIT; // Fast exit from the loop.
                }
                usedNames.insert(wLocalName);
            }
            // Ignore any other compiler-generated fields, show only normal fields.
            else if (!MetadataHelpers::IsSynthesizedLocalName(mdName) &&
                     usedNames.find(mdName) == usedNames.cend())
            {
                IfFailRet(cb(to_utf8(mdName.c_str()), getValue));
                if (Status == S_CAN_EXIT)
                {
                    return S_CAN_EXIT; // Fast exit from the loop.
                }
                usedNames.insert(mdName);
            }
            return S_OK; // Return success to continue walking.
        });
}

} // namespace dncdbg::Walkers
