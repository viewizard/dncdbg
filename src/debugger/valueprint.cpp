// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/valueprint.h"
#include "metadata/attributes.h"
#include "metadata/typeprinter.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include "utils/platform.h"
#include <array>
#include <iomanip>
#include <map>
#include <sstream>
#include <cstring>
#include <type_traits>
#include <vector>
#include <arrayholder.h>

namespace dncdbg
{

namespace
{

bool IsEnum(ICorDebugValue *pInputValue)
{
    ToRelease<ICorDebugValue> trValue;
    if (FAILED(DereferenceAndUnboxValue(pInputValue, &trValue, nullptr)))
    {
        return false;
    }

    std::string baseTypeName;
    ToRelease<ICorDebugValue2> trValue2;
    ToRelease<ICorDebugType> trType;
    ToRelease<ICorDebugType> trBaseType;

    if (FAILED(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2))) ||
        FAILED(trValue2->GetExactType(&trType)) ||
        FAILED(trType->GetBase(&trBaseType)) ||
        trBaseType == nullptr ||
        FAILED(TypePrinter::GetTypeOfValue(trBaseType, baseTypeName)))
    {
        return false;
    }

    return baseTypeName == "System.Enum";
}

HRESULT PrintEnumValue(ICorDebugValue *pInputValue, BYTE *enumValue, std::string &output)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, nullptr));

    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trType->GetClass(&trClass));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trClass->GetModule(&trModule));
    mdTypeDef currentTypeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&currentTypeDef));

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    //First, we need to figure out the underlying enum type so that we can correctly type cast the raw values of each enum constant
    //We get that from the non-static field of the enum variable (I think the field is called "value__" or something similar)
    ULONG numFields = 0;
    HCORENUM fEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    CorElementType enumUnderlyingType = ELEMENT_TYPE_MAX;
    while (SUCCEEDED(trMDImport->EnumFields(&fEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        DWORD fieldAttr = 0;
        PCCOR_SIGNATURE pSignatureBlob = nullptr;
        ULONG sigBlobLength = 0;
        if (SUCCEEDED(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, nullptr, &fieldAttr,
                                                &pSignatureBlob, &sigBlobLength, nullptr, nullptr, nullptr)))
        {
            if ((fieldAttr & fdStatic) == 0)
            {
                CorSigUncompressCallingConv(pSignatureBlob);
                enumUnderlyingType = CorSigUncompressElementType(pSignatureBlob);
                break;
            }
        }
    }
    trMDImport->CloseEnum(fEnum);

    auto getValue = [&enumUnderlyingType](const void *data) -> uint64_t
    {
        switch (enumUnderlyingType)
        {
        case ELEMENT_TYPE_CHAR:
        case ELEMENT_TYPE_I1:
            return static_cast<uint64_t>(*reinterpret_cast<const int8_t *>(data));
        case ELEMENT_TYPE_U1:
            return static_cast<uint64_t>(*reinterpret_cast<const uint8_t *>(data));
        case ELEMENT_TYPE_I2:
            return static_cast<uint64_t>(*reinterpret_cast<const int16_t *>(data));
        case ELEMENT_TYPE_U2:
            return static_cast<uint64_t>(*reinterpret_cast<const uint16_t *>(data));
        case ELEMENT_TYPE_I4:
            return static_cast<uint64_t>(*reinterpret_cast<const int32_t *>(data));
        case ELEMENT_TYPE_U4:
            return static_cast<uint64_t>(*reinterpret_cast<const uint32_t *>(data));
        case ELEMENT_TYPE_I8:
            return static_cast<uint64_t>(*reinterpret_cast<const int64_t *>(data));
        case ELEMENT_TYPE_U8:
            return static_cast<uint64_t>(*reinterpret_cast<const uint64_t *>(data));
        case ELEMENT_TYPE_I:
            return static_cast<uint64_t>(*reinterpret_cast<const int32_t *>(data));
        case ELEMENT_TYPE_U:
        case ELEMENT_TYPE_R4:
        case ELEMENT_TYPE_R8:
        // Technically U and the floating-point ones are options in the CLI, but not in the CLS or C#, so these are NYI
        default:
            return 0;
        }
    };

    // Enum could have explicitly specified any integral numeric type. enumValue type same as enumUnderlyingType.
    const uint64_t curValue = getValue(enumValue);

    // Care about Flags attribute (https://docs.microsoft.com/en-us/dotnet/api/system.flagsattribute),
    // that "Indicates that an enumeration can be treated as a bit field; that is, a set of flags".
    const bool foundFlagsAttr = HasAttribute(trMDImport, currentTypeDef, "System.FlagsAttribute..ctor");

    uint64_t remainingValue = curValue;
    std::map<uint64_t, std::string> OrderedFlags;
    fEnum = nullptr;
    while (SUCCEEDED(trMDImport->EnumFields(&fEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        ULONG nameLen = 0;
        DWORD fieldAttr = 0;
        std::array<WCHAR, mdNameLen> mdName{};
        UVCP_CONSTANT pRawValue = nullptr;
        ULONG rawValueLength = 0;
        if (SUCCEEDED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), mdNameLen, &nameLen, &fieldAttr,
                                                nullptr, nullptr, nullptr, &pRawValue, &rawValueLength)))
        {
            const DWORD enumValueRequiredAttributes = fdPublic | fdStatic | fdLiteral | fdHasDefault;
            if ((fieldAttr & enumValueRequiredAttributes) != enumValueRequiredAttributes)
            {
                continue;
            }

            const uint64_t currentConstValue = getValue(pRawValue);
            if (currentConstValue == curValue)
            {
                trMDImport->CloseEnum(fEnum);
                output = to_utf8(mdName.data());

                return S_OK;
            }
            if (foundFlagsAttr)
            {
                // Flag enumerated constant whose value is zero must be excluded from OR-ed expression.
                if (currentConstValue == 0)
                {
                    continue;
                }

                if ((currentConstValue == remainingValue) ||
                    ((currentConstValue != 0) && ((currentConstValue & remainingValue) == currentConstValue)))
                {
                    OrderedFlags.emplace(currentConstValue, to_utf8(mdName.data()));
                    remainingValue &= ~currentConstValue;
                }
            }
        }
    }
    trMDImport->CloseEnum(fEnum);

    // Don't lose data, provide number as-is instead.
    if (!OrderedFlags.empty() && (remainingValue == 0U))
    {
        std::ostringstream ss;
        for (const auto &Flag : OrderedFlags)
        {
            if (ss.tellp() > 0)
            {
                ss << " | ";
            }

            ss << Flag.second;
        }
        output = ss.str();
    }
    else
    {
        output = std::to_string(curValue);
    }

    return S_OK;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral_v<T>>>
