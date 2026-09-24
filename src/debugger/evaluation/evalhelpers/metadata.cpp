// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/evalhelpers/metadata.h"
#include "debugger/evaluation/walkers/walkers.h"
#include "debugger/evalhelpers.h"
#include "debugger/frames.h"
#include "debuginfo/debuginfo.h"
#include "metadata/helpers.h"
#include "metadata/modules.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <cassert>
#include <limits>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace dncdbg::EvalMetadataHelpers
{

namespace
{

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

std::vector<std::string> GatherGenericFQDisplayParameters(const std::vector<std::string> &identifiers, int indexEnd)
{
    std::vector<std::string> result;
    for (int i = 0; i < indexEnd; i++)
    {
        std::string metadataTypeName;
        const std::vector<std::string> genericFQDisplayTypeNames = MetadataHelpers::ConvertDisplayToMetadataName(identifiers.at(i), metadataTypeName);
        result.insert(result.end(), genericFQDisplayTypeNames.cbegin(), genericFQDisplayTypeNames.cend());
    }
    return result;
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

    const auto aliasNamespace = pdbImports.find(PDB::ImportsKind::AliasNamespace);
    if (aliasNamespace == pdbImports.cend())
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

    const auto aliasType = pdbImports.find(PDB::ImportsKind::AliasType);
    if (aliasType == pdbImports.cend())
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

        const std::vector<std::string> typeIdentifiers = MetadataHelpers::SplitFQDisplayTypeName(entry.displayName);

        identifiers.erase(identifiers.begin());
        identifiers.insert(identifiers.begin(), typeIdentifiers.cbegin(), typeIdentifiers.cend());
        break;
    }
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

    const auto importNamespace = pdbImports.find(PDB::ImportsKind::ImportNamespace);
    if (importNamespace == pdbImports.cend())
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
    // Used to detect circular type dependencies instead of relying on an iteration limit.
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
        if (resolvedTypes.find(entry.typeName) != resolvedTypes.cend())
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
                if (resolvedTypes.find(np) != resolvedTypes.cend())
                {
                    continue;
                }
                if (inProgress.find(np) != inProgress.cend())
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
            const auto findType = resolvedTypes.find(np);
            if (findType == resolvedTypes.cend())
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
        const auto it = resolvedTypes.find(param);
        if (it != resolvedTypes.cend())
        {
            trTypes.push_back(std::move(it->second));
        }
    }

    return S_OK;
}

} // unnamed namespace

