// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/helpers.h"
#include "debugger/evaluator.h" // FIXME: metadata should not depend on debugger
#include "debuginfo/debuginfo.h"
#include "metadata/attributes.h"
#include "metadata/modules.h"
#include "metadata/sigparse.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <charconv>
#include <map>
#include <set>
#include <sstream>
#include <string_view>

namespace dncdbg::MetadataHelpers
{

namespace
{

void TrimString(std::string &str)
{
    const auto first = str.find_first_not_of(" \t\r\n");
    const auto last = str.find_last_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        str.clear();
        return;
    }
    str = str.substr(first, last - first + 1);
}

std::string ConsumeGenericArgs(const std::string &name, std::list<std::string> *args)
{
    const std::size_t offset = name.find_last_not_of("0123456789");

    if (offset == std::string::npos || offset == name.size() - 1 || name.at(offset) != '`')
    {
        return name;
    }

    unsigned long numArgs = 0;
    try
    {
        numArgs = std::stoul(name.substr(offset + 1));
    }
    catch (const std::invalid_argument &/*e*/)
    {
        return name;
    }
    catch (const std::out_of_range &/*e*/)
    {
        return name;
    }

    if (numArgs == 0 || (args != nullptr && numArgs > args->size()))
    {
        return name;
    }

    std::ostringstream ss;
    ss << name.substr(0, offset);
    ss << "<";
    const char *sep = "";
    while (numArgs != 0U)
    {
        numArgs--;
        ss << sep;

        if (args != nullptr && !args->empty())
        {
            sep = ", ";
            ss << args->front();
            args->pop_front();
        }
        else
        {
            sep = ",";
        }
    }
    ss << ">";
    return ss.str();
}

// Get fully-qualified metadata (FQMD) name.
HRESULT GetFQMDNameForTypeRef(mdTypeRef tkTypeRef, IMetaDataImport *pMDImport, std::string &metadataName)
{
    HRESULT Status = S_OK;
    ULONG nameSize = 0;
    IfFailRet(pMDImport->GetTypeRefProps(tkTypeRef, nullptr, nullptr, 0, &nameSize));

    WSTRING wName(nameSize, 0);
    IfFailRet(pMDImport->GetTypeRefProps(tkTypeRef, nullptr, wName.data(), nameSize, nullptr));

    metadataName = to_utf8(wName.c_str());
    return S_OK;
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

    // Stack to accumulate array/pointer suffixes (processed from innermost to outermost)
    std::vector<std::string> typeSuffixes;

    // Helper lambda to build arrayType from accumulated suffixes
    auto finalizeSuffixes = [&]()
    {
        for (const auto &suffix : typeSuffixes)
        {
            arrayType += suffix;
        }
    };

    // Helper lambda to process nested type - returns true if we should continue loop
    auto processNestedType = [&]() -> bool
    {
        ToRelease<ICorDebugType> trFirstParameter;
        if (SUCCEEDED(trCurrentType->GetFirstTypeParameter(&trFirstParameter)))
        {
            trCurrentType = trFirstParameter.Detach();
            return true; // Continue processing the inner type
        }
        elementTypeName = "<unknown>";
        for (const auto &suffix : typeSuffixes)
        {
            arrayType += suffix;
        }
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
            std::ostringstream ss;
            ss << "(Unhandled CorElementType: 0x" << std::hex << elemType << ")";
            elementTypeName = ss.str();
            return S_OK;
        }

        case ELEMENT_TYPE_VALUETYPE:
        case ELEMENT_TYPE_CLASS:
        {
            std::ostringstream ss;
            // Defaults in case we fail...
            elementTypeName = (elemType == ELEMENT_TYPE_VALUETYPE) ? "struct" : "class";

            mdTypeDef typeDef = mdTypeDefNil;
            ToRelease<ICorDebugClass> trClass;
            if (SUCCEEDED(trCurrentType->GetClass(&trClass)) &&
                SUCCEEDED(trClass->GetToken(&typeDef)))
            {
                ToRelease<ICorDebugModule> trModule;
                IfFailRet(trClass->GetModule(&trModule));

                ToRelease<IUnknown> trUnknown;
                IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
                ToRelease<IMetaDataImport> trMDImport;
                IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

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

                // Build placeholder args list for GetFQDisplayNameForToken/ConsumeGenericArgs.
                // Each placeholder "\x01{N}" will be replaced with the resolved type
                // string by the caller after all type parameters are resolved.
                std::list<std::string> placeholderArgs;
                for (std::size_t i = 0; i < outTypeParams.size(); ++i)
                {
                    placeholderArgs.emplace_back(std::string("\x01{") + std::to_string(i) + "}");
                }

                std::string displayTypeName;
                if (SUCCEEDED(GetFQDisplayNameForToken(typeDef, trMDImport, displayTypeName, &placeholderArgs)))
                {
                    static const std::string_view nullablePattern = "System.Nullable<";
                    if (displayTypeName.rfind(nullablePattern, 0) == 0)
                    {
                        ss << displayTypeName.substr(nullablePattern.size(), displayTypeName.rfind('>') - nullablePattern.size()) << "?";
                    }
                    else
                    {
                        ss << displayTypeName;
                    }
                }
            }
            elementTypeName = ss.str();
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
            typeSuffixes.emplace_back("[]");
            if (processNestedType())
            {
                continue;
            }
            break;
        case ELEMENT_TYPE_ARRAY:
        {
            std::ostringstream ss;
            uint32_t rank = 0;
            trCurrentType->GetRank(&rank);
            ss << "[";
            for (uint32_t i = 0; i < rank - 1; i++)
            {
                ss << ",";
            }
            ss << "]";
            typeSuffixes.emplace_back(ss.str());
            if (processNestedType())
            {
                continue;
            }
            break;
        }
        case ELEMENT_TYPE_BYREF:
            typeSuffixes.emplace_back(""); // BYREF (in, out, ref) doesn't add visible prefix currently
            if (processNestedType())
            {
                continue;
            }
            break;
        case ELEMENT_TYPE_PTR:
            typeSuffixes.emplace_back("*");
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

        // Build arrayType from accumulated suffixes and exit from the loop
        finalizeSuffixes();
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

    // Stack to accumulate array/pointer suffixes (processed from innermost to outermost)
    std::vector<std::string> typeSuffixes;

    // Helper lambda to build arrayType from accumulated suffixes
    auto finalizeSuffixes = [&]()
    {
        for (const auto &suffix : typeSuffixes)
        {
            arrayType += suffix;
        }
    };

    // Helper lambda to process nested type - returns true if we should continue loop
    auto processNestedType = [&]() -> bool
    {
        ToRelease<ICorDebugType> trFirstParameter;
        if (SUCCEEDED(trCurrentType->GetFirstTypeParameter(&trFirstParameter)))
        {
            trCurrentType = trFirstParameter.Detach();
            return true; // Continue processing the inner type
        }
        elementTypeName = "<unknown>";
        for (const auto &suffix : typeSuffixes)
        {
            arrayType += suffix;
        }
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
            std::ostringstream ss;
            ss << "(Unhandled CorElementType: 0x" << std::hex << elemType << ")";
            elementTypeName = ss.str();
            return S_OK;
        }

        case ELEMENT_TYPE_VALUETYPE:
        case ELEMENT_TYPE_CLASS:
        {
            ToRelease<ICorDebugClass> trClass;
            IfFailRet(trCurrentType->GetClass(&trClass));
            ToRelease<ICorDebugModule> trModule;
            IfFailRet(trClass->GetModule(&trModule));
            ToRelease<IUnknown> trUnknown;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
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
            typeSuffixes.emplace_back("[]");
            if (processNestedType())
            {
                continue;
            }
            break;
        case ELEMENT_TYPE_ARRAY:
        {
            std::ostringstream ss;
            uint32_t rank = 0;
            trCurrentType->GetRank(&rank);
            ss << "[";
            for (uint32_t i = 0; i < rank - 1; i++)
            {
                ss << ",";
            }
            ss << "]";
            typeSuffixes.emplace_back(ss.str());
            if (processNestedType())
            {
                continue;
            }
            break;
        }
        }

        // Build arrayType from accumulated suffixes and exit from the loop
        finalizeSuffixes();
        break;
    }

    return S_OK;
}

// Replace all placeholder occurrences "\x01{N}" in `str` with the corresponding
// resolved type string from `resolvedParams`.
void ReplacePlaceholders(std::string &str, const std::vector<std::string> &resolvedParams)
{
    for (std::size_t i = 0; i < resolvedParams.size(); ++i)
    {
        const std::string placeholder = std::string("\x01{") + std::to_string(i) + "}";
        std::size_t pos = 0;
        while ((pos = str.find(placeholder, pos)) != std::string::npos)
        {
            str.replace(pos, placeholder.size(), resolvedParams.at(i));
            pos += resolvedParams.at(i).size();
        }
    }
}

// Iteratively resolve an ICorDebugType to its full string representation,
// including all nested generic type parameters. Uses an explicit work stack
// to avoid mutual recursion between type resolution and generic arg resolution.
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
        std::string *resultSlot = nullptr;                // where to write the final result
    };

    std::vector<StackFrame> stack;

    // Push the initial type
    {
        StackFrame frame;
        frame.resultSlot = &output;

        std::string elementType;
        std::string arrayType;
        HRESULT Status = S_OK;
        IfFailRet(ResolveSingleType(pType, elementType, arrayType, frame.typeParams));
        frame.baseString = elementType + arrayType;
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
            childFrame.resultSlot = &current.resolvedParams.at(paramIdx);

            std::string elementType;
            std::string arrayType;
            HRESULT Status = S_OK;
            IfFailRet(ResolveSingleType(paramType, elementType, arrayType, childFrame.typeParams));
            childFrame.baseString = elementType + arrayType;
            childFrame.resolvedParams.resize(childFrame.typeParams.size());

            if (childFrame.typeParams.empty())
            {
                // Simple type with no generic params - resolve immediately
                *childFrame.resultSlot = childFrame.baseString;
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
            *current.resultSlot = current.baseString;
            stack.pop_back();
        }
    }

    return S_OK;
}