HRESULT GetIntegralValue(ICorDebugValue *pInputValue, T &value)
{
    HRESULT Status = S_OK;

    BOOL isNull = TRUE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));

    if (isNull == TRUE)
    {
        return E_FAIL;
    }

    uint32_t cbSize = 0;
    IfFailRet(trValue->GetSize(&cbSize));
    if (cbSize != sizeof(value))
    {
        return E_FAIL;
    }

    CorElementType corElemType = ELEMENT_TYPE_MAX;
    IfFailRet(trValue->GetType(&corElemType));

    switch (corElemType)
    {
    case ELEMENT_TYPE_I1:
    case ELEMENT_TYPE_U1:
        if (typeid(T) == typeid(char) || typeid(T) == typeid(unsigned char) || typeid(T) == typeid(signed char))
        {
            break;
        }
        return E_FAIL;

    case ELEMENT_TYPE_I4:
    case ELEMENT_TYPE_U4:
        if (typeid(T) == typeid(int) || typeid(T) == typeid(unsigned))
        {
            break;
        }

        if (sizeof(int) == sizeof(long))
        {
            if (typeid(T) == typeid(long) || typeid(T) == typeid(unsigned long))
            {
                break;
            }
        }
        return E_FAIL;

    case ELEMENT_TYPE_I8:
    case ELEMENT_TYPE_U8:
        if (typeid(T) == typeid(long long) || typeid(T) == typeid(unsigned long long))
        {
            break;
        }

        if (sizeof(long long) == sizeof(long))
        {
            if (typeid(T) == typeid(long) || typeid(T) == typeid(unsigned long))
            {
                break;
            }
        }

        return E_FAIL;

    case ELEMENT_TYPE_I:
    case ELEMENT_TYPE_U:
        if (sizeof(T) == sizeof(int))
        {
            if (typeid(T) == typeid(int) || typeid(T) == typeid(unsigned))
            {
                break;
            }

            if (sizeof(int) == sizeof(long))
            {
                if (typeid(T) == typeid(long) || typeid(T) == typeid(unsigned long))
                {
                    break;
                }
            }
        }

        if (sizeof(T) == sizeof(long long))
        {
            if (typeid(T) == typeid(long long) || typeid(T) == typeid(unsigned long long))
            {
                break;
            }

            if (sizeof(long long) == sizeof(long))
            {
                if (typeid(T) == typeid(long) || typeid(T) == typeid(unsigned long))
                {
                    break;
                }
            }
        }

        return E_FAIL;

    default:
        return E_FAIL;
    }

    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    IfFailRet(trGenericValue->GetValue(&value));
    return S_OK;
}

