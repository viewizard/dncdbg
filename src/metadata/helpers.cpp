// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/helpers.h"
#include "metadata/attributes.h"
#include "metadata/enumerations.h"
#include "metadata/metadataname.h"
#include "metadata/sigparse.h"
#include "utils/hresult.h"
#include "utils/int_to_string.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <gsl/span>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace dncdbg::MetadataHelpers
{

namespace
{

std::string MakeUnhandledElementTypeName(CorElementType elemType)
{
    static constexpr std::string_view prefix = "(Unhandled CorElementType: 0x";
    static constexpr int hexBase = 16;
    // A 32-bit value has at most 8 hexadecimal digits.
    static constexpr std::size_t maxHexDigits = 2 * sizeof(uint32_t);
    std::array<char, maxHexDigits> digits{};
    const auto conv = std::to_chars(digits.data(), digits.data() + digits.size(),
                                    static_cast<uint32_t>(elemType), hexBase);

    // Reserve the exact worst-case size upfront, so the name is built with a single allocation.
    std::string name;
    name.reserve(prefix.size() + maxHexDigits + 1);
    name.append(prefix.data(), prefix.size());
    name.append(digits.data(), static_cast<std::size_t>(conv.ptr - digits.data()));
    name += ')';
    return name;
}

void TrimString(std::string &str)
{
    const auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        str.clear();
        return;
    }
    const auto last = str.find_last_not_of(" \t\r\n");
    if (first == 0 && last + 1 == str.size())
    {
        return; // Nothing to trim, so the string is left untouched.
    }
    str.erase(last + 1);
    str.erase(0, first);
}

// Appends the "display" form of one "metadata" name segment to `out`. For example, "List`2" becomes
// "List<int, string>" when generic arguments are available in `pArgs` (they are consumed from the front),
// or "List<,>" when they are not. Names without a valid generic arity are appended unchanged.
void AppendConsumedGenericArgs(std::string &out, std::string_view name, gsl::span<std::string> *pArgs)
{
    // Valid metadata never has such a large generic arity, so a bigger number in a malformed name is not
    // treated as arity. This also bounds the size of the placeholder output below.
    static constexpr unsigned long maxGenericArity = 0xFFFFU;

    const std::size_t offset = name.find_last_not_of("0123456789");
    if (offset == std::string_view::npos || offset == name.size() - 1 || name.at(offset) != '`')
    {
        out.append(name.data(), name.size());
        return;
    }

    const std::string_view digits = name.substr(offset + 1);
    unsigned long numArgs = 0;
    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), numArgs);
    if (result.ec != std::errc{} || numArgs == 0 || numArgs > maxGenericArity ||
        (pArgs != nullptr && numArgs > pArgs->size()))
    {
        out.append(name.data(), name.size());
        return;
    }

    out.append(name.data(), offset);
    out += '<';
    if (pArgs == nullptr)
    {
        // Generic arguments are not available: emit empty placeholders, for example "Dictionary<,>".
        out.append(static_cast<std::size_t>(numArgs - 1), ',');
    }
    else
    {
        out += pArgs->front();
        *pArgs = pArgs->subspan(1);
        for (unsigned long i = 1; i < numArgs; ++i)
        {
            out += ", ";
            out += pArgs->front();
            *pArgs = pArgs->subspan(1);
        }
    }
    out += '>';
}

// Get fully-qualified metadata (FQMD) name.
HRESULT GetFQMDNameForTypeRef(mdTypeRef tkTypeRef, IMetaDataImport *pMDImport, std::string &metadataName)
{
    const auto readTypeRefName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
    {
        return pMDImport->GetTypeRefProps(tkTypeRef, nullptr, buf, size, pNeeded);
    };
    return MetadataName::ReadUtf8(readTypeRefName, metadataName);
}

// Appends the array suffix for a rank, for example "[]" (rank 1) or "[,]" (rank 2), to `out`.
// Rank 0 is treated as rank 1 to avoid unsigned underflow of "rank - 1".
void AppendArrayRankSuffix(std::string &out, uint32_t rank)
{
    // The CLR supports up to 32 array dimensions, so a corrupted rank cannot cause a huge allocation.
    static constexpr uint32_t maxArrayRank = 32;
    const std::size_t commaCount = (rank > 1) ? static_cast<std::size_t>(std::min(rank - 1, maxArrayRank)) : 0;
    out += '[';
    out.append(commaCount, ',');
    out += ']';
}

// Resolve a single ICorDebugType to its element type string and array suffix.
// For ELEMENT_TYPE_VALUETYPE/ELEMENT_TYPE_CLASS with generic type parameters,
// the type parameters are collected into `outTypeParams` without being resolved
// to strings. The caller is responsible for resolving them separately.
HRESULT ResolveSingleType(ICorDebugType *pType, std::string &elementTypeName, std::string &arrayType,
                          std::vector<ToRelease<ICorDebugType>> &outTypeParams)
{
    if (pType == nullptr)
    {
        return E_INVALIDARG;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugType> trCurrentType(pType);
    trCurrentType->AddRef(); // Hold reference since we're taking ownership

    // Array/pointer suffixes are appended to `arrayType` as soon as the wrapper is met. The wrappers
    // are met from outermost to innermost, so the suffixes end up in that order.
    const auto processNestedType = [&]() -> bool
    {
        ToRelease<ICorDebugType> trFirstParameter;
        if (SUCCEEDED(trCurrentType->GetFirstTypeParameter(&trFirstParameter)))
        {
            trCurrentType = trFirstParameter.Detach();
            return true; // Continue processing the inner type
        }
        elementTypeName = "<unknown>";
        return false; // Exit loop
    };

    // Iteratively process nested types until we reach a base type
    while (trCurrentType != nullptr)
    {
        CorElementType elemType = ELEMENT_TYPE_MAX;
        IfFailRet(trCurrentType->GetType(&elemType));

        switch (elemType)
        {
        // List of unsupported CorElementTypes:
        // ELEMENT_TYPE_END            = 0x0,
        // ELEMENT_TYPE_VAR            = 0x13,     // a class type variable VAR <U1>
        // ELEMENT_TYPE_GENERICINST    = 0x15,     // GENERICINST <generic type> <argCnt> <arg1> ... <argn>
        // ELEMENT_TYPE_TYPEDBYREF     = 0x16,     // TYPEDREF  (it takes no args) a typed reference to some other type
        // ELEMENT_TYPE_MVAR           = 0x1e,     // a method type variable MVAR <U1>
        // ELEMENT_TYPE_CMOD_REQD      = 0x1F,     // required C modifier : E_T_CMOD_REQD <mdTypeRef/mdTypeDef>
        // ELEMENT_TYPE_CMOD_OPT       = 0x20,     // optional C modifier : E_T_CMOD_OPT <mdTypeRef/mdTypeDef>
        // ELEMENT_TYPE_INTERNAL       = 0x21,     // INTERNAL <typehandle>
        // ELEMENT_TYPE_MAX            = 0x22,     // first invalid element type
        // ELEMENT_TYPE_MODIFIER       = 0x40,
        // ELEMENT_TYPE_SENTINEL       = 0x01 | ELEMENT_TYPE_MODIFIER, // sentinel for varargs
        // ELEMENT_TYPE_PINNED         = 0x05 | ELEMENT_TYPE_MODIFIER,
        // ELEMENT_TYPE_R4_HFA         = 0x06 | ELEMENT_TYPE_MODIFIER, // used only internally for R4 HFA types
        // ELEMENT_TYPE_R8_HFA         = 0x07 | ELEMENT_TYPE_MODIFIER, // used only internally for R8 HFA types
        default:
        {
            elementTypeName = MakeUnhandledElementTypeName(elemType);
            return S_OK;
        }

        case ELEMENT_TYPE_VALUETYPE:
        case ELEMENT_TYPE_CLASS:
        {
            // Defaults in case we fail...
            elementTypeName = (elemType == ELEMENT_TYPE_VALUETYPE) ? "struct" : "class";

            mdTypeDef typeDef = mdTypeDefNil;
            ToRelease<ICorDebugClass> trClass;
            if (SUCCEEDED(trCurrentType->GetClass(&trClass)) &&
                SUCCEEDED(trClass->GetToken(&typeDef)))
            {
                ToRelease<ICorDebugModule> trModule;
                IfFailRet(trClass->GetModule(&trModule));
                ToRelease<IMetaDataImport> trMDImport;
                IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&trMDImport)));

                // Collect generic type parameters without resolving them to strings.
                // The caller will resolve each parameter and replace the placeholders.
                {
                    ToRelease<ICorDebugTypeEnum> trTypeEnum;
                    if (SUCCEEDED(trCurrentType->EnumerateTypeParameters(&trTypeEnum)))
                    {
                        ULONG fetched = 0;
                        ToRelease<ICorDebugType> trTypeParam;
                        while (SUCCEEDED(trTypeEnum->Next(1, &trTypeParam, &fetched)) && fetched == 1)
                        {
                            outTypeParams.emplace_back(trTypeParam.Detach());
                        }
                    }
                }

                // Build placeholder args list for GetFQDisplayNameForToken/AppendConsumedGenericArgs.
                // Each placeholder "\x01{N}" is replaced with the resolved type string by the caller.
                std::vector<std::string> placeholderArgs;
                placeholderArgs.reserve(outTypeParams.size());
                for (std::size_t i = 0; i < outTypeParams.size(); ++i)
                {
                    placeholderArgs.emplace_back("\x01{");
                    AppendInteger(placeholderArgs.back(), i);
                    placeholderArgs.back() += '}';
                }

                std::string displayTypeName;
                gsl::span<std::string> placeholderView(placeholderArgs);
                if (SUCCEEDED(GetFQDisplayNameForToken(typeDef, trMDImport, displayTypeName, &placeholderView)))
                {
                    static constexpr std::string_view nullablePattern = "System.Nullable<";
                    const std::size_t closePos = displayTypeName.rfind('>');
                    if (displayTypeName.rfind(nullablePattern, 0) == 0 && closePos != std::string::npos)
                    {
                        elementTypeName.assign(displayTypeName, nullablePattern.size(), closePos - nullablePattern.size());
                        elementTypeName += '?';
                    }
                    else
                    {
                        elementTypeName = std::move(displayTypeName);
                    }
                }
            }
            break;
        }

        case ELEMENT_TYPE_VOID:
        case ELEMENT_TYPE_BOOLEAN:
        case ELEMENT_TYPE_CHAR:
        case ELEMENT_TYPE_I1:
        case ELEMENT_TYPE_U1:
        case ELEMENT_TYPE_I2:
        case ELEMENT_TYPE_U2:
        case ELEMENT_TYPE_I4:
        case ELEMENT_TYPE_U4:
        case ELEMENT_TYPE_I8:
        case ELEMENT_TYPE_U8:
        case ELEMENT_TYPE_R4:
        case ELEMENT_TYPE_R8:
        case ELEMENT_TYPE_OBJECT:
        case ELEMENT_TYPE_STRING:
        case ELEMENT_TYPE_I:
        case ELEMENT_TYPE_U:
            Status = GetBuiltInTypeName(elemType, elementTypeName);
            assert(SUCCEEDED(Status));
            break;

        case ELEMENT_TYPE_SZARRAY:
            arrayType += "[]";
            if (processNestedType())
            {
                continue;
            }
            break;
        case ELEMENT_TYPE_ARRAY:
        {
            uint32_t rank = 0;
            trCurrentType->GetRank(&rank);
            AppendArrayRankSuffix(arrayType, rank);
            if (processNestedType())
            {
                continue;
            }
            break;
        }
        case ELEMENT_TYPE_BYREF:
            // BYREF (in, out, ref) doesn't add a visible suffix currently.
            if (processNestedType())
            {
                continue;
            }
            break;
        case ELEMENT_TYPE_PTR:
            arrayType += '*';
            if (processNestedType())
            {
                continue;
            }
            break;
        case ELEMENT_TYPE_FNPTR:
            elementTypeName = "*(...)";
            break;
        case ELEMENT_TYPE_TYPEDBYREF:
            elementTypeName = "typedbyref";
            break;
        }

        // Base type resolved, exit the loop.
        break;
    }

    return S_OK;
}

