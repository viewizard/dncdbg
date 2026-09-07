// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/sourcereference.h"
#include "debuginfo/pdbreader.h"
#include <filesystem>
#include <vector>

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

HRESULT SourceReference::GetSourceReference(const PDB::GlobalFileIndex &globalIndex, int32_t &sourceReference,
                                            std::string &correctSourceFilePath)
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    const auto refFind = GetGlobalIndexMap().find(globalIndex);
    if (refFind == GetGlobalIndexMap().cend())
    {
        return E_INVALIDARG;
    }

    sourceReference = refFind->second;
    correctSourceFilePath = "Source file extracted from PDB file. Original path: " + correctSourceFilePath;
    return S_OK;
}

void SourceReference::LoadModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress)
{
    std::vector<std::pair<uint32_t, std::string>> sourceFileIndexWithName;
    if (FAILED(PDBReader::ListEmbeddedSources(pdbHandle, sourceFileIndexWithName)))
    {
        return;
    }

    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    auto &globalIndexMap = GetGlobalIndexMap();
    auto &sourceReferenceMap = GetSourceReferenceMap();

    for (const auto &[index, filePath] : sourceFileIndexWithName)
    {
        std::error_code ec;
        const auto path = std::filesystem::u8path(filePath);

        // Skip sources that already exist on disk; they do not need a source reference.
        if (std::filesystem::is_regular_file(path, ec))
        {
            continue;
        }

        m_sourceReferenceCount++;
        globalIndexMap.emplace(PDB::GlobalFileIndex{modAddress, index}, m_sourceReferenceCount);
        sourceReferenceMap.emplace(m_sourceReferenceCount, PDB::GlobalFileIndex{modAddress, index});
    }
}

void SourceReference::ManagedCallbackUnloadModule(CORDB_ADDRESS modAddress)
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    auto &globalIndexMap = GetGlobalIndexMap();
    auto &sourceReferenceMap = GetSourceReferenceMap();

    auto it = globalIndexMap.begin();
    while (it != globalIndexMap.end())
    {
        if (it->first.modAddress == modAddress)
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
