// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/typeprinter.h"
#include "debuginfo/debuginfo.h"
#include "metadata/modules.h"
#include "metadata/sigparse.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <map>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace dncdbg::TypePrinter
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

std::string ConsumeGenericArgs(const std::string &name, std::list<std::string> &args)
{
    if (args.empty())
    {
        return name;
    }

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

    if (numArgs == 0 || numArgs > args.size())
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
        sep = ", ";
        ss << args.front();
        args.pop_front();
    }
    ss << ">";
    return ss.str();
}

HRESULT NameForTypeRef(mdTypeRef tkTypeRef, IMetaDataImport *pMDImport, std::string &mdName)
{
    // Note: instead of GetTypeDefProps(), GetTypeRefProps() returns fully-qualified name.
    // CoreCLR uses dynamically allocated or size-fixed buffers up to 16kb for GetTypeRefProps().
    HRESULT Status = S_OK;
    ULONG refNameSize = 0;
    IfFailRet(pMDImport->GetTypeRefProps(tkTypeRef, nullptr, nullptr, 0, &refNameSize));

    WSTRING refName(refNameSize, 0);
    IfFailRet(pMDImport->GetTypeRefProps(tkTypeRef, nullptr, refName.data(), refNameSize, nullptr));

    mdName = to_utf8(refName.c_str());

    return S_OK;
}

// Resolve a single ICorDebugType to its element type string and array suffix.
// For ELEMENT_TYPE_VALUETYPE/ELEMENT_TYPE_CLASS with generic type parameters,
// the type parameters are collected into `outTypeParams` without being resolved
// to strings. The caller is responsible for resolving them separately.
HRESULT ResolveSingleType(ICorDebugType *pType, std::string &elementType, std::string &arrayType,
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
        elementType = "<unknown>";
        for (const auto &suffix : typeSuffixes)
        {
            arrayType += suffix;
        }
        return false; // Exit loop
    };

    // Iteratively process nested types until we reach a base type
    while (trCurrentType != nullptr)
    {
        CorElementType corElemType = ELEMENT_TYPE_MAX;
        IfFailRet(trCurrentType->GetType(&corElemType));

        switch (corElemType)
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
            ss << "(Unhandled CorElementType: 0x" << std::hex << corElemType << ")";
            elementType = ss.str();
            return S_OK;
        }

        case ELEMENT_TYPE_VALUETYPE:
        case ELEMENT_TYPE_CLASS:
        {
            std::ostringstream ss;
            // Defaults in case we fail...
            elementType = (corElemType == ELEMENT_TYPE_VALUETYPE) ? "struct" : "class";

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

                // Build placeholder args list for NameForToken/ConsumeGenericArgs.
                // Each placeholder "\x01{N}" will be replaced with the resolved type
                // string by the caller after all type parameters are resolved.
                std::list<std::string> placeholderArgs;
                for (std::size_t i = 0; i < outTypeParams.size(); ++i)
                {
                    placeholderArgs.emplace_back(std::string("\x01{") + std::to_string(i) + "}");
                }

                std::string name;
                if (SUCCEEDED(NameForToken(TokenFromRid(typeDef, mdtTypeDef), trMDImport, name, false, &placeholderArgs)))
                {
                    static const std::string_view nullablePattern = "System.Nullable<";
                    if (name.rfind(nullablePattern, 0) == 0)
                    {
                        ss << name.substr(nullablePattern.size(), name.rfind('>') - nullablePattern.size()) << "?";
                    }
                    else
                    {
                        ss << name;
                    }
                }
            }
            elementType = ss.str();
            finalizeSuffixes();
            return S_OK;
        }

        case ELEMENT_TYPE_VOID:
            elementType = "void";
            break;
        case ELEMENT_TYPE_BOOLEAN:
            elementType = "bool";
            break;
        case ELEMENT_TYPE_CHAR:
            elementType = "char";
            break;
        case ELEMENT_TYPE_I1:
            elementType = "sbyte";
            break;
        case ELEMENT_TYPE_U1:
            elementType = "byte";
            break;
        case ELEMENT_TYPE_I2:
            elementType = "short";
            break;
        case ELEMENT_TYPE_U2:
            elementType = "ushort";
            break;
        case ELEMENT_TYPE_I4:
            elementType = "int";
            break;
        case ELEMENT_TYPE_U4:
            elementType = "uint";
            break;
        case ELEMENT_TYPE_I8:
            elementType = "long";
            break;
        case ELEMENT_TYPE_U8:
            elementType = "ulong";
            break;
        case ELEMENT_TYPE_R4:
            elementType = "float";
            break;
        case ELEMENT_TYPE_R8:
            elementType = "double";
            break;
        case ELEMENT_TYPE_OBJECT:
            elementType = "object";
            break;
        case ELEMENT_TYPE_STRING:
            elementType = "string";
            break;
        case ELEMENT_TYPE_I:
            elementType = "IntPtr";
            break;
        case ELEMENT_TYPE_U:
            elementType = "UIntPtr";
            break;
        case ELEMENT_TYPE_SZARRAY:
            typeSuffixes.emplace_back("[]");
            if (processNestedType())
            {
                continue;
            }
            return S_OK;
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
            return S_OK;
        }
        case ELEMENT_TYPE_BYREF:
            typeSuffixes.emplace_back(""); // BYREF (in, out, ref) doesn't add visible suffix currently
            if (processNestedType())
            {
                continue;
            }
            return S_OK;
        case ELEMENT_TYPE_PTR:
            typeSuffixes.emplace_back("*");
            if (processNestedType())
            {
                continue;
            }
            return S_OK;
        case ELEMENT_TYPE_FNPTR:
            elementType = "*(...)";
            break;
        case ELEMENT_TYPE_TYPEDBYREF:
            elementType = "typedbyref";
            break;
        }

        // For simple types, build arrayType from accumulated suffixes and return
        finalizeSuffixes();
        return S_OK;
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

