// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/pdbreader.h"
#include <dnmd.h>
#include <dnmd_pdb.h>

namespace dncdbg
{

HRESULT PDBReader::OpenPDB(const std::string &pdbPath, const PdbIdentity &pdbId, PDBHolder &pdbHolder)
{
    MemoryBuffer mBuff;
    if (!mBuff.Open(pdbPath))
    {
        return E_FAIL;
    }

    mdhandle_t handle = nullptr;
    if (!md_create_handle(mBuff.Data(), static_cast<uint32_t>(mBuff.Size()), &handle))
    {
        return E_FAIL;
    }

    std::array<uint8_t, g_pdbid_size> rawPdbId{};
    size_t size = g_pdbid_size;
    if (!md_get_pdb_id(handle, &size, rawPdbId.data()))
    {
        md_destroy_handle(handle);
        return E_FAIL;
    }

    if (rawPdbId != pdbId)
    {
        md_destroy_handle(handle);
        return E_FAIL;
    }

    // Destroy any existing handle before assigning a new one
    if (pdbHolder.m_handle != nullptr)
    {
        md_destroy_handle(pdbHolder.m_handle);
    }
    pdbHolder.m_handle = handle;
    pdbHolder.m_memBuff = std::move(mBuff);
    return S_OK;
}

HRESULT PDBReader::GetAllSourceFiles(mdhandle_t pdbHandle, std::vector<std::string> &sourceFiles)
{
    if (pdbHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    // Create cursor to the Document table
    mdcursor_t docCursor{};
    uint32_t docCount = 0;
    if (!md_create_cursor(pdbHandle, mdtid_Document, &docCursor, &docCount))
    {
        return E_FAIL;
    }

    // Reserve space for all documents
    sourceFiles.clear();
    sourceFiles.reserve(docCount);

    // Iterate through all documents
    for (uint32_t i = 0; i < docCount; ++i)
    {
        // Get the Name blob from the Document table
        uint8_t const *nameBlob = nullptr;
        uint32_t blobLen = 0;
        if (!md_get_column_value_as_blob(docCursor, mdtDocument_Name, &nameBlob, &blobLen))
        {
            md_cursor_move(&docCursor, 1);
            continue;
        }

        if (nameBlob == nullptr || blobLen == 0)
        {
            md_cursor_move(&docCursor, 1);
            continue;
        }

        // First, query the required buffer size
        size_t nameLen = 0;
        md_blob_parse_result_t result = md_parse_document_name(pdbHandle, nameBlob, blobLen, nullptr, &nameLen);
        if (result != mdbpr_InsufficientBuffer || nameLen == 0)
        {
            md_cursor_move(&docCursor, 1);
            continue;
        }

        // Allocate buffer and parse the document name
        std::string docName(nameLen, '\0');
        result = md_parse_document_name(pdbHandle, nameBlob, blobLen, docName.data(), &nameLen);
        if (result != mdbpr_Success)
        {
            md_cursor_move(&docCursor, 1);
            continue;
        }

        // Remove null terminator that was included in the length
        if (!docName.empty() && docName.back() == '\0')
        {
            docName.pop_back();
        }

        sourceFiles.push_back(std::move(docName));
        md_cursor_move(&docCursor, 1);
    }

    return S_OK;
}

HRESULT PDBReader::GetMethodsRanges(mdhandle_t pdbHandle, const std::set<mdMethodDef> &constrTokens,
                                    std::unordered_map<uint32_t, std::vector<MethodRange>> &srcMethodsMap)
{
    if (pdbHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    // Create cursor to the Document table
    mdcursor_t docCursor{};
    uint32_t docCount = 0;
    if (!md_create_cursor(pdbHandle, mdtid_Document, &docCursor, &docCount))
    {
        return E_FAIL;
    }

    // Reserve space for all sources
    srcMethodsMap.clear();
    srcMethodsMap.reserve(docCount);

    // Create cursor to the MethodDebugInformation table
    mdcursor_t mdiCursor{};
    uint32_t mdiCount = 0;
    if (!md_create_cursor(pdbHandle, mdtid_MethodDebugInformation, &mdiCursor, &mdiCount))
    {
        return E_FAIL;
    }

    // Iterate through all method debug information entries
    for (uint32_t i = 1; i <= mdiCount; ++i)
    {
        const mdToken methodToken = TokenFromRid(i, mdtMethodDef);

        // Get the SequencePoints blob
        uint8_t const *seqPointsBlob = nullptr;
        uint32_t blobLen = 0;
        if (!md_get_column_value_as_blob(mdiCursor, mdtMethodDebugInformation_SequencePoints, &seqPointsBlob, &blobLen))
        {
            md_cursor_move(&mdiCursor, 1);
            continue;
        }

        if (seqPointsBlob == nullptr || blobLen == 0)
        {
            md_cursor_move(&mdiCursor, 1);
            continue;
        }

        // First, query the required buffer size
        size_t bufferLen = 0;
        md_blob_parse_result_t result = md_parse_sequence_points(mdiCursor, seqPointsBlob, blobLen, nullptr, &bufferLen);
        if (result != mdbpr_InsufficientBuffer || bufferLen == 0)
        {
            md_cursor_move(&mdiCursor, 1);
            continue;
        }

        // Allocate buffer and parse sequence points
        std::vector<uint8_t> buffer(bufferLen, 0);
        auto *seqPoints = reinterpret_cast<md_sequence_points_t *>(buffer.data());
        result = md_parse_sequence_points(mdiCursor, seqPointsBlob, blobLen, seqPoints, &bufferLen);
        if (result != mdbpr_Success)
        {
            md_cursor_move(&mdiCursor, 1);
            continue;
        }

        // Extract line range from sequence points
        int32_t startLine = 0;
        int32_t startColumn = 0;
        int32_t endLine = 0;
        int32_t endColumn = 0;
        bool foundFirst = false;
        // Document token and index in sourceFiles vector returned by GetAllSourceFiles() method
        mdToken docToken{};
        uint32_t docIndex = 0;

        if (!md_cursor_to_token(seqPoints->document, &docToken))
        {
            // Document might be null for methods without source
            md_cursor_move(&mdiCursor, 1);
            continue;
        }
        docIndex = RidFromToken(docToken) - 1;

        // Check if this method is a constructor
        const bool isCtor = constrTokens.find(methodToken) != constrTokens.end();

        for (uint32_t j = 0; j < seqPoints->record_count; ++j)
        {
            const auto &record = seqPoints->records[j];

            if (record.kind == md_sequence_points_t::record_t::mdsp_DocumentRecord)
            {
                if (!md_cursor_to_token(record.document.document, &docToken)) // NOLINT(cppcoreguidelines-pro-type-union-access)
                {
                    continue;
                }
                docIndex = RidFromToken(docToken) - 1;

                continue;
            }

            if (record.kind == md_sequence_points_t::record_t::mdsp_SequencePointRecord)
            {
                if (!foundFirst)
                {
                    // First sequence point - set start position
                    startLine = static_cast<int32_t>(record.sequence_point.rolling_start_line); // NOLINT(cppcoreguidelines-pro-type-union-access)
                    startColumn = static_cast<int32_t>(record.sequence_point.rolling_start_column); // NOLINT(cppcoreguidelines-pro-type-union-access)
                    foundFirst = true;
                }
                // Update end position with each sequence point
                endLine = static_cast<int32_t>(record.sequence_point.rolling_start_line + // NOLINT(cppcoreguidelines-pro-type-union-access)
                                               static_cast<int64_t>(record.sequence_point.delta_lines)); // NOLINT(cppcoreguidelines-pro-type-union-access)
                endColumn = static_cast<int32_t>(record.sequence_point.rolling_start_column + // NOLINT(cppcoreguidelines-pro-type-union-access)
                                                 record.sequence_point.delta_columns); // NOLINT(cppcoreguidelines-pro-type-union-access)

                if (isCtor)
                {
                    // Add sequence point range to the src's collection
                    auto &methods = srcMethodsMap[docIndex];
                    methods.emplace_back(methodToken, startLine, endLine, startColumn, endColumn, isCtor);
                    foundFirst = false;
                }
            }
        }

        if (!foundFirst || isCtor)
        {
            // No valid sequence points found, or this is a constructor that we add as sequence points
            md_cursor_move(&mdiCursor, 1);
            continue;
        }

        // Add method range to the src's collection
        auto &methods = srcMethodsMap[docIndex];
        methods.emplace_back(methodToken, startLine, endLine, startColumn, endColumn, isCtor);

        md_cursor_move(&mdiCursor, 1);
    }

    return S_OK;
}

} // namespace dncdbg
