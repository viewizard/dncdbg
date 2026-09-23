// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/walkers/walkers.h"
#include "metadata/attributes.h"
#include "metadata/helpers.h"
#include "metadata/sigparse.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dncdbg::Walkers
{

namespace
{

struct ModuleExtensionMethods
{
    ToRelease<ICorDebugModule> trModule;
    std::vector<mdMethodDef> methodDefs;

    ModuleExtensionMethods(ICorDebugModule *pModule, std::vector<mdMethodDef> &&methodDefs_)
        : trModule(pModule),
          methodDefs(std::move(methodDefs_))
    {
    }
};

std::mutex &GetExtensionMethodsMutex()
{
    static std::mutex extensionMethodsMutex;
    return extensionMethodsMutex;
}

std::unordered_map<CORDB_ADDRESS, ModuleExtensionMethods> &GetExtensionMethodsCache()
{
    static std::unordered_map<CORDB_ADDRESS, ModuleExtensionMethods> extensionMethodsCache;
    return extensionMethodsCache;
}

HRESULT FillModuleExtensionMethodsCache(ICorDebugModule *pModule)
{
    // https://learn.microsoft.com/en-us/dotnet/api/system.runtime.compilerservices.extensionattribute
    // Indicates that a method is an extension method, or that a class or assembly contains extension methods.
    static const WSTRING extensionAttribute(W("System.Runtime.CompilerServices.ExtensionAttribute"));
    HRESULT Status = S_OK;

    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    ToRelease<IMetaDataAssemblyImport> trAssemblyImport;
    mdAssembly assemblyToken = mdAssemblyNil;
    if (SUCCEEDED(trUnknown->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&trAssemblyImport))) &&
        SUCCEEDED(trAssemblyImport->GetAssemblyFromScope(&assemblyToken)) &&
        !HasAttribute(trMDImport, assemblyToken, extensionAttribute))
    {
        return S_OK;
    }

    std::vector<mdMethodDef> moduleMethodDefs;
    HCORENUM hTypeEnum = nullptr;
    mdTypeDef typeDef = mdTypeDefNil;
    ULONG fetchedTypes = 0;
    while (SUCCEEDED(trMDImport->EnumTypeDefs(&hTypeEnum, &typeDef, 1, &fetchedTypes)) && fetchedTypes != 0)
    {
        if (!HasAttribute(trMDImport, typeDef, extensionAttribute))
        {
            continue;
        }

        HCORENUM hMethodEnum = nullptr;
        mdMethodDef methodDef = mdMethodDefNil;
        ULONG fetchedMethods = 0;
        while (SUCCEEDED(trMDImport->EnumMethods(&hMethodEnum, typeDef, &methodDef, 1, &fetchedMethods)) && fetchedMethods != 0)
        {
            DWORD methodAttr = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, nullptr,
                                                  &methodAttr, nullptr, nullptr, nullptr, nullptr)) ||
                (methodAttr & (mdMemberAccessMask | mdStatic)) != (mdPublic | mdStatic) || // NOLINT(bugprone-signed-bitwise)
                !HasAttribute(trMDImport, methodDef, extensionAttribute))
            {
                continue;
            }

            moduleMethodDefs.emplace_back(methodDef);
        }
        trMDImport->CloseEnum(hMethodEnum);
    }
    trMDImport->CloseEnum(hTypeEnum);

    if (!moduleMethodDefs.empty())
    {
        moduleMethodDefs.shrink_to_fit();

        const std::scoped_lock<std::mutex> lock(GetExtensionMethodsMutex());

        pModule->AddRef();
        GetExtensionMethodsCache().emplace(modAddress, ModuleExtensionMethods(pModule, std::move(moduleMethodDefs)));
    }

    return S_OK;
}

} // unnamed namespace

