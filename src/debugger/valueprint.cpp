// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/valueprint.h"
#include "debugger/evaluator.h"
#include "metadata/attributes.h"
#include "metadata/corhelpers.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"
#include "utils/print.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <array>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <vector>

namespace dncdbg
{

namespace
{

HRESULT PrintDebuggerDisplayAttribute(Evaluator *pEvaluator, EvalStackMachine *pEvalStackMachine, ICorDebugThread *pThread,
                                      ICorDebugValue *pInputValue, std::string &output)
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

    std::string textWithEval;
    if (!HasDebuggerAttribute(trMDImport, currentTypeDef, DebuggerAttribute::Display, textWithEval))
    {
        // Fall back to assembly-level DebuggerDisplayAttribute (Target / TargetTypeName).
        bool found = false;
        ToRelease<IMetaDataAssemblyImport> trAssemblyImport;
        mdAssembly assemblyToken = mdAssemblyNil;
        if (SUCCEEDED(trUnknown->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&trAssemblyImport))) &&
            SUCCEEDED(trAssemblyImport->GetAssemblyFromScope(&assemblyToken)))
        {
            std::string detectTypeName;
            if (SUCCEEDED(MetadataHelpers::GetFQMDTypeNameByToken(currentTypeDef, trMDImport, detectTypeName)) &&
                HasAssemblyDebuggerAttribute(trMDImport, assemblyToken, DebuggerAttribute::Display, detectTypeName, textWithEval))
            {
                found = true;
            }
        }

        if (!found)
        {
            return E_INVALIDARG;
        }
    }

    std::vector<std::pair<std::string, bool>> textWithEvalParts;
    CreateTextWithEvalParts(textWithEval, textWithEvalParts);
    BuildTextWithEval(pEvaluator, pEvalStackMachine, pThread, pInputValue, textWithEvalParts, output);
    return S_OK;
}

HRESULT PrintEnumValue(ICorDebugValue *pInputValue, const void *enumValue, std::string &output)
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
                // Skip calling convention with IMAGE_CEE_CS_CALLCONV_FIELD, since we're sure this is a field.
                IfFailRet(CorSigUncompressSkipOneByte_EndPtr(pSig, pSig + cbSig));
                IfFailRet(CorSigUncompressElementType_EndPtr(pSig, pSig + cbSig, enumUnderlyingType));
                break;
            }
        }
    }
    trMDImport->CloseEnum(fEnum);

    const auto getValue = [&enumUnderlyingType](const void *data) -> uint64_t
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
            return static_cast<uint64_t>(*reinterpret_cast<const intptr_t *>(data));
        case ELEMENT_TYPE_U:
            return static_cast<uint64_t>(*reinterpret_cast<const uintptr_t *>(data));
        case ELEMENT_TYPE_R4:
        case ELEMENT_TYPE_R8:
        // Technically the floating-point ones are options in the CLI, but not in the CLS or C#, so these are NYI
        default:
            return 0;
        }
    };

    // An enum can have any integral numeric type explicitly specified. The enumValue type is the same as enumUnderlyingType.
    const uint64_t curValue = getValue(enumValue);

    // Care about Flags attribute (https://docs.microsoft.com/en-us/dotnet/api/system.flagsattribute),
    // that "Indicates that an enumeration can be treated as a bit field; that is, a set of flags".
    const bool foundFlagsAttr = HasAttribute(trMDImport, currentTypeDef, W("System.FlagsAttribute"));

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
        std::vector<WCHAR> mdName(nameLen, '\0');
        UVCP_CONSTANT pRawValue = nullptr;
        ULONG rawValueLength = 0;
        if (SUCCEEDED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr, &fieldAttr,
                                                nullptr, nullptr, nullptr, &pRawValue, &rawValueLength)))
        {
            const DWORD enumValueRequiredAttributes = fdPublic | fdStatic | fdLiteral | fdHasDefault; // NOLINT(bugprone-signed-bitwise)
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
                // A flag enumerated constant whose value is zero must be excluded from the OR-ed expression.
                if (currentConstValue == 0)
                {
                    continue;
                }

                if (currentConstValue == remainingValue ||
                    (currentConstValue & remainingValue) == currentConstValue)
                {
                    OrderedFlags.emplace(currentConstValue, to_utf8(mdName.data()));
                    remainingValue &= ~currentConstValue;
                }
            }
        }
    }
    trMDImport->CloseEnum(fEnum);

    // Don't lose data; provide the number as-is instead.
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

