// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/primitivetypes/types.h"
#include "debugger/evalhelpers.h"
#include "debugger/valueprint.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <cassert>
#include <unordered_set>

namespace dncdbg::PrimitiveTypes
{

bool IsPrimitiveType(CorElementType elemType)
{
    static const std::unordered_set<CorElementType> supportedElementTypes{
        ELEMENT_TYPE_BOOLEAN, ELEMENT_TYPE_U1, ELEMENT_TYPE_I1,    ELEMENT_TYPE_CHAR, ELEMENT_TYPE_R8,
        ELEMENT_TYPE_R4,      ELEMENT_TYPE_I4, ELEMENT_TYPE_U4,    ELEMENT_TYPE_I8,   ELEMENT_TYPE_U8,
        ELEMENT_TYPE_I2,      ELEMENT_TYPE_U2, ELEMENT_TYPE_STRING};

    return supportedElementTypes.find(elemType) != supportedElementTypes.end();
}

HRESULT GetOperandData(ICorDebugValue *pValue, CorElementType elemType, PrimitiveValue &primValue)
{
    HRESULT Status = S_OK;

    if (elemType == ELEMENT_TYPE_STRING || elemType == ELEMENT_TYPE_BOOLEAN)
    {
        ToRelease<ICorDebugValue> trValue;
        BOOL isNull = FALSE;
        IfFailRet(DereferenceAndUnboxValue(pValue, &trValue, &isNull));

        if (elemType == ELEMENT_TYPE_STRING)
        {
            std::string String;
            if (isNull == FALSE)
            {
                IfFailRet(PrintStringValue(trValue, String));
            }
            primValue.type = ELEMENT_TYPE_STRING;
            primValue.value = String;
        }
        else if (elemType == ELEMENT_TYPE_BOOLEAN)
        {
            uint8_t boolValue = 0;
            ToRelease<ICorDebugGenericValue> trGenValue;
            IfFailRet(pValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenValue)));
            IfFailRet(trGenValue->GetValue(&boolValue));
            primValue.type = ELEMENT_TYPE_BOOLEAN;
            primValue.value.emplace<bool>(boolValue == 1);
        }

        return S_OK;
    }

    switch (elemType)
    {
    case ELEMENT_TYPE_U1:
        primValue.value.emplace<uint8_t>(static_cast<uint8_t>(0));
        break;

    case ELEMENT_TYPE_I1:
        primValue.value.emplace<int8_t>(static_cast<int8_t>(0));
        break;

    case ELEMENT_TYPE_CHAR:
        primValue.value.emplace<WCHAR>(static_cast<WCHAR>(0));
        break;

    case ELEMENT_TYPE_R8:
        primValue.value.emplace<double>(static_cast<double>(0));
        break;

    case ELEMENT_TYPE_R4:
        primValue.value.emplace<float>(static_cast<float>(0));
        break;

    case ELEMENT_TYPE_I4:
        primValue.value.emplace<int32_t>(static_cast<int32_t>(0));
        break;

    case ELEMENT_TYPE_U4:
        primValue.value.emplace<uint32_t>(static_cast<uint32_t>(0));
        break;

    case ELEMENT_TYPE_I8:
        primValue.value.emplace<int64_t>(static_cast<int64_t>(0));
        break;

    case ELEMENT_TYPE_U8:
        primValue.value.emplace<uint64_t>(static_cast<uint64_t>(0));
        break;

    case ELEMENT_TYPE_I2:
        primValue.value.emplace<int16_t>(static_cast<int16_t>(0));
        break;

    case ELEMENT_TYPE_U2:
        primValue.value.emplace<uint16_t>(static_cast<uint16_t>(0));
        break;

    default:
        return E_INVALIDARG;
    }

    ToRelease<ICorDebugGenericValue> trGenValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenValue)));

    std::visit(
        [&](auto &arg)
        {
            Status = trGenValue->GetValue(&arg);
        },
        primValue.value);

    primValue.type = elemType;
    return Status;
}

HRESULT CreateICorValue(ICorDebugThread *pThread, CorElementType elemType, void *ptr, ICorDebugValue **ppValue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugEval> trEval;
    IfFailRet(pThread->CreateEval(&trEval));
    IfFailRet(trEval->CreateValue(elemType, nullptr, ppValue));

    if (ptr == nullptr)
    {
        return S_OK;
    }

    ToRelease<ICorDebugGenericValue> trGenValue;
    IfFailRet((*ppValue)->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenValue)));
    return trGenValue->SetValue(ptr);
}

HRESULT CreateICorValue(ICorDebugThread *pThread, EvalHelpers *pEvalHelpers, PrimitiveValue &primValue, ICorDebugValue **ppValue)
{
    if (primValue.type == ELEMENT_TYPE_STRING)
    {
        assert(std::holds_alternative<std::string>(primValue.value));
        return pEvalHelpers->CreateString(pThread, std::get<std::string>(primValue.value), ppValue);
    }
    else if (primValue.type == ELEMENT_TYPE_BOOLEAN)
    {
        assert(std::holds_alternative<bool>(primValue.value));
        uint8_t boolValue = std::get<bool>(primValue.value) ? 1 : 0;
        return CreateICorValue(pThread, ELEMENT_TYPE_BOOLEAN, &boolValue, ppValue);
    }

    HRESULT Status = S_OK;
    std::visit(
        [&](auto &arg)
        {
            Status = CreateICorValue(pThread, primValue.type, &arg, ppValue);
        },
        primValue.value);

    return Status;
}

} // namespace dncdbg::PrimitiveTypes
