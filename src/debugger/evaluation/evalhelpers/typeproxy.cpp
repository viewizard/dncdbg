// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/evalhelpers/typeproxy.h"
#include "debugger/evaluation/evalexec.h" // NOLINT(misc-include-cleaner)
#include "metadata/attributes.h"
#include "metadata/helpers.h"
#include "metadata/modules.h"
#include "utils/filesystem.h"
#include <algorithm>
#include <charconv>

namespace dncdbg
{

namespace
{

// Helper function to remove leading and trailing whitespace from a std::string_view.
std::string_view TrimString(std::string_view str)
{
    const auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void ParseTypeName(std::string_view proxyTypeName, std::vector<std::string> &typeNameParts, std::string &assemblyName)
{
    // 1. Separate the full type info from the assembly metadata using the first comma.
    const auto firstComma = proxyTypeName.find(',');
    const std::string_view typePart = TrimString(proxyTypeName.substr(0, firstComma));
    const std::string_view remainder = (firstComma != std::string_view::npos) ? proxyTypeName.substr(firstComma + 1) : "";

    // 2. Extract the assembly name if it exists.
    if (!remainder.empty())
    {
        const auto nextComma = remainder.find(',');
        const std::string_view assemblyPart = TrimString(remainder.substr(0, nextComma));

        // Per C# spec, the assembly name cannot start with properties like "Version=", "Culture=", etc.
        if (!assemblyPart.empty() &&
            assemblyPart.rfind("Version=", 0) != 0 &&
            assemblyPart.rfind("Culture=", 0) != 0 &&
            assemblyPart.rfind("PublicKeyToken=", 0) != 0)
        {
            assemblyName = std::string(assemblyPart);
        }
    }

    // 3. Split the type part by the '+' delimiter (nested classes).
    size_t start = 0;
    while (start < typePart.size())
    {
        const auto plusPos = typePart.find('+', start);
        const std::string_view part = typePart.substr(start, plusPos - start);

        typeNameParts.emplace_back(TrimString(part));

        if (plusPos == std::string_view::npos)
        {
            break;
        }
        start = plusPos + 1;
    }
}

// Parses the generic arity (number after '`') and returns it as uint32_t.
// Returns 0 if there is no '`' character or if the parsing fails.
uint32_t ParseGenericArity(std::string_view typeName)
{
    // 1. Find the backtick character '`'.
    const auto backtickPos = typeName.find('`');
    if (backtickPos == std::string_view::npos)
    {
        return 0; // Not a generic type
    }

    // 2. Extract the substring representing the number.
    const std::string_view numberPart = typeName.substr(backtickPos + 1);
    if (numberPart.empty())
    {
        return 0; // Empty after backtick, e.g., "MyClass`"
    }

    // 3. Fast and safe string-to-number conversion using C++17 std::from_chars.
    uint32_t count = 0;
    const auto result = std::from_chars(numberPart.data(), numberPart.data() + numberPart.size(), count);

    // If conversion succeeded, return the count; otherwise, return 0.
    if (result.ec == std::errc{})
    {
        return count;
    }

    return 0;
}

void GetParameterMetadataTypeNames(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef,
                                   std::unordered_set<std::string> &parameterMetadataTypeNames)
{
    // Add the class name itself.
    std::string metadataTypeName;
    MetadataHelpers::GetFQMDTypeNameByToken(currentTypeDef, pMDImport, metadataTypeName);
    parameterMetadataTypeNames.emplace(std::move(metadataTypeName));

    // Add all interface names.
    HCORENUM hEnum = nullptr;
    mdInterfaceImpl ifaceImpl = mdInterfaceImplNil;
    ULONG cImpls = 0;
    while (SUCCEEDED(pMDImport->EnumInterfaceImpls(&hEnum, currentTypeDef, &ifaceImpl, 1, &cImpls)) && cImpls != 0)
    {
        mdTypeDef tkClass = mdTypeDefNil;
        mdToken tkIface = mdTokenNil;
        if (FAILED(pMDImport->GetInterfaceImplProps(ifaceImpl, &tkClass, &tkIface)))
        {
            continue;
        }

        std::string metadataTypeName;
        MetadataHelpers::GetFQMDTypeNameByToken(tkIface, pMDImport, metadataTypeName);
        parameterMetadataTypeNames.emplace(std::move(metadataTypeName));
    }
    pMDImport->CloseEnum(hEnum);
}

HRESULT GetConstructorFunction(ICorDebugModule *pModule, IMetaDataImport *pMDImport, mdTypeDef typeDef,
                               const std::unordered_set<std::string> &parameterMetadataTypeNames,
                               mdMethodDef &constrMethodDef, ICorDebugFunction **ppConstrFunction)
{
    constrMethodDef = mdMethodDefNil;

    HRESULT Status = S_OK;
    ULONG numMethods = 0;
    HCORENUM mEnum = nullptr;
    mdMethodDef enumMethodDef = mdMethodDefNil;
    while (S_OK == pMDImport->EnumMethodsWithName(&mEnum, typeDef, W(".ctor"), &enumMethodDef, 1, &numMethods) && numMethods == 1)
    {
        DWORD methodAttr = 0;
        PCCOR_SIGNATURE pSig = nullptr;
        ULONG cbSig = 0;
        if (FAILED(pMDImport->GetMethodProps(enumMethodDef, nullptr, nullptr, 0, nullptr,
                                             &methodAttr, &pSig, &cbSig, nullptr, nullptr)))
        {
            continue;
        }

        if ((methodAttr & mdMemberAccessMask) != mdPublic ||
            (methodAttr & mdStatic) != 0U ||
            (methodAttr & mdRTSpecialName) == 0U)
        {
            continue;
        }

        SigElementType returnElementType;
        std::vector<SigElementType> argElementTypes;
        if (FAILED(ParseMethodSig(pMDImport, enumMethodDef, pSig, pSig + cbSig, returnElementType, argElementTypes)))
        {
            continue;
        }

        if (argElementTypes.size() != 1 ||
            parameterMetadataTypeNames.find(argElementTypes.front().metadataTypeName) == parameterMetadataTypeNames.cend())
        {
            continue;
        }

        constrMethodDef = enumMethodDef;
        break;
    }
    pMDImport->CloseEnum(mEnum);

    if (constrMethodDef == mdMethodDefNil)
    {
        return E_FAIL;
    }

    IfFailRet(pModule->GetFunctionFromToken(constrMethodDef, ppConstrFunction));
    return S_OK;
}

HRESULT GetConstructorTypeParams(ICorDebugThread *pThread, ICorDebugType *pType, uint32_t enclosingTypesParamCount,
                                 std::vector<ToRelease<ICorDebugType>> &trTypeParams)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugTypeEnum> trTypeEnum;
    std::vector<ToRelease<ICorDebugType>> trCurrentTypes;
    if (SUCCEEDED(pType->EnumerateTypeParameters(&trTypeEnum)))
    {
        ICorDebugType *pCurType = nullptr;
        ULONG fetched = 0;
        while (SUCCEEDED(Status = trTypeEnum->Next(1, &pCurType, &fetched)) && fetched == 1)
        {
            trCurrentTypes.emplace_back(pCurType);
        }
    }

    // Add System.Object type parameters for enclosing classes.
    ToRelease<ICorDebugType> trObjectType;
    for (uint32_t j = 0; j < enclosingTypesParamCount; j++)
    {
        if (trObjectType == nullptr)
        {
            ToRelease<ICorDebugValue> trNullObjectValue;
            ToRelease<ICorDebugEval> trEval;
            IfFailRet(pThread->CreateEval(&trEval));
            IfFailRet(trEval->CreateValue(ELEMENT_TYPE_CLASS, nullptr, &trNullObjectValue));
            ToRelease<ICorDebugValue2> trNullObjectValue2;
            IfFailRet(trNullObjectValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trNullObjectValue2)));
            IfFailRet(trNullObjectValue2->GetExactType(&trObjectType));
        }
        trObjectType->AddRef();
        trTypeParams.emplace_back(trObjectType.GetPtr());
    }

