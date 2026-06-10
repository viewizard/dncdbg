// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/primitivetypes/types.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <array>
#include <cassert>
#include <charconv>
#include <unordered_set>

namespace dncdbg::PrimitiveTypes
{

bool IsPrimitiveType(CorElementType elemType)
{
    static const std::unordered_set<CorElementType> supportedElementTypes{
        ELEMENT_TYPE_BOOLEAN, ELEMENT_TYPE_U1, ELEMENT_TYPE_I1, ELEMENT_TYPE_CHAR,
        ELEMENT_TYPE_R4,      ELEMENT_TYPE_R8, ELEMENT_TYPE_I4, ELEMENT_TYPE_U4,
        ELEMENT_TYPE_I2,      ELEMENT_TYPE_U2, ELEMENT_TYPE_I8, ELEMENT_TYPE_U8};

    return supportedElementTypes.find(elemType) != supportedElementTypes.end();
}

HRESULT GetPrimitiveData(ICorDebugValue *pValue, PrimitiveValue &primValue)
{
    HRESULT Status = S_OK;

    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(pValue->GetType(&elemType));
    ToRelease<ICorDebugGenericValue> trGenValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenValue)));

    if (elemType == ELEMENT_TYPE_BOOLEAN)
    {
        uint8_t boolValue = 0;
        IfFailRet(trGenValue->GetValue(&boolValue));
        primValue.emplace<bool>(boolValue == 1);
        return S_OK;
    }

    auto readValue = [&](auto typeDummy) -> HRESULT
    {
        using T = decltype(typeDummy);
        auto &addr = primValue.emplace<T>();
        return trGenValue->GetValue(&addr);
    };

    switch (elemType)
    {
        case ELEMENT_TYPE_CHAR: return readValue(WCHAR{0});
        case ELEMENT_TYPE_U1:   return readValue(uint8_t{0});
        case ELEMENT_TYPE_I1:   return readValue(int8_t{0});
        case ELEMENT_TYPE_U2:   return readValue(uint16_t{0});
        case ELEMENT_TYPE_I2:   return readValue(int16_t{0});
        case ELEMENT_TYPE_U4:   return readValue(uint32_t{0});
        case ELEMENT_TYPE_I4:   return readValue(int32_t{0});
        case ELEMENT_TYPE_U8:   return readValue(uint64_t{0});
        case ELEMENT_TYPE_I8:   return readValue(int64_t{0});
        case ELEMENT_TYPE_R4:   return readValue(float{0});
        case ELEMENT_TYPE_R8:   return readValue(double{0});
        default:                return E_INVALIDARG;
    }
}

CorElementType GetCorElementType(const PrimitiveValue &primValue)
{
    return std::visit([](const auto &arg) -> CorElementType
    {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, bool>)          { return ELEMENT_TYPE_BOOLEAN; }
        else if constexpr (std::is_same_v<T, WCHAR>)    { return ELEMENT_TYPE_CHAR; }
        else if constexpr (std::is_same_v<T, uint8_t>)  { return ELEMENT_TYPE_U1; }
        else if constexpr (std::is_same_v<T, int8_t>)   { return ELEMENT_TYPE_I1; }
        else if constexpr (std::is_same_v<T, uint16_t>) { return ELEMENT_TYPE_U2; }
        else if constexpr (std::is_same_v<T, int16_t>)  { return ELEMENT_TYPE_I2; }
        else if constexpr (std::is_same_v<T, uint32_t>) { return ELEMENT_TYPE_U4; }
        else if constexpr (std::is_same_v<T, int32_t>)  { return ELEMENT_TYPE_I4; }
        else if constexpr (std::is_same_v<T, uint64_t>) { return ELEMENT_TYPE_U8; }
        else if constexpr (std::is_same_v<T, int64_t>)  { return ELEMENT_TYPE_I8; }
        else if constexpr (std::is_same_v<T, float>)    { return ELEMENT_TYPE_R4; }
        else if constexpr (std::is_same_v<T, double>)   { return ELEMENT_TYPE_R8; }
        else                                            { return ELEMENT_TYPE_MAX; }
    }, primValue);
}

std::string_view GetManagedTypeName(const PrimitiveValue &primValue)
{
    return std::visit([](const auto &arg) -> std::string_view
    {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, bool>)          { return "bool"; }
        else if constexpr (std::is_same_v<T, WCHAR>)    { return "char"; }
        else if constexpr (std::is_same_v<T, uint8_t>)  { return "byte"; }
        else if constexpr (std::is_same_v<T, int8_t>)   { return "sbyte"; }
        else if constexpr (std::is_same_v<T, uint16_t>) { return "ushort"; }
        else if constexpr (std::is_same_v<T, int16_t>)  { return "short"; }
        else if constexpr (std::is_same_v<T, uint32_t>) { return "uint"; }
        else if constexpr (std::is_same_v<T, int32_t>)  { return "int"; }
        else if constexpr (std::is_same_v<T, uint64_t>) { return "ulong"; }
        else if constexpr (std::is_same_v<T, int64_t>)  { return "long"; }
        else if constexpr (std::is_same_v<T, float>)    { return "float"; }
        else if constexpr (std::is_same_v<T, double>)   { return "double"; }
        else                                            { return "Unknown Type"; }
    }, primValue);
}

std::string ToString(const PrimitiveValue &primValue)
{
    return std::visit([](const auto &arg) -> std::string
    {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::monostate>)
        {
            assert(false && "value not properly initialized.");
            return {};
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            return arg ? "True" : "False";
        }
        else if constexpr (std::is_same_v<T, WCHAR>)
        {
            const std::array<WCHAR, 2> tmp{ arg, L'\0' };
            return to_utf8(tmp.data());
        }
        else
        {
            static constexpr int maxSymbols = 48;
            std::array<char, maxSymbols> buffer{};

            auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), arg);

            if (ec == std::errc{})
            {
                return std::string(buffer.data(), ptr - buffer.data());
            }

            return std::to_string(arg);
        }
    }, primValue);
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

HRESULT CreateICorValue(ICorDebugThread *pThread, PrimitiveValue &primValue, ICorDebugValue **ppValue)
{
    assert(!std::holds_alternative<std::monostate>(primValue) && "primValue not properly initialized.");

    CorElementType elemType = GetCorElementType(primValue);

    if (elemType == ELEMENT_TYPE_BOOLEAN)
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
