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
// Number of items fetched per enumeration call when only a few results are expected,
// for example, generic parameters and interface implementations.
constexpr ULONG enumSmallBatchSize = 8;

namespace detail
{

// Generic driver for metadata enumerations.
// EnumFn is invoked as enumFn(HCORENUM *, Item *, ULONG batchSize, ULONG *count).
// Callbacks may return S_CAN_EXIT to stop the enumeration early.
// S_FALSE returned by the metadata API is a success code that can accompany a partial batch,
// so only failures and S_CAN_EXIT stop the enumeration.
template <typename Item, ULONG BatchSize, typename Owner, typename EnumFn, typename Callback>
HRESULT ForEachEnum(Owner *pOwner, const EnumFn &enumFn, const Callback &cb)
{
    HRESULT Status = S_OK;
    ULONG count = 0;
    HCORENUM hEnum = nullptr;
    std::array<Item, BatchSize> items{};
    while (true)
    {
        Status = enumFn(&hEnum, items.data(), BatchSize, &count);
        if (FAILED(Status) || count == 0)
        {
            break;
        }

        for (ULONG i = 0; i < count; ++i)
        {
            Status = cb(items.at(i));
            if (FAILED(Status) || Status == S_CAN_EXIT)
            {
                pOwner->CloseEnum(hEnum);
                return Status;
            }
        }
    }
    pOwner->CloseEnum(hEnum);
    return FAILED(Status) ? Status : S_OK;
}

} // namespace detail

template <typename Callback>
HRESULT ForEachField(IMetaDataImport *pMDImport, mdTypeDef typeDef, const Callback &cb)
{
    return detail::ForEachEnum<mdFieldDef, enumBatchSize>(
        pMDImport,
        [&](HCORENUM *phEnum, mdFieldDef *buffer, ULONG batchSize, ULONG *count)
        {
            return pMDImport->EnumFields(phEnum, typeDef, buffer, batchSize, count);
        },
        cb);
}

template <typename Callback>
HRESULT ForEachProperty(IMetaDataImport *pMDImport, mdTypeDef typeDef, const Callback &cb)
{
    return detail::ForEachEnum<mdProperty, enumBatchSize>(
        pMDImport,
        [&](HCORENUM *phEnum, mdProperty *buffer, ULONG batchSize, ULONG *count)
        {
            return pMDImport->EnumProperties(phEnum, typeDef, buffer, batchSize, count);
        },
        cb);
}

template <typename Callback>
HRESULT ForEachTypeDef(IMetaDataImport *pMDImport, const Callback &cb)
{
    return detail::ForEachEnum<mdTypeDef, enumBatchSize>(
        pMDImport,
        [&](HCORENUM *phEnum, mdTypeDef *buffer, ULONG batchSize, ULONG *count)
        {
            return pMDImport->EnumTypeDefs(phEnum, buffer, batchSize, count);
        },
        cb);
}

template <typename Callback>
HRESULT ForEachMethod(IMetaDataImport *pMDImport, mdTypeDef typeDef, const Callback &cb)
{
    return detail::ForEachEnum<mdMethodDef, enumBatchSize>(
        pMDImport,
        [&](HCORENUM *phEnum, mdMethodDef *buffer, ULONG batchSize, ULONG *count)
        {
            return pMDImport->EnumMethods(phEnum, typeDef, buffer, batchSize, count);
        },
        cb);
}

template <typename Callback>
HRESULT ForEachMethodWithName(IMetaDataImport *pMDImport, const WCHAR *methodName, mdTypeDef typeDef, const Callback &cb)
{
    return detail::ForEachEnum<mdMethodDef, enumSmallBatchSize>(
        pMDImport,
        [&](HCORENUM *phEnum, mdMethodDef *buffer, ULONG batchSize, ULONG *count)
        {
            return pMDImport->EnumMethodsWithName(phEnum, typeDef, methodName, buffer, batchSize, count);
        },
        cb);
}

template <typename Callback>
HRESULT ForEachGenericParam(IMetaDataImport2 *pMDImport2, mdToken token, const Callback &cb)
{
    return detail::ForEachEnum<mdGenericParam, enumSmallBatchSize>(
        pMDImport2,
        [&](HCORENUM *phEnum, mdGenericParam *buffer, ULONG batchSize, ULONG *count)
        {
            return pMDImport2->EnumGenericParams(phEnum, token, buffer, batchSize, count);
        },
        cb);
}

template <typename Callback>
HRESULT ForEachInterfaceImpl(IMetaDataImport *pMDImport, mdTypeDef typeDef, const Callback &cb)
{
    return detail::ForEachEnum<mdInterfaceImpl, enumSmallBatchSize>(
        pMDImport,
        [&](HCORENUM *phEnum, mdInterfaceImpl *buffer, ULONG batchSize, ULONG *count)
        {
            return pMDImport->EnumInterfaceImpls(phEnum, typeDef, buffer, batchSize, count);
        },
        cb);
}

} // namespace dncdbg::MetadataEnumerations

#endif // METADATA_ENUMERATIONS_H