// Collect generic type argument strings from an ICorDebugType's type parameters.
// Uses ResolveTypeToString internally to avoid recursion.
HRESULT AddGenericArgs(ICorDebugType *pType, std::list<std::string> &args)
{
    ToRelease<ICorDebugTypeEnum> trTypeEnum;

    if (SUCCEEDED(pType->EnumerateTypeParameters(&trTypeEnum)))
    {
        ULONG fetched = 0;
        ToRelease<ICorDebugType> trCurrentTypeParam;

        while (SUCCEEDED(trTypeEnum->Next(1, &trCurrentTypeParam, &fetched)) && fetched == 1)
        {
            std::string name;
            ResolveTypeToString(trCurrentTypeParam, name);
            args.emplace_back(name);
            trCurrentTypeParam.Free();
        }
    }

    return S_OK;
}

HRESULT AddGenericArgs(ICorDebugFrame *pFrame, std::list<std::string> &args)
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

std::string RenameToSystem(const std::string &typeName)
{
    static const std::unordered_map<std::string, std::string> cs2system = {
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
        {"IntPtr",  "System.IntPtr"},
        {"UIntPtr", "System.UIntPtr"}
    };
    auto renamed = cs2system.find(typeName);
    return renamed != cs2system.end() ? renamed->second : typeName;
}

std::string RenameToCSharp(const std::string &typeName)
{
    static const std::unordered_map<std::string, std::string> system2cs = {
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
        {"System.IntPtr",  "IntPtr"},
        {"System.UIntPtr", "UIntPtr"}
    };
    auto renamed = system2cs.find(typeName);
    return renamed != system2cs.end() ? renamed->second : typeName;
}

