// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/valueprint.h"
#include "debugger/evalhelpers.h"
#include "metadata/attributes.h"
#include "metadata/corhelpers.h"
#include "metadata/typeprinter.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <array>
#include <cassert>
#include <iomanip>
#include <map>
#include <sstream>
#include <cstring>
#include <vector>

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

HRESULT PrintEnumValue(ICorDebugValue *pInputValue, void *enumValue, std::string &output)
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

    // First, we need to figure out the underlying enum type so that we can correctly type cast the raw values of each enum constant.
    // We get that from the non-static field of the enum variable (I think the field is called "value__" or something similar).
    ULONG numFields = 0;
    HCORENUM fEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    CorElementType enumUnderlyingType = ELEMENT_TYPE_MAX;
    while (SUCCEEDED(trMDImport->EnumFields(&fEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        DWORD fieldAttr = 0;
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        if (SUCCEEDED(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, nullptr, &fieldAttr,
                                                &pSig, &cbSig, nullptr, nullptr, nullptr)))
        {
            if ((fieldAttr & fdStatic) == 0)
            {
                // Skip calling convention with IMAGE_CEE_CS_CALLCONV_FIELD, since we sure this is field.
                IfFailRet(CorSigUncompressSkipOneByte_EndPtr(pSig, pSig + cbSig));
                IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSig + cbSig, enumUnderlyingType));
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
        if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen, nullptr,
                                             nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        DWORD fieldAttr = 0;
        WSTRING mdName(nameLen - 1, '\0'); // nameLen includes null terminator
        UVCP_CONSTANT pRawValue = nullptr;
        ULONG rawValueLength = 0;
        if (SUCCEEDED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr, &fieldAttr,
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
                output = to_utf8(mdName.c_str());

                return S_OK;
            }
            if (foundFlagsAttr)
            {
                // Flag enumerated constant whose value is zero must be excluded from OR-ed expression.
                if (currentConstValue == 0)
                {
                    continue;
                }

                if (currentConstValue == remainingValue ||
                    (currentConstValue & remainingValue) == currentConstValue)
                {
                    OrderedFlags.emplace(currentConstValue, to_utf8(mdName.c_str()));
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

HRESULT GetDecimalFields(ICorDebugValue *pValue, uint32_t &hi, uint32_t &mid, uint32_t &lo, uint32_t &flags)
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
        if(FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen, nullptr,
                                            nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        DWORD fieldAttr = 0;
        WSTRING mdName(nameLen - 1, '\0'); // nameLen includes null terminator
        if(SUCCEEDED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr, &fieldAttr,
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

            const std::string name = to_utf8(mdName.c_str());

            if (name == "hi" || name == "_hi32")
            {
                IfFailRet(GetIntegralValue(trFieldVal, hi));
                has_hi = true;
            }
            else if (name == "_lo64")
            {
                static constexpr uint32_t fourBytesShift = 32;
                uint64_t lo64 = 0;
                IfFailRet(GetIntegralValue(trFieldVal, lo64));
                mid = lo64 >> fourBytesShift;
                lo = lo64 & ((1ULL << fourBytesShift) - 1);
                has_mid = has_lo = true;
            }
            else if (name == "mid")
            {
                IfFailRet(GetIntegralValue(trFieldVal, mid));
                has_mid = true;
            }
            else if (name == "lo")
            {
                IfFailRet(GetIntegralValue(trFieldVal, lo));
                has_lo = true;
            }
            else if (name == "flags" || name == "_flags")
            {
                IfFailRet(GetIntegralValue(trFieldVal, flags));
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
    return v.at(0) == 0 && v.at(1) == 0 && v.at(2) == 0;
}

void udivrem96(std::array<uint32_t, 3> &dividend, uint32_t divisor, uint32_t &remainder)
{
    remainder = 0;
    for (int i = 2; i >= 0; i--)
    {
        const uint64_t partial_dividend = Make_64(remainder, dividend.at(i));
        if (partial_dividend == 0)
        {
            dividend.at(i) = 0;
            remainder = 0;
        }
        else if (partial_dividend < divisor)
        {
            dividend.at(i) = 0;
            remainder = Lo_32(partial_dividend);
        }
        else if (partial_dividend == divisor)
        {
            dividend.at(i) = 1;
            remainder = 0;
        }
        else
        {
            dividend.at(i) = Lo_32(partial_dividend / divisor);
            remainder = Lo_32(partial_dividend - (static_cast<uint64_t>(dividend.at(i)) * divisor));
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
        result.insert(0, 1, digits.at(rem));
    } while (!uint96_is_zero(v));
    return result;
}

void PrintDecimal(uint32_t hi, uint32_t mid, uint32_t lo, uint32_t flags, std::string &output)
{
    std::array<uint32_t, 3> v{lo, mid, hi};

    output = uint96_to_string(v);

    static constexpr uint32_t ScaleMask = 0x00FF0000UL;
    static constexpr uint32_t ScaleShift = 16;
    static constexpr uint32_t SignMask = 1UL << 31;

    const uint32_t scale = (flags & ScaleMask) >> ScaleShift;
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

    uint32_t hi = 0;
    uint32_t mid = 0;
    uint32_t lo = 0;
    uint32_t flags = 0;

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

        if (base.at(i) > 0)
        {
            ss << base.at(i) << ".." << (base.at(i) + dims.at(i) - 1);
        }
        else
        {
            ss << dims.at(i);
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
            s.at(i + count) = '0';
            break;
        case '\a':
            count = 1;
            s.insert(i, count, '\\');
            s.at(i + count) = 'a';
            break;
        case '\b':
            count = 1;
            s.insert(i, count, '\\');
            s.at(i + count) = 'b';
            break;
        case '\f':
            count = 1;
            s.insert(i, count, '\\');
            s.at(i + count) = 'f';
            break;
        case '\n':
            count = 1;
            s.insert(i, count, '\\');
            s.at(i + count) = 'n';
            break;
        case '\r':
            count = 1;
            s.insert(i, count, '\\');
            s.at(i + count) = 'r';
            break;
        case '\t':
            count = 1;
            s.insert(i, count, '\\');
            s.at(i + count) = 't';
            break;
        case '\v':
            count = 1;
            s.insert(i, count, '\\');
            s.at(i + count) = 'v';
            break;
        default:
            break;
        }
        i += count;
    }
}

} // unnamed namespace

HRESULT PrintStringValue(ICorDebugValue *pValue, std::string &output)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugStringValue> trStringValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugStringValue, reinterpret_cast<void **>(&trStringValue)));

    uint32_t cchValue = 0;
    IfFailRet(trStringValue->GetLength(&cchValue));
    cchValue++; // Allocate one more for null terminator

    WSTRING str(cchValue, '\0');

    uint32_t cchValueReturned = 0;
    IfFailRet(trStringValue->GetString(cchValue, &cchValueReturned, str.data()));

    output = to_utf8(str.c_str());

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
        if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen,
                                             nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        WSTRING mdName(nameLen - 1, '\0'); // nameLen includes null terminator
        if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr,
                                             nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
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
    }

    return S_OK;
}

HRESULT PrintValue(ICorDebugValue *pInputValue, std::string &output, bool escape)
{
    HRESULT Status = S_OK;

    pInputValue->AddRef();
    ToRelease<ICorDebugValue> trCurrentValue(pInputValue);

    while (true)
    {
        BOOL isNull = TRUE;
        ToRelease<ICorDebugValue> trValue;
        IfFailRet(DereferenceAndUnboxValue(trCurrentValue, &trValue, &isNull));

        if (isNull == TRUE)
        {
            output = "null";
            return S_OK;
        }

        uint32_t cbSize = 0;
        IfFailRet(trValue->GetSize(&cbSize));
        if (cbSize == 0)
        {
            return E_INVALIDARG;
        }
        std::vector<uint8_t> genericValue(cbSize, 0);

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
        IfFailRet(trGenericValue->GetValue(static_cast<void *>(genericValue.data())));

        if (IsEnum(trValue))
        {
            return PrintEnumValue(trValue, genericValue.data(), output);
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
                ToRelease<ICorDebugValue> trValueValue;
                ToRelease<ICorDebugValue> trHasValueValue;
                IfFailRet(GetNullableValue(trValue, &trValueValue, &trHasValueValue));

                uint8_t boolValue = 0;
                IfFailRet(GetIntegralValue(trHasValueValue, boolValue));

                if (boolValue == 1) // TRUE
                {
                    // Iterative handling: set trCurrentValue to the inner value and continue loop
                    trCurrentValue = trValueValue.Detach();
                    continue;
                }
                else
                {
                    ss << "null";
                }
            }
            else
            {
                ss << '{' << typeName << '}';
            }
        }
        break;

        case ELEMENT_TYPE_BOOLEAN:
            assert(genericValue.size() == 1);
            ss << (genericValue.at(0) == 0 ? "false" : "true");
            break;

        case ELEMENT_TYPE_CHAR:
        {
            const WSTRING wstr{*reinterpret_cast<WCHAR *>(genericValue.data()) , '\0'};
            std::string printableVal = to_utf8(wstr.c_str());
            if (!escape)
            {
                output = printableVal;
                return S_OK;
            }
            EscapeString(printableVal, '\'');
            ss << static_cast<unsigned int>(wstr.at(0)) << " '" << printableVal << "'";
        }
        break;

        case ELEMENT_TYPE_I1:
            assert(genericValue.size() == 1);
            ss << static_cast<int32_t>(*reinterpret_cast<int8_t *>(genericValue.data()));
            break;

        case ELEMENT_TYPE_U1:
            assert(genericValue.size() == 1);
            ss << static_cast<uint32_t>(genericValue.at(0));
            break;

        case ELEMENT_TYPE_I2:
            assert(genericValue.size() == 2);
            ss << *reinterpret_cast<int16_t *>(genericValue.data());
            break;

        case ELEMENT_TYPE_U2:
            assert(genericValue.size() == 2);
            ss << *reinterpret_cast<uint16_t *>(genericValue.data());
            break;

        case ELEMENT_TYPE_I:
        case ELEMENT_TYPE_I4:
            assert(genericValue.size() == 4);
            ss << *reinterpret_cast<int32_t *>(genericValue.data());
            break;

        case ELEMENT_TYPE_U:
        case ELEMENT_TYPE_U4:
            assert(genericValue.size() == 4);
            ss << *reinterpret_cast<uint32_t *>(genericValue.data());
            break;

        case ELEMENT_TYPE_I8:
            assert(genericValue.size() == 8);
            ss << *reinterpret_cast<int64_t *>(genericValue.data());
            break;

        case ELEMENT_TYPE_U8:
            assert(genericValue.size() == 8);
            ss << *reinterpret_cast<uint64_t *>(genericValue.data());
            break;

        case ELEMENT_TYPE_R4:
            assert(genericValue.size() == 4);
            ss << std::setprecision(floatPrecision) << *reinterpret_cast<float *>(genericValue.data());
            break;

        case ELEMENT_TYPE_R8:
            assert(genericValue.size() == 8);
            ss << std::setprecision(doublePrecision) << *reinterpret_cast<double *>(genericValue.data());
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
}

} // namespace dncdbg