    // Add the proxy class type parameters.
    for (auto &trType : trCurrentTypes)
    {
        trTypeParams.emplace_back(trType.Detach());
    }

    return S_OK;
}

HRESULT DetectDebuggerTypeProxyAttribute(ICorDebugType *pType, std::string &proxyTypeName, mdTypeDef &proxyAttrTypeDef, ToRelease<ICorDebugModule> &trProxyAttrModule)
{
    pType->AddRef();
    ToRelease<ICorDebugType> trTestType(pType);
    while (true)
    {
        trProxyAttrModule.Free();
        ToRelease<ICorDebugClass> trClass;
        mdTypeDef attrTypeDef = mdTypeDefNil;
        ToRelease<IUnknown> trUnknown;
        ToRelease<IMetaDataImport> trMDImport;
        if (FAILED(trTestType->GetClass(&trClass)) ||
            FAILED(trClass->GetModule(&trProxyAttrModule)) ||
            FAILED(trClass->GetToken(&attrTypeDef)) ||
            FAILED(trProxyAttrModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown)) ||
            FAILED(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport))))
        {
            break;
        }

        if (HasDebuggerAttribute(trMDImport, attrTypeDef, DebuggerAttribute::TypeProxy, proxyTypeName))
        {
            proxyAttrTypeDef = attrTypeDef;
            break;
        }

        ToRelease<IMetaDataAssemblyImport> trAssemblyImport;
        mdAssembly assemblyToken = mdAssemblyNil;
        if (SUCCEEDED(trUnknown->QueryInterface(IID_IMetaDataAssemblyImport, reinterpret_cast<void **>(&trAssemblyImport))) &&
            SUCCEEDED(trAssemblyImport->GetAssemblyFromScope(&assemblyToken)))
        {
            std::string detectTypeName;
            if (SUCCEEDED(MetadataHelpers::GetFQMDTypeNameByToken(attrTypeDef, trMDImport, detectTypeName)) &&
                HasAssemblyDebuggerAttribute(trMDImport, assemblyToken, DebuggerAttribute::TypeProxy, detectTypeName, proxyTypeName))
            {
                proxyAttrTypeDef = attrTypeDef;
                break;
            }
        }

        ToRelease<ICorDebugType> trBaseType;
        if (FAILED(trTestType->GetBase(&trBaseType)) || trBaseType == nullptr)
        {
            break;
        }
        trTestType.Free();
        trTestType = trBaseType.Detach();
    }

    return proxyAttrTypeDef != mdTypeDefNil && !proxyTypeName.empty() && trProxyAttrModule != nullptr ? S_OK : E_FAIL;
}

} // unnamed namespace