std::vector<std::string> GatherParameters(const std::vector<std::string> &identifiers, int indexEnd)
{
    std::vector<std::string> result;
    for (int i = 0; i < indexEnd; i++)
    {
        std::string metadataTypeName;
        std::vector<std::string> params = TypePrinter::ConvertDisplayToMetadataName(identifiers.at(i), metadataTypeName);
        result.insert(result.end(), params.begin(), params.end());
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
        TypePrinter::ConvertDisplayToMetadataName(identifiers.at(i), metadataName);
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
        TypePrinter::ConvertDisplayToMetadataName(identifiers.at(j), metadataName);
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

    std::string eTypeName;
    IfFailRet(TypePrinter::NameForToken(tkExtends, trMDImport, eTypeName, true, nullptr));

    const bool isValueType = eTypeName == "System.ValueType" || eTypeName == "System.Enum";
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
                              std::vector<ToRelease<ICorDebugType>> &trTypes)
{
    HRESULT Status = S_OK;

    // Map to store resolved types by type name.
    std::map<std::string, ToRelease<ICorDebugType>> resolvedTypes;
    // Work queue: type names that need to be resolved.
    std::vector<std::string> workQueue(params.begin(), params.end());

    // Use a stack-based (LIFO) work queue: when a type has unresolved dependencies,
    // re-push it after its dependencies so they get resolved first.
    std::size_t maxIterations = (params.size() + 1) * (params.size() + 1);
    while (!workQueue.empty())
    {
        if (maxIterations-- == 0)
        {
            return E_FAIL; // Prevent infinite loop on circular type dependencies.
        }

        std::string currentType = std::move(workQueue.back());
        workQueue.pop_back();

        // Skip if already resolved.
        if (resolvedTypes.find(currentType) != resolvedTypes.end())
        {
            continue;
        }

        std::vector<int> ranks;
        std::vector<std::string> classIdentifiers = TypePrinter::ParseFullyQualifiedDisplayTypeName(currentType, ranks);

        int nextClassIdentifier = 0;
        ToRelease<ICorDebugModule> trTypeModule;
        mdTypeDef typeToken = mdTypeDefNil;

        IfFailRet(Modules::ForEachModule(pThread,
            [&](ICorDebugModule *pModule) -> HRESULT
            {
                nextClassIdentifier = 0;
                if (SUCCEEDED(FindTypeInModule(pModule, classIdentifiers, nextClassIdentifier, typeToken)))
                {
                    pModule->AddRef();
                    trTypeModule = pModule;
                    assert(typeToken != mdTypeDefNil);
                    return S_CAN_EXIT; // Fast exit from the loop.
                }

                return S_OK;
            }));

        if (typeToken == mdTypeDefNil)
        {
            return E_FAIL;
        }

        const std::vector<std::string> nestedParams = GatherParameters(classIdentifiers, nextClassIdentifier);

        // Check for unresolved nested parameters and add them to the queue.
        bool hasUnresolved = false;
        for (const auto &np : nestedParams)
        {
            if (resolvedTypes.find(np) == resolvedTypes.end())
            {
                workQueue.push_back(np);
                hasUnresolved = true;
            }
        }
        if (hasUnresolved)
        {
            workQueue.push_back(std::move(currentType));
            continue;
        }

        // Collect resolved nested types.
        std::vector<ToRelease<ICorDebugType>> trNestedTypes;
        for (const auto &np : nestedParams)
        {
            ICorDebugType *pType = resolvedTypes.at(np).GetPtr();
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

        resolvedTypes.emplace(currentType, std::move(trType));
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

} // unnamed namespace

HRESULT FullyQualifiedNameForTypeDef(mdTypeDef tkTypeDef, IMetaDataImport *pMDImport, std::string &mdName)
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
    mdName.clear();
    for (auto it = nameStack.rbegin(); it != nameStack.rend(); ++it)
    {
        if (!mdName.empty())
        {
            mdName += it->delimiter;
        }
        mdName += it->name;
    }

    return S_OK;
}

HRESULT NameForTypeDef(mdTypeDef tkTypeDef, IMetaDataImport *pMDImport,
                       std::string &mdName, std::list<std::string> *args)
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
    mdName.clear();
    for (auto it = nameStack.rbegin(); it != nameStack.rend(); ++it)
    {
        if (!mdName.empty())
        {
            mdName += ".";
        }

        std::string currentName = *it;
        if (args != nullptr)
        {
            currentName = ConsumeGenericArgs(currentName, *args);
        }
        mdName += currentName;
    }

    return S_OK;
}

HRESULT NameForTypeByToken(mdToken mb, IMetaDataImport *pMDImport, std::string &mdName, std::list<std::string> *args)
{
    HRESULT Status = S_OK;
    mdName.clear();

    if (TypeFromToken(mb) == mdtTypeDef)
    {
        IfFailRet(NameForTypeDef(mb, pMDImport, mdName, args));
    }
    else if (TypeFromToken(mb) == mdtTypeRef)
    {
        IfFailRet(NameForTypeRef(mb, pMDImport, mdName));
    }
    else if (TypeFromToken(mb) == mdtTypeSpec)
    {
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        IfFailRet(pMDImport->GetTypeSpecFromToken(mb, &pSig, &cbSig));
        SigElementType sigType;
        IfFailRet(ParseElementType(pMDImport, pSig, pSig + cbSig, 0, sigType, true));
        mdName = sigType.typeName;
    }
    else
    {
        // Unsupported token type
        return CORDBG_E_UNSUPPORTED;
    }

    return S_OK;
}

HRESULT FullyQualifiedNameForTypeByToken(mdToken mb, IMetaDataImport *pMDImport, std::string &mdName)
{
    HRESULT Status = S_OK;
    mdName.clear();

    if (TypeFromToken(mb) == mdtTypeDef)
    {
        IfFailRet(FullyQualifiedNameForTypeDef(mb, pMDImport, mdName));
    }
    else if (TypeFromToken(mb) == mdtTypeRef)
    {
        IfFailRet(NameForTypeRef(mb, pMDImport, mdName));
    }
    else if (TypeFromToken(mb) == mdtTypeSpec)
    {
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        IfFailRet(pMDImport->GetTypeSpecFromToken(mb, &pSig, &cbSig));
        SigElementType sigType;
        IfFailRet(ParseElementType(pMDImport, pSig, pSig + cbSig, 0, sigType, true));
        mdName = sigType.typeName;
    }
    else
    {
        // Unsupported token type
        return CORDBG_E_UNSUPPORTED;
    }

    return S_OK;
}

HRESULT NameForTypeByType(ICorDebugType *pType, std::string &mdName)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugClass> trClass;
    IfFailRet(pType->GetClass(&trClass));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trClass->GetModule(&trModule));
    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
    mdToken tk = mdTokenNil;
    IfFailRet(trClass->GetToken(&tk));
    std::list<std::string> args;
    AddGenericArgs(pType, args);
    return NameForTypeByToken(tk, trMDImport, mdName, &args);
}

