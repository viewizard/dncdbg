// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/sourcereference.h"
#include "debuginfo/pdbreader.h"
#include <filesystem>
#include <vector>

namespace dncdbg
{

namespace
{

// Build a Source description for the loadedSource event. Fails when the document name cannot
// be read from the PDB file; checksum retrieval is best-effort, so empty checksums are skipped.
HRESULT GetLoadedSource(mdhandle_t pdbHandle, uint32_t sourceFileIndex, int32_t sourceReference, Source &source)
{
    std::string sourceFilePath;
    std::string algorithm;
    std::string checksum;
    if (FAILED(PDBReader::GetSourceFile(pdbHandle, sourceFileIndex, sourceFilePath, algorithm, checksum)))
    {
        return E_FAIL;
    }

    source = Source(sourceFilePath, sourceReference);
    if (!algorithm.empty() && !checksum.empty())
    {
        source.checksums.emplace_back(std::move(algorithm), std::move(checksum));
    }

    return S_OK;
}

} // unnamed namespace

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

void SourceReference::AddLoadedSourcesForModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress,
                                                std::vector<Source> &sources)
{
    for (const auto &[globalIndex, sourceReference] : GetGlobalIndexMap())
    {
        Source source;
        if (globalIndex.modAddress != modAddress ||
            FAILED(GetLoadedSource(pdbHandle, globalIndex.sourceFileIndex, sourceReference, source)))
        {
            continue;
        }

        sources.emplace_back(std::move(source));
    }
}

// Register embedded sources of the module and return descriptions for the loadedSource events.
// The caller should emit the events only after all debugger-internal locks are released.
std::vector<Source> SourceReference::LoadModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress)
{
    std::vector<std::pair<uint32_t, std::string>> sourceFileIndexWithName;
    if (FAILED(PDBReader::ListEmbeddedSources(pdbHandle, sourceFileIndexWithName)))
    {
        return {};
    }

    std::vector<Source> newSources;
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

        Source source;
        if (FAILED(GetLoadedSource(pdbHandle, index, m_sourceReferenceCount, source)))
        {
            continue;
        }

        newSources.emplace_back(std::move(source));
    }

    return newSources;
}

// Unregister embedded sources of the module and return descriptions for the loadedSource events.
// The caller should emit the events only after all debugger-internal locks are released.
std::vector<Source> SourceReference::UnloadModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress)
{
    std::vector<Source> removedSources;
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    auto &globalIndexMap = GetGlobalIndexMap();
    auto &sourceReferenceMap = GetSourceReferenceMap();

    auto it = globalIndexMap.begin();
    while (it != globalIndexMap.end())
    {
        if (it->first.modAddress == modAddress)
        {
            // Note, it->first is the global file index and it->second is the source reference.
            Source source;
            if (SUCCEEDED(GetLoadedSource(pdbHandle, it->first.sourceFileIndex, it->second, source)))
            {
                removedSources.emplace_back(std::move(source));
            }

            sourceReferenceMap.erase(it->second);
            it = globalIndexMap.erase(it);
        }
        else
        {
            ++it;
        }
    }

    return removedSources;
}

void SourceReference::Cleanup()
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    m_sourceReferenceCount = 0;
    GetGlobalIndexMap().clear();
    GetSourceReferenceMap().clear();
}

} // namespace dncdbg
