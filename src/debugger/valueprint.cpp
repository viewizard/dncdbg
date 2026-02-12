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

// From strike.cpp
HRESULT DereferenceAndUnboxValue(ICorDebugValue *pValue, ICorDebugValue **ppOutputValue, BOOL *pIsNull)
{
    HRESULT Status = S_OK;
    *ppOutputValue = nullptr;
    if (pIsNull != nullptr)
        *pIsNull = FALSE;

    ToRelease<ICorDebugReferenceValue> pReferenceValue;
    Status = pValue->QueryInterface(IID_ICorDebugReferenceValue, reinterpret_cast<void **>(&pReferenceValue));
    if (SUCCEEDED(Status))
    {
        BOOL isNull = FALSE;
        IfFailRet(pReferenceValue->IsNull(&isNull));
        if (isNull == FALSE)
        {
            ToRelease<ICorDebugValue> pDereferencedValue;
            IfFailRet(pReferenceValue->Dereference(&pDereferencedValue));
            return DereferenceAndUnboxValue(pDereferencedValue, ppOutputValue, pIsNull);
        }
        else
        {
            if (pIsNull != nullptr)
                *pIsNull = TRUE;
            pValue->AddRef();
            *ppOutputValue = pValue;
            return S_OK;
        }
    }

    ToRelease<ICorDebugBoxValue> pBoxedValue;
    Status = pValue->QueryInterface(IID_ICorDebugBoxValue, reinterpret_cast<void **>(&pBoxedValue));
    if (SUCCEEDED(Status))
    {
        ToRelease<ICorDebugObjectValue> pUnboxedValue;
        IfFailRet(pBoxedValue->GetObject(&pUnboxedValue));
        return DereferenceAndUnboxValue(pUnboxedValue, ppOutputValue, pIsNull);
    }
    pValue->AddRef();
    *ppOutputValue = pValue;
    return S_OK;
}

static bool IsEnum(ICorDebugValue *pInputValue)
{
    ToRelease<ICorDebugValue> pValue;
    if (FAILED(DereferenceAndUnboxValue(pInputValue, &pValue, nullptr)))
        return false;

    std::string baseTypeName;
    ToRelease<ICorDebugValue2> pValue2;
    ToRelease<ICorDebugType> pType;
    ToRelease<ICorDebugType> pBaseType;

    if (FAILED(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&pValue2))))
        return false;
    if (FAILED(pValue2->GetExactType(&pType)))
        return false;
    if (FAILED(pType->GetBase(&pBaseType)) || pBaseType == nullptr)
        return false;
    if (FAILED(TypePrinter::GetTypeOfValue(pBaseType, baseTypeName)))
        return false;

    return baseTypeName == "System.Enum";
}