HRESULT GetUIntValue(ICorDebugValue *pInputValue, unsigned &value)
{
    return GetIntegralValue(pInputValue, value);
}

HRESULT GetDecimalFields(ICorDebugValue *pValue, unsigned int &hi, unsigned int &mid, unsigned int &lo,
                         unsigned int &flags)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trType->GetClass(&trClass));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trClass->GetModule(&trModule));
    mdTypeDef currentTypeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&currentTypeDef));
    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    bool has_hi = false;
    bool has_mid = false;
    bool has_lo = false;
    bool has_flags = false;

    ULONG numFields = 0;
    HCORENUM fEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    while (SUCCEEDED(trMDImport->EnumFields(&fEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        ULONG nameLen = 0;
        DWORD fieldAttr = 0;
        std::array<WCHAR, mdNameLen> mdName{};
        if(SUCCEEDED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), mdNameLen, &nameLen, &fieldAttr,
                                               nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            if (((fieldAttr & fdLiteral) != 0U) ||
                ((fieldAttr & fdStatic) != 0U))
            {
                continue;
            }

            ToRelease<ICorDebugObjectValue> trObjValue;
            IfFailRet(pValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
            ToRelease<ICorDebugValue> trFieldVal;
            IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, &trFieldVal));

            const std::string name = to_utf8(mdName.data());

            if (name == "hi" || name == "_hi32")
            {
                IfFailRet(GetUIntValue(trFieldVal, hi));
                has_hi = true;
            }
            else if (name == "_lo64")
            {
                static constexpr unsigned int fourBytesShift = 32;
                unsigned long long lo64 = 0;
                IfFailRet(GetIntegralValue(trFieldVal, lo64));
                mid = lo64 >> fourBytesShift;
                lo = lo64 & ((1ULL << fourBytesShift) - 1);
                has_mid = has_lo = true;
            }
            else if (name == "mid")
            {
                IfFailRet(GetUIntValue(trFieldVal, mid));
                has_mid = true;
            }
            else if (name == "lo")
            {
                IfFailRet(GetUIntValue(trFieldVal, lo));
                has_lo = true;
            }
            else if (name == "flags" || name == "_flags")
            {
                IfFailRet(GetUIntValue(trFieldVal, flags));
                has_flags = true;
            }
        }
    }
    trMDImport->CloseEnum(fEnum);

    return (has_hi && has_mid && has_lo && has_flags ? S_OK : E_FAIL);
}

inline uint64_t Make_64(uint32_t h, uint32_t l)
{
    static constexpr uint32_t fourBytesShift = 32;
    uint64_t v = h;
    v <<= fourBytesShift;
    v |= l;
    return v;
}

inline uint32_t Lo_32(uint64_t v)
{
    return static_cast<uint32_t>(v);
}

bool uint96_is_zero(const std::array<uint32_t, 3> &v)
{
    return v[0] == 0 && v[1] == 0 && v[2] == 0;
}

void udivrem96(std::array<uint32_t, 3> &divident, uint32_t divisor, uint32_t &remainder)
{
    remainder = 0;
    for (int i = 2; i >= 0; i--)
    {
        const uint64_t partial_dividend = Make_64(remainder, divident[i]);
        if (partial_dividend == 0)
        {
            divident[i] = 0;
            remainder = 0;
        }
        else if (partial_dividend < divisor)
        {
            divident[i] = 0;
            remainder = Lo_32(partial_dividend);
        }
        else if (partial_dividend == divisor)
        {
            divident[i] = 1;
            remainder = 0;
        }
        else
        {
            divident[i] = Lo_32(partial_dividend / divisor);
            remainder = Lo_32(partial_dividend - (static_cast<uint64_t>(divident[i]) * divisor));
        }
    }
}

std::string uint96_to_string(std::array<uint32_t, 3> &v)
{
    static constexpr uint32_t divisor = 10;
    static constexpr std::array<char, 10> digits{'0','1','2','3','4','5','6','7','8','9'};
    std::string result;
    do
    {
        uint32_t rem = 0;
        udivrem96(v, divisor, rem);
        result.insert(0, 1, digits[rem]);
    } while (!uint96_is_zero(v));
    return result;
}