void uint96_to_string(std::array<uint32_t, 3> &v, std::string &output)
{
    static constexpr uint32_t divisor = 10;
    static constexpr std::array<char, 10> digits{'0','1','2','3','4','5','6','7','8','9'};
    do
    {
        uint32_t rem = 0;
        udivrem96(v, divisor, rem);
        output.insert(0, 1, digits.at(rem));
    }
    while (!uint96_is_zero(v));
}

HRESULT PrintDecimalValue(ICorDebugValue *pValue, std::string &output)
{
    HRESULT Status = S_OK;

    struct
    {
        uint32_t flags = 0;
        uint32_t hi = 0;
        uint32_t lo = 0;
        uint32_t mid = 0;
    } decimal;

    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    IfFailRet(trGenericValue->GetValue(static_cast<void *>(&decimal)));

    std::array<uint32_t, 3> v{decimal.lo, decimal.mid, decimal.hi};

    // Maximum length of a decimal string representation.
    // Calculated as: 1 (minus sign) + 1 (leading zero) + 1 (decimal separator) + 28 (max fractional digits).
    // Example of worst-case scenario: "-0.0000000000000000000000000001" (exactly 31 characters).
    static constexpr size_t decimalMaxLength = 31;
    output.clear();
    output.reserve(decimalMaxLength);
    uint96_to_string(v, output);

    static constexpr uint32_t ScaleMask = 0x00FF0000UL;
    static constexpr uint32_t ScaleShift = 16;
    static constexpr uint32_t SignMask = 1UL << 31U;

    const uint32_t scale = (decimal.flags & ScaleMask) >> ScaleShift;
    const bool is_negative = ((decimal.flags & SignMask) != 0U);

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

    return S_OK;
}

HRESULT PrintArrayValue(ICorDebugValue *pValue, std::string &output)
{
    HRESULT Status = S_OK;

    std::string displayElemType;
    std::string displayArrayType;
    ToRelease<ICorDebugValue2> trValue2;
    ToRelease<ICorDebugType> trType;
    ToRelease<ICorDebugType> trFirstParameter;
    if (FAILED(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2))) ||
        FAILED(trValue2->GetExactType(&trType)) ||
        FAILED(trType->GetFirstTypeParameter(&trFirstParameter)) ||
        FAILED(MetadataHelpers::GetFQDisplayTypeName(trFirstParameter, displayElemType, displayArrayType)))
    {
        displayElemType = "<error>";
    }

    std::ostringstream ss;
    ss << '{' << displayElemType << '[';

    ToRelease<ICorDebugArrayValue> trArrayValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugArrayValue, reinterpret_cast<void **>(&trArrayValue)));

    uint32_t nRank = 0;
    IfFailRet(trArrayValue->GetRank(&nRank));
    if (nRank < 1)
    {
        return E_UNEXPECTED;
    }

    std::vector<uint32_t> dims(nRank, 0);
    trArrayValue->GetDimensions(nRank, dims.data());

    std::vector<uint32_t> base(nRank, 0);
    BOOL hasBaseIndicies = FALSE;
    if (SUCCEEDED(trArrayValue->HasBaseIndicies(&hasBaseIndicies)) && (hasBaseIndicies == TRUE))
    {
        IfFailRet(trArrayValue->GetBaseIndicies(nRank, base.data()));
    }

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

    ss << ']' << displayArrayType << '}';
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