HRESULT ResolveMDSingleType(ICorDebugType *pType, std::string &elementTypeName, std::string &arrayType)
{
    if (pType == nullptr)
    {
        return E_INVALIDARG;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugType> trCurrentType(pType);
    trCurrentType->AddRef(); // Hold reference since we're taking ownership

    // Array suffixes are appended to `arrayType` as soon as the wrapper is met (outermost to innermost).
    // Moves to the wrapped type; returns false (and sets "<unknown>") when the wrapped type is not available.
    const auto processNestedType = [&]() -> bool
    {
        ToRelease<ICorDebugType> trFirstParameter;
        if (SUCCEEDED(trCurrentType->GetFirstTypeParameter(&trFirstParameter)))
        {
            trCurrentType = trFirstParameter.Detach();
            return true; // Continue processing the inner type
        }
        elementTypeName = "<unknown>";
        return false; // Exit loop
    };

    // Iteratively process nested types until we reach a base type
    while (trCurrentType != nullptr)
    {
        CorElementType elemType = ELEMENT_TYPE_MAX;
        IfFailRet(trCurrentType->GetType(&elemType));

        switch (elemType)
        {
        default:
        {
            elementTypeName = MakeUnhandledElementTypeName(elemType);
            return S_OK;
        }

        case ELEMENT_TYPE_VALUETYPE:
        case ELEMENT_TYPE_CLASS:
        {
            ToRelease<ICorDebugClass> trClass;
            IfFailRet(trCurrentType->GetClass(&trClass));
            ToRelease<ICorDebugModule> trModule;
            IfFailRet(trClass->GetModule(&trModule));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&trMDImport)));
            mdToken token = mdTokenNil;
            IfFailRet(trClass->GetToken(&token));
            IfFailRet(GetFQMDTypeNameByToken(token, trMDImport, elementTypeName));
            break;
        }

        case ELEMENT_TYPE_VOID:
        case ELEMENT_TYPE_BOOLEAN:
        case ELEMENT_TYPE_CHAR:
        case ELEMENT_TYPE_I1:
        case ELEMENT_TYPE_U1:
        case ELEMENT_TYPE_I2:
        case ELEMENT_TYPE_U2:
        case ELEMENT_TYPE_I4:
        case ELEMENT_TYPE_U4:
        case ELEMENT_TYPE_I8:
        case ELEMENT_TYPE_U8:
        case ELEMENT_TYPE_R4:
        case ELEMENT_TYPE_R8:
        case ELEMENT_TYPE_OBJECT:
        case ELEMENT_TYPE_STRING:
        case ELEMENT_TYPE_I:
        case ELEMENT_TYPE_U:
            Status = GetBuiltInTypeName(elemType, elementTypeName);
            assert(SUCCEEDED(Status));
            break;

        case ELEMENT_TYPE_SZARRAY:
            arrayType += "[]";
            if (processNestedType())
            {
                continue;
            }
            break;
        case ELEMENT_TYPE_ARRAY:
        {
            uint32_t rank = 0;
            trCurrentType->GetRank(&rank);
            AppendArrayRankSuffix(arrayType, rank);
            if (processNestedType())
            {
                continue;
            }
            break;
        }
        }

        // Base type resolved, exit the loop.
        break;
    }

    return S_OK;
}

// Replace all placeholder occurrences "\x01{N}" in `str` with the corresponding
// resolved type string from `resolvedParams`.
void ReplacePlaceholders(std::string &str, const std::vector<std::string> &resolvedParams)
{
    static constexpr std::string_view placeholderPrefix("\x01{", 2);

    std::size_t pos = str.find(placeholderPrefix);
    if (pos == std::string::npos || resolvedParams.empty())
    {
        return;
    }

    std::string result;
    result.reserve(str.size());
    std::size_t copyFrom = 0; // Start of the part of `str` that is not copied to `result` yet.
    while (pos != std::string::npos)
    {
        const std::size_t indexBegin = pos + placeholderPrefix.size();
        const std::size_t closePos = str.find('}', indexBegin);
        if (closePos == std::string::npos)
        {
            break;
        }

        std::size_t index = 0;
        const char *indexEnd = str.data() + closePos;
        const auto parsed = std::from_chars(str.data() + indexBegin, indexEnd, index);
        if (parsed.ec == std::errc{} && parsed.ptr == indexEnd && index < resolvedParams.size())
        {
            result.append(str, copyFrom, pos - copyFrom);
            result += resolvedParams.at(index);
            copyFrom = closePos + 1;
            pos = str.find(placeholderPrefix, copyFrom);
        }
        else
        {
            pos = str.find(placeholderPrefix, pos + 1);
        }
    }

    if (copyFrom == 0)
    {
        return; // Nothing was replaced.
    }
    result.append(str, copyFrom, std::string::npos);
    str = std::move(result);
}