std::string RenameToSystem(const std::string &typeName)
{
    static const std::unordered_map<std::string, std::string> cs2system{
        {"void",    "System.Void"},
        {"bool",    "System.Boolean"},
        {"byte",    "System.Byte"},
        {"sbyte",   "System.SByte"},
        {"char",    "System.Char"},
        {"decimal", "System.Decimal"},
        {"double",  "System.Double"},
        {"float",   "System.Single"},
        {"int",     "System.Int32"},
        {"uint",    "System.UInt32"},
        {"long",    "System.Int64"},
        {"ulong",   "System.UInt64"},
        {"object",  "System.Object"},
        {"short",   "System.Int16"},
        {"ushort",  "System.UInt16"},
        {"string",  "System.String"},
        {"nint",    "System.IntPtr"},
        {"nuint",   "System.UIntPtr"}
    };
    auto renamed = cs2system.find(typeName);
    return renamed != cs2system.end() ? renamed->second : typeName;
}

std::string RenameToCSharp(const std::string &typeName)
{
    static const std::unordered_map<std::string, std::string> system2cs{
        {"System.Void",    "void"},
        {"System.Boolean", "bool"},
        {"System.Byte",    "byte"},
        {"System.SByte",   "sbyte"},
        {"System.Char",    "char"},
        {"System.Decimal", "decimal"},
        {"System.Double",  "double"},
        {"System.Single",  "float"},
        {"System.Int32",   "int"},
        {"System.UInt32",  "uint"},
        {"System.Int64",   "long"},
        {"System.UInt64",  "ulong"},
        {"System.Object",  "object"},
        {"System.Int16",   "short"},
        {"System.UInt16",  "ushort"},
        {"System.String",  "string"},
        {"System.IntPtr",  "nint"},
        {"System.UIntPtr", "nuint"}
    };
    auto renamed = system2cs.find(typeName);
    return renamed != system2cs.end() ? renamed->second : typeName;
}

std::vector<std::string> GatherGenericFQDisplayParameters(const std::vector<std::string> &identifiers, int indexEnd)
{
    std::vector<std::string> result;
    for (int i = 0; i < indexEnd; i++)
    {
        std::string metadataTypeName;
        std::vector<std::string> genericFQDisplayTypeNames = MetadataHelpers::ConvertDisplayToMetadataName(identifiers.at(i), metadataTypeName);
        result.insert(result.end(), genericFQDisplayTypeNames.begin(), genericFQDisplayTypeNames.end());
    }
    return result;
}

mdTypeDef GetTypeTokenForName(IMetaDataImport *pMDImport, mdTypeDef tkEnclosingClass, const std::string &name)
{
    mdTypeDef typeToken = mdTypeDefNil;
    pMDImport->FindTypeDefByName(to_utf16(name).c_str(), tkEnclosingClass, &typeToken);
    return typeToken;
}

