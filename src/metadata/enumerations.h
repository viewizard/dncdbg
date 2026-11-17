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
#include <array>

namespace dncdbg::MetadataEnumerations
{

// Number of metadata tokens fetched per enumeration call.
constexpr ULONG enumBatchSize = 64;

// Note, the callbacks may return S_CAN_EXIT to stop the enumeration early.
// Note, S_FALSE is a success code that can accompany a partial batch, so only failures stop the enumeration.

template <typename Callback>
HRESULT ForEachField(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const Callback &cb)
{
    HRESULT Status = S_OK;
    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    std::array<mdFieldDef, enumBatchSize> fieldDefs{};
    while (true)
    {
        Status = pMDImport->EnumFields(&hEnum, currentTypeDef, fieldDefs.data(), enumBatchSize, &numFields);
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
HRESULT ForEachProperty(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const Callback &cb)
{
    HRESULT Status = S_OK;
    ULONG numProperties = 0;
    HCORENUM hEnum = nullptr;
    std::array<mdProperty, enumBatchSize> propertyDefs{};
    while (true)
    {
        Status = pMDImport->EnumProperties(&hEnum, currentTypeDef, propertyDefs.data(), enumBatchSize, &numProperties);
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

} // namespace dncdbg::MetadataEnumerations

#endif // METADATA_ENUMERATIONS_H