// Iteratively resolve an ICorDebugType to its full string representation,
// including all nested generic type parameters. Uses an explicit work stack
// to avoid mutual recursion between type resolution and generic argument resolution.
HRESULT ResolveTypeToString(ICorDebugType *pType, std::string &output)
{
    if (pType == nullptr)
    {
        return E_INVALIDARG;
    }

    // Each frame on the stack represents a type being resolved.
    // Processing order: resolve the base type string with placeholders,
    // then resolve each generic type parameter, then replace placeholders.
    struct StackFrame
    {
        std::string baseString;                           // elementType + arrayType with placeholders
        std::vector<ToRelease<ICorDebugType>> typeParams; // generic type params to resolve
        std::vector<std::string> resolvedParams;          // resolved strings for each param
        std::size_t nextParamIdx = 0;                     // next param index to resolve
        std::string *pResultSlot = nullptr;                // where to write the final result
    };

    std::vector<StackFrame> stack;

    // Push the initial type
    {
        StackFrame frame;
        frame.pResultSlot = &output;

        std::string elementType;
        std::string arrayType;
        HRESULT Status = S_OK;
        IfFailRet(ResolveSingleType(pType, elementType, arrayType, frame.typeParams));
        elementType += arrayType;
        frame.baseString = std::move(elementType);
        frame.resolvedParams.resize(frame.typeParams.size());
        stack.push_back(std::move(frame));
    }

    while (!stack.empty())
    {
        StackFrame &current = stack.back();

        if (current.nextParamIdx < current.typeParams.size())
        {
            // There are still unresolved generic type parameters.
            // Resolve the next one by pushing a new frame.
            const std::size_t paramIdx = current.nextParamIdx;
            current.nextParamIdx++;

            ICorDebugType *paramType = current.typeParams.at(paramIdx).GetPtr();

            StackFrame childFrame;
            childFrame.pResultSlot = &current.resolvedParams.at(paramIdx);

            std::string elementType;
            std::string arrayType;
            HRESULT Status = S_OK;
            IfFailRet(ResolveSingleType(paramType, elementType, arrayType, childFrame.typeParams));
            elementType += arrayType;
            childFrame.baseString = std::move(elementType);
            childFrame.resolvedParams.resize(childFrame.typeParams.size());

            if (childFrame.typeParams.empty())
            {
                // Simple type with no generic params - resolve immediately
                *childFrame.pResultSlot = std::move(childFrame.baseString);
            }
            else
            {
                // Complex type with generic params - push onto stack
                stack.push_back(std::move(childFrame));
            }
        }
        else
        {
            // All generic type parameters have been resolved.
            // Replace placeholders in the base string and write the result.
            ReplacePlaceholders(current.baseString, current.resolvedParams);
            *current.pResultSlot = std::move(current.baseString);
            stack.pop_back();
        }
    }

    return S_OK;
}

// Predefined type names are resolved for every display name, so both tables are sorted by their key
// and searched with std::lower_bound() instead of a linear scan.
using TypeNamePair = std::pair<std::string_view, std::string_view>;

constexpr std::array<TypeNamePair, 18> cs2system{{
    {"bool",    "System.Boolean"},
    {"byte",    "System.Byte"},
    {"char",    "System.Char"},
    {"decimal", "System.Decimal"},
    {"double",  "System.Double"},
    {"float",   "System.Single"},
    {"int",     "System.Int32"},
    {"long",    "System.Int64"},
    {"nint",    "System.IntPtr"},
    {"nuint",   "System.UIntPtr"},
    {"object",  "System.Object"},
    {"sbyte",   "System.SByte"},
    {"short",   "System.Int16"},
    {"string",  "System.String"},
    {"uint",    "System.UInt32"},
    {"ulong",   "System.UInt64"},
    {"ushort",  "System.UInt16"},
    {"void",    "System.Void"}
}};

constexpr std::array<TypeNamePair, 18> system2cs{{
    {"System.Boolean", "bool"},
    {"System.Byte",    "byte"},
    {"System.Char",    "char"},
    {"System.Decimal", "decimal"},
    {"System.Double",  "double"},
    {"System.Int16",   "short"},
    {"System.Int32",   "int"},
    {"System.Int64",   "long"},
    {"System.IntPtr",  "nint"},
    {"System.Object",  "object"},
    {"System.SByte",   "sbyte"},
    {"System.Single",  "float"},
    {"System.String",  "string"},
    {"System.UInt16",  "ushort"},
    {"System.UInt32",  "uint"},
    {"System.UInt64",  "ulong"},
    {"System.UIntPtr", "nuint"},
    {"System.Void",    "void"}
}};

// Checks at compile time that the table is sorted, as std::lower_bound() requires.
template <typename Table>
constexpr bool IsTypeNameTableSorted(const Table &table)
{
    std::string_view previous;
    for (const TypeNamePair &entry : table)
    {
        if (entry.first < previous)
        {
            return false;
        }
        previous = entry.first;
    }

    return true;
}

static_assert(IsTypeNameTableSorted(cs2system),
              "The C# to System type name table must be sorted by the C# type name.");
static_assert(IsTypeNameTableSorted(system2cs),
              "The System to C# type name table must be sorted by the System type name.");

void RenameToSystem(std::string &typeName)
{
    const std::string_view nameView(typeName);
    const auto renamed = std::lower_bound(cs2system.cbegin(), cs2system.cend(), nameView,
                                          [](const TypeNamePair &entry, std::string_view name)
                                          { return entry.first < name; });
    if (renamed != cs2system.cend() && renamed->first == nameView)
    {
        typeName.assign(renamed->second.data(), renamed->second.size());
    }
}

void RenameToCSharp(std::string &typeName)
{
    const std::string_view nameView(typeName);
    const auto renamed = std::lower_bound(system2cs.cbegin(), system2cs.cend(), nameView,
                                          [](const TypeNamePair &entry, std::string_view name)
                                          { return entry.first < name; });
    if (renamed != system2cs.cend() && renamed->first == nameView)
    {
        typeName.assign(renamed->second.data(), renamed->second.size());
    }
}

HRESULT GetFQMDTypeNameByTypeDef(mdTypeDef tkTypeDef, IMetaDataImport *pMDImport, std::string &metadataName)
{
    HRESULT Status = S_OK;
    mdTypeDef currentType = tkTypeDef;
    DWORD flags = 0;
    const auto readTypeName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
    {
        return pMDImport->GetTypeDefProps(currentType, buf, size, pNeeded, &flags, nullptr);
    };

    // Most types are not nested, so the name is read directly into the result and no additional
    // buffers or re-assembly are needed.
    IfFailRet(MetadataName::ReadUtf8(readTypeName, metadataName));
    if (!IsTdNested(flags))
    {
        return S_OK;
    }

    // Phase 1: Collect all nested type names (iteratively walk up the hierarchy).
    // Nested types are joined with '+' (C# convention), so the parts are collected from the
    // innermost type outwards and assembled in reverse order in Phase 2.
    std::vector<std::string> nameStack;
    nameStack.emplace_back(std::move(metadataName));
    while (true)
    {
        // Move to enclosing class
        mdTypeDef enclosingClass = mdTypeDefNil;
        IfFailRet(pMDImport->GetNestedClassProps(currentType, &enclosingClass));
        currentType = enclosingClass;

        flags = 0;
        std::string typeName;
        IfFailRet(MetadataName::ReadUtf8(readTypeName, typeName));

        const bool isNested = IsTdNested(flags);
        nameStack.emplace_back(std::move(typeName));
        if (!isNested)
        {
            break; // Reached the outermost non-nested type
        }
    }

    // Phase 2: Build the fully-qualified name from outside-in
    // nameStack contains: [innermost, ..., outermost]
    std::size_t totalNameSize = 0;
    for (const std::string &entry : nameStack)
    {
        totalNameSize += entry.size() + 1; // + '+' delimiter
    }
    metadataName.clear();
    // Reserve the exact size upfront, so Phase 2 never reallocates.
    metadataName.reserve(totalNameSize);
    for (auto it = nameStack.rbegin(); it != nameStack.rend(); ++it)
    {
        if (!metadataName.empty())
        {
            metadataName += '+';
        }
        metadataName += *it;
    }

    return S_OK;
}

HRESULT GetConstructorName(IMetaDataImport *pMDImport, mdTypeDef typeDef, std::string &constrName)
{
    HRESULT Status = S_OK;
    const auto readTypeName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
    {
        return pMDImport->GetTypeDefProps(typeDef, buf, size, pNeeded, nullptr, nullptr);
    };
    std::string typeName;
    IfFailRet(MetadataName::ReadUtf8(readTypeName, typeName));
    std::string_view str = typeName;

    const auto lastDotPos = str.rfind('.');
    if (lastDotPos != std::string_view::npos)
    {
        str = str.substr(lastDotPos + 1);
    }

    const auto backtickPos = str.rfind('`');
    if (backtickPos != std::string_view::npos)
    {
        str = str.substr(0, backtickPos);
    }

    if (str.empty())
    {
        return E_FAIL; // "constrName" is not modified on failure.
    }

    constrName.assign(str.data(), str.size());
    return S_OK;
}