HRESULT FindTypeInModule(ICorDebugModule *pModule, const std::vector<std::string> &identifiers,
                         int &nextIdentifier, mdTypeDef &typeToken)
{
    HRESULT Status = S_OK;

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    std::string currentTypeName;

    // Search for type in module
    assert(identifiers.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
    for (int i = nextIdentifier; i < static_cast<int>(identifiers.size()); i++)
    {
        std::string metadataName;
        MetadataHelpers::ConvertDisplayToMetadataName(identifiers.at(i), metadataName);
        currentTypeName += (currentTypeName.empty() ? "" : ".") + metadataName;

        typeToken = GetTypeTokenForName(trMDImport, mdTypeDefNil, currentTypeName);
        if (typeToken != mdTypeDefNil)
        {
            nextIdentifier = i + 1;
            break;
        }
    }

    if (typeToken == mdTypeDefNil) // type not found, continue search in next module
    {
        return E_FAIL;
    }

    // Resolve nested class
    for (int j = nextIdentifier; j < static_cast<int>(identifiers.size()); j++)
    {
        std::string metadataName;
        MetadataHelpers::ConvertDisplayToMetadataName(identifiers.at(j), metadataName);
        const mdTypeDef classToken = GetTypeTokenForName(trMDImport, typeToken, metadataName);
        if (classToken == mdTypeDefNil)
        {
            break;
        }
        typeToken = classToken;
        nextIdentifier = j + 1;
    }

    return S_OK;
}

// Replace the first identifier with the target namespace of a matching
// `using <alias> = <namespace>;` alias (ImportsKind::AliasNamespace), if any.
// Only applies when no identifiers have been consumed yet (nextIdentifier == 0),
// since the alias can only substitute the leading namespace component.
void ApplyNamespaceAlias(std::vector<std::string> &identifiers, int nextIdentifier, const PDB::ImportsAndAliases &pdbImports)
{
    if (nextIdentifier != 0)
    {
        return;
    }

    auto aliasNamespace = pdbImports.find(PDB::ImportsKind::AliasNamespace);
    if (aliasNamespace == pdbImports.end())
    {
        return;
    }

    for (const auto &entry : aliasNamespace->second)
    {
        if (entry.alias == identifiers.at(0))
        {
            identifiers.at(0) = entry.targetNamespace;
            break;
        }
    }
}

// Replace the first identifier with the target type of a matching
// `using <alias> = <type>;` alias (ImportsKind::AliasType), if any.
// Only applies when no identifiers have been consumed yet (nextIdentifier == 0),
// since the alias can only substitute the leading type component.
void ApplyTypeAlias(std::vector<std::string> &identifiers, int nextIdentifier, const PDB::ImportsAndAliases &pdbImports)
{
    if (nextIdentifier != 0)
    {
        return;
    }

    auto aliasType = pdbImports.find(PDB::ImportsKind::AliasType);
    if (aliasType == pdbImports.end())
    {
        return;
    }

    for (const auto &entry : aliasType->second)
    {
        if (entry.alias != identifiers.at(0))
        {
            continue;
        }

        // Skip entries whose target type display name could not be resolved.
        if (entry.displayName.empty())
        {
            continue;
        }

        std::vector<std::string> typeIdentifiers = SplitFQDisplayTypeName(entry.displayName);

        identifiers.erase(identifiers.begin());
        identifiers.insert(identifiers.begin(), typeIdentifiers.begin(), typeIdentifiers.end());
        break;
    }
}

// Search all modules for a type token matching `identifiers`. If the type is not
// found, retry the search with each imported namespace prefixed onto the first
// identifier (e.g. resolving `Console` into `System.Console` via `using System;`).
// On success, outputs the found module, type token, and number of consumed
// identifiers. Returns E_FAIL when the type cannot be resolved.
HRESULT FindTypeTokenInAllModules(ICorDebugThread *pThread, std::vector<std::string> &identifiers,
                                  const PDB::ImportsAndAliases &pdbImports, ToRelease<ICorDebugModule> &trTypeModule,
                                  int &nextIdentifier, mdTypeDef &typeToken)
{
    HRESULT Status = S_OK;

    ApplyNamespaceAlias(identifiers, nextIdentifier, pdbImports);
    ApplyTypeAlias(identifiers, nextIdentifier, pdbImports);

    IfFailRet(Modules::ForEachModule(pThread,
        [&](ICorDebugModule *pModule) -> HRESULT
        {
            int tmpNextIdentifier = nextIdentifier;
            if (SUCCEEDED(FindTypeInModule(pModule, identifiers, tmpNextIdentifier, typeToken)))
            {
                pModule->AddRef();
                trTypeModule = pModule;
                nextIdentifier = tmpNextIdentifier;
                assert(typeToken != mdTypeDefNil);
                return S_CAN_EXIT; // Fast exit from the loop.
            }

            return S_OK; // Return success to continue walking.
        }));

    if (typeToken != mdTypeDefNil)
    {
        return S_OK;
    }

    if (nextIdentifier != 0)
    {
        return E_FAIL;
    }

    auto importNamespace = pdbImports.find(PDB::ImportsKind::ImportNamespace);
    if (importNamespace == pdbImports.end())
    {
        return E_FAIL;
    }

    for (const auto &importName : importNamespace->second)
    {
        std::vector<std::string> testIdentifiers = identifiers;
        testIdentifiers.at(0) = importName.targetNamespace + "." + testIdentifiers.at(0);

        IfFailRet(Modules::ForEachModule(pThread,
            [&](ICorDebugModule *pModule) -> HRESULT
            {
                nextIdentifier = 0;
                if (SUCCEEDED(FindTypeInModule(pModule, testIdentifiers, nextIdentifier, typeToken)))
                {
                    pModule->AddRef();
                    trTypeModule = pModule;
                    assert(typeToken != mdTypeDefNil);
                    return S_CAN_EXIT; // Fast exit from the loop.
                }

                return S_OK; // Return success to continue walking.
            }));

        if (typeToken != mdTypeDefNil)
        {
            break;
        }
    }

    return typeToken != mdTypeDefNil ? S_OK : E_FAIL;
}

// Helper function to create a parameterized type from a class token.
HRESULT CreateParameterizedType(ICorDebugModule *pTypeModule, mdTypeDef typeToken,
                                std::vector<ToRelease<ICorDebugType>> &trTypes,
                                ICorDebugType **ppType)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugClass> trClass;
    IfFailRet(pTypeModule->GetClassFromToken(typeToken, &trClass));

    ToRelease<ICorDebugClass2> trClass2;
    IfFailRet(trClass->QueryInterface(IID_ICorDebugClass2, reinterpret_cast<void **>(&trClass2)));

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pTypeModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    DWORD flags = 0;
    ULONG nameLen = 0;
    mdToken tkExtends = mdTokenNil;
    IfFailRet(trMDImport->GetTypeDefProps(typeToken, nullptr, 0, &nameLen, &flags, &tkExtends));

    std::string displayTypeName;
    IfFailRet(MetadataHelpers::GetFQDisplayNameForToken(tkExtends, trMDImport, displayTypeName, nullptr));

    const bool isValueType = displayTypeName == "System.ValueType" || displayTypeName == "System.Enum";
    const CorElementType elemType = isValueType ? ELEMENT_TYPE_VALUETYPE : ELEMENT_TYPE_CLASS;

#ifdef BIT64
    assert(trTypes.size() <= static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
#endif
    ToRelease<ICorDebugType> trType;
    IfFailRet(trClass2->GetParameterizedType(elemType, static_cast<uint32_t>(trTypes.size()),
                                             reinterpret_cast<ICorDebugType **>(trTypes.data()), &trType));

    *ppType = trType.Detach();
    return S_OK;
}

HRESULT ResolveTypeParameters(const std::vector<std::string> &params, ICorDebugThread *pThread,
                              const PDB::ImportsAndAliases &pdbImports,
                              std::vector<ToRelease<ICorDebugType>> &trTypes)
{
    HRESULT Status = S_OK;

    // Map to store resolved types by type name.
    std::map<std::string, ToRelease<ICorDebugType>> resolvedTypes;

    // Work stack entry (LIFO). Since a generic type cannot be created before all its
    // generic arguments are created, each type name is processed in two steps:
    // 1. `expanded == false` - all not-yet-resolved generic arguments of this type are pushed
    //    on top of this entry, so they will be processed (created) first;
    // 2. `expanded == true`  - all generic arguments are resolved, the type can be created.
    struct WorkEntry
    {
        std::string typeName;
        bool expanded{false};
    };
    std::vector<WorkEntry> workStack;

    // Type names that are expanded, but not resolved yet (waiting for their generic arguments).
    // Used to detect circular type dependencies instead of relying on an iterations limit.
    std::set<std::string> inProgress;

    // Note, the work stack is LIFO, push in reverse order to process `params` in original order.
    for (auto it = params.rbegin(); it != params.rend(); ++it)
    {
        workStack.push_back({*it, false});
    }

    // Note, each type name can be expanded only once (it is protected by `inProgress` and
    // `resolvedTypes` checks) and each generic argument is pushed only for an expanded type name,
    // so the total count of iterations is bounded by the generic arguments count and nesting depth.
    while (!workStack.empty())
    {
        WorkEntry entry = std::move(workStack.back());
        workStack.pop_back();

        // Skip if already resolved (the same type name can be used as a generic argument
        // in several places, for example Dictionary<List<int>, List<int>>).
        if (resolvedTypes.find(entry.typeName) != resolvedTypes.end())
        {
            continue;
        }

        std::vector<int> ranks;
        std::vector<std::string> classIdentifiers = MetadataHelpers::SplitFQDisplayTypeName(entry.typeName, &ranks);
        if (classIdentifiers.empty())
        {
            return E_FAIL;
        }

        int nextClassIdentifier = 0;
        ToRelease<ICorDebugModule> trTypeModule;
        mdTypeDef typeToken = mdTypeDefNil;
        IfFailRet(FindTypeTokenInAllModules(pThread, classIdentifiers, pdbImports, trTypeModule, nextClassIdentifier, typeToken));

        const std::vector<std::string> nestedParams = GatherGenericFQDisplayParameters(classIdentifiers, nextClassIdentifier);

        if (!entry.expanded)
        {
            // Collect generic arguments that must be resolved before this type can be created.
            std::vector<std::string> unresolved;
            for (const auto &np : nestedParams)
            {
                if (resolvedTypes.find(np) != resolvedTypes.end())
                {
                    continue;
                }
                if (inProgress.find(np) != inProgress.end())
                {
                    return E_FAIL; // Circular type dependency.
                }
                unresolved.emplace_back(np);
            }

            if (!unresolved.empty())
            {
                inProgress.emplace(entry.typeName);
                entry.expanded = true;
                // Push this type name first, so it will be processed after all its generic
                // arguments that are pushed on top of it (the work stack is LIFO).
                workStack.push_back(std::move(entry));
                for (auto it = unresolved.rbegin(); it != unresolved.rend(); ++it)
                {
                    workStack.push_back({std::move(*it), false});
                }
                continue;
            }
        }

        // Collect resolved nested types.
        std::vector<ToRelease<ICorDebugType>> trNestedTypes;
        for (const auto &np : nestedParams)
        {
            auto findType = resolvedTypes.find(np);
            if (findType == resolvedTypes.end())
            {
                return E_FAIL;
            }
            ICorDebugType *pType = findType->second.GetPtr();
            pType->AddRef();
            trNestedTypes.emplace_back(pType);
        }

        // Create the type.
        ToRelease<ICorDebugType> trType;
        IfFailRet(CreateParameterizedType(trTypeModule, typeToken, trNestedTypes, &trType));

        // Handle array types.
        if (!ranks.empty())
        {
            ToRelease<ICorDebugAppDomain> trAppDomain;
            ToRelease<ICorDebugAppDomain2> trAppDomain2;
            IfFailRet(pThread->GetAppDomain(&trAppDomain));
            IfFailRet(trAppDomain->QueryInterface(IID_ICorDebugAppDomain2, reinterpret_cast<void **>(&trAppDomain2)));

            for (auto irank = ranks.rbegin(); irank != ranks.rend(); ++irank)
            {
                const ToRelease<ICorDebugType> trElementType = ToRelease<ICorDebugType>(trType.Detach());
                IfFailRet(trAppDomain2->GetArrayOrPointerType(*irank > 1 ? ELEMENT_TYPE_ARRAY : ELEMENT_TYPE_SZARRAY,
                                                              *irank, trElementType, &trType));
            }
        }

        inProgress.erase(entry.typeName);
        resolvedTypes.emplace(std::move(entry.typeName), std::move(trType));
    }

    // Copy resolved types to output in original order.
    for (const auto &param : params)
    {
        auto it = resolvedTypes.find(param);
        if (it != resolvedTypes.end())
        {
            trTypes.push_back(std::move(it->second));
        }
    }

    return S_OK;
}

HRESULT GetFQMDTypeNameByTypeDef(mdTypeDef tkTypeDef, IMetaDataImport *pMDImport, std::string &metadataName)
{
    HRESULT Status = S_OK;
    mdTypeDef currentType = tkTypeDef;
    // Stack entry holding a nested type name and the delimiter to use when
    // joining it with the enclosing name: '+' for nested types (C# convention)
    // and '.' for the outermost non-nested type.
    struct StackName
    {
        std::string name;
        char delimiter = '.';
    };
    std::vector<StackName> nameStack;

    // Phase 1: Collect all nested type names (iteratively walk up the hierarchy)
    while (true)
    {
        ULONG nameLen = 0;
        IfFailRet(pMDImport->GetTypeDefProps(currentType, nullptr, 0,
                                             &nameLen, nullptr, nullptr));

        DWORD flags = 0;
        std::vector<WCHAR> name(nameLen, '\0');
        IfFailRet(pMDImport->GetTypeDefProps(currentType, name.data(), nameLen,
                                             nullptr, &flags, nullptr));

        nameStack.push_back({to_utf8(name.data()), IsTdNested(flags) ? '+' : '.'});

        if (!IsTdNested(flags))
        {
            break; // Reached the outermost non-nested type
        }

        // Move to enclosing class
        mdTypeDef enclosingClass = mdTypeDefNil;
        IfFailRet(pMDImport->GetNestedClassProps(currentType, &enclosingClass));

        currentType = enclosingClass;
    }

    // Phase 2: Build the fully-qualified name from outside-in
    // nameStack contains: [innermost, ..., outermost]
    metadataName.clear();
    for (auto it = nameStack.rbegin(); it != nameStack.rend(); ++it)
    {
        if (!metadataName.empty())
        {
            metadataName += it->delimiter;
        }
        metadataName += it->name;
    }

    return S_OK;
}

// Get fully-qualified display name for typedef token.
HRESULT GetFQDisplayNameForTypeDef(mdTypeDef tkTypeDef, IMetaDataImport *pMDImport,
                                   std::string &displayTypeName, std::list<std::string> *args)
{
    HRESULT Status = S_OK;
    mdTypeDef currentType = tkTypeDef;
    // Stack to hold nested type names as we traverse up the hierarchy
    std::vector<std::string> nameStack;

    // Phase 1: Collect all nested type names (iteratively walk up the hierarchy)
    while (true)
    {
        ULONG nameLen = 0;
        IfFailRet(pMDImport->GetTypeDefProps(currentType, nullptr, 0,
                                             &nameLen, nullptr, nullptr));

        DWORD flags = 0;
        std::vector<WCHAR> name(nameLen, '\0');
        IfFailRet(pMDImport->GetTypeDefProps(currentType, name.data(), nameLen,
                                             nullptr, &flags, nullptr));

        nameStack.push_back(to_utf8(name.data()));

        if (!IsTdNested(flags))
        {
            break; // Reached the outermost non-nested type
        }

        // Move to enclosing class
        mdTypeDef enclosingClass = mdTypeDefNil;
        IfFailRet(pMDImport->GetNestedClassProps(currentType, &enclosingClass));

        currentType = enclosingClass;
    }

    // Phase 2: Build the fully-qualified name from outside-in
    // nameStack contains: [innermost, ..., outermost]
    // Process generic args from outermost to innermost
    displayTypeName.clear();
    for (auto it = nameStack.rbegin(); it != nameStack.rend(); ++it)
    {
        if (!displayTypeName.empty())
        {
            displayTypeName += ".";
        }

        displayTypeName += ConsumeGenericArgs(*it, args);
    }

    return S_OK;
}

// Collect the names of generic parameters declared on the given type or method token.
// The returned vector is ordered by the generic parameter ordinal (number), so the
// element at index N corresponds to the N-th generic parameter (VAR/MVAR number N).
// On failure or when the token has no generic parameters, an empty vector is returned.
std::vector<std::string> GetGenericParamNames(IMetaDataImport2 *pMDImport2, mdToken token)
{
    std::vector<std::string> names;

    HCORENUM hEnum = nullptr;
    mdGenericParam genParam = mdGenericParamNil;
    ULONG fetched = 0;
    while (SUCCEEDED(pMDImport2->EnumGenericParams(&hEnum, token, &genParam, 1, &fetched)) && fetched == 1)
    {
        ULONG genNameLen = 0;
        if (FAILED(pMDImport2->GetGenericParamProps(genParam, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &genNameLen)))
        {
            continue;
        }

        std::vector<WCHAR> szGenName(genNameLen, '\0');
        if (FAILED(pMDImport2->GetGenericParamProps(genParam, nullptr, nullptr, nullptr, nullptr,
                                                    szGenName.data(), genNameLen, nullptr)))
        {
            continue;
        }

        names.emplace_back(to_utf8(szGenName.data()));
    }
    pMDImport2->CloseEnum(hEnum);

    return names;
}

HRESULT GetDisplayTypeAndMethodName(ICorDebugFrame *pFrame, mdMethodDef methodDef,
                                    std::string &displayTypeName, std::string &displayMethodName)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pFrame->GetFunction(&trFunction));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
    ToRelease<IMetaDataImport2> trMDImport2;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport2, reinterpret_cast<void **>(&trMDImport2)));

    ULONG nameLen = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &nameLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));

    mdTypeDef typeDef = mdTypeDefNil;
    std::vector<WCHAR> szFunctionName(nameLen, '\0');
    IfFailRet(trMDImport->GetMethodProps(methodDef, &typeDef, szFunctionName.data(), nameLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));

    std::string funcName = to_utf8(szFunctionName.data());

    ULONG methodGenericsCount = 0;
    HCORENUM hEnum = nullptr;
    mdGenericParam genParam = mdGenericParamNil;
    ULONG fetched = 0;
    while (SUCCEEDED(trMDImport2->EnumGenericParams(&hEnum, methodDef, &genParam, 1, &fetched)) && fetched == 1)
    {
        methodGenericsCount++;
    }
    trMDImport2->CloseEnum(hEnum);

    if (methodGenericsCount > 0)
    {
        std::ostringstream ss;
        ss << funcName << '`' << methodGenericsCount;
        funcName = ss.str();
    }

    std::list<std::string> args;
    GetGenericArgs(pFrame, args);

    if (typeDef != mdTypeDefNil)
    {
        if (FAILED(GetFQDisplayNameForTypeDef(typeDef, trMDImport, displayTypeName, &args)))
        {
            displayTypeName = "";
        }
    }

    displayMethodName = ConsumeGenericArgs(funcName, &args);

    return S_OK;
}