HRESULT WalkExtensionMethods(ICorDebugType *pInputType, CorElementType elemType, const WalkMethodsCallback &cb)
{
    HRESULT Status = S_OK;

    std::unordered_set<std::string> allIfaceTypeNames;
    const auto fillIfaceTypeNames = [&]() -> HRESULT
    {
        // Walk the type and all its base types, collecting the type name and all
        // implemented interfaces (including those inherited from base types). This is
        // required for extension method resolution on types whose interfaces are
        // declared on a base class (e.g. CastICollectionIterator<int>, whose
        // IEnumerable<TResult> is implemented by the base Iterator<TResult>).
        ToRelease<ICorDebugType> trCurrentType(pInputType);
        trCurrentType->AddRef();
        while (trCurrentType != nullptr)
        {
            ToRelease<ICorDebugClass> trClass;
            IfFailRet(trCurrentType->GetClass(&trClass));
            ToRelease<ICorDebugModule> trModule;
            IfFailRet(trClass->GetModule(&trModule));
            mdTypeDef typeDef = mdTypeDefNil;
            IfFailRet(trClass->GetToken(&typeDef));
            ToRelease<IUnknown> trUnknown;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
            std::string typeName;
            IfFailRet(MetadataHelpers::GetFQMDTypeNameByToken(typeDef, trMDImport, typeName));

            allIfaceTypeNames.emplace(typeName);

            HCORENUM hEnum = nullptr;
            mdInterfaceImpl ifaceImpl = mdInterfaceImplNil;
            ULONG pcImpls = 0;
            while (SUCCEEDED(trMDImport->EnumInterfaceImpls(&hEnum, typeDef, &ifaceImpl, 1, &pcImpls)) &&
                   pcImpls != 0)
            {
                mdToken tkIface = mdTokenNil;
                if (FAILED(trMDImport->GetInterfaceImplProps(ifaceImpl, nullptr, &tkIface)))
                {
                    continue;
                }

                std::string ifaceTypeName;
                if (FAILED(MetadataHelpers::GetFQMDTypeNameByToken(tkIface, trMDImport, ifaceTypeName)))
                {
                    continue;
                }

                allIfaceTypeNames.emplace(ifaceTypeName);
            }
            trMDImport->CloseEnum(hEnum);

            ToRelease<ICorDebugType> trBaseType;
            if (FAILED(trCurrentType->GetBase(&trBaseType)) || trBaseType == nullptr)
            {
                break;
            }
            trCurrentType = trBaseType.Detach();
        }
        return S_OK;
    };

    if (elemType == ELEMENT_TYPE_CLASS || elemType == ELEMENT_TYPE_VALUETYPE)
    {
        IfFailRet(fillIfaceTypeNames());
    }
    else if (elemType == ELEMENT_TYPE_SZARRAY)
    {
        // Note: arrays use a metadata type name check, not elemType.
        std::string typeName;
        IfFailRet(MetadataHelpers::GetFQMDTypeNameByICorType(pInputType, typeName));
        allIfaceTypeNames.emplace(typeName);

        // Base Class Library collection interfaces for arrays.
        for (const char *ifaceName : {
                    "System.Collections.Generic.IList`1",
                    "System.Collections.Generic.ICollection`1",
                    "System.Collections.Generic.IEnumerable`1",
                    "System.Collections.Generic.IReadOnlyList`1",
                    "System.Collections.Generic.IReadOnlyCollection`1",
                    "System.Collections.IList",
                    "System.Collections.ICollection",
                    "System.Collections.IEnumerable"
                })
        {
            allIfaceTypeNames.emplace(ifaceName);
        }
    }
    else if (elemType == ELEMENT_TYPE_ARRAY)
    {
        // Note: arrays use a metadata type name check, not elemType.
        std::string typeName;
        IfFailRet(MetadataHelpers::GetFQMDTypeNameByICorType(pInputType, typeName));
        allIfaceTypeNames.emplace(typeName);

        // Base Class Library collection interfaces for arrays.
        for (const char *ifaceName : {
                    "System.Collections.IList",
                    "System.Collections.ICollection",
                    "System.Collections.IEnumerable"
                })
        {
            allIfaceTypeNames.emplace(ifaceName);
        }
    }
    else if (elemType == ELEMENT_TYPE_STRING)
    {
        // Note: strings don't need a metadata type name, since they use elemType for the check.

        // Base Class Library collection interfaces for strings.
        for (const char *ifaceName : {
                    "System.Collections.Generic.IEnumerable`1",
                    "System.Collections.IEnumerable"
                })
        {
            allIfaceTypeNames.emplace(ifaceName);
        }
    }

    const std::scoped_lock<std::mutex> lock(GetExtensionMethodsMutex());

    for (const auto &[modAddress, extensionMethods] : GetExtensionMethodsCache())
    {
        ICorDebugModule *pModule = extensionMethods.trModule.GetPtr();

        ToRelease<IUnknown> trUnknown;
        IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        for (const auto &methodDef : extensionMethods.methodDefs)
        {
            ULONG nameLen = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &nameLen,
                                                  nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            std::vector<WCHAR> szFunctionName(nameLen, '\0');
            PCCOR_SIGNATURE pSig = nullptr;
            ULONG cbSig = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, szFunctionName.data(), nameLen, nullptr,
                                                  nullptr, &pSig, &cbSig, nullptr, nullptr)))
            {
                continue;
            }

            SigElementType returnElementType;
            std::vector<SigElementType> argElementTypes;
            uint32_t methodGenParamCount = 0;
            if (FAILED(ParseMethodSig(trMDImport, methodDef, pSig, pSig + cbSig, returnElementType,
                                      argElementTypes, false, &methodGenParamCount)))
            {
                continue;
            }

            if (elemType == ELEMENT_TYPE_CLASS || elemType == ELEMENT_TYPE_VALUETYPE ||
                elemType == ELEMENT_TYPE_SZARRAY || elemType == ELEMENT_TYPE_ARRAY)
            {
                if (allIfaceTypeNames.find(argElementTypes.at(0).metadataTypeName) == allIfaceTypeNames.cend())
                {
                    continue; // Type name didn't match, try the next method
                }
            }
            else if (elemType == ELEMENT_TYPE_STRING)
            {
                if (elemType != argElementTypes.at(0).elemType &&
                    allIfaceTypeNames.find(argElementTypes.at(0).metadataTypeName) == allIfaceTypeNames.cend())
                {
                    continue; // Type name didn't match, try the next method
                }
            }
            else if (elemType != argElementTypes.at(0).elemType)
            {
                continue;
            }

            const auto getFunction = [&](ICorDebugFunction **ppResultFunction) -> HRESULT
            {
                return pModule->GetFunctionFromToken(methodDef, ppResultFunction);
            };

            // Pass `false` as isStatic - extension methods require `this` as their first parameter.
            // Note: extension methods explicitly provide `this` as the first argument in argElementTypes.
            IfFailRet(cb(false, to_utf8(szFunctionName.data()), returnElementType, argElementTypes, methodGenParamCount, getFunction));
            if (Status == S_CAN_EXIT)
            {
                return S_OK;
            }
        }
    }

    return S_OK;
}

HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule)
{
    HRESULT Status = S_OK;
    IfFailRet(FillModuleExtensionMethodsCache(pModule));

    return S_OK;
}

HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    {
        const std::scoped_lock<std::mutex> lock(GetExtensionMethodsMutex());

        GetExtensionMethodsCache().erase(modAddress);
    }

    return S_OK;
}

// Cleans up the Walkers internal state. See Cleanup() in manageddebugger.cpp.
void Cleanup()
{
    const std::scoped_lock<std::mutex> lock(GetExtensionMethodsMutex());
    GetExtensionMethodsCache().clear();
}

} // namespace dncdbg::Walkers