// Print a floating-point value using the shortest representation that round-trips
// to the same value, formatted like the C# default floating-point conversion does:
// fixed-point notation for moderate magnitudes and scientific notation (e.g. "1E+09")
// for very small and very large ones.
template<typename T>
void PrintFloatingPointValue(std::ostringstream &ss, const T value)
{
    if (std::isnan(value))
    {
        ss << "NaN";
        return;
    }
    if (std::isinf(value))
    {
        ss << ((value > 0) ? "Infinity" : "-Infinity");
        return;
    }

    static constexpr size_t bufferSize = 64;
    std::array<char, bufferSize> buffer{};

    // The std::to_chars overload without a format parameter produces the shortest
    // string that round-trips to the same value, in fixed-point or scientific notation.
    auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + bufferSize, value);
    if (ec != std::errc{})
    {
        ss << "<error>";
        return;
    }

    const std::string_view str(buffer.data(), static_cast<size_t>(ptr - buffer.data()));

    // Parse the to_chars output: [-]digits[.digits][e[-]digits].
    const bool isNegative = (str.front() == '-');
    const size_t mantissaBegin = isNegative ? 1U : 0U;
    const size_t expPos = str.find_first_of("eE");
    const std::string_view mantissa = str.substr(mantissaBegin,
        (expPos == std::string_view::npos ? str.size() : expPos) - mantissaBegin);

    // Mantissa digits without the decimal separator, e.g. "9.9" -> "99".
    std::string digits;
    digits.reserve(mantissa.size());
    size_t integerDigits = 0;
    bool dotFound = false;
    for (const char c : mantissa)
    {
        if (c == '.')
        {
            dotFound = true;
            continue;
        }
        if (!dotFound)
        {
            ++integerDigits;
        }
        digits.push_back(c);
    }

    // Exponent part, e.g. "+14" in "1e+14".
    static constexpr int decimalBase = 10;
    int explicitExponent = 0;
    if (expPos != std::string_view::npos)
    {
        size_t i = expPos + 1;
        bool expNegative = false;
        if (str.at(i) == '-' || str.at(i) == '+')
        {
            expNegative = (str.at(i) == '-');
            ++i;
        }
        for (; i < str.size(); ++i)
        {
            explicitExponent = (explicitExponent * decimalBase) + (str.at(i) - '0');
        }
        if (expNegative)
        {
            explicitExponent = -explicitExponent;
        }
    }

    // The value is 0.<digits> * 10^scale.
    int scale = explicitExponent + static_cast<int>(integerDigits);

    // Strip the leading zeros (e.g. "0.0001" provides digits "00001"), adjusting the scale.
    const size_t firstSignificant = digits.find_first_not_of('0');
    if (firstSignificant == std::string::npos)
    {
        // Zero (possibly negative, e.g. "-0").
        ss << (isNegative ? "-0" : "0");
        return;
    }
    digits.erase(0, firstSignificant);
    scale -= static_cast<int>(firstSignificant);

    // The decimal exponent of the first significant digit: value = d.ddd * 10^exponent.
    const int exponent = scale - 1;

    // Same magnitude thresholds as the C# default floating-point formatting has:
    // fixed-point notation for the decimal exponents within [-4, max_digits10 - 1]
    // (e.g. "100000000" for float, "10000000000000000" for double), scientific
    // notation otherwise (e.g. "1E+09" for float, "1E+17" for double, "9E-05").
    static constexpr int minFixedExponent = -4;
    const int maxFixedExponent = std::numeric_limits<T>::max_digits10 - 1;

    std::string result;
    if (isNegative)
    {
        result.push_back('-');
    }

    if (exponent < minFixedExponent || exponent > maxFixedExponent)
    {
        // Scientific notation, e.g. "1.7976931348623157E+308".
        result.push_back(digits.front());
        if (digits.size() > 1)
        {
            result.push_back('.');
            result.append(digits, 1, digits.size() - 1);
        }
        result.push_back('E');
        result.push_back((exponent < 0) ? '-' : '+');

        std::string exponentText = std::to_string((exponent < 0) ? -exponent : exponent);
        static constexpr size_t minExponentDigits = 2;
        if (exponentText.size() < minExponentDigits)
        {
            exponentText.insert(0, minExponentDigits - exponentText.size(), '0');
        }
        result.append(exponentText);
    }
    else if (scale <= 0)
    {
        // Fractional value without an integer part, e.g. "0.0001".
        result.append("0.");
        result.append(static_cast<size_t>(-scale), '0');
        result.append(digits);
    }
    else if (static_cast<size_t>(scale) >= digits.size())
    {
        // Integer value, e.g. "100000000".
        result.append(digits);
        result.append(static_cast<size_t>(scale) - digits.size(), '0');
    }
    else
    {
        // Value with both integer and fractional parts, e.g. "9.9".
        result.append(digits, 0, static_cast<size_t>(scale));
        result.push_back('.');
        result.append(digits, static_cast<size_t>(scale), digits.size() - static_cast<size_t>(scale));
    }

    ss << result;
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

