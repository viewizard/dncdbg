// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/evalhelpers/value.h"
#include "debugger/evaluation/evalhelpers/systemtypes.h"
#include "debugger/evaluation/walkers/walkers.h"
#include "metadata/enumerations.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace dncdbg::EvalValueHelpers
{

// From strike.cpp
HRESULT DereferenceAndUnboxValue(ICorDebugValue *pValue, ICorDebugValue **ppOutputValue, BOOL *pIsNull)
{
    *ppOutputValue = nullptr;
    if (pIsNull != nullptr)
    {
        *pIsNull = FALSE;
    }

    pValue->AddRef();
    ToRelease<ICorDebugValue> trCurrentValue(pValue);
    HRESULT Status = S_OK;

    while (true)
    {
        ToRelease<ICorDebugReferenceValue> trReferenceValue;
        if (SUCCEEDED(trCurrentValue->QueryInterface(IID_ICorDebugReferenceValue, reinterpret_cast<void **>(&trReferenceValue))))
        {
            BOOL isNull = FALSE;
            IfFailRet(trReferenceValue->IsNull(&isNull));
            if (isNull == FALSE)
            {
                ToRelease<ICorDebugValue> trDereferencedValue;
                IfFailRet(trReferenceValue->Dereference(&trDereferencedValue));
                trCurrentValue = trDereferencedValue.Detach();
                continue;
            }
            else
            {
                if (pIsNull != nullptr)
                {
                    *pIsNull = TRUE;
                }
                break; // unboxed until null reference
            }
        }

        ToRelease<ICorDebugBoxValue> trBoxedValue;
        if (SUCCEEDED(trCurrentValue->QueryInterface(IID_ICorDebugBoxValue, reinterpret_cast<void **>(&trBoxedValue))))
        {
            ToRelease<ICorDebugObjectValue> trUnboxedValue;
            IfFailRet(trBoxedValue->GetObject(&trUnboxedValue));
            trCurrentValue = trUnboxedValue.Detach();
            continue;
        }

        break; // unboxed until object
    }

    *ppOutputValue = trCurrentValue.Detach();
    return S_OK;
}

HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, ICorDebugValue **ppHasValueValue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    if (trType == nullptr)
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trType->GetClass(&trClass));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trClass->GetModule(&trModule));
    mdTypeDef currentTypeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&currentTypeDef));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&trMDImport)));

    ToRelease<ICorDebugObjectValue> trObjValue;
    ToRelease<ICorDebugValue> trUnboxedResultValue;
    IfFailRet(DereferenceAndUnboxValue(pValue, &trUnboxedResultValue));
    IfFailRet(trUnboxedResultValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));

    IfFailRet(MetadataEnumerations::ForEachField(trMDImport, currentTypeDef,
        [&](mdFieldDef fieldDef) -> HRESULT
        {
            ULONG nameLen = 0;
            if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                return S_OK; // Return success to continue walking.
            }

            WSTRING mdName(nameLen, '\0');
            if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                return S_OK; // Return success to continue walking.
            }

            // Remove null terminator that was included in the length
            if (!mdName.empty() && mdName.back() == '\0')
            {
                mdName.pop_back();
            }

            // https://github.com/dotnet/runtime/blob/adba54da2298de9c715922b506bfe17a974a3650/src/libraries/System.Private.CoreLib/src/System/Nullable.cs#L24
            if (mdName == W("value"))
            {
                IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppValueValue));
            }

            // https://github.com/dotnet/runtime/blob/adba54da2298de9c715922b506bfe17a974a3650/src/libraries/System.Private.CoreLib/src/System/Nullable.cs#L23
            if (mdName == W("hasValue"))
            {
                IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppHasValueValue));
            }

            return S_OK; // Return success to continue walking.
        }));

    return (*ppValueValue == nullptr || *ppHasValueValue == nullptr) ? E_FAIL : S_OK;
}

HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, bool &hasValue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trHasValueValue;
    IfFailRet(GetNullableValue(pValue, ppValueValue, &trHasValueValue));

    BOOL isNull = TRUE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(trHasValueValue, &trValue, &isNull));

    if (isNull == TRUE)
    {
        return E_FAIL;
    }

    uint8_t boolHasValue = 0;
    uint32_t cbSize = 0;
    IfFailRet(trValue->GetSize(&cbSize));
    if (cbSize != sizeof(boolHasValue))
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    IfFailRet(trGenericValue->GetValue(&boolHasValue));

    hasValue = (boolHasValue == 1);

    return S_OK;
}

HRESULT GetDictionaryItemValue(ICorDebugThread *pThread, ICorDebugValue *pValue,
                               FormatSpecifier specifier, ICorDebugValue **ppItemValue)
{
    HRESULT Status = S_OK;

    IfFailRet(Walkers::WalkMembers(pValue, pThread, FrameLevel{0}, false, specifier,
        [&](ICorDebugType *, bool isStatic, const std::string &name,
            const Walkers::GetValueCallback &getValue, Walkers::SetterData *, std::string *) -> HRESULT
        {
            if (isStatic || name != "Value")
            {
                return S_OK; // Return success to continue walking.
            }

            IfFailRet(getValue(ppItemValue, nullptr));
            return S_CAN_EXIT;
        }));

    return (*ppItemValue == nullptr) ? E_FAIL : S_OK;
}

HRESULT GetArrayElement(ICorDebugValue *pInputValue, std::vector<uint32_t> &indexes, ICorDebugValue **ppResultValue)
{
    HRESULT Status = S_OK;

    if (indexes.empty())
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugArrayValue> trArrayVal;
    IfFailRet(pInputValue->QueryInterface(IID_ICorDebugArrayValue, reinterpret_cast<void **>(&trArrayVal)));

    uint32_t nRank = 0;
    IfFailRet(trArrayVal->GetRank(&nRank));

    if (indexes.size() != nRank)
    {
        return E_FAIL;
    }

#ifdef BIT64
    assert(indexes.size() <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
#endif
    return trArrayVal->GetElement(static_cast<uint32_t>(indexes.size()), indexes.data(), ppResultValue);
}

bool IsEnumeration(ICorDebugType *pInputType)
{
    mdTypeDef systemEnumTypeDef = mdTypeDefNil;
    CORDB_ADDRESS systemEnumModAddress = 0;
    ToRelease<ICorDebugClass> trEnumClass;
    ToRelease<ICorDebugModule> trEnumModule;
    if (FAILED(SystemTypes::GetClass(SystemTypes::SystemType::Enum, &trEnumClass)) ||
        FAILED(trEnumClass->GetModule(&trEnumModule)) ||
        FAILED(trEnumClass->GetToken(&systemEnumTypeDef)) ||
        FAILED(trEnumModule->GetBaseAddress(&systemEnumModAddress)))
    {
        return false;
    }

    ToRelease<ICorDebugType> trBaseType;
    ToRelease<ICorDebugClass> trBaseClass;
    ToRelease<ICorDebugModule> trModule;
    CORDB_ADDRESS modAddress = 0;
    mdTypeDef typeDef = mdTypeDefNil;
    return SUCCEEDED(pInputType->GetBase(&trBaseType)) &&
           trBaseType != nullptr &&
           SUCCEEDED(trBaseType->GetClass(&trBaseClass)) &&
           SUCCEEDED(trBaseClass->GetModule(&trModule)) &&
           SUCCEEDED(trModule->GetBaseAddress(&modAddress)) &&
           modAddress == systemEnumModAddress &&
           SUCCEEDED(trBaseClass->GetToken(&typeDef)) &&
           typeDef == systemEnumTypeDef;
}

bool IsEnumeration(ICorDebugValue *pInputValue)
{
    BOOL isNull = FALSE;
    ToRelease<ICorDebugValue> trValue;
    if (FAILED(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull)) ||
        isNull == TRUE)
    {
        return false;
    }

    ToRelease<ICorDebugValue2> trValue2;
    ToRelease<ICorDebugType> trType;
    return SUCCEEDED(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2))) &&
           SUCCEEDED(trValue2->GetExactType(&trType)) &&
           IsEnumeration(trType);
}

} // namespace dncdbg::EvalValueHelpers