void PrintDecimal(unsigned int hi, unsigned int mid, unsigned int lo, unsigned int flags, std::string &output)
{
    std::array<uint32_t, 3> v{lo, mid, hi};

    output = uint96_to_string(v);

    static constexpr unsigned int ScaleMask = 0x00FF0000UL;
    static constexpr unsigned int ScaleShift = 16;
    static constexpr unsigned int SignMask = 1UL << 31;

    const unsigned int scale = (flags & ScaleMask) >> ScaleShift;
    const bool is_negative = ((flags & SignMask) != 0U);

    const size_t len = output.length();

    if (len > scale)
    {
        if (scale != 0)
        {
            output.insert(len - scale, 1, '.');
        }
    }
    else
    {
        output.insert(0, "0.");
        output.insert(2, scale - len, '0');
    }

    if (is_negative)
    {
        output.insert(0, 1, '-');
    }
}

HRESULT PrintDecimalValue(ICorDebugValue *pValue, std::string &output)
{
    HRESULT Status = S_OK;

    unsigned int hi = 0;
    unsigned int mid = 0;
    unsigned int lo = 0;
    unsigned int flags = 0;

    IfFailRet(GetDecimalFields(pValue, hi, mid, lo, flags));

    PrintDecimal(hi, mid, lo, flags, output);

    return S_OK;
}

HRESULT PrintArrayValue(ICorDebugValue *pValue, std::string &output)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugArrayValue> trArrayValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugArrayValue, reinterpret_cast<void **>(&trArrayValue)));

    uint32_t nRank = 0;
    IfFailRet(trArrayValue->GetRank(&nRank));
    if (nRank < 1)
    {
        return E_UNEXPECTED;
    }

    uint32_t cElements = 0;
    IfFailRet(trArrayValue->GetCount(&cElements));

    std::ostringstream ss;
    ss << "{";

    std::string elementType;
    std::string arrayType;

    ToRelease<ICorDebugType> trFirstParameter;
    ToRelease<ICorDebugValue2> trValue2;
    ToRelease<ICorDebugType> trType;
    if (SUCCEEDED(trArrayValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2))) &&
        SUCCEEDED(trValue2->GetExactType(&trType)))
    {
        if (SUCCEEDED(trType->GetFirstTypeParameter(&trFirstParameter)))
        {
            TypePrinter::GetTypeOfValue(trFirstParameter, elementType, arrayType);
        }
    }

    std::vector<uint32_t> dims(nRank, 0);
    trArrayValue->GetDimensions(nRank, dims.data());

    std::vector<uint32_t> base(nRank, 0);
    BOOL hasBaseIndicies = FALSE;
    if (SUCCEEDED(trArrayValue->HasBaseIndicies(&hasBaseIndicies)) && (hasBaseIndicies == TRUE))
    {
        IfFailRet(trArrayValue->GetBaseIndicies(nRank, base.data()));
    }

    ss << elementType << "[";
    const char *sep = "";
    for (size_t i = 0; i < dims.size(); ++i)
    {
        ss << sep;
        sep = ", ";

        if (base[i] > 0)
        {
            ss << base[i] << ".." << (base[i] + dims[i] - 1);
        }
        else
        {
            ss << dims[i];
        }
    }
    ss << "]" << arrayType;

    ss << "}";
    output = ss.str();
    return S_OK;
}

void EscapeString(std::string &s, char q = '\"')
{
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        int count = 0;
        const char c = s.at(i);
        switch (c)
        {
        case '\'':
        case '\"':
            count = c != q ? 0 : 1;
            s.insert(i, count, '\\');
            break;
        case '\\':
            count = 1;
            s.insert(i, count, '\\');
            break;
        case '\0':
            count = 1;
            s.insert(i, count, '\\');
            s[i + count] = '0';
            break;
        case '\a':
            count = 1;
            s.insert(i, count, '\\');
            s[i + count] = 'a';
            break;
        case '\b':
            count = 1;
            s.insert(i, count, '\\');
            s[i + count] = 'b';
            break;
        case '\f':
            count = 1;
            s.insert(i, count, '\\');
            s[i + count] = 'f';
            break;
        case '\n':
            count = 1;
            s.insert(i, count, '\\');
            s[i + count] = 'n';
            break;
        case '\r':
            count = 1;
            s.insert(i, count, '\\');
            s[i + count] = 'r';
            break;
        case '\t':
            count = 1;
            s.insert(i, count, '\\');
            s[i + count] = 't';
            break;
        case '\v':
            count = 1;
            s.insert(i, count, '\\');
            s[i + count] = 'v';
            break;
        default:
            break;
        }
        i += count;
    }
}

} // unnamed namespace

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
                break; // unboxed till null reference
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

        break; // unboxed till object
    }

    trCurrentValue->AddRef();
    *ppOutputValue = trCurrentValue;
    return S_OK;
}