HRESULT GetDisplayTypeAndMethodName(ICorDebugModule *pModule, mdMethodDef methodDef,
                                    std::string &displayTypeName, std::string &displayMethodName)
{
    HRESULT Status = S_OK;

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
    ToRelease<IMetaDataImport2> trMDImport2;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport2, reinterpret_cast<void **>(&trMDImport2)));

    ULONG nameLen = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &nameLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));

    mdTypeDef typeDef = mdTypeDefNil;
    std::vector<WCHAR> szFunctionName(nameLen, '\0');
    IfFailRet(trMDImport->GetMethodProps(methodDef, &typeDef, szFunctionName.data(), nameLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));

    std::list<std::string> args;
    auto fillArgs = [&](mdToken token) -> void
    {
        const std::vector<std::string> names = GetGenericParamNames(trMDImport2, token);
        args.assign(names.begin(), names.end());
    };

    fillArgs(methodDef);
    if (!args.empty())
    {
        std::ostringstream ss;
        ss << to_utf8(szFunctionName.data()) << '`' << args.size();
        displayMethodName = ConsumeGenericArgs(ss.str(), &args);
    }
    else
    {
        displayMethodName = to_utf8(szFunctionName.data());
    }

    if (typeDef != mdTypeDefNil)
    {
        fillArgs(typeDef);
        if (FAILED(GetFQDisplayNameForTypeDef(typeDef, trMDImport, displayTypeName, &args)))
        {
            displayTypeName = "";
        }
    }

    return S_OK;
}

