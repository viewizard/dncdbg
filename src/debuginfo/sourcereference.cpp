// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/sourcereference.h"
#include "debuginfo/pdbreader.h"
#include <filesystem>
#include <unordered_set>
#include <vector>

namespace dncdbg
{

namespace
{

// Build a Source description for the loadedSource event. A non-empty `urlStr` (SourceLink) is
// used as the source path; otherwise the document name from the PDB file is used. Fails when
// the document name cannot be read from the PDB file; checksum retrieval is best-effort, so
// empty checksums are skipped.
HRESULT GetSource(mdhandle_t pdbHandle, const std::string &urlStr, uint32_t sourceFileIndex, int32_t sourceReference, Source &source)
{
    std::string sourceFilePath;
    std::string algorithm;
    std::string checksum;
    if (FAILED(PDBReader::GetSourceFile(pdbHandle, sourceFileIndex, sourceFilePath, algorithm, checksum)))
    {
        return E_FAIL;
    }

    source = Source(urlStr.empty() ? sourceFilePath : urlStr, sourceReference);
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

    auto &sourceURLMap = GetSourceURLMap();
    const auto urlFind = sourceURLMap.find(sourceReference);

    if (urlFind != sourceURLMap.cend())
    {
        correctSourceFilePath = urlFind->second;
    }
    else
    {
        correctSourceFilePath = "Source file extracted from PDB file. Original path: " + correctSourceFilePath;
    }
    return S_OK;
}

HRESULT SourceReference::GetSourceURL(const PDB::GlobalFileIndex &globalIndex, std::string &url)
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    const auto refFind = GetGlobalIndexMap().find(globalIndex);
    if (refFind == GetGlobalIndexMap().cend())
    {
        return E_INVALIDARG;
    }

    auto &sourceURLMap = GetSourceURLMap();
    const auto urlFind = sourceURLMap.find(refFind->second);
    if (urlFind == sourceURLMap.cend())
    {
        return E_INVALIDARG;
    }

    url = urlFind->second;
    return S_OK;
}

void SourceReference::AddLoadedSourcesForModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress,
                                                std::vector<Source> &sources)
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    for (const auto &[globalIndex, sourceReference] : GetGlobalIndexMap())
    {
        if (globalIndex.modAddress != modAddress)
        {
            continue;
        }

        // SourceLink sources are listed with their URL instead of the local path.
        std::string url;
        const auto urlFind = GetSourceURLMap().find(sourceReference);
        if (urlFind != GetSourceURLMap().cend())
        {
            url = urlFind->second;
        }

        Source source;
        if (FAILED(GetSource(pdbHandle, url, globalIndex.sourceFileIndex, sourceReference, source)))
        {
            continue;
        }

        sources.emplace_back(std::move(source));
    }
}

// Register the embedded and SourceLink sources of the module and return descriptions for the
// loadedSource events. The caller should emit the events only after all debugger-internal locks are released.
std::vector<Source> SourceReference::LoadModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress)
{
    std::vector<std::pair<uint32_t, std::string>> sourceFileIndexWithName;
    PDBReader::ListEmbeddedSources(pdbHandle, sourceFileIndexWithName);

    std::vector<Source> newSources;
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    auto &globalIndexMap = GetGlobalIndexMap();
    auto &sourceReferenceMap = GetSourceReferenceMap();
    auto &sourceURLMap = GetSourceURLMap();

    std::unordered_set<uint32_t> embeddedSourceIndexes;

    // Register the source and add its description to newSources. A non-empty URL means the
    // source must be downloaded (SourceLink), so it is remembered in the URL map.
    const auto registerSource = [&](uint32_t sourceFileIndex, const std::string &filePath, const std::string &urlStr)
    {
        std::error_code ec;
        const auto path = std::filesystem::u8path(filePath);

        // Skip sources that already exist on disk; they do not need a source reference.
        if (std::filesystem::is_regular_file(path, ec))
        {
            return;
        }

        m_sourceReferenceCount++;
        globalIndexMap.emplace(PDB::GlobalFileIndex{modAddress, sourceFileIndex}, m_sourceReferenceCount);
        sourceReferenceMap.emplace(m_sourceReferenceCount, PDB::GlobalFileIndex{modAddress, sourceFileIndex});

        if (!urlStr.empty())
        {
            sourceURLMap.emplace(m_sourceReferenceCount, urlStr);
        }

        Source source;
        if (FAILED(GetSource(pdbHandle, urlStr, sourceFileIndex, m_sourceReferenceCount, source)))
        {
            return;
        }

        newSources.emplace_back(std::move(source));
    };

    for (const auto &[index, filePath] : sourceFileIndexWithName)
    {
        embeddedSourceIndexes.emplace(index);
        registerSource(index, filePath, std::string{});
    }

    // SourceLink sources (all with an `http://` or `https://` URL). Not every PDB contains
    // SourceLink information, so its absence is not an error.
    std::vector<std::tuple<uint32_t, std::string, std::string>> sourceFileIndexWithNameAndURL;
    if (SUCCEEDED(PDBReader::ListSourceLinkSources(pdbHandle, sourceFileIndexWithNameAndURL)))
    {
        for (const auto &[index, filePath, urlStr] : sourceFileIndexWithNameAndURL)
        {
            // Skip sources that were already added as embedded sources.
            if (embeddedSourceIndexes.find(index) != embeddedSourceIndexes.cend())
            {
                continue;
            }

            registerSource(index, filePath, urlStr);
        }
    }

    return newSources;
}

// Unregister the embedded and SourceLink sources of the module and return descriptions for the
// loadedSource events. The caller should emit the events only after all debugger-internal locks are released.
std::vector<Source> SourceReference::UnloadModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress)
{
    std::vector<Source> removedSources;
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    auto &globalIndexMap = GetGlobalIndexMap();
    auto &sourceReferenceMap = GetSourceReferenceMap();
    auto &sourceURLMap = GetSourceURLMap();

    auto it = globalIndexMap.begin();
    while (it != globalIndexMap.end())
    {
        if (it->first.modAddress == modAddress)
        {
            std::string url;
            const auto urlFind = sourceURLMap.find(it->second);
            if (urlFind != sourceURLMap.cend())
            {
                url = urlFind->second;
            }

            // Note, it->first is the global file index and it->second is the source reference.
            Source source;
            if (SUCCEEDED(GetSource(pdbHandle, url, it->first.sourceFileIndex, it->second, source)))
            {
                removedSources.emplace_back(std::move(source));
            }

            sourceReferenceMap.erase(it->second);
            sourceURLMap.erase(it->second);
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
    GetSourceURLMap().clear();
}

} // namespace dncdbg
