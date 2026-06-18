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
        uint8_t const* nameBlob = nullptr;
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

} // namespace dncdbg