HRESULT NameForTypeByValue(ICorDebugValue *pValue, std::string &mdName)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    return NameForTypeByType(trType, mdName);
}

HRESULT NameForToken(mdToken mb, IMetaDataImport *pMDImport, std::string &mdName, bool bClassName,
                     std::list<std::string> *args)
{
    HRESULT Status = S_OK;
    mdName.clear();

    if (TypeFromToken(mb) == mdtTypeDef)
    {
        IfFailRet(NameForTypeDef(mb, pMDImport, mdName, args));
    }
    else if (TypeFromToken(mb) == mdtFieldDef)
    {
        ULONG size = 0;
        IfFailRet(pMDImport->GetMemberProps(mb, nullptr, nullptr, 0, &size, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
        mdTypeDef mdClass = mdTypeDefNil;
        std::vector<WCHAR> name(size, '\0');
        IfFailRet(pMDImport->GetMemberProps(mb, &mdClass, name.data(), size, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));
        if (mdClass != mdTypeDefNil && bClassName)
        {
            IfFailRet(NameForTypeDef(mdClass, pMDImport, mdName, args));
            mdName += ".";
        }
        mdName += to_utf8(name.data());
    }
    else if (TypeFromToken(mb) == mdtMethodDef)
    {
        ULONG size = 0;
        IfFailRet(pMDImport->GetMethodProps(mb, nullptr, nullptr, 0, &size,
                                            nullptr, nullptr, nullptr, nullptr, nullptr));
        mdTypeDef mdClass = mdTypeDefNil;
        std::vector<WCHAR> name(size, '\0');
        IfFailRet(pMDImport->GetMethodProps(mb, &mdClass, name.data(), size, nullptr,
                                            nullptr, nullptr, nullptr, nullptr, nullptr));
        if (mdClass != mdTypeDefNil && bClassName)
        {
            IfFailRet(NameForTypeDef(mdClass, pMDImport, mdName, args));
            mdName += ".";
        }
        mdName += to_utf8(name.data());
    }
    else if (TypeFromToken(mb) == mdtMemberRef)
    {
        ULONG size = 0;
        IfFailRet(pMDImport->GetMemberRefProps(mb, nullptr, nullptr, 0, &size, nullptr, nullptr));
        mdTypeDef mdClass = mdTypeDefNil;
        std::vector<WCHAR> name(size, '\0');
        IfFailRet(pMDImport->GetMemberRefProps(mb, &mdClass, name.data(), size, nullptr, nullptr, nullptr));
        if (TypeFromToken(mdClass) == mdtTypeRef && bClassName)
        {
            IfFailRet(NameForTypeRef(mdClass, pMDImport, mdName));
            mdName += ".";
        }
        else if (TypeFromToken(mdClass) == mdtTypeDef && bClassName)
        {
            IfFailRet(NameForTypeDef(mdClass, pMDImport, mdName, args));
            mdName += ".";
        }
        // TODO TypeSpec
        mdName += to_utf8(name.data());
    }
    else if (TypeFromToken(mb) == mdtTypeRef)
    {
        IfFailRet(NameForTypeRef(mb, pMDImport, mdName));
    }
    else
    {
        // Unsupported token type
        return CORDBG_E_UNSUPPORTED;
    }

    mdName = RenameToCSharp(mdName);
    return S_OK;
}

HRESULT GetTypeOfValue(ICorDebugValue *pValue, std::string &output)
{
    ToRelease<ICorDebugType> trType;
    ToRelease<ICorDebugValue2> trValue2;
    if (SUCCEEDED(pValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>((&trValue2)))) &&
        SUCCEEDED(trValue2->GetExactType(&trType)))
    {
        return GetTypeOfValue(trType, output);
    }
    else
    {
        output = "<unknown>";
    }

    return S_OK;
}

HRESULT GetTypeOfValue(ICorDebugType *pType, std::string &elementType, std::string &arrayType)
{
    std::vector<ToRelease<ICorDebugType>> typeParams;
    HRESULT Status = S_OK;
    IfFailRet(ResolveSingleType(pType, elementType, arrayType, typeParams));

    if (!typeParams.empty())
    {
        // Resolve each generic type parameter using the iterative resolver
        // and replace placeholders in the combined output string.
        std::vector<std::string> resolvedParams(typeParams.size());
        for (std::size_t i = 0; i < typeParams.size(); ++i)
        {
            IfFailRet(ResolveTypeToString(typeParams.at(i), resolvedParams.at(i)));
        }
        ReplacePlaceholders(elementType, resolvedParams);
    }

    return S_OK;
}

HRESULT GetTypeOfValue(ICorDebugType *pType, std::string &output)
{
    HRESULT Status = S_OK;
    std::string elementType;
    std::string arrayType;
    IfFailRet(GetTypeOfValue(pType, elementType, arrayType));
    output = elementType + arrayType;
    return S_OK;
}

HRESULT GetTypeAndMethodName(ICorDebugFrame *pFrame, DebugInfo *pDebugInfo, std::string &typeName, std::string &methodName)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pFrame->GetFunction(&trFunction));

    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trFunction->GetClass(&trClass));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));
    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(trFunction->GetToken(&methodToken));

    mdMethodDef methodDef = mdMethodDefNil;
    if (FAILED(pDebugInfo->GetStateMachineKickoffMethod(trModule, methodToken, methodDef)))
    {
        methodDef = methodToken;
    }

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&typeDef));

    ToRelease<IMetaDataImport2> trMDImport2;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport2, reinterpret_cast<void **>(&trMDImport2)));

    ULONG nameLen = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &nameLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));

    mdTypeDef memTypeDef = mdTypeDefNil;
    std::vector<WCHAR> szFunctionName(nameLen, '\0');
    IfFailRet(trMDImport->GetMethodProps(methodDef, &memTypeDef, szFunctionName.data(), nameLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));

    std::string funcName = to_utf8(szFunctionName.data());

    ULONG methodGenericsCount = 0;
    HCORENUM hEnum = nullptr;
    mdGenericParam gp = mdGenericParamNil;
    ULONG fetched = 0;
    while (SUCCEEDED(trMDImport2->EnumGenericParams(&hEnum, methodDef, &gp, 1, &fetched)) && fetched == 1)
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
    AddGenericArgs(pFrame, args);

    if (memTypeDef != mdTypeDefNil)
    {
        if (FAILED(NameForTypeDef(memTypeDef, trMDImport, typeName, &args)))
        {
            typeName = "";
        }
    }

    methodName = ConsumeGenericArgs(funcName, args);

    return S_OK;
}