static HRESULT PrintEnumValue(ICorDebugValue *pInputValue, BYTE *enumValue, std::string &output)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugValue> pValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &pValue, nullptr));

    mdTypeDef currentTypeDef = mdTypeDefNil;
    ToRelease<ICorDebugClass> pClass;
    ToRelease<ICorDebugValue2> pValue2;
    ToRelease<ICorDebugType> pType;
    ToRelease<ICorDebugModule> pModule;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&pValue2)));
    IfFailRet(pValue2->GetExactType(&pType));
    IfFailRet(pType->GetClass(&pClass));
    IfFailRet(pClass->GetModule(&pModule));
    IfFailRet(pClass->GetToken(&currentTypeDef));

    ToRelease<IUnknown> pMDUnknown;
    ToRelease<IMetaDataImport> pMD;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &pMDUnknown));
    IfFailRet(pMDUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&pMD)));

    //First, we need to figure out the underlying enum type so that we can correctly type cast the raw values of each enum constant
    //We get that from the non-static field of the enum variable (I think the field is called "value__" or something similar)
    ULONG numFields = 0;
    HCORENUM fEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    CorElementType enumUnderlyingType = ELEMENT_TYPE_MAX;
    while (SUCCEEDED(pMD->EnumFields(&fEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        DWORD fieldAttr = 0;
        PCCOR_SIGNATURE pSignatureBlob = nullptr;
        ULONG sigBlobLength = 0;
        if (SUCCEEDED(pMD->GetFieldProps(fieldDef, nullptr, nullptr, 0, nullptr, &fieldAttr,
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
    pMD->CloseEnum(fEnum);

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
    const bool foundFlagsAttr = HasAttribute(pMD, currentTypeDef, "System.FlagsAttribute..ctor");

    uint64_t remainingValue = curValue;
    std::map<uint64_t, std::string> OrderedFlags;
    fEnum = nullptr;
    while (SUCCEEDED(pMD->EnumFields(&fEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        ULONG nameLen = 0;
        DWORD fieldAttr = 0;
        std::array<WCHAR, mdNameLen> mdName{};
        UVCP_CONSTANT pRawValue = nullptr;
        ULONG rawValueLength = 0;
        if (SUCCEEDED(pMD->GetFieldProps(fieldDef, nullptr, mdName.data(), mdNameLen, &nameLen, &fieldAttr,
                                         nullptr, nullptr, nullptr, &pRawValue, &rawValueLength)))
        {
            const DWORD enumValueRequiredAttributes = fdPublic | fdStatic | fdLiteral | fdHasDefault;
            if ((fieldAttr & enumValueRequiredAttributes) != enumValueRequiredAttributes)
                continue;

            const uint64_t currentConstValue = getValue(pRawValue);
            if (currentConstValue == curValue)
            {
                pMD->CloseEnum(fEnum);
                output = to_utf8(mdName.data());

                return S_OK;
            }
            if (foundFlagsAttr)
            {
                // Flag enumerated constant whose value is zero must be excluded from OR-ed expression.
                if (currentConstValue == 0)
                    continue;

                if ((currentConstValue == remainingValue) ||
                    ((currentConstValue != 0) && ((currentConstValue & remainingValue) == currentConstValue)))
                {
                    OrderedFlags.emplace(currentConstValue, to_utf8(mdName.data()));
                    remainingValue &= ~currentConstValue;
                }
            }
        }
    }
    pMD->CloseEnum(fEnum);

    // Don't lose data, provide number as-is instead.
    if (!OrderedFlags.empty() && (remainingValue == 0U))
    {
        std::ostringstream ss;
        for (const auto &Flag : OrderedFlags)
        {
            if (ss.tellp() > 0)
                ss << " | ";

            ss << Flag.second;
        }
        output = ss.str();
    }
    else
        output = std::to_string(curValue);

    return S_OK;
}

template <typename T, typename = typename std::enable_if_t<std::is_integral_v<T>>>
static HRESULT GetIntegralValue(ICorDebugValue *pInputValue, T &value)
{
    HRESULT Status = S_OK;

    BOOL isNull = TRUE;
    ToRelease<ICorDebugValue> pValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &pValue, &isNull));

    if (isNull == TRUE)
        return E_FAIL;

    uint32_t cbSize = 0;
    IfFailRet(pValue->GetSize(&cbSize));
    if (cbSize != sizeof(value))
        return E_FAIL;

    CorElementType corElemType = ELEMENT_TYPE_MAX;
    IfFailRet(pValue->GetType(&corElemType));

    switch (corElemType)
    {
    case ELEMENT_TYPE_I1:
    case ELEMENT_TYPE_U1:
        if (typeid(T) == typeid(char) || typeid(T) == typeid(unsigned char) || typeid(T) == typeid(signed char))
            break;
        return E_FAIL;

    case ELEMENT_TYPE_I4:
    case ELEMENT_TYPE_U4:
        if (typeid(T) == typeid(int) || typeid(T) == typeid(unsigned))
            break;

        if (sizeof(int) == sizeof(long))
        {
            if (typeid(T) == typeid(long) || typeid(T) == typeid(unsigned long))
                break;
        }
        return E_FAIL;

    case ELEMENT_TYPE_I8:
    case ELEMENT_TYPE_U8:
        if (typeid(T) == typeid(long long) || typeid(T) == typeid(unsigned long long))
            break;

        if (sizeof(long long) == sizeof(long))
        {
            if (typeid(T) == typeid(long) || typeid(T) == typeid(unsigned long))
                break;
        }

        return E_FAIL;

    case ELEMENT_TYPE_I:
    case ELEMENT_TYPE_U:
        if (sizeof(T) == sizeof(int))
        {
            if (typeid(T) == typeid(int) || typeid(T) == typeid(unsigned))
                break;

            if (sizeof(int) == sizeof(long))
            {
                if (typeid(T) == typeid(long) || typeid(T) == typeid(unsigned long))
                    break;
            }
        }

        if (sizeof(T) == sizeof(long long))
        {
            if (typeid(T) == typeid(long long) || typeid(T) == typeid(unsigned long long))
                break;

            if (sizeof(long long) == sizeof(long))
            {
                if (typeid(T) == typeid(long) || typeid(T) == typeid(unsigned long))
                    break;
            }
        }

        return E_FAIL;

    default:
        return E_FAIL;
    }

    ToRelease<ICorDebugGenericValue> pGenericValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&pGenericValue)));
    IfFailRet(pGenericValue->GetValue(&value));
    return S_OK;
}

static HRESULT GetUIntValue(ICorDebugValue *pInputValue, unsigned &value)
{
    return GetIntegralValue(pInputValue, value);
}

static HRESULT GetDecimalFields(ICorDebugValue *pValue, unsigned int &hi, unsigned int &mid, unsigned int &lo,
                                unsigned int &flags)
{
    HRESULT Status = S_OK;

    mdTypeDef currentTypeDef = mdTypeDefNil;
    ToRelease<ICorDebugClass> pClass;
    ToRelease<ICorDebugValue2> pValue2;
    ToRelease<ICorDebugType> pType;
    ToRelease<ICorDebugModule> pModule;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&pValue2)));
    IfFailRet(pValue2->GetExactType(&pType));
    IfFailRet(pType->GetClass(&pClass));
    IfFailRet(pClass->GetModule(&pModule));
    IfFailRet(pClass->GetToken(&currentTypeDef));
    ToRelease<IUnknown> pMDUnknown;
    ToRelease<IMetaDataImport> pMD;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &pMDUnknown));
    IfFailRet(pMDUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&pMD)));

    bool has_hi = false;
    bool has_mid = false;
    bool has_lo = false;
    bool has_flags = false;

    ULONG numFields = 0;
    HCORENUM fEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    while (SUCCEEDED(pMD->EnumFields(&fEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        ULONG nameLen = 0;
        DWORD fieldAttr = 0;
        std::array<WCHAR, mdNameLen> mdName{};
        if(SUCCEEDED(pMD->GetFieldProps(fieldDef, nullptr, mdName.data(), mdNameLen, &nameLen, &fieldAttr,
                                        nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            if ((fieldAttr & fdLiteral) != 0U)
                continue;
            if ((fieldAttr & fdStatic) != 0U)
                continue;

            ToRelease<ICorDebugValue> pFieldVal;
            ToRelease<ICorDebugObjectValue> pObjValue;
            IfFailRet(pValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&pObjValue)));
            IfFailRet(pObjValue->GetFieldValue(pClass, fieldDef, &pFieldVal));

            const std::string name = to_utf8(mdName.data());

            if (name == "hi" || name == "_hi32")
            {
                IfFailRet(GetUIntValue(pFieldVal, hi));
                has_hi = true;
            }
            else if (name == "_lo64")
            {
                unsigned long long lo64 = 0;
                IfFailRet(GetIntegralValue(pFieldVal, lo64));
                mid = lo64 >> 32;
                lo = lo64 & ((1ULL << 32) - 1);
                has_mid = has_lo = true;
            }
            else if (name == "mid")
            {
                IfFailRet(GetUIntValue(pFieldVal, mid));
                has_mid = true;
            }
            else if (name == "lo")
            {
                IfFailRet(GetUIntValue(pFieldVal, lo));
                has_lo = true;
            }
            else if (name == "flags" || name == "_flags")
            {
                IfFailRet(GetUIntValue(pFieldVal, flags));
                has_flags = true;
            }
        }
    }
    pMD->CloseEnum(fEnum);

    return (has_hi && has_mid && has_lo && has_flags ? S_OK : E_FAIL);
}

static inline uint64_t Make_64(uint32_t h, uint32_t l)
{
    uint64_t v = h;
    v <<= 32;
    v |= l;
    return v;
}
static inline uint32_t Lo_32(uint64_t v)
{
    return static_cast<uint32_t>(v);
}

static bool uint96_is_zero(const std::array<uint32_t, 3> &v)
{
    return v[0] == 0 && v[1] == 0 && v[2] == 0;
}

static void udivrem96(std::array<uint32_t, 3> &divident, uint32_t divisor, uint32_t &remainder)
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

static std::string uint96_to_string(std::array<uint32_t, 3> &v)
{
    static const char *digits = "0123456789";
    std::string result;
    do
    {
        uint32_t rem = 0;
        udivrem96(v, 10, rem);
        result.insert(0, 1, digits[rem]);
    } while (!uint96_is_zero(v));
    return result;
}

static void PrintDecimal(unsigned int hi, unsigned int mid, unsigned int lo, unsigned int flags, std::string &output)
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
            output.insert(len - scale, 1, '.');
    }
    else
    {
        output.insert(0, "0.");
        output.insert(2, scale - len, '0');
    }

    if (is_negative)
        output.insert(0, 1, '-');
}

