// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/primitivetypes/types.h"
#include "debugger/evalhelpers.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <cstdint>

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

    return std::visit([&](const auto &arg) -> HRESULT
    {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::monostate> ||
                      std::is_same_v<T, bool> ||
                      std::is_floating_point_v<T>)
        {
            return E_INVALIDARG;
        }
        else
        {
            if constexpr (std::is_signed_v<T>)
            {
                if (arg < 0)
                {
                    return E_INVALIDARG;
                }
            }

            if constexpr (sizeof(T) > sizeof(uint32_t))
            {
                if (arg > static_cast<T>(UINT32_MAX))
                {
                    return E_INVALIDARG;
                }
            }

            number = static_cast<uint32_t>(arg); // NOLINT(bugprone-signed-char-misuse,cert-str34-c)
            return S_OK;
        }
    }, primValue);
}

} // namespace dncdbg::PrimitiveTypes