HRESULT PrintStringValue(ICorDebugValue *pValue, std::string &output)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugStringValue> trStringValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugStringValue, reinterpret_cast<void **>(&trStringValue)));

    uint32_t cchValue = 0;
    IfFailRet(trStringValue->GetLength(&cchValue));
    cchValue++; // Allocate one more for null terminator

    ArrayHolder<WCHAR> str = new WCHAR[cchValue];

    uint32_t cchValueReturned = 0;
    IfFailRet(trStringValue->GetString(cchValue, &cchValueReturned, str));

    output = to_utf8(str);

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
    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    ToRelease<ICorDebugObjectValue> trObjValue;
    ToRelease<ICorDebugValue> trUnboxedResultValue;
    IfFailRet(DereferenceAndUnboxValue(pValue, &trUnboxedResultValue));
    IfFailRet(trUnboxedResultValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));

    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    while (SUCCEEDED(trMDImport->EnumFields(&hEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        ULONG nameLen = 0;
        std::array<WCHAR, mdNameLen> mdName{};
        if (SUCCEEDED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), mdNameLen, &nameLen,
                                                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            // https://github.com/dotnet/runtime/blob/adba54da2298de9c715922b506bfe17a974a3650/src/libraries/System.Private.CoreLib/src/System/Nullable.cs#L24
            if (str_equal(mdName.data(), W("value")))
            {
                IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppValueValue));
            }

            // https://github.com/dotnet/runtime/blob/adba54da2298de9c715922b506bfe17a974a3650/src/libraries/System.Private.CoreLib/src/System/Nullable.cs#L23
            if (str_equal(mdName.data(), W("hasValue")))
            {
                IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppHasValueValue));
            }
        }
    }

    return S_OK;
}