HRESULT GetTypeAndMethodName(ICorDebugModule *pModule, mdMethodDef methodToken, DebugInfo *pDebugInfo, std::string &typeName, std::string &methodName)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pModule->GetFunctionFromToken(methodToken, &trFunction));
    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trFunction->GetClass(&trClass));

    mdMethodDef methodDef = mdMethodDefNil;
    if (FAILED(pDebugInfo->GetStateMachineKickoffMethod(pModule, methodToken, methodDef)))
    {
        methodDef = methodToken;
    }

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&typeDef));

    ToRelease<IMetaDataImport2> trMDImport2;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport2, reinterpret_cast<void **>(&trMDImport2)));

    ULONG nameLen = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &nameLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));

    mdTypeDef memTypeDef = mdTypeDefNil;
    std::vector<WCHAR> szFunctionName(nameLen, '\0');
    IfFailRet(trMDImport->GetMethodProps(methodDef, &memTypeDef, szFunctionName.data(), nameLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));

    methodName = to_utf8(szFunctionName.data());

    if (memTypeDef != mdTypeDefNil)
    {
        if (FAILED(NameForTypeDef(memTypeDef, trMDImport, typeName, nullptr)))
        {
            typeName = "";
        }
    }

    return S_OK;
}

HRESULT GetFullyQualifiedMethodName(ICorDebugFrame *pFrame, DebugInfo *pDebugInfo, std::string &output)
{
    HRESULT Status = S_OK;

    std::string typeName;
    std::string methodName;

    std::ostringstream ss;
    IfFailRet(GetTypeAndMethodName(pFrame, pDebugInfo, typeName, methodName));
    if (!typeName.empty())
    {
        ss << typeName << ".";
    }
    ss << methodName << "(";

    auto addMethodParameters = [&]() -> HRESULT
    {
        ToRelease<ICorDebugFunction> trFunction;
        IfFailRet(pFrame->GetFunction(&trFunction));

        ToRelease<ICorDebugModule> trModule;
        IfFailRet(trFunction->GetModule(&trModule));

        ToRelease<IUnknown> trUnknown;
        IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        mdMethodDef methodToken = mdMethodDefNil;
        IfFailRet(trFunction->GetToken(&methodToken));

        mdMethodDef methodDef = mdMethodDefNil;
        bool asyncMethod = true;
        if (FAILED(pDebugInfo->GetStateMachineKickoffMethod(trModule, methodToken, methodDef)))
        {
            methodDef = methodToken;
            asyncMethod = false;
        }

        ToRelease<ICorDebugILFrame> trILFrame;
        IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

        DWORD methodAttr = 0;
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, nullptr,
                                             &methodAttr, &pSig, &cbSig, nullptr, nullptr));

        SigElementType returnElementType;
        std::vector<SigElementType> argElementTypes;
        const std::vector<SigElementType> typeGenerics; // TODO fill this vector
        const std::vector<SigElementType> methodGenerics; // TODO fill this vector
        // Ignore failed return code here, we need all we could parse from sig.
        ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType, argElementTypes, true);

        auto cArguments = static_cast<ULONG>(argElementTypes.size());
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

            std::string valueType;
            ToRelease<ICorDebugValue> trValue;
            if (argElementTypes.size() > i && !argElementTypes.at(i).typeName.empty()) // FIXME care about typeGenerics and methodGenerics
            {
                ss << argElementTypes.at(i).typeName << " ";
            }
            else if (!asyncMethod &&
                     SUCCEEDED(Status = trILFrame->GetArgument((methodAttr & mdStatic) == 0 ? i + 1 : i, &trValue)) &&
                     SUCCEEDED(GetTypeOfValue(trValue, valueType)))
            {
                ss << valueType << " ";
            }
            // else
            //    in case of failure, ignore parameter type, print only parameter name

            ss << to_utf8(wParamName.data());
        }
        return S_OK;
    };
    addMethodParameters();

    ss << ")";
    output = ss.str();
    return S_OK;
}

