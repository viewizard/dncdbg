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
#include <cstddef>

namespace dncdbg::MetadataEnumerations
{

constexpr std::size_t enumBatchSize = 64;

// Note: the callback may return S_CAN_EXIT for a fast exit.
template <typename Callback>
HRESULT ForEachField(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const Callback &cb)
{
    HRESULT Status = S_OK;
    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    std::array<mdFieldDef, enumBatchSize> fieldDefs{};
    while (S_OK == pMDImport->EnumFields(&hEnum, currentTypeDef, fieldDefs.data(), enumBatchSize, &numFields) &&
           numFields != 0)
    {
        bool breakLoop = false;
        for (ULONG i = 0; i < numFields; ++i)
        {
            if (FAILED(Status = cb(fieldDefs.at(i))) ||
                Status == S_CAN_EXIT)
            {
                breakLoop = true;
                break;
            }
        }
        if (breakLoop)
        {
            break;
        }
    }
    pMDImport->CloseEnum(hEnum);
    return Status;
}

// Note: the callback may return S_CAN_EXIT for a fast exit.
template <typename Callback>
HRESULT ForEachProperty(IMetaDataImport *pMDImport, mdTypeDef currentTypeDef, const Callback &cb)
{
    HRESULT Status = S_OK;
    ULONG numProperties = 0;
    HCORENUM hEnum = nullptr;
    std::array<mdProperty, enumBatchSize> propertyDefs{};
    while (S_OK == pMDImport->EnumProperties(&hEnum, currentTypeDef, propertyDefs.data(), enumBatchSize, &numProperties) &&
           numProperties != 0)
    {
        bool breakLoop = false;
        for (ULONG i = 0; i < numProperties; ++i)
        {
            if (FAILED(Status = cb(propertyDefs.at(i))) ||
                Status == S_CAN_EXIT)
            {
                breakLoop = true;
                break;
            }
        }
        if (breakLoop)
        {
            break;
        }
    }
    pMDImport->CloseEnum(hEnum);
    return Status;
}

} // namespace dncdbg::MetadataEnumerations

#endif // METADATA_ENUMERATIONS_H