// Returns true when the type `typeDef` or any of its enclosing types declares generic parameters. This is the same
// condition as the presence of a generic arity ('`' suffix) in the "metadata" name of the type, but it does not
// allocate any strings. Returns true on metadata failure, so that the caller falls back to fetching the arguments.
bool HasGenericParamsInTypeChain(IMetaDataImport *pMDImport, IMetaDataImport2 *pMDImport2, mdTypeDef typeDef)
{
    mdTypeDef currentType = typeDef;
    while (true)
    {
        bool hasGenericParams = false;
        if (FAILED(MetadataEnumerations::ForEachGenericParam(pMDImport2, currentType,
            [&hasGenericParams](mdGenericParam /*genParam*/) -> HRESULT
            {
                hasGenericParams = true;
                return S_CAN_EXIT; // A single generic parameter is enough.
            })))
        {
            return true;
        }
        if (hasGenericParams)
        {
            return true;
        }

        DWORD flags = 0;
        if (FAILED(pMDImport->GetTypeDefProps(currentType, nullptr, 0, nullptr, &flags, nullptr)))
        {
            return true;
        }
        if (!IsTdNested(flags))
        {
            return false; // Reached the outermost non-nested type.
        }

        // Move to enclosing class.
        mdTypeDef enclosingClass = mdTypeDefNil;
        if (FAILED(pMDImport->GetNestedClassProps(currentType, &enclosingClass)))
        {
            return true;
        }
        currentType = enclosingClass;
    }
}

} // unnamed namespace

// Collect the names of generic parameters declared on the given type or method token.
// The returned vector is ordered by the generic parameter ordinal (number), so the
// element at index N corresponds to the N-th generic parameter (VAR/MVAR number N).
// The vector is empty when the token has no generic parameters. If the enumeration fails part way,
// the names collected so far are returned.
std::vector<std::string> GetGenericParamNames(IMetaDataImport2 *pMDImport2, mdToken token)
{
    std::vector<std::string> names;

    MetadataEnumerations::ForEachGenericParam(pMDImport2, token,
        [&](mdGenericParam genParam) -> HRESULT
        {
            // Generic parameter names are short ("T", "TKey", ...), so the stack buffer of
            // MetadataName::ReadUtf8() is used and no scratch buffer is kept between iterations.
            const auto readGenName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
            {
                return pMDImport2->GetGenericParamProps(genParam, nullptr, nullptr, nullptr, nullptr,
                                                        buf, size, pNeeded);
            };
            std::string name;
            if (SUCCEEDED(MetadataName::ReadUtf8(readGenName, name)))
            {
                names.emplace_back(std::move(name));
            }
            return S_OK;
        });

    return names;
}

// Get fully-qualified display name for typedef token.
HRESULT GetFQDisplayNameForTypeDef(mdTypeDef tkTypeDef, IMetaDataImport *pMDImport,
                                   std::string &displayTypeName, gsl::span<std::string> *pArgs)
{
    HRESULT Status = S_OK;
    mdTypeDef currentType = tkTypeDef;
    DWORD flags = 0;
    const auto readTypeName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
    {
        return pMDImport->GetTypeDefProps(currentType, buf, size, pNeeded, &flags, nullptr);
    };

    // Most types are not nested, so the display name is built directly from the single type name.
    std::string typeName;
    IfFailRet(MetadataName::ReadUtf8(readTypeName, typeName));
    if (!IsTdNested(flags))
    {
        displayTypeName.clear();
        AppendConsumedGenericArgs(displayTypeName, typeName, pArgs);
        return S_OK;
    }

    // Phase 1: Collect all nested type names (iteratively walk up the hierarchy).
    // Nested type names collected while walking up the hierarchy, innermost first.
    std::vector<std::string> nameStack;
    nameStack.emplace_back(std::move(typeName));
    while (true)
    {
        // Move to enclosing class
        mdTypeDef enclosingClass = mdTypeDefNil;
        IfFailRet(pMDImport->GetNestedClassProps(currentType, &enclosingClass));
        currentType = enclosingClass;

        flags = 0;
        std::string enclosingName;
        IfFailRet(MetadataName::ReadUtf8(readTypeName, enclosingName));

        const bool isNested = IsTdNested(flags);
        nameStack.emplace_back(std::move(enclosingName));
        if (!isNested)
        {
            break; // Reached the outermost non-nested type
        }
    }

    // Phase 2: Build the fully-qualified name from outside-in
    // nameStack contains: [innermost, ..., outermost]
    // Process generic args from outermost to innermost
    std::size_t totalNameSize = 0;
    for (const std::string &entry : nameStack)
    {
        totalNameSize += entry.size() + 1; // + delimiter; a lower bound, generic args may expand it
    }
    displayTypeName.clear();
    // Reserve upfront to avoid reallocations while the name is assembled.
    displayTypeName.reserve(totalNameSize);
    for (auto it = nameStack.rbegin(); it != nameStack.rend(); ++it)
    {
        if (!displayTypeName.empty())
        {
            displayTypeName += '.';
        }

        AppendConsumedGenericArgs(displayTypeName, *it, pArgs);
    }

    return S_OK;
}

HRESULT GetDisplayTypeAndMethodName(ICorDebugFrame *pFrame, mdMethodDef methodDef,
                                    std::string &displayTypeName, std::string &displayMethodName)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pFrame->GetFunction(&trFunction));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));

    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&trMDImport)));
    ToRelease<IMetaDataImport2> trMDImport2;
    IfFailRet(trMDImport->QueryInterface(IID_IMetaDataImport2, reinterpret_cast<void **>(&trMDImport2)));

    mdTypeDef typeDef = mdTypeDefNil;
    const auto readMethodName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
    {
        return trMDImport->GetMethodProps(methodDef, &typeDef, buf, size, pNeeded,
                                          nullptr, nullptr, nullptr, nullptr, nullptr);
    };
    std::string funcName;
    IfFailRet(MetadataName::ReadUtf8(readMethodName, funcName));

    ULONG methodGenericsCount = 0;
    MetadataEnumerations::ForEachGenericParam(trMDImport2, methodDef,
        [&](mdGenericParam /*genParam*/) -> HRESULT
        {
            methodGenericsCount++;
            return S_OK;
        });

    if (funcName == ".ctor" || funcName == ".cctor")
    {
        GetConstructorName(trMDImport, typeDef, funcName);
    }

    if (methodGenericsCount > 0)
    {
        funcName += '`';
        AppendInteger(funcName, methodGenericsCount);
    }

    // The frame's generic arguments are needed only when the method or its declaring type has generic arity.
    // Fetching them costs several debugger calls, so they are skipped for non-generic code (the common case).
    // Without any generic arity nothing is consumed from the argument list, so the result is the same.
    bool hasGenericArity = methodGenericsCount > 0;
    if (!hasGenericArity && typeDef != mdTypeDefNil)
    {
        hasGenericArity = HasGenericParamsInTypeChain(trMDImport, trMDImport2, typeDef);
    }

    std::vector<std::string> argsStorage;
    if (hasGenericArity)
    {
        GetGenericArgs(pFrame, argsStorage);
    }
    gsl::span<std::string> args(argsStorage);

    if (typeDef != mdTypeDefNil)
    {
        if (FAILED(GetFQDisplayNameForTypeDef(typeDef, trMDImport, displayTypeName, &args)))
        {
            displayTypeName = "";
        }
    }

    displayMethodName.clear();
    AppendConsumedGenericArgs(displayMethodName, funcName, &args);

    return S_OK;
}

