// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/pdbreader.h"
#include "utils/utftoupper.h"
#include <dnmd.h>
#include <dnmd_pdb.h>
#include <cstddef>
#include <cstring>
#include <memory>

namespace dncdbg::PDBReader
{

namespace
{

// GUIDs are taken from Roslyn source code:
// https://github.com/dotnet/roslyn/blob/afd10305a37c0ffb2cfb2c2d8446154c68cfa87a/src/Dependencies/CodeAnalysis.Debugging/PortableCustomDebugInfoKinds.cs#L14
// {6DA9A61E-F8C7-4874-BE62-68BC5630DF71}
constexpr std::array<uint8_t, 16> guidStateMachineHoistedLocalScopes{
    0x1e, 0xa6, 0xa9, 0x6d,                        // Data1 (0x6DA9A61E)
    0xc7, 0xf8,                                    // Data2 (0xF8C7)
    0x74, 0x48,                                    // Data3 (0x4874)
    0xbe, 0x62, 0x68, 0xbc, 0x56, 0x30, 0xdf, 0x71 // Data4 (BE62-68BC5630DF71)
};
// {54FD2AC5-E925-401A-9C2A-F94F171072F8}
constexpr std::array<uint8_t, 16> asyncMethodSteppingInformation{
    0xc5, 0x2a, 0xfd, 0x54,                        // Data1 (0x54FD2AC5)
    0x25, 0xe9,                                    // Data2 (0xE925)
    0x1a, 0x40,                                    // Data3 (0x401A)
    0x9c, 0x2a, 0xf9, 0x4f, 0x17, 0x10, 0x72, 0xf8 // Data4 (9C2A-F94F171072F8)
};

// Constants for parsing StateMachineHoistedLocalScopes blob
constexpr uint32_t int32Size = 4;
constexpr uint32_t hoistedLocalEntrySize = 8; // 2 int32 values: startOffset + length
constexpr uint32_t bitShift8 = 8;
constexpr uint32_t bitShift16 = 16;
constexpr uint32_t bitShift24 = 24;

// Constants for compressed integer parsing (ECMA-335 II.23.2 Blobs and sigs)
constexpr uint8_t compressedIntOneByteMask = 0x80;      // Top bit unset: 1-byte encoding
constexpr uint8_t compressedIntTwoBytePattern = 0x80;   // Top 2 bits: 10 = 2-byte encoding
constexpr uint8_t compressedIntFourBytePattern = 0xC0;  // Top 3 bits: 110 = 4-byte encoding
constexpr uint8_t compressedIntTwoByteMask = 0xC0;      // Mask for checking 2-byte pattern
constexpr uint8_t compressedIntFourByteMask = 0xE0;     // Mask for checking 4-byte pattern

// Read little-endian uint32 from blob at given offset
uint32_t ReadLittleEndianUInt32(const uint8_t *blob, uint32_t offset)
{
    return static_cast<uint32_t>(blob[offset]) |
           (static_cast<uint32_t>(blob[offset + 1]) << bitShift8) |
           (static_cast<uint32_t>(blob[offset + 2]) << bitShift16) |
           (static_cast<uint32_t>(blob[offset + 3]) << bitShift24);
}

// Returns the size in bytes of a compressed integer at the given offset, or 0 on error.
// Note: This function only returns the byte count and does not decode the actual value.
uint32_t SkipCompressedInteger(const uint8_t *blob, uint32_t offset)
{
    if (blob == nullptr)
    {
        return 0;
    }

    const uint8_t firstByte = blob[offset];

    // Top bit unset: 1-byte encoding (values 0x00-0x7F)
    if ((firstByte & compressedIntOneByteMask) == 0)
    {
        return 1;
    }

    // Top 2 bits are 10: 2-byte encoding (values 0x80-0x3FFF)
    if ((firstByte & compressedIntTwoByteMask) == compressedIntTwoBytePattern)
    {
        return 2;
    }

    // Top 3 bits are 110: 4-byte encoding (values 0x4000-0x1FFFFFFF)
    if ((firstByte & compressedIntFourByteMask) == compressedIntFourBytePattern)
    {
        return 4;
    }

    // Invalid encoding (top 3 bits are 111)
    return 0;
}

// RAII wrapper for properly aligned md_sequence_points_t buffer.
// md_sequence_points_t contains int64_t and mdcursor_t (intptr_t) members,
// which require 8-byte alignment. On Linux arm32, misaligned 64-bit access
// can cause SIGBUS. Using aligned operator new guarantees correct alignment
// regardless of platform, unlike std::vector<uint8_t> which only guarantees
// alignment for uint8_t (1 byte).
struct SeqPointsDeleter
{
    void operator()(md_sequence_points_t *p) const noexcept
    {
        ::operator delete(p, static_cast<std::align_val_t>(alignof(md_sequence_points_t)));
    }
};
using SeqPointsPtr = std::unique_ptr<md_sequence_points_t, SeqPointsDeleter>;

} // unnamed namespace

HRESULT OpenPDB(const std::string &pdbPath, const PDB::Identity &pdbId, MemoryBuffer &memBuffer, mdhandle_t &pdbHandle)
{
    MemoryBuffer tmpBuff;
    if (!tmpBuff.Open(pdbPath))
    {
        return E_FAIL;
    }

    mdhandle_t tmpHandle = nullptr;
    if (!md_create_handle(tmpBuff.Data(), static_cast<uint32_t>(tmpBuff.Size()), &tmpHandle))
    {
        return E_FAIL;
    }

    std::array<uint8_t, PDB::IDSize> rawPdbId{};
    size_t size = PDB::IDSize;
    if (!md_get_pdb_id(tmpHandle, &size, rawPdbId.data()))
    {
        md_destroy_handle(tmpHandle);
        return E_FAIL;
    }

    if (rawPdbId != pdbId)
    {
        md_destroy_handle(tmpHandle);
        return E_FAIL;
    }

    // Destroy any existing handle before assigning a new one
    if (pdbHandle != nullptr)
    {
        md_destroy_handle(pdbHandle);
    }
    pdbHandle = tmpHandle;
    memBuffer = std::move(tmpBuff);
    return S_OK;
}

HRESULT GetAllSourceFiles(mdhandle_t pdbHandle, std::vector<std::string> &sourceFiles)
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

#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
        docName = to_uppercase(docName);
#endif