HRESULT TypeProxy::GetDebuggerTypeProxyValue(ICorDebugThread *pThread, ICorDebugModule *pModule, ICorDebugModule *pAttrModule,
                                             ICorDebugValue *pFrontValue, ICorDebugType *pType, mdTypeDef currentTypeDef,
                                             mdTypeDef proxyAttrTypeDef, const std::string &proxyTypeName, ICorDebugValue **ppTypeProxyValue)
{
    HRESULT Status = S_OK;

    std::vector<std::string> proxyTypeNameParts;
    std::string assemblyName;
    ParseTypeName(proxyTypeName, proxyTypeNameParts, assemblyName);

    pAttrModule->AddRef();
    ToRelease<ICorDebugModule> trProxyTypeModule(pAttrModule);
    if (!assemblyName.empty())
    {
        IfFailRet(Modules::ForEachModule(pThread,
            [&](ICorDebugModule *pTestModule) -> HRESULT
            {
                uint32_t nameLen = 0;
                if (FAILED(pTestModule->GetName(0, &nameLen, nullptr)))
                {
                    return S_OK;
                }

                std::vector<WCHAR> wModName(nameLen, '\0');
                if (FAILED(pTestModule->GetName(nameLen, nullptr, wModName.data())))
                {
                    return S_OK;
                }

                if (RemoveExtension(GetBasename(to_utf8(wModName.data()))) == assemblyName)
                {
                    trProxyTypeModule.Free();
                    pTestModule->AddRef();
                    trProxyTypeModule = pTestModule;
                    return S_CAN_EXIT;
                }

                return S_OK;
            }));
    }

    ToRelease<IUnknown> trUnknown;
    IfFailRet(trProxyTypeModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdTypeDef typeDef = mdTypeDefNil;
    if (proxyTypeNameParts.size() == 1 && assemblyName.empty() &&
        std::count(proxyTypeNameParts.front().cbegin(), proxyTypeNameParts.front().cend(), '.') == 0)
    {
        const WSTRING wProxyTypeName = to_utf16(proxyTypeNameParts.front());
        IfFailRet(trMDImport->FindTypeDefByName(wProxyTypeName.c_str(), proxyAttrTypeDef, &typeDef));
    }

    if (typeDef == mdTypeDefNil)
    {
        mdToken enclosingClass = mdTypeDefNil;
        for (const auto &namePart : proxyTypeNameParts)
        {
            const WSTRING wNamePart = to_utf16(namePart);
            IfFailRet(trMDImport->FindTypeDefByName(wNamePart.c_str(), enclosingClass, &typeDef));
            enclosingClass = typeDef;
        }
    }

    // Get the proper fully-qualified proxy type name.
    std::string fullProxyTypeName;
    IfFailRet(MetadataHelpers::GetFQMDTypeNameByToken(typeDef, trMDImport, fullProxyTypeName));
    proxyTypeNameParts.clear();
    std::string ignoredAssemblyName; // Assembly name is not needed for the resolved type.
    ParseTypeName(fullProxyTypeName, proxyTypeNameParts, ignoredAssemblyName);

    std::unordered_set<std::string> parameterMetadataTypeNames;
    GetParameterMetadataTypeNames(trMDImport, proxyAttrTypeDef, parameterMetadataTypeNames);

    mdMethodDef constrMethodDef = mdMethodDefNil;
    ToRelease<ICorDebugFunction> trConstrFunction;
    IfFailRet(GetConstructorFunction(trProxyTypeModule, trMDImport, typeDef, parameterMetadataTypeNames, constrMethodDef, &trConstrFunction));

    std::vector<ToRelease<ICorDebugType>> trTypeParams;
    uint32_t enclosingTypesParamCount = 0; // type parameters for enclosing classes
    if (proxyTypeNameParts.size() > 1)
    {
        for (std::size_t i = 0; i < proxyTypeNameParts.size() - 1; i++)
        {
            for (uint32_t j = 0; j < ParseGenericArity(proxyTypeNameParts.at(i)); j++)
            {
                enclosingTypesParamCount++;
            }
        }
    }
    IfFailRet(GetConstructorTypeParams(pThread, pType, enclosingTypesParamCount, trTypeParams));
    IfFailRet(m_sharedEvalExec->CallConstructor(pThread, trConstrFunction, trTypeParams, &pFrontValue, 1, ppTypeProxyValue));

    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));
    CORDB_ADDRESS proxyTypeModAddress = 0;
    IfFailRet(trProxyTypeModule->GetBaseAddress(&proxyTypeModAddress));

    const std::scoped_lock<std::mutex> lock(m_debuggerTypeProxyMutex);

    m_debuggerTypeProxyCache[modAddress].emplace(currentTypeDef, DebuggerTypeProxyCache{proxyTypeModAddress, constrMethodDef, enclosingTypesParamCount});

    if (m_debuggerTypeProxyModuleCache.find(proxyTypeModAddress) == m_debuggerTypeProxyModuleCache.cend())
    {
        m_debuggerTypeProxyModuleCache.emplace(proxyTypeModAddress, trProxyTypeModule.Detach());
    }

    return S_OK;
}