HRESULT GetDisplayTypeAndMethodName(ICorDebugModule *pModule, mdMethodDef methodDef,
                                    std::string &displayTypeName, std::string &displayMethodName)
{
    HRESULT Status = S_OK;

    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&trMDImport)));
    ToRelease<IMetaDataImport2> trMDImport2;
    IfFailRet(trMDImport->QueryInterface(IID_IMetaDataImport2, reinterpret_cast<void **>(&trMDImport2)));

    mdTypeDef typeDef = mdTypeDefNil;
    const auto readMethodName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
    {
        return trMDImport->GetMethodProps(methodDef, &typeDef, buf, size, pNeeded,
                                          nullptr, nullptr, nullptr, nullptr, nullptr);
    };
    std::string funcName;
    IfFailRet(MetadataName::ReadUtf8(readMethodName, funcName));

    std::vector<std::string> args;
    const auto fillArgs = [&](mdToken token) -> void
    {
        args = GetGenericParamNames(trMDImport2, token);
    };

    if (funcName == ".ctor" || funcName == ".cctor")
    {
        GetConstructorName(trMDImport, typeDef, funcName);
    }

    fillArgs(methodDef);
    if (!args.empty())
    {
        funcName += '`';
        AppendInteger(funcName, args.size());
        displayMethodName.clear();
        gsl::span<std::string> methodArgsView(args);
        AppendConsumedGenericArgs(displayMethodName, funcName, &methodArgsView);
    }
    else
    {
        displayMethodName = funcName;
    }

    if (typeDef != mdTypeDefNil)
    {
        fillArgs(typeDef);
        gsl::span<std::string> typeArgsView(args);
        if (FAILED(GetFQDisplayNameForTypeDef(typeDef, trMDImport, displayTypeName, &typeArgsView)))
        {
            displayTypeName = "";
        }
    }

    return S_OK;
}

HRESULT GetFQMDTypeNameByToken(mdToken token, IMetaDataImport *pMDImport, std::string &metadataName)
{
    HRESULT Status = S_OK;
    metadataName.clear();

    if (TypeFromToken(token) == mdtTypeDef)
    {
        IfFailRet(GetFQMDTypeNameByTypeDef(token, pMDImport, metadataName));
    }
    else if (TypeFromToken(token) == mdtTypeRef)
    {
        IfFailRet(GetFQMDNameForTypeRef(token, pMDImport, metadataName));
    }
    else if (TypeFromToken(token) == mdtTypeSpec)
    {
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        IfFailRet(pMDImport->GetTypeSpecFromToken(token, &pSig, &cbSig));
        SigElementType sigType;
        IfFailRet(ParseElementType(pMDImport, pSig, pSig + cbSig, 0, sigType, nullptr, true));
        metadataName = std::move(sigType.metadataTypeName);
    }
    else
    {
        // Unsupported token type
        return CORDBG_E_UNSUPPORTED;
    }

    return S_OK;
}

HRESULT GetFQMDTypeNameByICorType(ICorDebugType *pType, std::string &metadataName)
{
    HRESULT Status = S_OK;
    std::string metadataElemType;
    std::string metadataArrayType;
    IfFailRet(ResolveMDSingleType(pType, metadataElemType, metadataArrayType));
    metadataName = std::move(metadataElemType);
    metadataName += metadataArrayType;
    return S_OK;
}

HRESULT GetFQMDTypeNameByICorValue(ICorDebugValue *pValue, std::string &metadataName)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    return GetFQMDTypeNameByICorType(trType, metadataName);
}

HRESULT GetFQDisplayNameForToken(mdToken token, IMetaDataImport *pMDImport, std::string &displayName,
                                 gsl::span<std::string> *pArgs)
{
    HRESULT Status = S_OK;
    displayName.clear();

    if (TypeFromToken(token) == mdtTypeDef)
    {
        IfFailRet(GetFQDisplayNameForTypeDef(token, pMDImport, displayName, pArgs));
    }
    else if (TypeFromToken(token) == mdtFieldDef)
    {
        mdTypeDef typeDef = mdTypeDefNil;
        const auto readMemberName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
        {
            return pMDImport->GetMemberProps(token, &typeDef, buf, size, pNeeded, nullptr, nullptr,
                                             nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        };
        std::string memberName;
        IfFailRet(MetadataName::ReadUtf8(readMemberName, memberName));
        if (typeDef != mdTypeDefNil)
        {
            IfFailRet(GetFQDisplayNameForTypeDef(typeDef, pMDImport, displayName, pArgs));
            displayName += '.';
        }
        displayName += memberName;
    }
    else if (TypeFromToken(token) == mdtMethodDef)
    {
        mdTypeDef typeDef = mdTypeDefNil;
        const auto readMethodName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
        {
            return pMDImport->GetMethodProps(token, &typeDef, buf, size, pNeeded,
                                             nullptr, nullptr, nullptr, nullptr, nullptr);
        };
        std::string methodName;
        IfFailRet(MetadataName::ReadUtf8(readMethodName, methodName));
        if (typeDef != mdTypeDefNil)
        {
            IfFailRet(GetFQDisplayNameForTypeDef(typeDef, pMDImport, displayName, pArgs));
            displayName += '.';
        }
        displayName += methodName;
    }
    else if (TypeFromToken(token) == mdtMemberRef)
    {
        mdToken typeToken = mdTypeDefNil;
        const auto readMemberRefName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
        {
            return pMDImport->GetMemberRefProps(token, &typeToken, buf, size, pNeeded, nullptr, nullptr);
        };
        std::string memberName;
        IfFailRet(MetadataName::ReadUtf8(readMemberRefName, memberName));
        if (TypeFromToken(typeToken) == mdtTypeRef)
        {
            std::string metadataName;
            IfFailRet(GetFQMDNameForTypeRef(typeToken, pMDImport, metadataName));
            displayName = ConvertMetadataToDisplayName(metadataName, pArgs);
            displayName += '.';
        }
        else if (TypeFromToken(typeToken) == mdtTypeDef)
        {
            IfFailRet(GetFQDisplayNameForTypeDef(typeToken, pMDImport, displayName, pArgs));
            displayName += '.';
        }
        else
        {
            // Unsupported token type (TypeSpec is not yet handled here).
            return CORDBG_E_UNSUPPORTED;
        }
        displayName += memberName;
    }
    else if (TypeFromToken(token) == mdtTypeRef)
    {
        std::string metadataName;
        IfFailRet(GetFQMDNameForTypeRef(token, pMDImport, metadataName));
        displayName = ConvertMetadataToDisplayName(metadataName, pArgs);
    }
    else if (TypeFromToken(token) == mdtTypeSpec)
    {
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        IfFailRet(pMDImport->GetTypeSpecFromToken(token, &pSig, &cbSig));
        SigElementType sigType;
        IfFailRet(ParseElementType(pMDImport, pSig, pSig + cbSig, 0, sigType, nullptr, true));
        displayName = ConvertMetadataToDisplayName(sigType.metadataTypeName, pArgs);
    }
    else
    {
        // Unsupported token type
        return CORDBG_E_UNSUPPORTED;
    }

    RenameToCSharp(displayName);
    return S_OK;
}

HRESULT GetFQDisplayTypeName(ICorDebugType *pType, std::string &displayElemType, std::string &displayArrayType)
{
    std::vector<ToRelease<ICorDebugType>> typeParams;
    HRESULT Status = S_OK;
    IfFailRet(ResolveSingleType(pType, displayElemType, displayArrayType, typeParams));

    if (!typeParams.empty())
    {
        // Resolve each generic type parameter using the iterative resolver
        // and replace placeholders in the combined output string.
        std::vector<std::string> resolvedParams(typeParams.size());
        for (std::size_t i = 0; i < typeParams.size(); ++i)
        {
            IfFailRet(ResolveTypeToString(typeParams.at(i), resolvedParams.at(i)));
        }
        ReplacePlaceholders(displayElemType, resolvedParams);
    }

    return S_OK;
}

HRESULT GetFQDisplayTypeName(ICorDebugType *pType, std::string &displayTypeName)
{
    HRESULT Status = S_OK;
    std::string displayElemType;
    std::string displayArrayType;
    IfFailRet(GetFQDisplayTypeName(pType, displayElemType, displayArrayType));
    displayTypeName = std::move(displayElemType);
    displayTypeName += displayArrayType;
    return S_OK;
}

HRESULT GetFQDisplayTypeName(ICorDebugValue *pValue, std::string &displayTypeName)
{
    ToRelease<ICorDebugType> trType;
    ToRelease<ICorDebugValue2> trValue2;
    if (SUCCEEDED(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>((&trValue2)))) &&
        SUCCEEDED(trValue2->GetExactType(&trType)))
    {
        return GetFQDisplayTypeName(trType, displayTypeName);
    }
    else
    {
        displayTypeName = "<unknown>";
    }

    return S_OK;
}

