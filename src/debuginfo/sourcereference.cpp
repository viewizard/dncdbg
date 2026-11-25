// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/sourcereference.h"
#include "debuginfo/pdbreader.h"
#include <filesystem>
#include <mutex>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace dncdbg::SourceReference
{

namespace
{

int32_t &GetSourceReferenceId()
{
    static int32_t sourceReferenceId = 0;
    return sourceReferenceId;
}

std::unordered_map<PDB::GlobalFileIndex, int32_t, PDB::GlobalFileIndexHash> &GetGlobalIndexMap()
{
    static std::unordered_map<PDB::GlobalFileIndex, int32_t, PDB::GlobalFileIndexHash> globalIndexMap;
    return globalIndexMap;
}

std::unordered_map<int32_t, PDB::GlobalFileIndex> &GetSourceReferenceMap()
{
    static std::unordered_map<int32_t, PDB::GlobalFileIndex> sourceReferenceMap;
    return sourceReferenceMap;
}

std::unordered_map<int32_t, std::string> &GetSourceURLMap()
{
    static std::unordered_map<int32_t, std::string> sourceURLMap;
    return sourceURLMap;
}

std::mutex &GetSourceReferenceMutex()
{
    static std::mutex sourceReferenceMutex;
    return sourceReferenceMutex;
}

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

// Returns true if the source file already exists on disk; such sources do not need a source reference.
bool IsSourceOnDisk(const std::string &filePath)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::u8path(filePath), ec);
}

} // unnamed namespace

HRESULT GetGlobalIndex(int32_t sourceReference, PDB::GlobalFileIndex &globalIndex)
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    const auto &sourceReferenceMap = GetSourceReferenceMap();
    const auto indexFind = sourceReferenceMap.find(sourceReference);
    if (indexFind == sourceReferenceMap.cend())
    {
        return E_INVALIDARG;
    }

    globalIndex = indexFind->second;
    return S_OK;
}

HRESULT GetSourceReference(const PDB::GlobalFileIndex &globalIndex, int32_t &sourceReference, std::string &correctSourceFilePath)
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    const auto &globalIndexMap = GetGlobalIndexMap();
    const auto refFind = globalIndexMap.find(globalIndex);
    if (refFind == globalIndexMap.cend())
    {
        return E_INVALIDARG;
    }

    sourceReference = refFind->second;

    const auto &sourceURLMap = GetSourceURLMap();
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

HRESULT GetSourceURL(const PDB::GlobalFileIndex &globalIndex, std::string &url)
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    const auto &globalIndexMap = GetGlobalIndexMap();
    const auto refFind = globalIndexMap.find(globalIndex);
    if (refFind == globalIndexMap.cend())
    {
        return E_INVALIDARG;
    }

    const auto &sourceURLMap = GetSourceURLMap();
    const auto urlFind = sourceURLMap.find(refFind->second);
    if (urlFind == sourceURLMap.cend())
    {
        return E_INVALIDARG;
    }

    url = urlFind->second;
    return S_OK;
}

void AddLoadedSourcesForModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress, std::vector<Source> &sources)
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    // The map is only read in the loop below, so take the reference once.
    const auto &sourceURLMap = GetSourceURLMap();

    for (const auto &[globalIndex, sourceReference] : GetGlobalIndexMap())
    {
        if (globalIndex.modAddress != modAddress)
        {
            continue;
        }

        // SourceLink sources are listed with their URL instead of the local path.
        std::string url;
        const auto urlFind = sourceURLMap.find(sourceReference);
        if (urlFind != sourceURLMap.cend())
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
std::vector<Source> LoadModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress)
{
    std::vector<std::pair<uint32_t, std::string>> sourceFileIndexWithName;
    PDBReader::ListEmbeddedSources(pdbHandle, sourceFileIndexWithName);

    std::unordered_set<uint32_t> embeddedSourceIndexes;
    // Sources to register, as pairs of the source file index and the SourceLink URL (empty for embedded sources).
    std::vector<std::pair<uint32_t, std::string>> pendingSources;

    for (const auto &[index, filePath] : sourceFileIndexWithName)
    {
        embeddedSourceIndexes.emplace(index);
        if (!IsSourceOnDisk(filePath))
        {
            pendingSources.emplace_back(index, std::string{});
        }
    }

    // SourceLink sources (those with an `http://` or `https://` URL). Not every PDB contains
    // SourceLink information, so the absence of it is not an error.
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

            if (!IsSourceOnDisk(filePath))
            {
                pendingSources.emplace_back(index, urlStr);
            }
        }
    }

    std::vector<Source> newSources;
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    auto &globalIndexMap = GetGlobalIndexMap();
    auto &sourceReferenceMap = GetSourceReferenceMap();
    auto &sourceURLMap = GetSourceURLMap();

    // Register each source and add its description to newSources. A non-empty URL means that the
    // source must be downloaded (SourceLink), so the URL is remembered in the URL map.
    for (const auto &[sourceFileIndex, urlStr] : pendingSources)
    {
        int32_t &sourceReferenceId = GetSourceReferenceId();
        sourceReferenceId++;
        globalIndexMap.emplace(PDB::GlobalFileIndex{modAddress, sourceFileIndex}, sourceReferenceId);
        sourceReferenceMap.emplace(sourceReferenceId, PDB::GlobalFileIndex{modAddress, sourceFileIndex});

        if (!urlStr.empty())
        {
            sourceURLMap.emplace(sourceReferenceId, urlStr);
        }

        Source source;
        if (FAILED(GetSource(pdbHandle, urlStr, sourceFileIndex, sourceReferenceId, source)))
        {
            continue;
        }

        newSources.emplace_back(std::move(source));
    }

    return newSources;
}

// Unregister the embedded and SourceLink sources of the module and return descriptions for the
// loadedSource events. The caller should emit the events only after all debugger-internal locks are released.
std::vector<Source> UnloadModule(mdhandle_t pdbHandle, CORDB_ADDRESS modAddress)
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

            // Note that it->first is the global file index and it->second is the source reference.
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

void Initialize()
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    GetSourceReferenceId() = 0;
    GetGlobalIndexMap().clear();
    GetSourceReferenceMap().clear();
    GetSourceURLMap().clear();
}

void Cleanup()
{
    const std::scoped_lock<std::mutex> lock(GetSourceReferenceMutex());

    // Don't reset GetSourceReferenceId since source references must remain unique across debug sessions.
    GetGlobalIndexMap().clear();
    GetSourceReferenceMap().clear();
    GetSourceURLMap().clear();
}

} // namespace dncdbg::SourceReference