HRESULT GetFullyQualifiedMethodName(ICorDebugModule *pModule, mdMethodDef methodToken, DebugInfo *pDebugInfo, std::string &output)
{
    HRESULT Status = S_OK;

    std::string typeName;
    std::string methodName;

    std::ostringstream ss;
    IfFailRet(GetTypeAndMethodName(pModule, methodToken, pDebugInfo, typeName, methodName));
    if (!typeName.empty())
    {
        ss << typeName << ".";
    }
    ss << methodName << "(";

    auto addMethodParameters = [&]() -> HRESULT
    {
        ToRelease<IUnknown> trUnknown;
        IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        mdMethodDef methodDef = mdMethodDefNil;
        if (FAILED(pDebugInfo->GetStateMachineKickoffMethod(pModule, methodToken, methodDef)))
        {
            methodDef = methodToken;
        }

        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, nullptr,
                                             nullptr, &pSig, &cbSig, nullptr, nullptr));

        SigElementType returnElementType;
        std::vector<SigElementType> argElementTypes;
        // Ignore failed return code here, we need all we could parse from sig.
        ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType, argElementTypes, true);

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

            if (!argElementTypes.at(i).typeName.empty())
            {
                ss << argElementTypes.at(i).typeName << " ";
            }
            // else
            //    in case of failure, ignore parameter type, print only parameter name

            ss << to_utf8(wParamName.data());
        }
        return S_OK;
    };
    addMethodParameters();

    ss << ")";
    output = ss.str();
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

    int paramDepth = 0; // Depth inside generic angle brackets "<...>".
    int arrayDepth = 0; // Depth inside array brackets "[...]"; commas inside are rank separators, not arg separators.

    genericTypes.emplace_back();

    for (std::size_t i = start; i < displayName.size(); i++)
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
    metadataName = baseName + '`' + std::to_string(genericTypes.size());
    return genericTypes;
}

