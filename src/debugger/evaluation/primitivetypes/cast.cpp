// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/primitivetypes/types.h"
#include "debugger/evalhelpers.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <limits>
#include <type_traits>

namespace dncdbg::PrimitiveTypes
{

namespace
{

// https://docs.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/conversions#implicit-numeric-conversions
template <typename From, typename To>
constexpr bool IsImplicitCastable()
{
    if constexpr (std::is_same_v<From, To>)
    {
        return true;
    }

    if constexpr (std::is_same_v<From, std::monostate> || std::is_same_v<To, std::monostate>)
    {
        return false;
    }

    if constexpr (std::is_same_v<From, bool> || std::is_same_v<To, bool>)
    {
        return false;
    }

    if constexpr (std::is_same_v<From, WCHAR>)
    {
        return std::is_same_v<To, uint16_t> || std::is_same_v<To, int32_t> ||
               std::is_same_v<To, uint32_t> || std::is_same_v<To, int64_t> ||
               std::is_same_v<To, uint64_t> || std::is_same_v<To, float> ||
               std::is_same_v<To, double>;
    }
    if constexpr (std::is_same_v<To, WCHAR>)
    {
        return false;
    }

    if constexpr (std::is_integral_v<From> && std::is_integral_v<To>)
    {
        if constexpr (std::is_signed_v<From> && std::is_unsigned_v<To>)
        {
            return false;
        }

        if constexpr (std::is_unsigned_v<From> && std::is_signed_v<To>)
        {
            return sizeof(To) > sizeof(From);
        }

        return sizeof(To) >= sizeof(From);
    }

    if constexpr (std::is_integral_v<From> && std::is_floating_point_v<To>)
    {
        return true;
    }

    if constexpr (std::is_floating_point_v<From> && std::is_integral_v<To>)
    {
        return false;
    }

    if constexpr (std::is_floating_point_v<From> && std::is_floating_point_v<To>)
    {
        return sizeof(To) >= sizeof(From);
    }

    return false;
}

} // unnamed namespace

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
    IfFailRet(GetPrimitiveData(trValue, primValue));

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
                if (arg > static_cast<T>(std::numeric_limits<uint32_t>::max()))
                {
                    return E_INVALIDARG;
                }
            }

            number = static_cast<uint32_t>(arg); // NOLINT(bugprone-signed-char-misuse,cert-str34-c)
            return S_OK;
        }
    }, primValue);
}

// https://docs.microsoft.com/en-us/dotnet/csharp/language-reference/builtin-types/integral-numeric-types#integer-literals
// If the compiler determines the type of an integer literal as int and the value
// represented by the literal is within the range of the destination type, the value
// can be implicitly converted to sbyte, byte, short, ushort, uint, ulong, nint, or nuint.
HRESULT ImplicitCastIntLiteral(ICorDebugValue *pSrcValue, ICorDebugValue *pDstValue)
{
    HRESULT Status = S_OK;

    CorElementType elemSrcType = ELEMENT_TYPE_MAX;
    IfFailRet(pSrcValue->GetType(&elemSrcType));
    if (elemSrcType != ELEMENT_TYPE_I4)
    {
        return E_INVALIDARG;
    }

    CorElementType elemDstType = ELEMENT_TYPE_MAX;
    IfFailRet(pDstValue->GetType(&elemDstType));

    ToRelease<ICorDebugGenericValue> trSrcGenValue;
    IfFailRet(pSrcValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trSrcGenValue)));
    ToRelease<ICorDebugGenericValue> trDstGenValue;
    IfFailRet(pDstValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trDstGenValue)));

    int32_t srcData = 0;
    IfFailRet(trSrcGenValue->GetValue(&srcData));

    auto checkAndWrite = [&](auto typeDummy) -> HRESULT
    {
        using T = decltype(typeDummy);

        if constexpr (std::is_floating_point_v<T>)
        {
            T casted = static_cast<T>(srcData);
            return trDstGenValue->SetValue(&casted);
        }
        else
        {
            if constexpr (std::is_unsigned_v<T>)
            {
                if (srcData < 0)
                {
                    return E_INVALIDARG;
                }
            }

            if constexpr (sizeof(T) < sizeof(int32_t))
            {
                if (srcData > static_cast<int32_t>(std::numeric_limits<T>::max()) ||
                    srcData < static_cast<int32_t>(std::numeric_limits<T>::min()))
                {
                    return E_INVALIDARG;
                }
            }

            T casted = static_cast<T>(srcData);
            return trDstGenValue->SetValue(&casted);
        }
    };

    switch (elemDstType)
    {
        case ELEMENT_TYPE_I1:   return checkAndWrite(int8_t{0});
        case ELEMENT_TYPE_U1:   return checkAndWrite(uint8_t{0});
        case ELEMENT_TYPE_I2:   return checkAndWrite(int16_t{0});
        case ELEMENT_TYPE_U2:   return checkAndWrite(uint16_t{0});
        case ELEMENT_TYPE_I4:   return checkAndWrite(int32_t{0});
        case ELEMENT_TYPE_U4:   return checkAndWrite(uint32_t{0});
        case ELEMENT_TYPE_I8:   return checkAndWrite(int64_t{0});
        case ELEMENT_TYPE_U8:   return checkAndWrite(uint64_t{0});
        case ELEMENT_TYPE_R4:   return checkAndWrite(float{0});
        case ELEMENT_TYPE_R8:   return checkAndWrite(double{0});
        default:                return E_INVALIDARG;
    }
}

HRESULT ImplicitCast(ICorDebugValue *pSrcValue, ICorDebugValue *pDstValue)
{
    HRESULT Status = S_OK;

    PrimitiveValue primSrcValue;
    IfFailRet(GetPrimitiveData(pSrcValue, primSrcValue));
    PrimitiveValue primDstValue;
    IfFailRet(GetPrimitiveData(pDstValue, primDstValue));

    ToRelease<ICorDebugGenericValue> trDstGenValue;
    IfFailRet(pDstValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trDstGenValue)));

    return std::visit([&](const auto &src, const auto &dst) -> HRESULT
    {
        using FromType = std::decay_t<decltype(src)>;
        using ToType = std::decay_t<decltype(dst)>;

        if constexpr (IsImplicitCastable<FromType, ToType>())
        {
            auto castedValue = static_cast<ToType>(src); // NOLINT(bugprone-signed-char-misuse,cert-str34-c)
            return trDstGenValue->SetValue(&castedValue);
        }
        else
        {
            return E_INVALIDARG;
        }
    }, primSrcValue, primDstValue);
}

} // namespace dncdbg::PrimitiveTypes