static HRESULT PrintDecimalValue(ICorDebugValue *pValue, std::string &output)
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

static HRESULT PrintArrayValue(ICorDebugValue *pValue, std::string &output)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugArrayValue> pArrayValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugArrayValue, reinterpret_cast<void **>(&pArrayValue)));

    uint32_t nRank = 0;
    IfFailRet(pArrayValue->GetRank(&nRank));
    if (nRank < 1)
    {
        return E_UNEXPECTED;
    }

    uint32_t cElements = 0;
    IfFailRet(pArrayValue->GetCount(&cElements));

    std::ostringstream ss;
    ss << "{";

    std::string elementType;
    std::string arrayType;

    ToRelease<ICorDebugType> pFirstParameter;
    ToRelease<ICorDebugValue2> pValue2;
    ToRelease<ICorDebugType> pType;
    if (SUCCEEDED(pArrayValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&pValue2))) &&
        SUCCEEDED(pValue2->GetExactType(&pType)))
    {
        if (SUCCEEDED(pType->GetFirstTypeParameter(&pFirstParameter)))
        {
            TypePrinter::GetTypeOfValue(pFirstParameter, elementType, arrayType);
        }
    }

    std::vector<uint32_t> dims(nRank, 0);
    pArrayValue->GetDimensions(nRank, dims.data());

    std::vector<uint32_t> base(nRank, 0);
    BOOL hasBaseIndicies = FALSE;
    if (SUCCEEDED(pArrayValue->HasBaseIndicies(&hasBaseIndicies)) && (hasBaseIndicies == TRUE))
        IfFailRet(pArrayValue->GetBaseIndicies(nRank, base.data()));

    ss << elementType << "[";
    const char *sep = "";
    for (size_t i = 0; i < dims.size(); ++i)
    {
        ss << sep;
        sep = ", ";

        if (base[i] > 0)
            ss << base[i] << ".." << (base[i] + dims[i] - 1);
        else
            ss << dims[i];
    }
    ss << "]" << arrayType;

    ss << "}";
    output = ss.str();
    return S_OK;
}