HRESULT TypeProxy::GetCachedDebuggerTypeProxyValue(ICorDebugThread *pThread, ICorDebugModule *pModule, ICorDebugValue *pFrontValue, ICorDebugType *pType,
                                                   mdTypeDef currentTypeDef, bool &typeChecked, ICorDebugValue **ppTypeProxyValue)
{
    typeChecked = false;

    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    std::unique_lock<std::mutex> lock(m_debuggerTypeProxyMutex);

    const auto findCheckedModule = m_debuggerTypeProxyCheckedTypes.find(modAddress);
    if (findCheckedModule == m_debuggerTypeProxyCheckedTypes.cend())
    {
        m_debuggerTypeProxyCheckedTypes.emplace(modAddress, std::unordered_set<mdTypeDef>{});
        m_debuggerTypeProxyCheckedTypes.at(modAddress).emplace(currentTypeDef);
        return E_FAIL;
    }

    const auto findCheckedType = findCheckedModule->second.find(currentTypeDef);
    if (findCheckedType == findCheckedModule->second.cend())
    {
        findCheckedModule->second.emplace(currentTypeDef);
        return E_FAIL;
    }

    typeChecked = true;

    const auto findCacheByModule = m_debuggerTypeProxyCache.find(modAddress);
    if (findCacheByModule == m_debuggerTypeProxyCache.cend())
    {
        return E_FAIL;
    }

    const auto findCache = findCacheByModule->second.find(currentTypeDef);
    if (findCache == findCacheByModule->second.cend())
    {
        return E_FAIL;
    }

    const DebuggerTypeProxyCache &proxyCache = findCache->second;
    ICorDebugModule *pProxyTypeModule = m_debuggerTypeProxyModuleCache.at(proxyCache.modAddress);

    ToRelease<ICorDebugFunction> trConstrFunction;
    IfFailRet(pProxyTypeModule->GetFunctionFromToken(proxyCache.methodDef, &trConstrFunction));

    std::vector<ToRelease<ICorDebugType>> trTypeParams;
    IfFailRet(GetConstructorTypeParams(pThread, pType, proxyCache.enclosingTypesParamCount, trTypeParams));

    lock.unlock();

    IfFailRet(m_sharedEvalExec->CallConstructor(pThread, trConstrFunction, trTypeParams, &pFrontValue, 1, ppTypeProxyValue));

    return S_OK;
}