// Note, could return S_CAN_EXIT for fast exit.
HRESULT ForEachFields(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const WalkFieldsCallback &cb)
{
    HRESULT Status = S_OK;
    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    while (SUCCEEDED(pMDImport->EnumFields(&hEnum, currentTypeDef, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        if (FAILED(Status = cb(fieldDef)) ||
            Status == S_CAN_EXIT)
        {
            break;
        }
    }
    pMDImport->CloseEnum(hEnum);
    return Status;
}

// Note, could return S_CAN_EXIT for fast exit.
HRESULT ForEachProperties(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const WalkPropertiesCallback &cb)
{
    HRESULT Status = S_OK;
    mdProperty propertyDef = mdPropertyNil;
    ULONG numProperties = 0;
    HCORENUM propEnum = nullptr;
    while (SUCCEEDED(pMDImport->EnumProperties(&propEnum, currentTypeDef, &propertyDef, 1, &numProperties)) &&
           numProperties != 0)
    {
        if (FAILED(Status = cb(propertyDef)) ||
            Status == S_CAN_EXIT)
        {
            break;
        }
    }
    pMDImport->CloseEnum(propEnum);
    return Status;
}

HRESULT FindThisProxyFieldValue(IMetaDataImport *pMDImport, ICorDebugClass *pClass, mdTypeDef typeDef,
                                ICorDebugValue *pInputValue, ICorDebugValue **ppResultValue)
{
    HRESULT Status = S_OK;
    BOOL isNull = FALSE;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(DereferenceAndUnboxValue(pInputValue, &trValue, &isNull));
    if (isNull == TRUE)
    {
        return E_INVALIDARG;
    }

    Status = ForEachFields(pMDImport, typeDef,
        [&](mdFieldDef fieldDef) -> HRESULT
        {
            ULONG nameLen = 0;
            IfFailRet(pMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen,
                                               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr));

            WSTRING mdName(nameLen, '\0');
            if (SUCCEEDED(pMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr,
                                                   nullptr, nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                // Remove null terminator that was included in the length
                if (!mdName.empty() && mdName.back() == '\0')
                {
                    mdName.pop_back();
                }

                const auto getValue = [&](ICorDebugValue **ppResultValue) -> HRESULT
                {
                    ToRelease<ICorDebugObjectValue> trObjValue;
                    IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
                    IfFailRet(trObjValue->GetFieldValue(pClass, fieldDef, ppResultValue));
                    return S_OK;
                };

                const MetadataHelpers::GeneratedNameKind generatedNameKind = MetadataHelpers::GetLocalOrFieldNameKind(mdName);
                if (generatedNameKind == MetadataHelpers::GeneratedNameKind::ThisProxyField)
                {
                    IfFailRet(getValue(ppResultValue));
                    return S_CAN_EXIT; // Fast exit from the loop.
                }
                else if (generatedNameKind == MetadataHelpers::GeneratedNameKind::DisplayClassLocalOrField)
                {
                    ToRelease<ICorDebugValue> trDisplayClassValue;
                    IfFailRet(getValue(&trDisplayClassValue));
                    ToRelease<ICorDebugClass> trDisplayClass;

                    ToRelease<ICorDebugValue2> trValue2;
                    IfFailRet(trDisplayClassValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
                    ToRelease<ICorDebugType> trType;
                    IfFailRet(trValue2->GetExactType(&trType));
                    IfFailRet(trType->GetClass(&trDisplayClass));
                    mdTypeDef displayClassTypeDef = mdTypeDefNil;
                    IfFailRet(trDisplayClass->GetToken(&displayClassTypeDef));

                    IfFailRet(FindThisProxyFieldValue(pMDImport, trDisplayClass, displayClassTypeDef, trDisplayClassValue, ppResultValue));
                    if (ppResultValue != nullptr)
                    {
                        return S_CAN_EXIT; // Fast exit from the loop.
                    }
                }
            }
            return S_OK; // Return success to continue walking.
        });

    // Note, ForEachFields() could return S_CAN_EXIT for fast exit.
    return SUCCEEDED(Status) ? S_OK : Status;
}

HRESULT GetFirstUserCodeEnclosingClass(IMetaDataImport *pMDImport, mdTypeDef typeDef, mdTypeDef &userTypeDef)
{
    HRESULT Status = S_OK;

    while (true)
    {
        ULONG nameLen = 0;
        IfFailRet(pMDImport->GetTypeDefProps(typeDef, nullptr, 0, &nameLen, nullptr, nullptr));

        WSTRING mdName(nameLen, '\0');
        IfFailRet(pMDImport->GetTypeDefProps(typeDef, mdName.data(), nameLen, nullptr, nullptr, nullptr));
        // Remove null terminator that was included in the length
        if (!mdName.empty() && mdName.back() == '\0')
        {
            mdName.pop_back();
        }

        if (!MetadataHelpers::IsSynthesizedLocalName(mdName))
        {
            userTypeDef = typeDef;
            break;
        }

        IfFailRet(pMDImport->GetNestedClassProps(typeDef, &typeDef));
    };

    return S_OK;
}

HRESULT GetFQDisplayTypeName(ICorDebugThread *pThread, FrameLevel frameLevel, std::string &displayTypeName, bool &haveThis)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(trFrame->GetFunction(&trFunction));

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunction->GetModule(&trModule));

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdMethodDef methodDef = mdMethodDefNil;
    IfFailRet(trFunction->GetToken(&methodDef));

    ULONG szMethodLen = 0;
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &szMethodLen,
                                         nullptr, nullptr, nullptr, nullptr, nullptr));

    DWORD methodAttr = 0;
    WSTRING szMethod(szMethodLen, '\0');
    IfFailRet(trMDImport->GetMethodProps(methodDef, nullptr, szMethod.data(), szMethodLen, nullptr,
                                         &methodAttr, nullptr, nullptr, nullptr, nullptr));
    // Remove null terminator that was included in the length
    if (!szMethod.empty() && szMethod.back() == '\0')
    {
        szMethod.pop_back();
    }

    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trFunction->GetClass(&trClass));
    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&typeDef));
    // We are inside a method of this class; if typeDef is not a TypeDef token, something has definitely gone wrong.
    if (TypeFromToken(typeDef) != mdtTypeDef)
    {
        return E_FAIL;
    }

    std::list<std::string> args;
    MetadataHelpers::GetGenericArgs(trFrame, args);

    haveThis = ((methodAttr & mdStatic) == 0);
    // In case of a static method, this is definitely not an async/lambda case.
    if (!haveThis)
    {
        return MetadataHelpers::GetFQDisplayNameForToken(typeDef, trMDImport, displayTypeName, &args);
    }

    MetadataHelpers::GeneratedCodeKind generatedCodeKind = MetadataHelpers::GeneratedCodeKind::Normal;
    IfFailRet(MetadataHelpers::GetGeneratedCodeKind(trMDImport, szMethod, typeDef, generatedCodeKind));
    if (generatedCodeKind == MetadataHelpers::GeneratedCodeKind::Normal)
    {
        return MetadataHelpers::GetFQDisplayNameForToken(typeDef, trMDImport, displayTypeName, &args);
    }

    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));
    ToRelease<ICorDebugValue> trCurrentThis;
    IfFailRet(trILFrame->GetArgument(0, &trCurrentThis));

    // Check whether we have a real "this" value (it should be stored in ThisProxyField).
    ToRelease<ICorDebugValue> trUserThis;
    IfFailRet(FindThisProxyFieldValue(trMDImport, trClass, typeDef, trCurrentThis, &trUserThis));
    haveThis = (trUserThis != nullptr);

    // Find the first user code enclosing class, since the compiler adds async/lambda as a nested class.
    mdTypeDef userTypeDef = mdTypeDefNil;
    IfFailRet(GetFirstUserCodeEnclosingClass(trMDImport, typeDef, userTypeDef));

    return MetadataHelpers::GetFQDisplayNameForToken(userTypeDef, trMDImport, displayTypeName, &args);
}

