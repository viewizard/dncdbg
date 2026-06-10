// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/primitivetypes/types.h"
#include "debugger/evalhelpers.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <limits>

namespace dncdbg::PrimitiveTypes
{

HRESULT ForceCastToUint(ICorDebugValue *pInputValue, uint32_t &number)
{
    HRESULT Status = S_OK;

    BOOL isNull = FALSE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));
    if (isNull == TRUE)
    {
        return E_INVALIDARG;
    }

    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(trValue->GetType(&elemType));
    if (!IsPrimitiveType(elemType))
    {
        return E_INVALIDARG;
    }

    PrimitiveValue primValue;
    IfFailRet(GetOperandData(trValue, elemType, primValue));

    return std::visit(overloaded {
        [&](const WCHAR &arg)-> HRESULT
        {
            number = static_cast<uint32_t>(arg);
            return S_OK;
        },
        [&](const uint8_t &arg)-> HRESULT
        {
            number = static_cast<uint32_t>(arg);
            return S_OK;
        },
        [&](const uint16_t &arg)-> HRESULT
        {
            number = static_cast<uint32_t>(arg);
            return S_OK;
        },
        [&](const uint32_t &arg) -> HRESULT
        {
            number = arg;
            return S_OK;
        },
        [&](const uint64_t &arg) -> HRESULT
        {
            if (arg > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
            {
                return E_INVALIDARG;
            }
            number = static_cast<uint32_t>(arg);
            return S_OK;
        },
        [&](const int8_t &arg) -> HRESULT
        {
            if (arg < 0)
            {
                return E_INVALIDARG;
            }
            number = static_cast<uint32_t>(arg); // NOLINT(bugprone-signed-char-misuse,cert-str34-c)
            return S_OK;
        },
        [&](const int16_t &arg) -> HRESULT
        {
            if (arg < 0)
            {
                return E_INVALIDARG;
            }
            number = static_cast<uint32_t>(arg);
            return S_OK;
        },
        [&](const int32_t &arg) -> HRESULT
        {
            if (arg < 0)
            {
                return E_INVALIDARG;
            }
            number = static_cast<uint32_t>(arg);
            return S_OK;
        },
        [&](const int64_t &arg) -> HRESULT
        {
            if (arg < 0 ||
                arg > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
            {
                return E_INVALIDARG;
            }
            number = static_cast<uint32_t>(arg);
            return S_OK;
        },
        [](const auto &) -> HRESULT
        {
            return E_INVALIDARG;
        }
    }, primValue);
}

} // namespace dncdbg::PrimitiveTypes
