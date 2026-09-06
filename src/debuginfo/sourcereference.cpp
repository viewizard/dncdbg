// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/sourcereference.h"

namespace dncdbg
{

int32_t SourceReference::m_sourceReferenceCount = 0;

HRESULT SourceReference::GetGlobalIndex(int32_t sourceReference, PDB::GlobalFileIndex &globalIndex)
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    const auto indexFind = GetSourceReferenceMap().find(sourceReference);
    if (indexFind == GetSourceReferenceMap().cend())
    {
        return E_INVALIDARG;
    }

    globalIndex = indexFind->second;
    return S_OK;
}

HRESULT SourceReference::GetSourceReference(const PDB::GlobalFileIndex &globalIndex, int32_t &sourceReference)
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    const auto refFind = GetGlobalIndexMap().find(globalIndex);
    if (refFind == GetGlobalIndexMap().cend())
    {
        return E_INVALIDARG;
    }

    sourceReference = refFind->second;
    return S_OK;
}

void SourceReference::LoadModule()
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    // TODO: Implement sourceReference id assignment. Per the DAP spec, when
    // sourceReference > 0 the source contents must be retrieved through the
    // `source` request, which is not handled yet.
}

void SourceReference::ManagedCallbackUnloadModule(CORDB_ADDRESS baseAddress)
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    auto &globalIndexMap = GetGlobalIndexMap();
    auto &sourceReferenceMap = GetSourceReferenceMap();

    auto it = globalIndexMap.begin();
    while (it != globalIndexMap.end())
    {
        if (it->first.modAddress == baseAddress)
        {
            sourceReferenceMap.erase(it->second);
            it = globalIndexMap.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void SourceReference::Cleanup()
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    m_sourceReferenceCount = 0;
    GetGlobalIndexMap().clear();
    GetSourceReferenceMap().clear();
}

} // namespace dncdbg