// Find kickoff method for an async state machine `MoveNext` method.
// If possible, call the faster `DebugInfo::GetStateMachineKickoffMethod()` first.
HRESULT GetStateMachineKickoffMethod(ICorDebugModule *pModule, mdMethodDef moveNextMethodToken, mdMethodDef &kickoffMethodToken)
{
    HRESULT Status = S_OK;
    kickoffMethodToken = mdMethodDefNil;

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    ULONG funcNameLen = 0;
    IfFailRet(trMDImport->GetMethodProps(moveNextMethodToken, nullptr, nullptr, 0, &funcNameLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));
    mdTypeDef typeDef = mdTypeDefNil;
    WSTRING funcName(funcNameLen, '\0');
    IfFailRet(trMDImport->GetMethodProps(moveNextMethodToken, &typeDef, funcName.data(), funcNameLen, nullptr,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));

    // Remove null terminator that was included in the length
    if (!funcName.empty() && funcName.back() == '\0')
    {
        funcName.pop_back();
    }

    if (funcName != W("MoveNext"))
    {
        return E_INVALIDARG;
    }

    std::string metadataTypeName;
    IfFailRet(GetFQMDTypeNameByToken(typeDef, trMDImport, metadataTypeName));

    // Async state machine types are always nested classes; find the enclosing class with the kickoff method.
    mdTypeDef enclosingClass = mdTypeDefNil;
    IfFailRet(trMDImport->GetNestedClassProps(typeDef, &enclosingClass));

    ULONG numMethods = 0;
    HCORENUM fEnum = nullptr;
    mdMethodDef methodDef = mdMethodDefNil;
    while (SUCCEEDED(trMDImport->EnumMethods(&fEnum, enclosingClass, &methodDef, 1, &numMethods)) && numMethods != 0)
    {
        std::string stateMachineClass;
        if (HasAsyncStateMachineAttribute(trMDImport, methodDef, stateMachineClass) &&
            stateMachineClass == metadataTypeName)
        {
            kickoffMethodToken = methodDef;
            break;
        }
    }
    trMDImport->CloseEnum(fEnum);

    return kickoffMethodToken != mdMethodDefNil ? S_OK : E_FAIL;
}

} // unnamed namespace

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
        metadataName = sigType.metadataTypeName;
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
    metadataName = metadataElemType + metadataArrayType;
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
                                 std::list<std::string> *args)
{
    HRESULT Status = S_OK;
    displayName.clear();

    if (TypeFromToken(token) == mdtTypeDef)
    {
        IfFailRet(GetFQDisplayNameForTypeDef(token, pMDImport, displayName, args));
    }
    else if (TypeFromToken(token) == mdtFieldDef)
    {
        ULONG size = 0;
        IfFailRet(pMDImport->GetMemberProps(token, nullptr, nullptr, 0, &size, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
        mdTypeDef typeDef = mdTypeDefNil;
        std::vector<WCHAR> name(size, '\0');
        IfFailRet(pMDImport->GetMemberProps(token, &typeDef, name.data(), size, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
        if (typeDef != mdTypeDefNil)
        {
            IfFailRet(GetFQDisplayNameForTypeDef(typeDef, pMDImport, displayName, args));
            displayName += ".";
        }
        displayName += to_utf8(name.data());
    }
    else if (TypeFromToken(token) == mdtMethodDef)
    {
        ULONG size = 0;
        IfFailRet(pMDImport->GetMethodProps(token, nullptr, nullptr, 0, &size,
                                            nullptr, nullptr, nullptr, nullptr, nullptr));
        mdTypeDef typeDef = mdTypeDefNil;
        std::vector<WCHAR> methodName(size, '\0');
        IfFailRet(pMDImport->GetMethodProps(token, &typeDef, methodName.data(), size, nullptr,
                                            nullptr, nullptr, nullptr, nullptr, nullptr));
        if (typeDef != mdTypeDefNil)
        {
            IfFailRet(GetFQDisplayNameForTypeDef(typeDef, pMDImport, displayName, args));
            displayName += ".";
        }
        displayName += to_utf8(methodName.data());
    }
    else if (TypeFromToken(token) == mdtMemberRef)
    {
        ULONG size = 0;
        IfFailRet(pMDImport->GetMemberRefProps(token, nullptr, nullptr, 0, &size, nullptr, nullptr));
        mdToken typeToken = mdTypeDefNil;
        std::vector<WCHAR> memberName(size, '\0');
        IfFailRet(pMDImport->GetMemberRefProps(token, &typeToken, memberName.data(), size, nullptr, nullptr, nullptr));
        if (TypeFromToken(typeToken) == mdtTypeRef)
        {
            std::string metadataName;
            IfFailRet(GetFQMDNameForTypeRef(typeToken, pMDImport, metadataName));
            displayName = ConvertMetadataToDisplayName(metadataName, args);
            displayName += ".";
        }
        else if (TypeFromToken(typeToken) == mdtTypeDef)
        {
            IfFailRet(GetFQDisplayNameForTypeDef(typeToken, pMDImport, displayName, args));
            displayName += ".";
        }
        else
        {
            // Unsupported token type (TypeSpec is not yet handled here).
            return CORDBG_E_UNSUPPORTED;
        }
        displayName += to_utf8(memberName.data());
    }
    else if (TypeFromToken(token) == mdtTypeRef)
    {
        std::string metadataName;
        IfFailRet(GetFQMDNameForTypeRef(token, pMDImport, metadataName));
        displayName = ConvertMetadataToDisplayName(metadataName, args);
    }
    else if (TypeFromToken(token) == mdtTypeSpec)
    {
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        IfFailRet(pMDImport->GetTypeSpecFromToken(token, &pSig, &cbSig));
        SigElementType sigType;
        IfFailRet(ParseElementType(pMDImport, pSig, pSig + cbSig, 0, sigType, nullptr, true));
        displayName = ConvertMetadataToDisplayName(sigType.metadataTypeName, args);
    }
    else
    {
        // Unsupported token type
        return CORDBG_E_UNSUPPORTED;
    }

    displayName = RenameToCSharp(displayName);
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
    displayTypeName = displayElemType + displayArrayType;
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

HRESULT GetFQDisplayRealCodeTypeName(ICorDebugFrame *pFrame, DebugInfo *pDebugInfo, std::string &displayTypeName)
{
    HRESULT Status = S_OK;
    displayTypeName.clear();

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pFrame->GetFunction(&trFunction));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));
    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(trFunction->GetToken(&methodToken));

    mdMethodDef methodDef = mdMethodDefNil;
    if (FAILED(pDebugInfo->GetStateMachineKickoffMethod(trModule, methodToken, methodDef)) &&
        FAILED(GetStateMachineKickoffMethod(trModule, methodToken, methodDef)))
    {
        methodDef = methodToken;
    }

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trMDImport->GetMethodProps(methodDef, &typeDef, nullptr, 0, nullptr,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));

    if (typeDef == mdTypeDefNil)
    {
        return E_FAIL;
    }

    std::list<std::string> args;
    GetGenericArgs(pFrame, args);
    IfFailRet(GetFQDisplayNameForTypeDef(typeDef, trMDImport, displayTypeName, &args));

    return S_OK;
}

HRESULT GetFQDisplayRealCodeMethodName(ICorDebugFrame *pFrame, DebugInfo *pDebugInfo, std::string &displayName)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pFrame->GetFunction(&trFunction));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));
    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(trFunction->GetToken(&methodToken));

    mdMethodDef methodDef = mdMethodDefNil;
    bool asyncMethod = true;
    if (FAILED(pDebugInfo->GetStateMachineKickoffMethod(trModule, methodToken, methodDef)) &&
        FAILED(GetStateMachineKickoffMethod(trModule, methodToken, methodDef)))
    {
        methodDef = methodToken;
        asyncMethod = false;
    }

    std::ostringstream ss;
    std::string displayTypeName;
    std::string displayMethodName;
    IfFailRet(GetDisplayTypeAndMethodName(pFrame, methodDef, displayTypeName, displayMethodName));

    if (!displayTypeName.empty())
    {
        ss << displayTypeName << ".";
    }
    ss << displayMethodName << "(";

    auto addMethodParameters = [&]() -> HRESULT
    {
        ToRelease<IUnknown> trUnknown;
        IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        ToRelease<ICorDebugILFrame> trILFrame;
        IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

        DWORD methodAttr = 0;
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, nullptr,
                                             &methodAttr, &pSig, &cbSig, nullptr, nullptr));

        SigElementType returnElementType;
        std::vector<SigElementType> argElementTypes;
        // Ignore failed return code here; we need all we could parse from the sig.
        ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType, argElementTypes, true);

        ULONG cArguments = 0;
        std::unordered_map<std::string, ToRelease<ICorDebugValue>> asyncMethodParams;
        if (!asyncMethod)
        {
            ToRelease<ICorDebugValueEnum> trArgumentEnum;
            IfFailRet(trILFrame->EnumerateArguments(&trArgumentEnum));
            IfFailRet(trArgumentEnum->GetCount(&cArguments));
            // Decrement argument count to exclude `this` for instance methods.
            if ((methodAttr & mdStatic) == 0)
            {
                cArguments--;
            }
        }
        else
        {
            ToRelease<ICorDebugValue> trCurrentThis;
            if (SUCCEEDED(trILFrame->GetArgument(0, &trCurrentThis)))
            {
                std::unordered_set<WSTRING> usedNames;
                Evaluator::WalkGeneratedClassFields(trMDImport, trCurrentThis, 0, usedNames, methodDef, pDebugInfo, trModule,
                    [&](const std::string &name, const Evaluator::GetValueCallback &getValue) -> HRESULT
                    {
                        ToRelease<ICorDebugValue> trValue;
                        if (FAILED(getValue(&trValue, nullptr)))
                        {
                            return S_OK;
                        }

                        asyncMethodParams.emplace(name, trValue.Detach());
                        cArguments++;
                        return S_OK;
                    });
            }
        }

        for (ULONG i = 0; i < cArguments; i++)
        {
            // https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/metadata/imetadataimport-getparamformethodindex-method
            // The ordinal position in the parameter list where the requested parameter occurs. Parameters are numbered starting from one, with the method's return value in position zero.
            // Note: IMetaDataImport::GetParamForMethodIndex() doesn't include "this", but ICorDebugILFrame::GetArgument() does. This is why we have different logic here.
            const ULONG idx = i + 1;
            mdParamDef paramDef = mdParamDefNil;
            ULONG paramNameLen = 0;
            if (FAILED(trMDImport->GetParamForMethodIndex(methodDef, idx, &paramDef)) ||
                FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, nullptr, 0,
                                                 &paramNameLen, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            std::vector<WCHAR> wParamName(paramNameLen, '\0');
            if (FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, wParamName.data(), paramNameLen,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            if (i != 0)
            {
                ss << ", ";
            }

            if (argElementTypes.size() > i && !argElementTypes.at(i).parameterModifier.empty())
            {
                ss << argElementTypes.at(i).parameterModifier << " ";
            }

            const std::string paramName = to_utf8(wParamName.data());
            auto asyncParam = asyncMethodParams.find(paramName);

            std::string displayTypeName;
            ToRelease<ICorDebugValue> trValue;
            if ((asyncMethod && asyncParam != asyncMethodParams.end() &&
                 SUCCEEDED(GetFQDisplayTypeName(asyncParam->second, displayTypeName))) ||
                (!asyncMethod &&
                 SUCCEEDED(Status = trILFrame->GetArgument((methodAttr & mdStatic) == 0 ? i + 1 : i, &trValue)) &&
                 SUCCEEDED(GetFQDisplayTypeName(trValue, displayTypeName))))
            {
                ss << displayTypeName << " ";
            }
            else if (argElementTypes.size() > i && !argElementTypes.at(i).metadataTypeName.empty() &&
                     // TODO: replace with proper type and method generic parameters
                     argElementTypes.at(i).genericElemType != ELEMENT_TYPE_VAR &&
                     argElementTypes.at(i).genericElemType != ELEMENT_TYPE_MVAR)
            {
                ss << ConvertMetadataToDisplayName(argElementTypes.at(i).metadataTypeName, nullptr) << " ";
            }
            // else
            //    in case of failure, ignore the parameter type and print only the parameter name

            ss << paramName;
        }
        return S_OK;
    };
    addMethodParameters();

    ss << ")";
    displayName = ss.str();
    return S_OK;
}