std::vector<std::string> ParseFullyQualifiedDisplayTypeName(const std::string &displayTypeName, std::vector<int> &ranks)
{
    // Splits a fully-qualified display type name into its dot-separated
    // identifier components (namespace/class path) and records array ranks.
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
            if (paramDepth == 0)
            {
                ranks.push_back(1);
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
                if (!ranks.empty())
                {
                    ranks.back()++;
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

HRESULT FindType(const std::vector<std::string> &identifiers, int &nextIdentifier, ICorDebugThread *pThread,
                 ICorDebugModule *pModule, ICorDebugType **ppType)
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

                return S_OK; // Return with success to continue walk.
            }));
    }
    else
    {
        FindTypeInModule(trTypeModule, identifiers, nextIdentifier, typeToken);
    }

    if (typeToken == mdTypeDefNil)
    {
        return E_FAIL;
    }

    if (ppType != nullptr)
    {
        const std::vector<std::string> params = GatherParameters(identifiers, nextIdentifier);
        std::vector<ToRelease<ICorDebugType>> trTypes;
        IfFailRet(ResolveTypeParameters(params, pThread, trTypes));

        ToRelease<ICorDebugType> trType;
        IfFailRet(CreateParameterizedType(trTypeModule, typeToken, trTypes, &trType));

        *ppType = trType.Detach();
    }

    return S_OK;
}

HRESULT FindTypeModule(const std::vector<std::string> &identifiers, ICorDebugThread *pThread, ICorDebugModule **ppModule)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugModule> trTypeModule;
    mdTypeDef typeToken = mdTypeDefNil;

    IfFailRet(Modules::ForEachModule(pThread,
        [&](ICorDebugModule *pModule) -> HRESULT
        {
            int nextIdentifier = 0;
            if (SUCCEEDED(FindTypeInModule(pModule, identifiers, nextIdentifier, typeToken)))
            {
                pModule->AddRef();
                trTypeModule = pModule;
                assert(typeToken != mdTypeDefNil);
                return S_CAN_EXIT; // Fast exit from the loop.
            }

            return S_OK; // Return with success to continue walk.
        }));

    if (trTypeModule == nullptr)
    {
        return E_FAIL;
    }

    if (ppModule != nullptr)
    {
        *ppModule = trTypeModule.Detach();
    }

    return S_OK;
}

} // namespace dncdbg::TypePrinter