std::vector<std::string> ConvertDisplayToMetadataName(std::string_view displayName, std::string &metadataName)
{
    std::vector<std::string> genericTypes;

    const std::size_t start = displayName.find('<');
    if (start == std::string_view::npos)
    {
        metadataName.assign(displayName.data(), displayName.size());
        return genericTypes;
    }

    // The closing '>' of the outermost generic "<...>" is the last '>' in the
    // name: array suffixes ("[]", "[,]", ...) never contain '>'.
    // Everything after it is the suffix, preserved verbatim so it is not
    // swallowed into the last generic argument (and lost from the metadata name).
    std::size_t end = displayName.rfind('>');
    std::string_view suffix;
    if (end == std::string_view::npos)
    {
        // No closing '>': malformed input. Fall back to processing the whole string (no suffix).
        end = displayName.size() - 1;
    }
    else if (end + 1 < displayName.size())
    {
        suffix = displayName.substr(end + 1);
    }

    int paramDepth = 0; // Depth inside generic angle brackets "<...>".
    int arrayDepth = 0; // Depth inside array brackets "[...]"; commas inside are rank separators, not arg separators.

    genericTypes.emplace_back();

    for (std::size_t i = start; i <= end; i++)
    {
        const char c = displayName.at(i);
        switch (c)
        {
        case ',':
            // A comma at the top generic level (depth 1) and outside of any
            // array brackets separates generic type arguments.
            if (paramDepth == 1 && arrayDepth == 0)
            {
                genericTypes.emplace_back();
                continue;
            }
            break;
        case '[':
            arrayDepth++;
            break;
        case ']':
            if (arrayDepth > 0)
            {
                arrayDepth--;
            }
            break;
        case '<':
            paramDepth++;
            if (paramDepth == 1)
            {
                continue; // Skip the opening '<' of the outermost generic.
            }
            break;
        case '>':
            // Guard against underflow on malformed/unbalanced input so a
            // stray '>' cannot desynchronize the depth tracking.
            if (paramDepth > 0)
            {
                paramDepth--;
            }
            if (paramDepth == 0)
            {
                continue; // Skip the closing '>' of the outermost generic.
            }
            break;
        default:
            break;
        }
        genericTypes.back() += c;
    }

    for (auto &arg : genericTypes)
    {
        TrimString(arg);
    }

    metadataName.assign(displayName.data(), start);
    TrimString(metadataName);
    metadataName += '`';
    AppendInteger(metadataName, genericTypes.size());
    metadataName += suffix;
    return genericTypes;
}

std::string ConvertMetadataToDisplayName(std::string_view metadataName, gsl::span<std::string> *pArgs)
{
    // If the metadata name has no type parameters or nested classes, it is identical to the display name.
    if (metadataName.find_first_of("+`") == std::string_view::npos)
    {
        return std::string(metadataName);
    }

    // The name is only read here, so string views are used to avoid copying its parts.
    std::string_view processName(metadataName);
    std::string_view suffix;
    const std::size_t startSuffix = processName.find('[');
    if (startSuffix != std::string_view::npos)
    {
        suffix = processName.substr(startSuffix);
        processName = processName.substr(0, startSuffix);
    }

    static constexpr std::string_view mdDelimiters = ".+";
    static constexpr char dispDelimiter = '.';

    std::string result;
    result.reserve(metadataName.size());
    std::string_view::size_type start = 0;
    std::string_view::size_type end = processName.find_first_of(mdDelimiters);

    while (end != std::string_view::npos)
    {
        if (!result.empty())
        {
            result += dispDelimiter;
        }
        AppendConsumedGenericArgs(result, processName.substr(start, end - start), pArgs);

        start = end + 1;
        end = processName.find_first_of(mdDelimiters, start);
    }

    if (start < processName.size())
    {
        if (!result.empty())
        {
            result += dispDelimiter;
        }
        AppendConsumedGenericArgs(result, processName.substr(start), pArgs);
    }

    result.append(suffix.data(), suffix.size());
    return result;
}

std::vector<std::string> SplitFQDisplayTypeName(std::string_view displayTypeName, std::vector<int> *pRanks)
{
    // Splits a fully-qualified display type name into its dot-separated
    // identifier components (namespace/class path). When "pRanks" is non-null,
    // the array ranks encountered are recorded into it.
    //
    // Examples:
    //   "System.Collections.Generic.Dictionary<int, string>"
    //     -> {"System", "Collections", "Generic", "Dictionary<int,string>"}
    //   "int[,,]"            -> {"int"}, ranks = {3}
    //   "int[][]"            -> {"int"}, ranks = {1, 1}
    //   "System.Nullable<int>[]"
    //     -> {"System", "Nullable<int>"}, ranks = {1}
    //
    // Generic argument lists ("<...>") stay attached to their owning
    // component; dots, brackets and rank commas inside them are preserved
    // verbatim. Whitespace is skipped so that the ", " separator used by
    // AppendConsumedGenericArgs does not leak into component names.

    std::vector<std::string> identifiers;
    int paramDepth = 0; // Depth inside generic angle brackets "<...>".

    identifiers.emplace_back();

    for (const char c : displayTypeName)
    {
        // Skip all whitespace (display names use ", " as the generic arg
        // separator; spaces would corrupt component names and break
        // FindTypeDefByName / RenameToSystem lookups).
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            continue;
        }

        switch (c)
        {
        case '.':
            // A dot at the top level separates namespace/class components.
            // Dots inside generic args are part of an argument's own
            // qualified name and are preserved.
            if (paramDepth == 0)
            {
                identifiers.emplace_back();
                continue;
            }
            break;
        case '[':
            // An opening bracket at the top level starts a new array rank.
            // Brackets inside generic args belong to an argument's array
            // type (e.g. "List<int[]>") and are preserved verbatim.
            if (pRanks != nullptr && paramDepth == 0)
            {
                pRanks->push_back(1);
                continue;
            }
            break;
        case ']':
            // A closing bracket at the top level ends the current rank.
            if (paramDepth == 0)
            {
                continue;
            }
            break;
        case ',':
            // A comma at the top level is a rank dimension separator for
            // multi-dimensional arrays (e.g. "int[,,]"). Commas inside
            // generic args separate type arguments and are preserved.
            if (paramDepth == 0)
            {
                if (pRanks != nullptr && !(*pRanks).empty())
                {
                    (*pRanks).back()++;
                }
                continue;
            }
            break;
        case '<':
            paramDepth++;
            break;
        case '>':
            // Guard against underflow on malformed/unbalanced input so a
            // stray '>' cannot desynchronize the depth tracking.
            if (paramDepth > 0)
            {
                paramDepth--;
            }
            break;
        default:
            break;
        }
        identifiers.back() += c;
    }

    // A single identifier may be a C# primitive keyword (e.g. "int"), so
    // rename it to its System.* metadata name. With two or more identifiers
    // the name is already a "namespace.class" path whose components are never
    // C# keywords, so no rename is needed there.
    if (identifiers.size() == 1)
    {
        RenameToSystem(identifiers.front());
    }

    return identifiers;
}

HRESULT GetGenericTypeParameters(ICorDebugType *pType, std::vector<SigElementType> &genericTypeParameters)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugTypeEnum> trTypeEnum;

    if (SUCCEEDED(pType->EnumerateTypeParameters(&trTypeEnum)))
    {
        ULONG fetched = 0;
        ToRelease<ICorDebugType> trCurrentTypeParam;

        while (SUCCEEDED(trTypeEnum->Next(1, &trCurrentTypeParam, &fetched)) && fetched == 1)
        {
            SigElementType argElType;
            trCurrentTypeParam->GetType(&argElType.elemType);
            if (argElType.elemType == ELEMENT_TYPE_VALUETYPE || argElType.elemType == ELEMENT_TYPE_CLASS ||
                argElType.elemType == ELEMENT_TYPE_SZARRAY || argElType.elemType == ELEMENT_TYPE_ARRAY)
            {
                IfFailRet(MetadataHelpers::GetFQMDTypeNameByICorType(trCurrentTypeParam, argElType.metadataTypeName));
            }
            genericTypeParameters.emplace_back(std::move(argElType));
            trCurrentTypeParam.Free();
        }
    }

    return S_OK;
}

HRESULT GetGenericArgs(ICorDebugFrame *pFrame, std::vector<std::string> &args)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugILFrame2> trILFrame2;
    IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame2, reinterpret_cast<void **>(&trILFrame2)));

    ToRelease<ICorDebugTypeEnum> trTypeEnum;
    if (SUCCEEDED(trILFrame2->EnumerateTypeParameters(&trTypeEnum)))
    {
        ULONG numTypes = 0;
        ToRelease<ICorDebugType> trCurrentTypeParam;

        while (SUCCEEDED(trTypeEnum->Next(1, &trCurrentTypeParam, &numTypes)) && numTypes == 1)
        {
            std::string name;
            ResolveTypeToString(trCurrentTypeParam, name);
            args.emplace_back(std::move(name));
            trCurrentTypeParam.Free();
        }
    }

    return S_OK;
}