HRESULT GetFQDisplayRealCodeMethodName(ICorDebugModule *pModule, mdMethodDef methodToken, DebugInfo *pDebugInfo, std::string &displayName)
{
    HRESULT Status = S_OK;

    mdMethodDef methodDef = mdMethodDefNil;
    if (FAILED(pDebugInfo->GetStateMachineKickoffMethod(pModule, methodToken, methodDef)) &&
        FAILED(GetStateMachineKickoffMethod(pModule, methodToken, methodDef)))
    {
        methodDef = methodToken;
    }

    std::ostringstream ss;
    std::string displayTypeName;
    std::string displayMethodName;
    IfFailRet(GetDisplayTypeAndMethodName(pModule, methodDef, displayTypeName, displayMethodName));
    if (!displayTypeName.empty())
    {
        ss << displayTypeName << ".";
    }
    ss << displayMethodName << "(";

    auto addMethodParameters = [&]() -> HRESULT
    {
        ToRelease<IUnknown> trUnknown;
        IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
        ToRelease<IMetaDataImport2> trMDImport2;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport2, reinterpret_cast<void **>(&trMDImport2)));

        mdTypeDef typeDef = mdTypeDefNil;
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        IfFailRet(trMDImport->GetMethodProps(methodDef, &typeDef, nullptr, 0, nullptr,
                                             nullptr, &pSig, &cbSig, nullptr, nullptr));

        SigElementType returnElementType;
        std::vector<SigElementType> argElementTypes;
        // Ignore failed return code here, we need all we could parse from sig.
        ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType, argElementTypes, true);

        const std::vector<std::string> typeParameterNames = GetGenericParamNames(trMDImport2, typeDef);
        const std::vector<std::string> methodParameterNames = GetGenericParamNames(trMDImport2, methodDef);

        // Without an ICorDebugFrame we cannot resolve the concrete generic argument
        // types, so fill `metadataTypeName` with the generic parameter declaration
        // names (e.g. "T", "TKey") instead of the actual type names.
        for (auto &methodArg : argElementTypes)
        {
            if (methodArg.genericElemType == ELEMENT_TYPE_VAR)
            {
                if (methodArg.varNum < typeParameterNames.size())
                {
                    methodArg.metadataTypeName = typeParameterNames.at(methodArg.varNum);
                }
            }
            else if (methodArg.genericElemType == ELEMENT_TYPE_MVAR)
            {
                if (methodArg.varNum < methodParameterNames.size())
                {
                    methodArg.metadataTypeName = methodParameterNames.at(methodArg.varNum);
                }
            }
        }

        auto cArguments = static_cast<ULONG>(argElementTypes.size());
        for (ULONG i = 0; i < cArguments; i++)
        {
            // https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/metadata/imetadataimport-getparamformethodindex-method
            // The ordinal position in the parameter list where the requested parameter occurs. Parameters are numbered starting from one, with the method's return value in position zero.
            const ULONG idx = i + 1;
            mdParamDef paramDef = mdParamDefNil;
            ULONG paramNameLen = 0;
            if (FAILED(trMDImport->GetParamForMethodIndex(methodDef, idx, &paramDef)) ||
                FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, nullptr, 0,
                                                 &paramNameLen, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            std::vector<WCHAR> wParamName(paramNameLen, '\0');
            if (FAILED(trMDImport->GetParamProps(paramDef, nullptr, nullptr, wParamName.data(), paramNameLen,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            if (i != 0)
            {
                ss << ", ";
            }

            if (!argElementTypes.at(i).parameterModifier.empty())
            {
                ss << argElementTypes.at(i).parameterModifier << " ";
            }

            if (!argElementTypes.at(i).metadataTypeName.empty())
            {
                ss << ConvertMetadataToDisplayName(argElementTypes.at(i).metadataTypeName, nullptr) << " ";
            }
            // else
            //    in case of failure, ignore parameter type, print only parameter name

            ss << to_utf8(wParamName.data());
        }
        return S_OK;
    };
    addMethodParameters();

    ss << ")";
    displayName = ss.str();
    return S_OK;
}

std::vector<std::string> ConvertDisplayToMetadataName(const std::string &displayName, std::string &metadataName)
{
    std::vector<std::string> genericTypes;

    const std::size_t start = displayName.find('<');
    if (start == std::string::npos)
    {
        metadataName = displayName;
        return genericTypes;
    }

    // The closing '>' of the outermost generic "<...>" is the last '>' in the
    // name: array suffixes ("[]", "[,]", ...) never contain '>'.
    // Everything after it is the suffix, preserved verbatim so it is not
    // swallowed into the last generic argument (and lost from the metadata name).
    std::size_t end = displayName.rfind('>');
    std::string suffix;
    if (end == std::string::npos)
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

    std::string baseName = displayName.substr(0, start);
    TrimString(baseName);
    metadataName = baseName + '`' + std::to_string(genericTypes.size()) + suffix;
    return genericTypes;
}

std::string ConvertMetadataToDisplayName(const std::string &metadataName, std::list<std::string> *args)
{
    // If the metadata name has no type parameters or nested classes, it is identical to the display name.
    if (metadataName.find_first_of("+`") == std::string::npos)
    {
        return metadataName;
    }

    std::string processName = metadataName;
    std::string suffix;
    const std::size_t startSuffix = processName.find('[');
    if (startSuffix != std::string::npos)
    {
        suffix = processName.substr(startSuffix);
        processName.erase(startSuffix);
    }

    static constexpr std::string_view mdDelimiters = ".+";
    static constexpr char dispDelimiter = '.';

    std::string::size_type start = 0;
    std::string::size_type end = processName.find_first_of(mdDelimiters);
    std::string result;

    while (end != std::string::npos)
    {
        if (!result.empty())
        {
            result += dispDelimiter;
        }
        result += ConsumeGenericArgs(processName.substr(start, end - start), args);

        start = end + 1;
        end = processName.find_first_of(mdDelimiters, start);
    }

    if (start < processName.length())
    {
        if (!result.empty())
        {
            result += dispDelimiter;
        }
        result += ConsumeGenericArgs(processName.substr(start), args);
    }

    return result + suffix;
}

std::vector<std::string> SplitFQDisplayTypeName(const std::string &displayTypeName, std::vector<int> *ranks)
{
    // Splits a fully-qualified display type name into its dot-separated
    // identifier components (namespace/class path). When "ranks" is non-null,
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
    // ConsumeGenericArgs does not leak into component names.

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
            if (ranks != nullptr && paramDepth == 0)
            {
                (*ranks).push_back(1);
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
                if (ranks != nullptr && !(*ranks).empty())
                {
                    (*ranks).back()++;
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

    for (auto &id : identifiers)
    {
        TrimString(id);
    }

    // A single identifier may be a C# primitive keyword (e.g. "int"), so
    // rename it to its System.* metadata name. With two or more identifiers
    // the name is already a "namespace.class" path whose components are never
    // C# keywords, so no rename is needed there.
    if (identifiers.size() == 1)
    {
        identifiers.at(0) = RenameToSystem(identifiers.at(0));
    }

    return identifiers;
}

HRESULT FindType(std::vector<std::string> &identifiers, int &nextIdentifier, ICorDebugThread *pThread,
                 ICorDebugModule *pModule, const PDB::ImportsAndAliases &pdbImports, ICorDebugType **ppType)
{
    HRESULT Status = S_OK;

    if (pModule != nullptr)
    {
        pModule->AddRef();
    }
    ToRelease<ICorDebugModule> trTypeModule(pModule);

    mdTypeDef typeToken = mdTypeDefNil;

    if (trTypeModule == nullptr)
    {
        IfFailRet(FindTypeTokenInAllModules(pThread, identifiers, pdbImports, trTypeModule, nextIdentifier, typeToken));
    }
    else
    {
        ApplyNamespaceAlias(identifiers, nextIdentifier, pdbImports);
        ApplyTypeAlias(identifiers, nextIdentifier, pdbImports);

        int tmpNextIdentifier = nextIdentifier;
        if (SUCCEEDED(FindTypeInModule(trTypeModule, identifiers, tmpNextIdentifier, typeToken)))
        {
            nextIdentifier = tmpNextIdentifier;
            assert(typeToken != mdTypeDefNil);
        }
        else if (nextIdentifier == 0)
        {
            auto importNamespace = pdbImports.find(PDB::ImportsKind::ImportNamespace);
            if (importNamespace == pdbImports.end())
            {
                return E_FAIL;
            }

            for (const auto &importName : importNamespace->second)
            {
                std::vector<std::string> testIdentifiers = identifiers;
                testIdentifiers.at(0) = importName.targetNamespace + "." + testIdentifiers.at(0);

                nextIdentifier = 0;
                if (SUCCEEDED(FindTypeInModule(trTypeModule, testIdentifiers, nextIdentifier, typeToken)))
                {
                    assert(typeToken != mdTypeDefNil);
                    break;
                }
            }

            if (typeToken == mdTypeDefNil)
            {
                return E_FAIL;
            }
        }
    }

    if (typeToken == mdTypeDefNil)
    {
        return E_FAIL;
    }

    if (ppType != nullptr)
    {
        const std::vector<std::string> params = GatherGenericFQDisplayParameters(identifiers, nextIdentifier);
        std::vector<ToRelease<ICorDebugType>> trTypes;
        IfFailRet(ResolveTypeParameters(params, pThread, pdbImports, trTypes));

        ToRelease<ICorDebugType> trType;
        IfFailRet(CreateParameterizedType(trTypeModule, typeToken, trTypes, &trType));

        *ppType = trType.Detach();
    }

    return S_OK;
}

HRESULT FindTypeModule(std::vector<std::string> &identifiers, ICorDebugThread *pThread,
                       const PDB::ImportsAndAliases &pdbImports, ICorDebugModule **ppModule)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugModule> trTypeModule;
    mdTypeDef typeToken = mdTypeDefNil;
    int nextIdentifier = 0;
    IfFailRet(FindTypeTokenInAllModules(pThread, identifiers, pdbImports, trTypeModule, nextIdentifier, typeToken));

    if (ppModule != nullptr)
    {
        *ppModule = trTypeModule.Detach();
    }

    return S_OK;
}

SigElementType GetSigElementTypeByDisplayTypeName(ICorDebugThread *pThread, const std::string &displayTypeName,
                                                  const PDB::ImportsAndAliases &pdbImports)
{
    static const std::unordered_map<std::string, SigElementType> stypes{
        {"void",    {ELEMENT_TYPE_VOID,    ""}},
        {"bool",    {ELEMENT_TYPE_BOOLEAN, ""}},
        {"byte",    {ELEMENT_TYPE_U1,      ""}},
        {"sbyte",   {ELEMENT_TYPE_I1,      ""}},
        {"char",    {ELEMENT_TYPE_CHAR,    ""}},
        {"double",  {ELEMENT_TYPE_R8,      ""}},
        {"float",   {ELEMENT_TYPE_R4,      ""}},
        {"int",     {ELEMENT_TYPE_I4,      ""}},
        {"uint",    {ELEMENT_TYPE_U4,      ""}},
        {"long",    {ELEMENT_TYPE_I8,      ""}},
        {"ulong",   {ELEMENT_TYPE_U8,      ""}},
        {"object",  {ELEMENT_TYPE_OBJECT,  ""}},
        {"short",   {ELEMENT_TYPE_I2,      ""}},
        {"ushort",  {ELEMENT_TYPE_U2,      ""}},
        {"string",  {ELEMENT_TYPE_STRING,  ""}},
        {"nint",    {ELEMENT_TYPE_I,       ""}},
        {"nuint",   {ELEMENT_TYPE_U,       ""}}
    };

    auto found = stypes.find(displayTypeName);
    if (found != stypes.end())
    {
        return found->second;
    }

    const std::string parseDisplayTypeName = displayTypeName == "decimal" ? "System.Decimal" : displayTypeName;
    std::vector<std::string> identifiers = SplitFQDisplayTypeName(parseDisplayTypeName);

    SigElementType sigElemType;
    int nextIdentifier = 0;
    ToRelease<ICorDebugType> trType;
    if (SUCCEEDED(FindType(identifiers, nextIdentifier, pThread, nullptr, pdbImports, &trType)) &&
        SUCCEEDED(trType->GetType(&sigElemType.elemType)) &&
        SUCCEEDED(GetFQMDTypeNameByICorType(trType, sigElemType.metadataTypeName)))
    {
        return sigElemType;
    }

    sigElemType.elemType = ELEMENT_TYPE_CLASS;
    sigElemType.metadataTypeName = displayTypeName;
    return sigElemType;
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
            genericTypeParameters.emplace_back(argElType);
            trCurrentTypeParam.Free();
        }
    }

    return S_OK;
}

HRESULT GetGenericArgs(ICorDebugFrame *pFrame, std::list<std::string> &args)
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
            args.emplace_back(name);
            trCurrentTypeParam.Free();
        }
    }

    return S_OK;
}