HRESULT PrintNullableValue(ICorDebugValue *pValue, std::string &outTextValue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trValueValue;
    ToRelease<ICorDebugValue> trHasValueValue;
    IfFailRet(GetNullableValue(pValue, &trValueValue, &trHasValueValue));

    uint32_t cbSize = 0;
    IfFailRet(trHasValueValue->GetSize(&cbSize));
    ArrayHolder<BYTE> rgbValue = new (std::nothrow) BYTE[cbSize];
    if (rgbValue == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    memset(rgbValue.GetPtr(), 0, cbSize * sizeof(BYTE));

    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(trHasValueValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    IfFailRet(trGenericValue->GetValue(static_cast<void *>(&rgbValue[0])));
    // trHasValueValue is ELEMENT_TYPE_BOOLEAN
    if (rgbValue[0] != 0)
    {
        PrintValue(trValueValue, outTextValue, true);
    }
    else
    {
        outTextValue = "null";
    }

    return S_OK;
}

HRESULT PrintValue(ICorDebugValue *pInputValue, std::string &output, bool escape)
{
    HRESULT Status = S_OK;

    BOOL isNull = TRUE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));

    if (isNull == TRUE)
    {
        output = "null";
        return S_OK;
    }

    uint32_t cbSize = 0;
    IfFailRet(trValue->GetSize(&cbSize));
    ArrayHolder<BYTE> rgbValue = new (std::nothrow) BYTE[cbSize];
    if (rgbValue == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    memset(rgbValue.GetPtr(), 0, cbSize * sizeof(BYTE));

    CorElementType corElemType = ELEMENT_TYPE_MAX;
    IfFailRet(trValue->GetType(&corElemType));
    if (corElemType == ELEMENT_TYPE_STRING)
    {
        std::string raw_str;
        IfFailRet(PrintStringValue(trValue, raw_str));

        if (!escape)
        {
            output = raw_str;
            return S_OK;
        }

        EscapeString(raw_str, '"');

        std::ostringstream ss;
        ss << "\"" << raw_str << "\"";
        output = ss.str();
        return S_OK;
    }

    if (corElemType == ELEMENT_TYPE_SZARRAY || corElemType == ELEMENT_TYPE_ARRAY)
    {
        return PrintArrayValue(trValue, output);
    }

    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    IfFailRet(trGenericValue->GetValue(static_cast<void *>(&rgbValue[0])));

    if (IsEnum(trValue))
    {
        return PrintEnumValue(trValue, rgbValue, output);
    }

    static constexpr uint32_t floatPrecision = 8;
    static constexpr uint32_t doublePrecision = 16;
    std::ostringstream ss;

    switch (corElemType)
    {
    default:
        ss << "(Unhandled CorElementType: 0x" << std::hex << corElemType << ")";
        break;

    case ELEMENT_TYPE_PTR:
        ss << "<pointer>";
        break;

    case ELEMENT_TYPE_FNPTR:
    {
        CORDB_ADDRESS addr = 0;
        ToRelease<ICorDebugReferenceValue> trReferenceValue;
        if (SUCCEEDED(trValue->QueryInterface(IID_ICorDebugReferenceValue, reinterpret_cast<void **>(&trReferenceValue))))
        {
            trReferenceValue->GetValue(&addr);
        }
        ss << "<function pointer 0x" << std::hex << addr << ">";
    }
    break;

    case ELEMENT_TYPE_VALUETYPE:
    case ELEMENT_TYPE_CLASS:
    {
        std::string typeName;
        TypePrinter::GetTypeOfValue(trValue, typeName);
        if (typeName == "decimal") // TODO: implement mechanism for printing custom type values
        {
            std::string val;
            PrintDecimalValue(trValue, val);
            ss << val;
        }
        else if (typeName == "void")
        {
            ss << "Expression has been evaluated and has no value";
        }
        else if (typeName.back() == '?') // System.Nullable<T>
        {
            std::string val;
            PrintNullableValue(trValue, val);
            ss << val;
        }
        else
        {
            ss << '{' << typeName << '}';
        }
    }
    break;

    case ELEMENT_TYPE_BOOLEAN:
        ss << (rgbValue[0] == 0 ? "false" : "true");
        break;

    case ELEMENT_TYPE_CHAR:
    {
        const WSTRING wstr{*reinterpret_cast<WCHAR *>(&rgbValue[0]) , '\0'};
        std::string printableVal = to_utf8(wstr.c_str());
        if (!escape)
        {
            output = printableVal;
            return S_OK;
        }
        EscapeString(printableVal, '\'');
        ss << static_cast<unsigned int>(wstr.c_str()[0]) << " '" << printableVal << "'";
    }
    break;

    case ELEMENT_TYPE_I1:
        ss << static_cast<int32_t>(*reinterpret_cast<int8_t *>(&rgbValue[0]));
        break;

    case ELEMENT_TYPE_U1:
        ss << static_cast<uint32_t>(rgbValue[0]);
        break;

    case ELEMENT_TYPE_I2:
        ss << *reinterpret_cast<int16_t *>(&rgbValue[0]);
        break;

    case ELEMENT_TYPE_U2:
        ss << *reinterpret_cast<uint16_t *>(&rgbValue[0]);
        break;

    case ELEMENT_TYPE_I:
    case ELEMENT_TYPE_I4:
        ss << *reinterpret_cast<int32_t *>(&rgbValue[0]);
        break;

    case ELEMENT_TYPE_U:
    case ELEMENT_TYPE_U4:
        ss << *reinterpret_cast<uint32_t *>(&rgbValue[0]);
        break;

    case ELEMENT_TYPE_I8:
        ss << *reinterpret_cast<int64_t *>(&rgbValue[0]);
        break;

    case ELEMENT_TYPE_U8:
        ss << *reinterpret_cast<uint64_t *>(&rgbValue[0]);
        break;

    case ELEMENT_TYPE_R4:
        ss << std::setprecision(floatPrecision) << *reinterpret_cast<float *>(&rgbValue[0]);
        break;

    case ELEMENT_TYPE_R8:
        ss << std::setprecision(doublePrecision) << *reinterpret_cast<double *>(&rgbValue[0]);
        break;

    case ELEMENT_TYPE_OBJECT:
        ss << "object";
        break;

        // TODO: The following corElementTypes are not yet implemented here.  Array
        // might be interesting to add, though the others may be of rather limited use:
        //
        // ELEMENT_TYPE_GENERICINST    = 0x15,     // GENERICINST <generic type> <argCnt> <arg1> ... <argn>
    }

    output = ss.str();
    return S_OK;
}

} // namespace dncdbg