HRESULT PrintStringValue(ICorDebugValue *pValue, std::string &output)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugStringValue> pStringValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugStringValue, reinterpret_cast<void **>(&pStringValue)));

    uint32_t cchValue = 0;
    IfFailRet(pStringValue->GetLength(&cchValue));
    cchValue++; // Allocate one more for null terminator

    ArrayHolder<WCHAR> str = new WCHAR[cchValue];

    uint32_t cchValueReturned = 0;
    IfFailRet(pStringValue->GetString(cchValue, &cchValueReturned, str));

    output = to_utf8(str);

    return S_OK;
}

static void EscapeString(std::string &s, char q = '\"')
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

HRESULT GetNullableValue(ICorDebugValue *pValue, ICorDebugValue **ppValueValue, ICorDebugValue **ppHasValueValue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue2> pValue2;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&pValue2)));
    ToRelease<ICorDebugType> pType;
    IfFailRet(pValue2->GetExactType(&pType));
    if (pType == nullptr)
        return E_FAIL;

    ToRelease<ICorDebugClass> pClass;
    IfFailRet(pType->GetClass(&pClass));
    ToRelease<ICorDebugModule> pModule;
    IfFailRet(pClass->GetModule(&pModule));
    mdTypeDef currentTypeDef = mdTypeDefNil;
    IfFailRet(pClass->GetToken(&currentTypeDef));
    ToRelease<IUnknown> pMDUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &pMDUnknown));
    ToRelease<IMetaDataImport> pMD;
    IfFailRet(pMDUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&pMD)));

    ToRelease<ICorDebugObjectValue> pObjValue;
    ToRelease<ICorDebugValue> unboxedResultValue;
    IfFailRet(DereferenceAndUnboxValue(pValue, &unboxedResultValue));
    IfFailRet(unboxedResultValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&pObjValue)));

    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    while (SUCCEEDED(pMD->EnumFields(&hEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        ULONG nameLen = 0;
        std::array<WCHAR, mdNameLen> mdName{};
        if (SUCCEEDED(pMD->GetFieldProps(fieldDef, nullptr, mdName.data(), mdNameLen, &nameLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            // https://github.com/dotnet/runtime/blob/adba54da2298de9c715922b506bfe17a974a3650/src/libraries/System.Private.CoreLib/src/System/Nullable.cs#L24
            if (str_equal(mdName.data(), W("value")))
                IfFailRet(pObjValue->GetFieldValue(pClass, fieldDef, ppValueValue));

            // https://github.com/dotnet/runtime/blob/adba54da2298de9c715922b506bfe17a974a3650/src/libraries/System.Private.CoreLib/src/System/Nullable.cs#L23
            if (str_equal(mdName.data(), W("hasValue")))
                IfFailRet(pObjValue->GetFieldValue(pClass, fieldDef, ppHasValueValue));
        }
    }

    return S_OK;
}

HRESULT PrintNullableValue(ICorDebugValue *pValue, std::string &outTextValue)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> pValueValue;
    ToRelease<ICorDebugValue> pHasValueValue;
    IfFailRet(GetNullableValue(pValue, &pValueValue, &pHasValueValue));

    uint32_t cbSize = 0;
    IfFailRet(pHasValueValue->GetSize(&cbSize));
    ArrayHolder<BYTE> rgbValue = new (std::nothrow) BYTE[cbSize];
    if (rgbValue == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    memset(rgbValue.GetPtr(), 0, cbSize * sizeof(BYTE));

    ToRelease<ICorDebugGenericValue> pGenericValue;
    IfFailRet(pHasValueValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&pGenericValue)));
    IfFailRet(pGenericValue->GetValue(static_cast<void *>(&rgbValue[0])));
    // pHasValueValue is ELEMENT_TYPE_BOOLEAN
    if (rgbValue[0] != 0)
    {
        PrintValue(pValueValue, outTextValue, true);
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
    ToRelease<ICorDebugValue> pValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &pValue, &isNull));

    if (isNull == TRUE)
    {
        output = "null";
        return S_OK;
    }

    uint32_t cbSize = 0;
    IfFailRet(pValue->GetSize(&cbSize));
    ArrayHolder<BYTE> rgbValue = new (std::nothrow) BYTE[cbSize];
    if (rgbValue == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    memset(rgbValue.GetPtr(), 0, cbSize * sizeof(BYTE));

    CorElementType corElemType = ELEMENT_TYPE_MAX;
    IfFailRet(pValue->GetType(&corElemType));
    if (corElemType == ELEMENT_TYPE_STRING)
    {
        std::string raw_str;
        IfFailRet(PrintStringValue(pValue, raw_str));

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
        return PrintArrayValue(pValue, output);
    }

    ToRelease<ICorDebugGenericValue> pGenericValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&pGenericValue)));
    IfFailRet(pGenericValue->GetValue(static_cast<void *>(&rgbValue[0])));

    if (IsEnum(pValue))
    {
        return PrintEnumValue(pValue, rgbValue, output);
    }

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
        ToRelease<ICorDebugReferenceValue> pReferenceValue;
        if (SUCCEEDED(pValue->QueryInterface(IID_ICorDebugReferenceValue, reinterpret_cast<void **>(&pReferenceValue))))
            pReferenceValue->GetValue(&addr);
        ss << "<function pointer 0x" << std::hex << addr << ">";
    }
    break;

    case ELEMENT_TYPE_VALUETYPE:
    case ELEMENT_TYPE_CLASS:
    {
        std::string typeName;
        TypePrinter::GetTypeOfValue(pValue, typeName);
        if (typeName == "decimal") // TODO: implement mechanism for printing custom type values
        {
            std::string val;
            PrintDecimalValue(pValue, val);
            ss << val;
        }
        else if (typeName == "void")
        {
            ss << "Expression has been evaluated and has no value";
        }
        else if (typeName.back() == '?') // System.Nullable<T>
        {
            std::string val;
            PrintNullableValue(pValue, val);
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
        ss << std::setprecision(8) << *reinterpret_cast<float *>(&rgbValue[0]);
        break;

    case ELEMENT_TYPE_R8:
        ss << std::setprecision(16) << *reinterpret_cast<double *>(&rgbValue[0]);
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