HRESULT PrintValue(ICorDebugThread *pThread, Evaluator *pEvaluator, EvalStackMachine *pEvalStackMachine,
                   ICorDebugValue *pInputValue, FormatSpecifier specifier, std::string &output)
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

        CorElementType corElemType = ELEMENT_TYPE_MAX;
        IfFailRet(trValue->GetType(&corElemType));

        if (corElemType == ELEMENT_TYPE_STRING)
        {
            std::string raw_str;
            IfFailRet(PrintStringValue(trValue, raw_str));

            // Same behavior as MS vsdbg and MSVS C# debugger have - add character escaping in strings.
            EscapeString(raw_str, '"');

            std::ostringstream ss;
            if ((specifier & FormatSpecifier::StringWithNoQuotes) == FormatSpecifier::StringWithNoQuotes)
            {
                ss << raw_str;
            }
            else
            {
                ss << '\"' << raw_str << '\"';
            }
            output = ss.str();
            return S_OK;
        }

        if (corElemType == ELEMENT_TYPE_SZARRAY || corElemType == ELEMENT_TYPE_ARRAY)
        {
            return PrintArrayValue(trValue, output);
        }

        uint32_t cbSize = 0;
        IfFailRet(trValue->GetSize(&cbSize));
        if (cbSize == 0)
        {
            return E_INVALIDARG;
        }
        std::vector<uint8_t> genericValue(cbSize, 0);

        ToRelease<ICorDebugGenericValue> trGenericValue;
        IfFailRet(trValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
        IfFailRet(trGenericValue->GetValue(static_cast<void *>(genericValue.data())));

        std::ostringstream ss;

        if ((specifier & FormatSpecifier::HexadecimalInteger) == FormatSpecifier::HexadecimalInteger)
        {
            int displayNumCount = 0;
            switch (corElemType)
            {
            case ELEMENT_TYPE_I1:
            case ELEMENT_TYPE_U1:
                displayNumCount = sizeof(uint8_t) * 2;
                break;

            case ELEMENT_TYPE_I2:
            case ELEMENT_TYPE_U2:
                displayNumCount = sizeof(uint16_t) * 2;
                break;

            case ELEMENT_TYPE_I4:
            case ELEMENT_TYPE_U4:
                displayNumCount = sizeof(uint32_t) * 2;
                break;

            case ELEMENT_TYPE_I:
            case ELEMENT_TYPE_U:
                displayNumCount = sizeof(uintptr_t) * 2;
                break;

            case ELEMENT_TYPE_I8:
            case ELEMENT_TYPE_U8:
                displayNumCount = sizeof(uint64_t) * 2;
                break;

            default:
                break;
            }

            if (displayNumCount != 0)
            {
                ss << "0x" << std::setfill('0') << std::hex << std::setw(displayNumCount);
            }
        }

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
            break;
        }

        case ELEMENT_TYPE_VALUETYPE:
        case ELEMENT_TYPE_CLASS:
        {
            std::string displayTypeName;
            MetadataHelpers::GetFQDisplayTypeName(trValue, displayTypeName);
            if (displayTypeName == "decimal")
            {
                std::string val;
                PrintDecimalValue(trValue, val);
                ss << val;
            }
            else if (displayTypeName == "void")
            {
                ss << "Expression has been evaluated and has no value";
            }
            else if (displayTypeName.back() == '?') // System.Nullable<T>
            {
                ToRelease<ICorDebugValue> trValueValue;
                bool hasValue = false;
                IfFailRet(GetNullableValue(trValue, &trValueValue, hasValue));

                if (hasValue)
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
            else if (displayTypeName == "System.Guid")
            {
                GUID guid{};
                if (cbSize == sizeof(GUID) &&
                    SUCCEEDED(trGenericValue->GetValue(static_cast<void *>(&guid))))
                {
                    ss << '{' << PrintGUID(guid) << '}';
                }
                else
                {
                    std::string valueToString;
                    if (SUCCEEDED(pEvaluator->CallOverriddenToString(pThread, trCurrentValue, specifier, valueToString)))
                    {
                        ss << '{' << valueToString << '}';
                    }
                    else
                    {
                        ss << "{System.Guid}";
                    }
                }
            }
            else
            {
                if (SUCCEEDED(PrintDebuggerDisplayAttribute(pEvaluator, pEvalStackMachine, pThread, trCurrentValue, output)))
                {
                    return S_OK;
                }

                if (pEvaluator->IsEnumeration(trValue))
                {
                    return PrintEnumValue(trValue, genericValue.data(), output);
                }

                ss << '{';
                std::string valueToString;
                if (displayTypeName != "System.Exception" && displayTypeName != "System.Object" && displayTypeName != "System.ValueType" &&
                    SUCCEEDED(pEvaluator->CallOverriddenToString(pThread, trCurrentValue, specifier, valueToString)))
                {
                    ss << valueToString;
                }
                else
                {
                    ss << displayTypeName;
                }
                ss << '}';
            }
            break;
        }

        case ELEMENT_TYPE_BOOLEAN:
            assert(genericValue.size() == 1);
            ss << (genericValue.at(0) == 0 ? "false" : "true");
            break;

        case ELEMENT_TYPE_CHAR:
        {
            const WSTRING wstr{*reinterpret_cast<WCHAR *>(genericValue.data()) , '\0'};
            std::string printableVal = to_utf8(wstr.c_str());

            // Same behavior as MS vsdbg and MSVS C# debugger have - add character escaping for chars.
            EscapeString(printableVal, '\'');
            if ((specifier & FormatSpecifier::HexadecimalInteger) == FormatSpecifier::HexadecimalInteger)
            {
                static constexpr int displayWcharCount = 4;
                ss << "0x" << std::setfill('0') << std::hex << std::setw(displayWcharCount) << static_cast<unsigned int>(wstr.at(0));
            }
            else
            {
                ss << static_cast<unsigned int>(wstr.at(0));
            }
            ss << " '" << printableVal << "'";
            break;
        }

        case ELEMENT_TYPE_I1:
            assert(genericValue.size() == 1);
            if ((specifier & FormatSpecifier::HexadecimalInteger) == FormatSpecifier::HexadecimalInteger)
            {
                static constexpr uint32_t oneByteMask = 0xFF;
                ss << static_cast<int32_t>(static_cast<uint32_t>(*reinterpret_cast<int8_t *>(genericValue.data())) & oneByteMask);
            }
            else
            {
                ss << static_cast<int32_t>(*reinterpret_cast<int8_t *>(genericValue.data()));
            }
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

        case ELEMENT_TYPE_I4:
            assert(genericValue.size() == 4);
            ss << *reinterpret_cast<int32_t *>(genericValue.data());
            break;

        case ELEMENT_TYPE_U4:
            assert(genericValue.size() == 4);
            ss << *reinterpret_cast<uint32_t *>(genericValue.data());
            break;

        case ELEMENT_TYPE_I:
            assert(genericValue.size() == sizeof(intptr_t));
            ss << *reinterpret_cast<intptr_t *>(genericValue.data());
            break;

        case ELEMENT_TYPE_U:
            assert(genericValue.size() == sizeof(uintptr_t));
            ss << *reinterpret_cast<uintptr_t *>(genericValue.data());
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
            PrintFloatingPointValue(ss, *reinterpret_cast<float *>(genericValue.data()));
            break;

        case ELEMENT_TYPE_R8:
            assert(genericValue.size() == 8);
            PrintFloatingPointValue(ss, *reinterpret_cast<double *>(genericValue.data()));
            break;

        case ELEMENT_TYPE_OBJECT:
            ss << "object";
            break;
        }

        output = ss.str();
        return S_OK;
    }
}

} // namespace dncdbg