HRESULT GetBuiltInTypeName(CorElementType elemType, std::string &typeName)
{
    static std::unordered_map<CorElementType, std::string> builtInTypesAndKeywords{
        {ELEMENT_TYPE_VOID,    "void"},
        {ELEMENT_TYPE_BOOLEAN, "bool"},
        {ELEMENT_TYPE_CHAR,    "char"},
        {ELEMENT_TYPE_I1,      "sbyte"},
        {ELEMENT_TYPE_U1,      "byte"},
        {ELEMENT_TYPE_I2,      "short"},
        {ELEMENT_TYPE_U2,      "ushort"},
        {ELEMENT_TYPE_I4,      "int"},
        {ELEMENT_TYPE_U4,      "uint"},
        {ELEMENT_TYPE_I8,      "long"},
        {ELEMENT_TYPE_U8,      "ulong"},
        {ELEMENT_TYPE_R4,      "float"},
        {ELEMENT_TYPE_R8,      "double"},
        {ELEMENT_TYPE_U,       "nuint"},
        {ELEMENT_TYPE_I,       "nint"},
        {ELEMENT_TYPE_STRING,  "string"},
        {ELEMENT_TYPE_OBJECT,  "object"}
    };

    auto findName = builtInTypesAndKeywords.find(elemType);
    if (findName == builtInTypesAndKeywords.end())
    {
        return E_FAIL;
    }

    typeName = findName->second;
    return S_OK;
}

std::string AddrToString(CORDB_ADDRESS corAddr)
{
    static constexpr int32_t addrSize = 16; // CORDB_ADDRESS is ULONG64 for all arches.
    std::string strAddr(addrSize + 2, '0');
    strAddr.at(1) = 'x';

    auto [ptr, ec] = std::to_chars(strAddr.data() + 2, strAddr.data() + strAddr.size(), corAddr, addrSize);

    if (ptr < strAddr.data() + strAddr.size())
    {
        const std::size_t writtenLen = ptr - (strAddr.data() + 2);
        std::copy_backward(strAddr.data() + 2, ptr, strAddr.data() + strAddr.size());
        std::fill_n(strAddr.data() + 2, addrSize - writtenLen, '0');
    }

    return strAddr;
}

} // namespace dncdbg::MetadataHelpers