        sourceFiles.push_back(std::move(docName));
        md_cursor_move(&docCursor, 1);
    }

    return S_OK;
}

HRESULT GetMethodsRanges(mdhandle_t pdbHandle, const std::unordered_set<mdMethodDef> &constrTokens,
                         std::unordered_map<uint32_t, std::vector<PDB::MethodRange>> &srcMethodsMap)
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

        // Allocate properly aligned buffer and parse sequence points
        // Use aligned operator new to guarantee correct alignment for md_sequence_points_t
        // which contains int64_t and mdcursor_t members requiring 8-byte alignment.
        void *rawBuffer = ::operator new(bufferLen, static_cast<std::align_val_t>(alignof(md_sequence_points_t)));
        SeqPointsPtr seqPoints(static_cast<md_sequence_points_t *>(rawBuffer));
        result = md_parse_sequence_points(mdiCursor, seqPointsBlob, blobLen, seqPoints.get(), &bufferLen);
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

HRESULT GetLocalConstants(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                          std::vector<PDB::LocalConstant> &localConsts)
{
    if (pdbHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    localConsts.clear();

    // Create cursor to the LocalScope table
    mdcursor_t lscopeCursor{};
    uint32_t lscopeCount = 0;
    if (!md_create_cursor(pdbHandle, mdtid_LocalScope, &lscopeCursor, &lscopeCount))
    {
        return E_FAIL;
    }

    // Iterate through all local scopes
    for (uint32_t i = 0; i < lscopeCount; ++i)
    {
        // Get the Method column to check if this scope belongs to our method
        mdToken scopeMethodToken = mdTokenNil;
        if (!md_get_column_value_as_token(lscopeCursor, mdtLocalScope_Method, &scopeMethodToken))
        {
            md_cursor_move(&lscopeCursor, 1);
            continue;
        }

        // Check if this scope belongs to the requested method
        if (scopeMethodToken != methodToken)
        {
            md_cursor_move(&lscopeCursor, 1);
            continue;
        }

        // Get StartOffset and Length to check IL offset range
        uint32_t startOffset = 0;
        uint32_t length = 0;
        if (!md_get_column_value_as_constant(lscopeCursor, mdtLocalScope_StartOffset, &startOffset) ||
            !md_get_column_value_as_constant(lscopeCursor, mdtLocalScope_Length, &length))
        {
            md_cursor_move(&lscopeCursor, 1);
            continue;
        }

        const uint32_t endOffset = startOffset + length;

        // Check if IL offset is within this scope [startOffset, endOffset)
        if (ilOffset < startOffset || ilOffset >= endOffset)
        {
            md_cursor_move(&lscopeCursor, 1);
            continue;
        }

        // Get the ConstantList range for this scope
        mdcursor_t constCursor{};
        uint32_t constCount = 0;
        if (!md_get_column_value_as_range(lscopeCursor, mdtLocalScope_ConstantList, &constCursor, &constCount))
        {
            md_cursor_move(&lscopeCursor, 1);
            continue;
        }

        // Iterate through all local constants in this scope
        for (uint32_t j = 0; j < constCount; ++j)
        {
            // Get the constant name
            char const *namePtr = nullptr;
            if (!md_get_column_value_as_utf8(constCursor, mdtLocalConstant_Name, &namePtr))
            {
                md_cursor_move(&constCursor, 1);
                continue;
            }

            // Get the signature blob
            uint8_t const *sigBlob = nullptr;
            uint32_t sigLen = 0;
            if (!md_get_column_value_as_blob(constCursor, mdtLocalConstant_Signature, &sigBlob, &sigLen))
            {
                md_cursor_move(&constCursor, 1);
                continue;
            }

            if (namePtr != nullptr && sigBlob != nullptr && sigLen > 0)
            {
                PDB::LocalConstant localConst;
                localConst.name = to_utf16(namePtr);
                localConst.signature.assign(sigBlob, sigBlob + sigLen);
                localConsts.push_back(std::move(localConst));
            }

            md_cursor_move(&constCursor, 1);
        }

        md_cursor_move(&lscopeCursor, 1);
    }

    return S_OK;
}

HRESULT GetLocalVariableName(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                             uint32_t localVarIndex, WSTRING &localVarName)
{
    if (pdbHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    // Create cursor to the LocalScope table
    mdcursor_t lscopeCursor{};
    uint32_t lscopeCount = 0;
    if (!md_create_cursor(pdbHandle, mdtid_LocalScope, &lscopeCursor, &lscopeCount))
    {
        return E_FAIL;
    }

    // Iterate through all local scopes
    for (uint32_t i = 0; i < lscopeCount; ++i)
    {
        // Get the Method column to check if this scope belongs to our method
        mdToken scopeMethodToken = mdTokenNil;
        if (!md_get_column_value_as_token(lscopeCursor, mdtLocalScope_Method, &scopeMethodToken))
        {
            md_cursor_move(&lscopeCursor, 1);
            continue;
        }

        // Check if this scope belongs to the requested method
        if (scopeMethodToken != methodToken)
        {
            md_cursor_move(&lscopeCursor, 1);
            continue;
        }

        // Get StartOffset and Length to check IL offset range
        uint32_t startOffset = 0;
        uint32_t length = 0;
        if (!md_get_column_value_as_constant(lscopeCursor, mdtLocalScope_StartOffset, &startOffset) ||
            !md_get_column_value_as_constant(lscopeCursor, mdtLocalScope_Length, &length))
        {
            md_cursor_move(&lscopeCursor, 1);
            continue;
        }

        const uint32_t endOffset = startOffset + length;

        // Check if IL offset is within this scope [startOffset, endOffset)
        if (ilOffset < startOffset || ilOffset >= endOffset)
        {
            md_cursor_move(&lscopeCursor, 1);
            continue;
        }

        // Get the VariableList range for this scope
        mdcursor_t varCursor{};
        uint32_t varCount = 0;
        if (!md_get_column_value_as_range(lscopeCursor, mdtLocalScope_VariableList, &varCursor, &varCount))
        {
            md_cursor_move(&lscopeCursor, 1);
            continue;
        }

        // Iterate through all local variables in this scope
        for (uint32_t j = 0; j < varCount; ++j)
        {
            // Check local variable index
            uint32_t index = 0;
            if (!md_get_column_value_as_constant(varCursor, mdtLocalVariable_Index, &index) ||
                index != localVarIndex)
            {
                md_cursor_move(&varCursor, 1);
                continue;
            }

            // Check local variable attributes
            uint32_t attributes = 0;
            static constexpr uint32_t debuggerHidden = 0x0001;
            if (!md_get_column_value_as_constant(varCursor, mdtLocalVariable_Attributes, &attributes) ||
                (attributes & debuggerHidden) == debuggerHidden)
            {
                return E_FAIL;
            }

            // Get the variable name
            char const *namePtr = nullptr;
            if (!md_get_column_value_as_utf8(varCursor, mdtLocalVariable_Name, &namePtr) ||
                namePtr == nullptr)
            {
                return E_FAIL;
            }

            localVarName = to_utf16(namePtr);
            return S_OK;
        }

        md_cursor_move(&lscopeCursor, 1);
    }

    return E_FAIL;
}

bool IsHoistedLocalInScope(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset, uint32_t hoistedLocalIndex)
{
    if (pdbHandle == nullptr)
    {
        return false;
    }

    // Create cursor to the CustomDebugInformation table
    mdcursor_t cdiCursor;
    uint32_t cdiCount = 0;
    if (!md_create_cursor(pdbHandle, mdtid_CustomDebugInformation, &cdiCursor, &cdiCount))
    {
        return false;
    }

    // Iterate through all custom debug information
    for (uint32_t i = 0; i < cdiCount; ++i)
    {
        // Get the Parent column to check if this information belongs to our method
        mdToken cdiMethodToken = mdTokenNil;
        if (!md_get_column_value_as_token(cdiCursor, mdtCustomDebugInformation_Parent, &cdiMethodToken) ||
            cdiMethodToken != methodToken)
        {
            md_cursor_move(&cdiCursor, 1);
            continue;
        }

        // Get the Kind column to check if this information belongs to hoisted locals
        mdguid_t guid;
        if (!md_get_column_value_as_guid(cdiCursor, mdtCustomDebugInformation_Kind, &guid) ||
            std::memcmp(&guid, guidStateMachineHoistedLocalScopes.data(), sizeof(mdguid_t)) != 0)
        {
            md_cursor_move(&cdiCursor, 1);
            continue;
        }

        // Get the hoisted locals blob
        uint8_t const *hlBlob = nullptr;
        uint32_t hlBlobSize = 0;
        if (!md_get_column_value_as_blob(cdiCursor, mdtCustomDebugInformation_Value, &hlBlob, &hlBlobSize) ||
            hlBlobSize == 0)
        {
            break;
        }

        // Parse the hoisted locals blob
        // Format: sequence of (int32 startOffset, int32 length) pairs, no count prefix
        const uint32_t count = hlBlobSize / hoistedLocalEntrySize;

        for (uint32_t slotIndex = 0; slotIndex < count; ++slotIndex)
        {
            const uint32_t offset = slotIndex * hoistedLocalEntrySize;
            if (offset + hoistedLocalEntrySize > hlBlobSize)
            {
                break;
            }

            // Note: While stored as int32 in PDB, startOffset and length are always non-negative
            const uint32_t startOffset = ReadLittleEndianUInt32(hlBlob, offset);
            const uint32_t length = ReadLittleEndianUInt32(hlBlob, offset + int32Size);

            if (slotIndex != hoistedLocalIndex)
            {
                continue;
            }

            const uint32_t endOffset = startOffset + length;

            // Check if IL offset is within this scope [startOffset, endOffset)
            return ilOffset >= startOffset && ilOffset < endOffset;
        }

        break;
    }

    return false;
}

HRESULT GetAsyncMethodSteppingInfo(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t &catchHandlerOffset,
                                   std::vector<PDB::AsyncAwaitInfoBlock> &awaitInfos)
{
    if (pdbHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    catchHandlerOffset = 0;
    awaitInfos.clear();

    // Create cursor to the CustomDebugInformation table
    mdcursor_t cdiCursor;
    uint32_t cdiCount = 0;
    if (!md_create_cursor(pdbHandle, mdtid_CustomDebugInformation, &cdiCursor, &cdiCount))
    {
        return E_FAIL;
    }

    // Iterate through all custom debug information
    for (uint32_t i = 0; i < cdiCount; ++i)
    {
        // Get the Parent column to check if this information belongs to our method
        mdToken cdiMethodToken = mdTokenNil;
        if (!md_get_column_value_as_token(cdiCursor, mdtCustomDebugInformation_Parent, &cdiMethodToken) ||
            cdiMethodToken != methodToken)
        {
            md_cursor_move(&cdiCursor, 1);
            continue;
        }

        // Get the Kind column to check if this is async method stepping information
        mdguid_t guid;
        if (!md_get_column_value_as_guid(cdiCursor, mdtCustomDebugInformation_Kind, &guid) ||
            std::memcmp(&guid, asyncMethodSteppingInformation.data(), sizeof(mdguid_t)) != 0)
        {
            md_cursor_move(&cdiCursor, 1);
            continue;
        }

        // Get the async method stepping blob
        // Format: catchHandlerOffset (4 bytes) followed by sequence of:
        //   yieldOffset (4 bytes) + resumeOffset (4 bytes) + kickoffMethodRid (compressed integer)
        uint8_t const *asyncBlob = nullptr;
        uint32_t asyncBlobSize = 0;
        if (!md_get_column_value_as_blob(cdiCursor, mdtCustomDebugInformation_Value, &asyncBlob, &asyncBlobSize) ||
            asyncBlobSize < int32Size)
        {
            return E_FAIL;
        }

        // Read catch handler offset (first 4 bytes)
        catchHandlerOffset = ReadLittleEndianUInt32(asyncBlob, 0);
        asyncBlob += int32Size;
        asyncBlobSize -= int32Size;

        // Parse await info blocks: each entry has yield/resume offsets (8 bytes) + compressed RID
        static constexpr uint32_t awaitEntryFixedSize = int32Size * 2; // yieldOffset + resumeOffset

        awaitInfos.reserve(asyncBlobSize / (awaitEntryFixedSize + 1));

        while (asyncBlobSize > awaitEntryFixedSize) // Need fixed part (8 bytes) + at least 1 byte for compressed RID
        {
            const uint32_t yieldOffset = ReadLittleEndianUInt32(asyncBlob, 0);
            const uint32_t resumeOffset = ReadLittleEndianUInt32(asyncBlob, int32Size);

            // Skip the kickoff method MethodDef RID (compressed integer)
            const uint32_t ridSize = SkipCompressedInteger(asyncBlob, awaitEntryFixedSize);
            if (ridSize == 0 || asyncBlobSize < awaitEntryFixedSize + ridSize)
            {
                return E_FAIL;
            }

            asyncBlob += awaitEntryFixedSize + ridSize;
            asyncBlobSize -= awaitEntryFixedSize + ridSize;

            awaitInfos.emplace_back(yieldOffset, resumeOffset);
        }

        break;
    }

    return awaitInfos.empty() ? E_FAIL : S_OK;
}

HRESULT GetSequencePointByILOffset(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                   PDB::SequencePoint &sequencePoint)
{
    if (pdbHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    sequencePoint.startLine = 0;
    sequencePoint.startColumn = 0;
    sequencePoint.endLine = 0;
    sequencePoint.endColumn = 0;
    sequencePoint.ilOffset = 0;
    sequencePoint.sourceFileIndex = 0;

    // Create cursor to the MethodDebugInformation table
    mdcursor_t mdiCursor{};
    uint32_t mdiCount = 0;
    if (!md_create_cursor(pdbHandle, mdtid_MethodDebugInformation, &mdiCursor, &mdiCount))
    {
        return E_FAIL;
    }

    const uint32_t methodIndex = RidFromToken(methodToken) - 1;
    if (methodIndex >= mdiCount)
    {
        return E_INVALIDARG;
    }

    // Move cursor for requested method
    if (methodIndex != 0)
    {
        md_cursor_move(&mdiCursor, static_cast<int32_t>(methodIndex));
    }

    // Get the SequencePoints blob
    uint8_t const *seqPointsBlob = nullptr;
    uint32_t blobLen = 0;
    if (!md_get_column_value_as_blob(mdiCursor, mdtMethodDebugInformation_SequencePoints, &seqPointsBlob, &blobLen))
    {
        return E_FAIL;
    }

    if (seqPointsBlob == nullptr || blobLen == 0)
    {
        return E_FAIL;
    }

    // First, query the required buffer size
    size_t bufferLen = 0;
    md_blob_parse_result_t result = md_parse_sequence_points(mdiCursor, seqPointsBlob, blobLen, nullptr, &bufferLen);
    if (result != mdbpr_InsufficientBuffer || bufferLen == 0)
    {
        return E_FAIL;
    }

    // Allocate properly aligned buffer and parse sequence points
    // Use aligned operator new to guarantee correct alignment for md_sequence_points_t
    // which contains int64_t and mdcursor_t members requiring 8-byte alignment.
    void *rawBuffer = ::operator new(bufferLen, static_cast<std::align_val_t>(alignof(md_sequence_points_t)));
    SeqPointsPtr seqPoints(static_cast<md_sequence_points_t *>(rawBuffer));
    result = md_parse_sequence_points(mdiCursor, seqPointsBlob, blobLen, seqPoints.get(), &bufferLen);
    if (result != mdbpr_Success)
    {
        return E_FAIL;
    }

    // Document token and index in sourceFiles vector returned by GetAllSourceFiles() method
    mdToken docToken{};
    uint32_t docIndex = 0;

    if (!md_cursor_to_token(seqPoints->document, &docToken))
    {
        // Document might be null for methods without source
        return E_FAIL;
    }
    docIndex = RidFromToken(docToken) - 1;
    bool found = false;

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

        if (record.kind != md_sequence_points_t::record_t::mdsp_SequencePointRecord)
        {
            continue;
        }

        const uint32_t recordIlOffset = record.sequence_point.rolling_il_offset; // NOLINT(cppcoreguidelines-pro-type-union-access)

        if (recordIlOffset > ilOffset)
        {
            break;
        }

        sequencePoint.startLine = static_cast<int32_t>(record.sequence_point.rolling_start_line); // NOLINT(cppcoreguidelines-pro-type-union-access)
        sequencePoint.startColumn = static_cast<int32_t>(record.sequence_point.rolling_start_column); // NOLINT(cppcoreguidelines-pro-type-union-access)
        sequencePoint.endLine = static_cast<int32_t>(record.sequence_point.rolling_start_line + // NOLINT(cppcoreguidelines-pro-type-union-access)
                                                     static_cast<int64_t>(record.sequence_point.delta_lines)); // NOLINT(cppcoreguidelines-pro-type-union-access)
        sequencePoint.endColumn = static_cast<int32_t>(record.sequence_point.rolling_start_column + // NOLINT(cppcoreguidelines-pro-type-union-access)
                                                       record.sequence_point.delta_columns); // NOLINT(cppcoreguidelines-pro-type-union-access)
        sequencePoint.ilOffset = recordIlOffset;
        sequencePoint.sourceFileIndex = docIndex;
        found = true;
    }

    return found ? S_OK : E_FAIL;
}

HRESULT GetNextUserCodeILOffset(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset, uint32_t &ilNextOffset)
{
    if (pdbHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    ilNextOffset = 0;

    // Create cursor to the MethodDebugInformation table
    mdcursor_t mdiCursor{};
    uint32_t mdiCount = 0;
    if (!md_create_cursor(pdbHandle, mdtid_MethodDebugInformation, &mdiCursor, &mdiCount))
    {
        return E_FAIL;
    }

    const uint32_t methodIndex = RidFromToken(methodToken) - 1;
    if (methodIndex >= mdiCount)
    {
        return E_INVALIDARG;
    }

    // Move cursor for requested method
    if (methodIndex != 0)
    {
        md_cursor_move(&mdiCursor, static_cast<int32_t>(methodIndex));
    }

    // Get the SequencePoints blob
    uint8_t const *seqPointsBlob = nullptr;
    uint32_t blobLen = 0;
    if (!md_get_column_value_as_blob(mdiCursor, mdtMethodDebugInformation_SequencePoints, &seqPointsBlob, &blobLen))
    {
        return E_FAIL;
    }

    if (seqPointsBlob == nullptr || blobLen == 0)
    {
        return CORDBG_E_CODE_NOT_AVAILABLE; // No user code found
    }

    // First, query the required buffer size
    size_t bufferLen = 0;
    md_blob_parse_result_t result = md_parse_sequence_points(mdiCursor, seqPointsBlob, blobLen, nullptr, &bufferLen);
    if (result != mdbpr_InsufficientBuffer || bufferLen == 0)
    {
        return E_FAIL;
    }

    // Allocate properly aligned buffer and parse sequence points
    // Use aligned operator new to guarantee correct alignment for md_sequence_points_t
    // which contains int64_t and mdcursor_t members requiring 8-byte alignment.
    void *rawBuffer = ::operator new(bufferLen, static_cast<std::align_val_t>(alignof(md_sequence_points_t)));
    SeqPointsPtr seqPoints(static_cast<md_sequence_points_t *>(rawBuffer));
    result = md_parse_sequence_points(mdiCursor, seqPointsBlob, blobLen, seqPoints.get(), &bufferLen);
    if (result != mdbpr_Success)
    {
        return E_FAIL;
    }

    for (uint32_t j = 0; j < seqPoints->record_count; ++j)
    {
        const auto &record = seqPoints->records[j];

        if (record.kind != md_sequence_points_t::record_t::mdsp_SequencePointRecord)
        {
            continue;
        }

        const uint32_t recordIlOffset = record.sequence_point.rolling_il_offset; // NOLINT(cppcoreguidelines-pro-type-union-access)

        if (recordIlOffset >= ilOffset)
        {
            ilNextOffset = recordIlOffset;
            return S_OK;
        }
    }

    // No user code found after ilOffset
    return CORDBG_E_CODE_NOT_AVAILABLE;
}

HRESULT GetStepRangeFromILOffset(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                 uint32_t &ilStartOffset, uint32_t &ilEndOffset)
{
    if (pdbHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    ilStartOffset = 0;
    ilEndOffset = 0;

    // Create cursor to the MethodDebugInformation table
    mdcursor_t mdiCursor{};
    uint32_t mdiCount = 0;
    if (!md_create_cursor(pdbHandle, mdtid_MethodDebugInformation, &mdiCursor, &mdiCount))
    {
        return E_FAIL;
    }

    const uint32_t methodIndex = RidFromToken(methodToken) - 1;
    if (methodIndex >= mdiCount)
    {
        return E_INVALIDARG;
    }

    // Move cursor for requested method
    if (methodIndex != 0)
    {
        md_cursor_move(&mdiCursor, static_cast<int32_t>(methodIndex));
    }

    // Get the SequencePoints blob
    uint8_t const *seqPointsBlob = nullptr;
    uint32_t blobLen = 0;
    if (!md_get_column_value_as_blob(mdiCursor, mdtMethodDebugInformation_SequencePoints, &seqPointsBlob, &blobLen))
    {
        return E_FAIL;
    }

    if (seqPointsBlob == nullptr || blobLen == 0)
    {
        return E_FAIL;
    }

    // First, query the required buffer size
    size_t bufferLen = 0;
    md_blob_parse_result_t result = md_parse_sequence_points(mdiCursor, seqPointsBlob, blobLen, nullptr, &bufferLen);
    if (result != mdbpr_InsufficientBuffer || bufferLen == 0)
    {
        return E_FAIL;
    }

    // Allocate properly aligned buffer and parse sequence points
    // Use aligned operator new to guarantee correct alignment for md_sequence_points_t
    // which contains int64_t and mdcursor_t members requiring 8-byte alignment.
    void *rawBuffer = ::operator new(bufferLen, static_cast<std::align_val_t>(alignof(md_sequence_points_t)));
    SeqPointsPtr seqPoints(static_cast<md_sequence_points_t *>(rawBuffer));
    result = md_parse_sequence_points(mdiCursor, seqPointsBlob, blobLen, seqPoints.get(), &bufferLen);
    if (result != mdbpr_Success)
    {
        return E_FAIL;
    }

    bool found = false;
    for (uint32_t j = 0; j < seqPoints->record_count; ++j)
    {
        const auto &record = seqPoints->records[j];

        if (record.kind != md_sequence_points_t::record_t::mdsp_SequencePointRecord)
        {
            continue;
        }

        const uint32_t recordIlOffset = record.sequence_point.rolling_il_offset; // NOLINT(cppcoreguidelines-pro-type-union-access)

        if (recordIlOffset <= ilOffset)
        {
            ilStartOffset = recordIlOffset;
            // If returning [recordIlOffset, recordIlOffset] (last sequence point in method), caller should calculate end offset by IL code size
            ilEndOffset = recordIlOffset;
            found = true;
            continue;
        }

        // Could return [0, recordIlOffset] range; this is OK
        ilEndOffset = recordIlOffset;
        found = true;
        break;
    }

    return found ? S_OK : E_FAIL;
}

HRESULT ResolveBreakpoints(mdhandle_t pdbHandle, const std::vector<mdMethodDef> &methodTokens, mdMethodDef nestedMethodToken,
                           uint32_t sourceFileIndex, int32_t sourceLine, std::vector<PDB::ResolvedBreakpoint> &resolvedBreakpoints)
{
    if (pdbHandle == nullptr || methodTokens.empty())
    {
        return E_INVALIDARG;
    }

    resolvedBreakpoints.clear();
    resolvedBreakpoints.reserve(methodTokens.size());

    enum class Position : uint8_t
    {
        First,
        Last
    };

    auto SequencePointForSourceLine = [&](Position reqPos, mdMethodDef methodToken, PDB::SequencePoint &nearestSP) -> HRESULT
    {
        // Create cursor to the MethodDebugInformation table
        mdcursor_t mdiCursor{};
        uint32_t mdiCount = 0;
        if (!md_create_cursor(pdbHandle, mdtid_MethodDebugInformation, &mdiCursor, &mdiCount))
        {
            return E_FAIL;
        }

        const uint32_t methodIndex = RidFromToken(methodToken) - 1;
        if (methodIndex >= mdiCount)
        {
            return E_INVALIDARG;
        }

        // Move cursor for requested method
        if (methodIndex != 0)
        {
            md_cursor_move(&mdiCursor, static_cast<int32_t>(methodIndex));
        }

        // Get the SequencePoints blob
        uint8_t const *seqPointsBlob = nullptr;
        uint32_t blobLen = 0;
        if (!md_get_column_value_as_blob(mdiCursor, mdtMethodDebugInformation_SequencePoints, &seqPointsBlob, &blobLen))
        {
            return E_FAIL;
        }

        if (seqPointsBlob == nullptr || blobLen == 0)
        {
            return E_FAIL;
        }

        // First, query the required buffer size
        size_t bufferLen = 0;
        md_blob_parse_result_t result = md_parse_sequence_points(mdiCursor, seqPointsBlob, blobLen, nullptr, &bufferLen);
        if (result != mdbpr_InsufficientBuffer || bufferLen == 0)
        {
            return E_FAIL;
        }

        // Allocate properly aligned buffer and parse sequence points
        // Use aligned operator new to guarantee correct alignment for md_sequence_points_t
        // which contains int64_t and mdcursor_t members requiring 8-byte alignment.
        void *rawBuffer = ::operator new(bufferLen, static_cast<std::align_val_t>(alignof(md_sequence_points_t)));
        SeqPointsPtr seqPoints(static_cast<md_sequence_points_t *>(rawBuffer));
        result = md_parse_sequence_points(mdiCursor, seqPointsBlob, blobLen, seqPoints.get(), &bufferLen);
        if (result != mdbpr_Success)
        {
            return E_FAIL;
        }

        // Document token and index in sourceFiles vector returned by GetAllSourceFiles() method
        mdToken docToken{};
        uint32_t docIndex = 0;

        if (!md_cursor_to_token(seqPoints->document, &docToken))
        {
            // Document might be null for methods without source
            return E_FAIL;
        }
        docIndex = RidFromToken(docToken) - 1;

        // In case nestedMethodToken + sourceLine is part of a constructor (tokenNum > 1), we could have cases:
        // 1. type FieldName1 = new Type();
        //    void MethodName() {}; type FieldName2 = new Type(); ...  <-- sourceLine
        // 2. type FieldName1 = new Type(); void MethodName() {}; ...  <-- sourceLine
        //    type FieldName2 = new Type();
        // In the first case, we need to set up a breakpoint in nestedMethodToken's method (MethodName in examples above),
        // in the second case - ignore it.

        // In case nestedMethodToken + sourceLine is in a normal method, we could have cases:
        // 1. ... line without code ...                                <-- sourceLine
        //    void MethodName { ...
        // 2. ... line with code ... void MethodName { ...             <-- sourceLine
        // We need to check if nestedMethodToken's method code is closer to sourceLine than code from methodToken's method.
        // If sourceLine is closer to nestedMethodToken's method code - set up a breakpoint in nestedMethodToken's method.

        bool found = false;

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

            if (record.kind != md_sequence_points_t::record_t::mdsp_SequencePointRecord)
            {
                continue;
            }

            const auto endLine = static_cast<int32_t>(record.sequence_point.rolling_start_line + // NOLINT(cppcoreguidelines-pro-type-union-access)
                                                      static_cast<int64_t>(record.sequence_point.delta_lines)); // NOLINT(cppcoreguidelines-pro-type-union-access)
            const auto endColumn = static_cast<int32_t>(record.sequence_point.rolling_start_column + // NOLINT(cppcoreguidelines-pro-type-union-access)
                                                        record.sequence_point.delta_columns); // NOLINT(cppcoreguidelines-pro-type-union-access)

            // Note: in case of constructors, we must care about source too, since we may have a situation when
            // a field/property has the same line in another source.
            if (sourceFileIndex != docIndex ||
                endLine < sourceLine)
            {
                continue;
            }

            if (!found)
            {
                found = true;
            }
            else if (endLine != nearestSP.endLine)
            {
                if ((reqPos != Position::First || endLine >= nearestSP.endLine) &&
                    (reqPos != Position::Last || endLine <= nearestSP.endLine))
                {
                    continue;
                }
            }
            else
            {
                if ((reqPos != Position::First || endColumn >= nearestSP.endColumn) &&
                    (reqPos != Position::Last || endColumn <= nearestSP.endColumn))
                {
                    continue;
                }
            }

            nearestSP.startLine = static_cast<int32_t>(record.sequence_point.rolling_start_line); // NOLINT(cppcoreguidelines-pro-type-union-access)
            nearestSP.startColumn = static_cast<int32_t>(record.sequence_point.rolling_start_column); // NOLINT(cppcoreguidelines-pro-type-union-access)
            nearestSP.endLine = endLine;
            nearestSP.endColumn = endColumn;
            nearestSP.ilOffset = record.sequence_point.rolling_il_offset; // NOLINT(cppcoreguidelines-pro-type-union-access)
            nearestSP.sourceFileIndex = docIndex;
        }

        return S_OK;
    };

    for (const auto &token : methodTokens)
    {
        PDB::SequencePoint currentSP;
        if (FAILED(SequencePointForSourceLine(Position::First, token, currentSP)))
        {
            continue;
        }

        // Note: we don't check whether currentSP was found or not, since we know for sure that sourceLine can be resolved in the method.
        // Same idea for the nested SequencePoint below: if we have nestedMethodToken - it will be resolved for sure.

        if (nestedMethodToken != 0 && nestedMethodToken != mdMethodDefNil)
        {
            // Check if nestedMethodToken is within range of currentSP. Example -
            //     await Parallel.ForEachAsync(userHandlers, parallelOptions, async (uri, token) =>   <- breakpoint at this line
            //     {
            //        await new HttpClient().GetAsync("https://google.com");
            //     });
            // nestedMethodToken here is the anonymous async func, and having a breakpoint at the 1st line should
            // break on the outer call.
            PDB::SequencePoint nestedStartSP;
            PDB::SequencePoint nestedEndSP;
            if (FAILED(SequencePointForSourceLine(Position::First, nestedMethodToken, nestedStartSP)) ||
                FAILED(SequencePointForSourceLine(Position::Last, nestedMethodToken, nestedEndSP)))
            {
                continue;
            }
            if ((nestedStartSP.startLine > currentSP.startLine || (nestedStartSP.startLine == currentSP.startLine && nestedStartSP.startColumn > currentSP.startColumn)) &&
                (nestedEndSP.endLine < currentSP.endLine || (nestedEndSP.endLine == currentSP.endLine && nestedEndSP.endColumn < currentSP.endColumn)))
            {
                resolvedBreakpoints.emplace_back(token, currentSP.startLine, currentSP.endLine, currentSP.ilOffset);
                break;
            }

            // Note: sequence points can't partially overlap each other, since the same lexemes can't belong to 2 different sequence points.
            // In this case, we can check not the "line" (start line - end line data) but only the "point" (end line data) for
            // the current method sequence point and the first nested method sequence point.
            if (currentSP.endLine > nestedStartSP.endLine || (currentSP.endLine == nestedStartSP.endLine && currentSP.endColumn > nestedStartSP.endColumn))
            {
                resolvedBreakpoints.emplace_back(nestedMethodToken, nestedStartSP.startLine, nestedStartSP.endLine, nestedStartSP.ilOffset);
                // When methodTokens.size() > 1, we can have lines added to multiple constructors. In this case, the result will be the same for all methodTokens.
                // We need unique tokens only for breakpoints, to prevent adding nestedMethodToken multiple times.
                break;
            }
        }

        nestedMethodToken = 0; // Don't check nested block in the next cycle (will have the same results).

        resolvedBreakpoints.emplace_back(token, currentSP.startLine, currentSP.endLine, currentSP.ilOffset);
    }

    return resolvedBreakpoints.empty() ? E_FAIL : S_OK;
}

} // namespace dncdbg::PDBReader
