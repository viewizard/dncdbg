// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/systemtypes.h"
#include "utils/hresult.h"
#include "utils/utf.h"
#include <cassert>
#include <unordered_map>

namespace dncdbg
{

HRESULT SystemTypes::GetClass(SystemType systemType, ICorDebugClass **ppClass)
{
    if (ppClass == nullptr)
    {
        return E_INVALIDARG;
    }

    const auto typeIndex = static_cast<size_t>(systemType);
    auto &systemTypes = GetSystemTypes();
    assert(systemTypes.size() > typeIndex);

    if (systemTypes.at(typeIndex) == nullptr)
    {
        return CORDBG_E_CLASS_NOT_LOADED;
    }

    *ppClass = systemTypes.at(typeIndex);
    (*ppClass)->AddRef();

    return S_OK;
}

HRESULT SystemTypes::GetClass(CorElementType elemType, ICorDebugClass **ppClass)
{
    static const std::unordered_map<CorElementType, SystemType> elementToSystemTypesMap{
        {ELEMENT_TYPE_VOID,     SystemType::Void},
        {ELEMENT_TYPE_BOOLEAN,  SystemType::Boolean},
        {ELEMENT_TYPE_CHAR,     SystemType::Char},
        {ELEMENT_TYPE_I1,       SystemType::SByte},
        {ELEMENT_TYPE_U1,       SystemType::Byte},
        {ELEMENT_TYPE_I2,       SystemType::Int16},
        {ELEMENT_TYPE_U2,       SystemType::UInt16},
        {ELEMENT_TYPE_I4,       SystemType::Int32},
        {ELEMENT_TYPE_U4,       SystemType::UInt32},
        {ELEMENT_TYPE_I8,       SystemType::Int64},
        {ELEMENT_TYPE_U8,       SystemType::UInt64},
        {ELEMENT_TYPE_R4,       SystemType::Single},
        {ELEMENT_TYPE_R8,       SystemType::Double},
        {ELEMENT_TYPE_I,        SystemType::IntPtr},
        {ELEMENT_TYPE_U,        SystemType::UIntPtr},
        {ELEMENT_TYPE_ARRAY,    SystemType::Array},
        {ELEMENT_TYPE_SZARRAY,  SystemType::Array}
    };

    const auto findType = elementToSystemTypesMap.find(elemType);
    if (findType == elementToSystemTypesMap.cend())
    {
        return E_INVALIDARG;
    }

    return GetClass(findType->second, ppClass);
}

HRESULT SystemTypes::ManagedCallbackLoadModule(ICorDebugModule *pModule)
{
    static const std::unordered_map<SystemType, const WCHAR *> systemTypesNameMap{
        {SystemType::Void,    W("System.Void")},
        {SystemType::Boolean, W("System.Boolean")},
        {SystemType::Char,    W("System.Char")},
        {SystemType::SByte,   W("System.SByte")},
        {SystemType::Byte,    W("System.Byte")},
        {SystemType::Int16,   W("System.Int16")},
        {SystemType::UInt16,  W("System.UInt16")},
        {SystemType::Int32,   W("System.Int32")},
        {SystemType::UInt32,  W("System.UInt32")},
        {SystemType::Int64,   W("System.Int64")},
        {SystemType::UInt64,  W("System.UInt64")},
        {SystemType::Single,  W("System.Single")},
        {SystemType::Double,  W("System.Double")},
        {SystemType::IntPtr,  W("System.IntPtr")},
        {SystemType::UIntPtr, W("System.UIntPtr")},
        {SystemType::Decimal, W("System.Decimal")},
        {SystemType::Array,   W("System.Array")},
        {SystemType::Enum,    W("System.Enum")}
    };

    assert(systemTypesNameMap.size() == static_cast<size_t>(SystemType::size));

    HRESULT Status = S_OK;
    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    auto &systemTypes = GetSystemTypes();

    for (size_t i = 0; i < static_cast<size_t>(SystemType::size); i++)
    {
        const auto systemType = static_cast<SystemType>(i);

        assert(systemTypesNameMap.find(systemType) != systemTypesNameMap.cend());
        assert(systemTypes.size() == i);

        systemTypes.emplace_back(nullptr);

        mdTypeDef typeDef = mdTypeDefNil;
        if (FAILED(trMDImport->FindTypeDefByName(systemTypesNameMap.at(systemType), mdTypeDefNil, &typeDef)))
        {
            continue;
        }

        pModule->GetClassFromToken(typeDef, &systemTypes.at(i));
    }

    return S_OK;
}

} // namespace dncdbg