HRESULT GetBuiltInTypeName(CorElementType elemType, std::string &typeName)
{
    switch (elemType)
    {
    case ELEMENT_TYPE_VOID:    typeName = "void";    break;
    case ELEMENT_TYPE_BOOLEAN: typeName = "bool";    break;
    case ELEMENT_TYPE_CHAR:    typeName = "char";    break;
    case ELEMENT_TYPE_I1:      typeName = "sbyte";   break;
    case ELEMENT_TYPE_U1:      typeName = "byte";    break;
    case ELEMENT_TYPE_I2:      typeName = "short";   break;
    case ELEMENT_TYPE_U2:      typeName = "ushort";  break;
    case ELEMENT_TYPE_I4:      typeName = "int";     break;
    case ELEMENT_TYPE_U4:      typeName = "uint";    break;
    case ELEMENT_TYPE_I8:      typeName = "long";    break;
    case ELEMENT_TYPE_U8:      typeName = "ulong";   break;
    case ELEMENT_TYPE_R4:      typeName = "float";   break;
    case ELEMENT_TYPE_R8:      typeName = "double";  break;
    case ELEMENT_TYPE_U:       typeName = "nuint";   break;
    case ELEMENT_TYPE_I:       typeName = "nint";    break;
    case ELEMENT_TYPE_STRING:  typeName = "string";  break;
    case ELEMENT_TYPE_OBJECT:  typeName = "object";  break;
    default:
        return E_FAIL;
    }

    return S_OK;
}

HRESULT GetNativeAddress(ICorDebugFunction *pFunction, uint32_t ilOffset, CORDB_ADDRESS &nativeAddress)
{
    if (pFunction == nullptr)
    {
        return E_INVALIDARG;
    }

    nativeAddress = 0;

    HRESULT Status = S_OK;
    ToRelease<ICorDebugCode> trNativeCode;
    IfFailRet(pFunction->GetNativeCode(&trNativeCode));
    CORDB_ADDRESS nativeBaseAddress = 0;
    IfFailRet(trNativeCode->GetAddress(&nativeBaseAddress));

    uint32_t mapElementsCount = 0;
    IfFailRet(trNativeCode->GetILToNativeMapping(0, &mapElementsCount, nullptr));
    if (mapElementsCount == 0)
    {
        return E_FAIL; // Mapping data is unavailable (e.g., lightweight/dynamic methods)
    }

    std::vector<COR_DEBUG_IL_TO_NATIVE_MAP> mapping(mapElementsCount);
    IfFailRet(trNativeCode->GetILToNativeMapping(mapElementsCount, &mapElementsCount, mapping.data()));

    bool found = false;
    uint32_t diff = std::numeric_limits<uint32_t>::max();

    // Search the JIT map to find which native offset corresponds to our IL offset
    for (uint32_t i = 0; i < mapElementsCount; ++i)
    {
        const auto &entry = mapping.at(i);

        // Skip internal CLR runtime markers that do not map to real IL code:
        // - NO_MAPPING (0xffffffff): Code generated by CLR (GC checks, security blocks)
        // - PROLOG     (0xfffffffe): Method initialization code
        // - EPILOG     (0xfffffffd): Method return/cleanup code
        if (entry.ilOffset == static_cast<uint32_t>(NO_MAPPING) ||
            entry.ilOffset == static_cast<uint32_t>(PROLOG) ||
            entry.ilOffset == static_cast<uint32_t>(EPILOG))
        {
            continue;
        }

        // Exact match: The breakpoint IL offset points precisely to an IL boundary
        if (entry.ilOffset == ilOffset)
        {
            nativeAddress = nativeBaseAddress + entry.nativeStartOffset;
            found = true;
            break;
        }

        // Closest match: The breakpoint IL offset most likely points inside an IL range.
        // We keep track of the closest preceding boundary (the 'left' bound of the IL range).
        if (entry.ilOffset <= ilOffset &&
            diff > ilOffset - entry.ilOffset)
        {
            nativeAddress = nativeBaseAddress + entry.nativeStartOffset;
            diff = ilOffset - entry.ilOffset;
            found = true;
        }
    }

    return found ? S_OK : E_FAIL;
}

std::string AddrToString(CORDB_ADDRESS corAddr)
{
    static constexpr std::size_t addrDigits = 16; // CORDB_ADDRESS is ULONG64 for all arches.
    static constexpr std::size_t prefixLength = 2; // "0x"
    static constexpr std::size_t bitsPerDigit = 4;
    static constexpr CORDB_ADDRESS digitMask = 0xF;
    static constexpr std::string_view hexDigits = "0123456789abcdef";

    std::string strAddr = "0x";
    strAddr.resize(prefixLength + addrDigits, '0');
    // Fill in the digits starting from the least significant one, so the value is zero-padded to 16 digits.
    for (std::size_t pos = strAddr.size(); pos-- > prefixLength;)
    {
        strAddr.at(pos) = hexDigits.at(static_cast<std::size_t>(corAddr & digitMask));
        corAddr >>= bitsPerDigit;
    }

    return strAddr;
}

// Returns E_INVALIDARG when the string is empty, malformed, or does not fit into CORDB_ADDRESS.
HRESULT StringToAddr(std::string_view memoryReference, CORDB_ADDRESS &corAddr)
{
    // Hexadecimal values start with the "0x" (or "0X") prefix; everything else is parsed as decimal.
    const bool isHex = memoryReference.size() >= 2 && memoryReference.at(0) == '0' &&
                       (memoryReference.at(1) == 'x' || memoryReference.at(1) == 'X');

    // Note, std::from_chars does not recognize the "0x" base prefix, so it must be skipped manually.
    const std::string_view numberView = isHex ? memoryReference.substr(2) : memoryReference;

    CORDB_ADDRESS addr = 0;
    const auto result = std::from_chars(numberView.data(), numberView.data() + numberView.size(), addr,
                                        isHex ? 16 : 10);
    if (result.ec != std::errc{} || result.ptr != numberView.data() + numberView.size())
    {
        return E_INVALIDARG; // Empty, malformed (e.g. with trailing characters), or out-of-range input.
    }

    corAddr = addr;
    return S_OK;
}

// Find the kickoff method for an async state machine `MoveNext` method.
// If possible, call the faster `DebugInfo::GetStateMachineKickoffMethod()` first.
HRESULT GetStateMachineKickoffMethod(ICorDebugModule *pModule, mdMethodDef moveNextMethodToken, mdMethodDef &kickoffMethodToken)
{
    HRESULT Status = S_OK;
    kickoffMethodToken = mdMethodDefNil;

    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&trMDImport)));

    // The method name is read into the stack buffer of MetadataName::ReadUtf8(); the expected name is
    // short, so neither the read nor the comparison allocates memory in the common case.
    mdTypeDef typeDef = mdTypeDefNil;
    const auto readMethodName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
    {
        return trMDImport->GetMethodProps(moveNextMethodToken, &typeDef, buf, size, pNeeded,
                                          nullptr, nullptr, nullptr, nullptr, nullptr);
    };
    std::string funcName;
    IfFailRet(MetadataName::ReadUtf8(readMethodName, funcName));
    if (funcName != "MoveNext")
    {
        return E_INVALIDARG;
    }

    std::string metadataTypeName;
    IfFailRet(GetFQMDTypeNameByToken(typeDef, trMDImport, metadataTypeName));

    // Async state machine types are always nested classes; find the enclosing class with the kickoff method.
    mdTypeDef enclosingClass = mdTypeDefNil;
    IfFailRet(trMDImport->GetNestedClassProps(typeDef, &enclosingClass));

    // One attribute text buffer is shared across all methods of the enclosing class; HasAttribute() clears it on entry.
    std::string stateMachineClass;
    IfFailRet(MetadataEnumerations::ForEachMethod(trMDImport, enclosingClass,
        [&](mdMethodDef methodDef) -> HRESULT
        {
            if (HasAttribute(trMDImport, methodDef, DebuggerAttribute::GetAsyncStateMachine(), stateMachineClass) &&
                stateMachineClass == metadataTypeName)
            {
                kickoffMethodToken = methodDef;
                return S_CAN_EXIT;
            }
            return S_OK;
        }));

    return kickoffMethodToken != mdMethodDefNil ? S_OK : E_FAIL;
}

