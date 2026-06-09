// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/primitivetypes/types.h"
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

    if (elemType == ELEMENT_TYPE_STRING)
    {
        ToRelease<ICorDebugValue> trValue;
        BOOL isNull = FALSE;
        IfFailRet(DereferenceAndUnboxValue(pValue, &trValue, &isNull));

        std::string String;
        if (isNull == FALSE)
        {
            IfFailRet(PrintStringValue(trValue, String));
        }
        primValue = String;
        return S_OK;
    }

    ToRelease<ICorDebugGenericValue> trGenValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenValue)));

    if (elemType == ELEMENT_TYPE_BOOLEAN)
    {
        uint8_t boolValue = 0;
        IfFailRet(trGenValue->GetValue(&boolValue));
        primValue.emplace<bool>(boolValue == 1);
        return S_OK;
    }

    switch (elemType)
    {
    case ELEMENT_TYPE_U1:
        primValue = uint8_t{0};
        break;

    case ELEMENT_TYPE_I1:
        primValue = int8_t{0};
        break;

    case ELEMENT_TYPE_CHAR:
        primValue = WCHAR{0};
        break;

    case ELEMENT_TYPE_R8:
        primValue = double{0};
        break;

    case ELEMENT_TYPE_R4:
        primValue = float{0};
        break;

    case ELEMENT_TYPE_I4:
        primValue = int32_t{0};
        break;

    case ELEMENT_TYPE_U4:
        primValue = uint32_t{0};
        break;

    case ELEMENT_TYPE_I8:
        primValue = int64_t{0};
        break;

    case ELEMENT_TYPE_U8:
        primValue = uint64_t{0};
        break;

    case ELEMENT_TYPE_I2:
        primValue = int16_t{0};
        break;

    case ELEMENT_TYPE_U2:
        primValue = uint16_t{0};
        break;

    default:
        return E_INVALIDARG;
    }

    std::visit(
        [&](auto &arg)
        {
            Status = trGenValue->GetValue(&arg);
        },
        primValue);

    return Status;
}

CorElementType GetCorElementType(const PrimitiveValue &primValue)
{
    CorElementType elemType = ELEMENT_TYPE_MAX;

    std::visit(overloaded {
        [](const std::monostate &) { ; },
        [&](const bool &) { elemType = ELEMENT_TYPE_BOOLEAN; },
        [&](const WCHAR &) { elemType = ELEMENT_TYPE_CHAR; },
        [&](const std::string &) { elemType = ELEMENT_TYPE_STRING; },
        [&](const uint8_t &) { elemType = ELEMENT_TYPE_U1; },
        [&](const uint16_t &) { elemType = ELEMENT_TYPE_U2; },
        [&](const uint32_t &) { elemType = ELEMENT_TYPE_U4; },
        [&](const uint64_t &) { elemType = ELEMENT_TYPE_U8; },
        [&](const int8_t &) { elemType = ELEMENT_TYPE_I1; },
        [&](const int16_t &) { elemType = ELEMENT_TYPE_I2; },
        [&](const int32_t &) { elemType = ELEMENT_TYPE_I4; },
        [&](const int64_t &) { elemType = ELEMENT_TYPE_I8; },
        [&](const double &) { elemType = ELEMENT_TYPE_R8; },
        [&](const float &) { elemType = ELEMENT_TYPE_R4; }
    }, primValue);

    return elemType;
}

std::string_view GetManagedTypeName(const PrimitiveValue &primValue)
{
    std::string_view name = "Unknown Type";

    std::visit(overloaded {
        [](const std::monostate &) { ; },
        [&](const bool &) { name = "bool"; },
        [&](const WCHAR &) { name = "char"; },
        [&](const std::string &) { name = "string"; },
        [&](const uint8_t &) { name = "byte"; },
        [&](const uint16_t &) { name = "ushort"; },
        [&](const uint32_t &) { name = "uint"; },
        [&](const uint64_t &) { name = "ulong"; },
        [&](const int8_t &) { name = "sbyte"; },
        [&](const int16_t &) { name = "short"; },
        [&](const int32_t &) { name = "int"; },
        [&](const int64_t &) { name = "long"; },
        [&](const double &) { name = "double"; },
        [&](const float &) { name = "float"; }
    }, primValue);

    return name;
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
    assert(!std::holds_alternative<std::monostate>(primValue) && "primValue not properly initialized.");

    CorElementType elemType = GetCorElementType(primValue);

    if (elemType == ELEMENT_TYPE_STRING)
    {
        assert(std::holds_alternative<std::string>(primValue));
        return pEvalHelpers->CreateString(pThread, std::get<std::string>(primValue), ppValue);
    }
    else if (elemType == ELEMENT_TYPE_BOOLEAN)
    {
        assert(std::holds_alternative<bool>(primValue));
        uint8_t boolValue = std::get<bool>(primValue) ? 1 : 0;
        return CreateICorValue(pThread, ELEMENT_TYPE_BOOLEAN, &boolValue, ppValue);
    }

    HRESULT Status = S_OK;
    std::visit(
        [&](auto &arg)
        {
            Status = CreateICorValue(pThread, elemType, &arg, ppValue);
        },
        primValue);

    return Status;
}

} // namespace dncdbg::PrimitiveTypes
