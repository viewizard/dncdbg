// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef METADATA_ENUMERATIONS_H
#define METADATA_ENUMERATIONS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "utils/hresult.h"
#include "utils/utf.h"
#include <array>

namespace dncdbg::MetadataEnumerations
{

// Number of metadata tokens fetched per enumeration call.
constexpr ULONG enumBatchSize = 64;
// Number of items fetched per enumeration call when only a few results are expected, for example, generic parameters.
constexpr ULONG enumSmallBatchSize = 8;

// Callbacks may return S_CAN_EXIT to stop the enumeration early.
// S_FALSE returned by the metadata API is a success code that can accompany a partial batch,
// so only failures and S_CAN_EXIT stop the enumeration.

template <typename Callback>
HRESULT ForEachField(IMetaDataImport *pMDImport, mdTypeDef typeDef, const Callback &cb)
{
    HRESULT Status = S_OK;
    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    std::array<mdFieldDef, enumBatchSize> fieldDefs{};
    while (true)
    {
        Status = pMDImport->EnumFields(&hEnum, typeDef, fieldDefs.data(), enumBatchSize, &numFields);
        if (FAILED(Status) || numFields == 0)
        {
            break;
        }

        for (ULONG i = 0; i < numFields; ++i)
        {
            Status = cb(fieldDefs.at(i));
            if (FAILED(Status) || Status == S_CAN_EXIT)
            {
                pMDImport->CloseEnum(hEnum);
                return Status;
            }
        }
    }
    pMDImport->CloseEnum(hEnum);
    return FAILED(Status) ? Status : S_OK;
}

template <typename Callback>
HRESULT ForEachProperty(IMetaDataImport *pMDImport, mdTypeDef typeDef, const Callback &cb)
{
    HRESULT Status = S_OK;
    ULONG numProperties = 0;
    HCORENUM hEnum = nullptr;
    std::array<mdProperty, enumBatchSize> propertyDefs{};
    while (true)
    {
        Status = pMDImport->EnumProperties(&hEnum, typeDef, propertyDefs.data(), enumBatchSize, &numProperties);
        if (FAILED(Status) || numProperties == 0)
        {
            break;
        }

        for (ULONG i = 0; i < numProperties; ++i)
        {
            Status = cb(propertyDefs.at(i));
            if (FAILED(Status) || Status == S_CAN_EXIT)
            {
                pMDImport->CloseEnum(hEnum);
                return Status;
            }
        }
    }
    pMDImport->CloseEnum(hEnum);
    return FAILED(Status) ? Status : S_OK;
}

template <typename Callback>
HRESULT ForEachTypeDef(IMetaDataImport *pMDImport, const Callback &cb)
{
    HRESULT Status = S_OK;
    ULONG numTypeDefs = 0;
    HCORENUM hEnum = nullptr;
    std::array<mdTypeDef, enumBatchSize> typeDefs{};
    while (true)
    {
        Status = pMDImport->EnumTypeDefs(&hEnum, typeDefs.data(), enumBatchSize, &numTypeDefs);
        if (FAILED(Status) || numTypeDefs == 0)
        {
            break;
        }

        for (ULONG i = 0; i < numTypeDefs; ++i)
        {
            Status = cb(typeDefs.at(i));
            if (FAILED(Status) || Status == S_CAN_EXIT)
            {
                pMDImport->CloseEnum(hEnum);
                return Status;
            }
        }
    }
    pMDImport->CloseEnum(hEnum);
    return FAILED(Status) ? Status : S_OK;
}

template <typename Callback>
HRESULT ForEachMethod(IMetaDataImport *pMDImport, mdTypeDef typeDef, const Callback &cb)
{
    HRESULT Status = S_OK;
    ULONG numMethods = 0;
    HCORENUM hEnum = nullptr;
    std::array<mdMethodDef, enumBatchSize> methodDefs{};
    while (true)
    {
        Status = pMDImport->EnumMethods(&hEnum, typeDef, methodDefs.data(), enumBatchSize, &numMethods);
        if (FAILED(Status) || numMethods == 0)
        {
            break;
        }

        for (ULONG i = 0; i < numMethods; ++i)
        {
            Status = cb(methodDefs.at(i));
            if (FAILED(Status) || Status == S_CAN_EXIT)
            {
                pMDImport->CloseEnum(hEnum);
                return Status;
            }
        }
    }
    pMDImport->CloseEnum(hEnum);
    return FAILED(Status) ? Status : S_OK;
}

template <typename Callback>
HRESULT ForEachMethodWithName(IMetaDataImport *pMDImport, const WCHAR *methodName, mdTypeDef typeDef, const Callback &cb)
{
    HRESULT Status = S_OK;
    ULONG numMethods = 0;
    HCORENUM hEnum = nullptr;
    std::array<mdMethodDef, enumSmallBatchSize> methodDefs{};
    while (true)
    {
        Status = pMDImport->EnumMethodsWithName(&hEnum, typeDef, methodName, methodDefs.data(), enumSmallBatchSize, &numMethods);
        if (FAILED(Status) || numMethods == 0)
        {
            break;
        }

        for (ULONG i = 0; i < numMethods; ++i)
        {
            Status = cb(methodDefs.at(i));
            if (FAILED(Status) || Status == S_CAN_EXIT)
            {
                pMDImport->CloseEnum(hEnum);
                return Status;
            }
        }
    }
    pMDImport->CloseEnum(hEnum);
    return FAILED(Status) ? Status : S_OK;
}

template <typename Callback>
HRESULT ForEachGenericParam(IMetaDataImport2 *pMDImport2, mdToken token, const Callback &cb)
{
    HRESULT Status = S_OK;
    ULONG numGenericParams = 0;
    HCORENUM hEnum = nullptr;
    std::array<mdGenericParam, enumSmallBatchSize> genericParams{};
    while (true)
    {
        Status = pMDImport2->EnumGenericParams(&hEnum, token, genericParams.data(), enumSmallBatchSize, &numGenericParams);
        if (FAILED(Status) || numGenericParams == 0)
        {
            break;
        }

        for (ULONG i = 0; i < numGenericParams; ++i)
        {
            Status = cb(genericParams.at(i));
            if (FAILED(Status) || Status == S_CAN_EXIT)
            {
                pMDImport2->CloseEnum(hEnum);
                return Status;
            }
        }
    }
    pMDImport2->CloseEnum(hEnum);
    return FAILED(Status) ? Status : S_OK;
}

} // namespace dncdbg::MetadataEnumerations

#endif // METADATA_ENUMERATIONS_H