// https://github.com/dotnet/roslyn/blob/3fdd28bc26238f717ec1124efc7e1f9c2158bce2/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameParser.cs#L139-L159
HRESULT TryParseSlotIndex(const WSTRING &mdName, int32_t &index)
{
    // https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameConstants.cs#L11
    static constexpr WSTRING::size_type suffixSeparatorLength = 2; // Length of W("__").
    const WSTRING::size_type suffixSeparatorOffset = mdName.rfind(W("__"));
    if (suffixSeparatorOffset == WSTRING::npos)
    {
        return E_FAIL;
    }

    static constexpr std::size_t intMaxSizeInChars = 10;
    const WSTRING::size_type digitsOffset = suffixSeparatorOffset + suffixSeparatorLength;
    const WSTRING::size_type digitsCount = mdName.size() - digitsOffset;
    // The slot index is a positive 4-byte int, which means the max is 10 characters (2147483647).
    if (digitsCount == 0 || digitsCount > intMaxSizeInChars)
    {
        return E_FAIL;
    }

    static constexpr int64_t decimalBase = 10;
    int64_t slotIndex = 0;
    for (WSTRING::size_type i = digitsOffset; i < mdName.size(); ++i)
    {
        const WCHAR wChar = mdName.at(i);
        if (wChar < W('0') || wChar > W('9'))
        {
            return E_FAIL;
        }

        slotIndex = (slotIndex * decimalBase) + static_cast<int64_t>(wChar - W('0'));
    }

    // The slot index starts from 1 and must fit into int32_t.
    if (slotIndex < 1 || slotIndex > static_cast<int64_t>(std::numeric_limits<int32_t>::max()))
    {
        return E_FAIL;
    }

    index = static_cast<int32_t>(slotIndex - 1);
    return S_OK;
}

// https://github.com/dotnet/roslyn/blob/3fdd28bc26238f717ec1124efc7e1f9c2158bce2/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameParser.cs#L20-L59
HRESULT TryParseGeneratedName(const WSTRING &mdName, WSTRING &wGeneratedName)
{
    if (mdName.length() <= 3)
    {
        return E_FAIL;
    }

    const WSTRING::size_type nameStartOffset = mdName.find(W('<'));
    if (nameStartOffset == WSTRING::npos)
    {
        return E_FAIL;
    }

    const WSTRING::size_type closeBracketOffset = mdName.find(W('>'), nameStartOffset);
    if (closeBracketOffset == WSTRING::npos)
    {
        return E_FAIL;
    }

    wGeneratedName = mdName.substr(nameStartOffset + 1, closeBracketOffset - nameStartOffset - 1);
    return S_OK;
}

// https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameParser.cs#L13
bool IsSynthesizedLocalName(const WSTRING &mdName)
{
    if (mdName.empty())
    {
        return false;
    }
    return mdName.front() == W('<') ||                                   // "<..."
           (mdName.size() >= 4 && mdName.compare(0, 4, W("CS$<")) == 0); // "CS$<..."
}

HRESULT GetGeneratedCodeKind(IMetaDataImport *pMDImport, const WSTRING &methodName, mdTypeDef typeDef, GeneratedCodeKind &result)
{
    HRESULT Status = S_OK;
    const auto readTypeName = [&](WCHAR *buf, ULONG size, ULONG *pNeeded) -> HRESULT
    {
        return pMDImport->GetTypeDefProps(typeDef, buf, size, pNeeded, nullptr, nullptr);
    };
    // The generated name markers are ASCII characters, which are encoded identically in UTF-8, so
    // they are searched directly in the name returned by MetadataName::ReadUtf8().
    std::string typeName;
    IfFailRet(MetadataName::ReadUtf8(readTypeName, typeName));

    // https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameParser.cs#L20-L24
    //  Parse the generated name. Returns true for names of the form
    //  [CS$]<[middle]>c[__[suffix]] where [CS$] is included for certain
    //  generated names, where [middle] and [__[suffix]] are optional,
    //  and where c is a single character in [1-9a-z]
    //  (csharp\LanguageAnalysis\LIB\SpecialName.cpp).

    // https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameKind.cs#L13-L20
    //  LambdaMethod = 'b',
    //  LambdaDisplayClass = 'c',
    //  StateMachineType = 'd',

    // https://github.com/dotnet/roslyn/blob/21055e1858548dbd8f4c1fd5d25a9c9617873806/src/Compilers/Core/Portable/PublicAPI.Shipped.txt#L252
    //  const Microsoft.CodeAnalysis.WellKnownMemberNames.MoveNextMethodName = "MoveNext" -> string!
    //  ... used in SynthesizedStateMachineMoveNextMethod class constructor.

    if (methodName.rfind(W("MoveNext"), 0) != WSTRING::npos && typeName.find(">d") != std::string::npos)
    {
        result = GeneratedCodeKind::Async;
    }
    else if (methodName.find(W(">b")) != WSTRING::npos && typeName.find(">c") != std::string::npos)
    {
        result = GeneratedCodeKind::Lambda;
    }
    else
    {
        result = GeneratedCodeKind::Normal;
    }

    return S_OK;
}

GeneratedNameKind GetLocalOrFieldNameKind(const WSTRING &localOrFieldName)
{
    // https://github.com/dotnet/roslyn/blob/d1e617ded188343ba43d24590802dd51e68e8e32/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameParser.cs#L20-L24
    //  Parse the generated name. Returns true for names of the form
    //  [CS$]<[middle]>c[__[suffix]] where [CS$] is included for certain
    //  generated names, where [middle] and [__[suffix]] are optional,
    //  and where c is a single character in [1-9a-z]
    //  (csharp\LanguageAnalysis\LIB\SpecialName.cpp).

    // https://github.com/dotnet/roslyn/blob/f7c7a5972ea0c8c645ddef58ec00a0e03136fd70/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNameKind.cs#L13-L20
    //  ThisProxyField = '4'
    //  HoistedLocalField = '5'
    //  DisplayClassLocalOrField = '8'
    //  PrimaryConstructorParameter = 'P'

    if (localOrFieldName.length() <= 3)
    {
        return GeneratedNameKind::None;
    }

    if (localOrFieldName.find(W(">4")) != WSTRING::npos)
    {
        return GeneratedNameKind::ThisProxyField;
    }
    else if (localOrFieldName.find(W(">5")) != WSTRING::npos)
    {
        return GeneratedNameKind::HoistedLocalField;
    }
    else if (localOrFieldName.find(W(">8")) != WSTRING::npos)
    {
        return GeneratedNameKind::DisplayClassLocalOrField;
    }
    else if (localOrFieldName.find(W(">P")) != WSTRING::npos)
    {
        return GeneratedNameKind::PrimaryConstructorParameterField;
    }

    return GeneratedNameKind::None;
}

uint32_t ParseTotalGenericArity(std::string_view metadataTypeName)
{
    // Drop trailing array suffixes ("[]", "[,,]", ...), since they are not part of the type name
    // itself, e.g. "System.Collections.Generic.List`1[]" -> "System.Collections.Generic.List`1".
    while (!metadataTypeName.empty() && metadataTypeName.back() == ']')
    {
        const std::size_t openPos = metadataTypeName.rfind('[');
        if (openPos == std::string_view::npos)
        {
            break; // Unbalanced brackets, keep the name as is.
        }
        metadataTypeName.remove_suffix(metadataTypeName.size() - openPos);
    }

    // Nested types are joined with '+' and each part may carry its own arity after '`'.
    uint64_t total = 0;
    std::size_t offset = 0;
    while (offset < metadataTypeName.size())
    {
        const std::size_t separatorPos = metadataTypeName.find('+', offset);
        const std::size_t partEnd = (separatorPos == std::string_view::npos) ? metadataTypeName.size() : separatorPos;
        const std::string_view part = metadataTypeName.substr(offset, partEnd - offset);
        offset = partEnd + 1;

        // Find the backtick character '`'.
        const auto backtickPos = part.rfind('`');
        if (backtickPos == std::string_view::npos)
        {
            continue; // Not a generic type
        }

        // The arity digits must follow the backtick and extend to the end of the part.
        const std::string_view numberPart = part.substr(backtickPos + 1);
        uint32_t count = 0;
        const auto result = std::from_chars(numberPart.data(), numberPart.data() + numberPart.size(), count);
        if (result.ec != std::errc{} || result.ptr != numberPart.data() + numberPart.size())
        {
            continue; // Empty after the backtick, e.g. "MyClass`", or non-digit characters, e.g. "MyClass`2x".
        }

        total += count;
    }

    return (total <= std::numeric_limits<uint32_t>::max()) ? static_cast<uint32_t>(total) : 0;
}

} // namespace dncdbg::MetadataHelpers