HRESULT GetFQDisplayRealCodeMethodName(ICorDebugFrame *pFrame, std::string &displayName)
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
    if (FAILED(DebugInfo::GetStateMachineKickoffMethod(trModule, methodToken, methodDef)) &&
        FAILED(MetadataHelpers::GetStateMachineKickoffMethod(trModule, methodToken, methodDef)))
    {
        methodDef = methodToken;
        asyncMethod = false;
    }

    std::ostringstream ss;
    std::string displayTypeName;
    std::string displayMethodName;
    IfFailRet(MetadataHelpers::GetDisplayTypeAndMethodName(pFrame, methodDef, displayTypeName, displayMethodName));

    if (!displayTypeName.empty())
    {
        ss << displayTypeName << ".";
    }
    ss << displayMethodName << "(";

    const auto addMethodParameters = [&]() -> HRESULT
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
        // Ignore failed return code here; we need everything we can parse from the sig.
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
                Walkers::WalkGeneratedClassFields(trMDImport, trCurrentThis, 0, usedNames, methodDef, trModule,
                    [&](const std::string &name, const Walkers::GetValueCallback &getValue) -> HRESULT
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
            const auto asyncParam = asyncMethodParams.find(paramName);

            std::string displayTypeName;
            ToRelease<ICorDebugValue> trValue;
            if ((asyncMethod && asyncParam != asyncMethodParams.cend() &&
                 SUCCEEDED(MetadataHelpers::GetFQDisplayTypeName(asyncParam->second, displayTypeName))) ||
                (!asyncMethod &&
                 SUCCEEDED(Status = trILFrame->GetArgument((methodAttr & mdStatic) == 0 ? i + 1 : i, &trValue)) &&
                 SUCCEEDED(MetadataHelpers::GetFQDisplayTypeName(trValue, displayTypeName))))
            {
                ss << displayTypeName << " ";
            }
            else if (argElementTypes.size() > i && !argElementTypes.at(i).metadataTypeName.empty() &&
                     // TODO: replace with proper type and method generic parameters
                     argElementTypes.at(i).genericElemType != ELEMENT_TYPE_VAR &&
                     argElementTypes.at(i).genericElemType != ELEMENT_TYPE_MVAR)
            {
                ss << MetadataHelpers::ConvertMetadataToDisplayName(argElementTypes.at(i).metadataTypeName, nullptr) << " ";
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

    const auto found = stypes.find(displayTypeName);
    if (found != stypes.cend())
    {
        return found->second;
    }

    const std::string parseDisplayTypeName = displayTypeName == "decimal" ? "System.Decimal" : displayTypeName;
    std::vector<std::string> identifiers = MetadataHelpers::SplitFQDisplayTypeName(parseDisplayTypeName);

    SigElementType sigElemType;
    int nextIdentifier = 0;
    ToRelease<ICorDebugType> trType;
    if (SUCCEEDED(FindType(identifiers, nextIdentifier, pThread, nullptr, pdbImports, &trType)) &&
        SUCCEEDED(trType->GetType(&sigElemType.elemType)) &&
        SUCCEEDED(MetadataHelpers::GetFQMDTypeNameByICorType(trType, sigElemType.metadataTypeName)))
    {
        return sigElemType;
    }

    sigElemType.elemType = ELEMENT_TYPE_CLASS;
    sigElemType.metadataTypeName = displayTypeName;
    return sigElemType;
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
            const auto importNamespace = pdbImports.find(PDB::ImportsKind::ImportNamespace);
            if (importNamespace == pdbImports.cend())
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

HRESULT GetFQDisplayRealCodeTypeName(ICorDebugFrame *pFrame, std::string &displayTypeName)
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
    if (FAILED(DebugInfo::GetStateMachineKickoffMethod(trModule, methodToken, methodDef)) &&
        FAILED(MetadataHelpers::GetStateMachineKickoffMethod(trModule, methodToken, methodDef)))
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
    MetadataHelpers::GetGenericArgs(pFrame, args);
    IfFailRet(MetadataHelpers::GetFQDisplayNameForTypeDef(typeDef, trMDImport, displayTypeName, &args));

    return S_OK;
}

HRESULT GetFQDisplayRealCodeMethodName(ICorDebugModule *pModule, mdMethodDef methodToken, std::string &displayName)
{
    HRESULT Status = S_OK;

    mdMethodDef methodDef = mdMethodDefNil;
    if (FAILED(DebugInfo::GetStateMachineKickoffMethod(pModule, methodToken, methodDef)) &&
        FAILED(MetadataHelpers::GetStateMachineKickoffMethod(pModule, methodToken, methodDef)))
    {
        methodDef = methodToken;
    }

    std::ostringstream ss;
    std::string displayTypeName;
    std::string displayMethodName;
    IfFailRet(MetadataHelpers::GetDisplayTypeAndMethodName(pModule, methodDef, displayTypeName, displayMethodName));
    if (!displayTypeName.empty())
    {
        ss << displayTypeName << ".";
    }
    ss << displayMethodName << "(";

    const auto addMethodParameters = [&]() -> HRESULT
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
        // Ignore failed return code here; we need everything we can parse from the sig.
        ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType, argElementTypes, true);

        const std::vector<std::string> typeParameterNames = MetadataHelpers::GetGenericParamNames(trMDImport2, typeDef);
        const std::vector<std::string> methodParameterNames = MetadataHelpers::GetGenericParamNames(trMDImport2, methodDef);

        // Without an ICorDebugFrame we cannot resolve the concrete generic argument
        // types, so fill `metadataTypeName` with the generic parameter declaration
        // names (e.g. "T", "TKey") instead of the actual type names.
        for (auto &methodArg : argElementTypes)
        {
            if (methodArg.genericElemType == ELEMENT_TYPE_VAR &&
                methodArg.varNum < typeParameterNames.size())
            {
                methodArg.metadataTypeName = typeParameterNames.at(methodArg.varNum);
            }
            else if (methodArg.genericElemType == ELEMENT_TYPE_MVAR &&
                     methodArg.varNum < methodParameterNames.size())
            {
                methodArg.metadataTypeName = methodParameterNames.at(methodArg.varNum);
            }
        }

        const auto cArguments = static_cast<ULONG>(argElementTypes.size());
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
                ss << MetadataHelpers::ConvertMetadataToDisplayName(argElementTypes.at(i).metadataTypeName, nullptr) << " ";
            }
            // else
            //    in case of failure, ignore the parameter type and print only the parameter name

            ss << to_utf8(wParamName.data());
        }
        return S_OK;
    };
    addMethodParameters();

    ss << ")";
    displayName = ss.str();
    return S_OK;
}

} // namespace dncdbg::EvalMetadataHelpers