HRESULT TypeProxy::GetDebuggerTypeProxyValue(ICorDebugThread *pThread, ICorDebugModule *pModule, ICorDebugValue *pFrontValue,
                                             ICorDebugType *pType, mdTypeDef currentTypeDef, ICorDebugValue **ppTypeProxyValue)
{
    bool typeChecked = false;
    if (SUCCEEDED(GetCachedDebuggerTypeProxyValue(pThread, pModule, pFrontValue, pType,
                                                  currentTypeDef, typeChecked, ppTypeProxyValue)))
    {
        return S_OK;
    }

    std::string proxyTypeName;
    mdTypeDef proxyAttrTypeDef = mdTypeDefNil;
    ToRelease<ICorDebugModule> trProxyAttrModule;
    if (!typeChecked &&
        SUCCEEDED(DetectDebuggerTypeProxyAttribute(pType, proxyTypeName, proxyAttrTypeDef, trProxyAttrModule)))
    {
        CORDB_ADDRESS modAddress = 0;
        if (SUCCEEDED(GetDebuggerTypeProxyValue(pThread, pModule, trProxyAttrModule, pFrontValue, pType, currentTypeDef,
                                                proxyAttrTypeDef, proxyTypeName, ppTypeProxyValue)))
        {
            return S_OK;
        }
        else if (SUCCEEDED(pModule->GetBaseAddress(&modAddress)))
        {
            const std::scoped_lock<std::mutex> lock(m_debuggerTypeProxyMutex);
            // Could be an issue with thread state, reset checked status, try next time.
            m_debuggerTypeProxyCheckedTypes.at(modAddress).erase(currentTypeDef);
        }
    }

    return E_FAIL;
}

HRESULT TypeProxy::ManagedCallbackUnloadModule(ICorDebugModule *pModule)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    {
        const std::scoped_lock<std::mutex> lock(m_debuggerTypeProxyMutex);

        m_debuggerTypeProxyCheckedTypes.erase(modAddress);
        m_debuggerTypeProxyCache.erase(modAddress);
        m_debuggerTypeProxyModuleCache.erase(modAddress);
    }

    return S_OK;
}

} // namespace dncdbg
