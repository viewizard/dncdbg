// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_PDB_H
#define DEBUGINFO_PDB_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "utils/memorybuffer.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <array>
#include <cstdint>
#include <dnmd.h>
#include <forward_list>
#include <vector>
#include <unordered_map>

namespace dncdbg
{

namespace PDB
{

struct MethodRange
{
    mdMethodDef methodToken{mdMethodDefNil};
    int32_t startLine{0};   // first segment/method SequencePoint's startLine
    int32_t endLine{0};     // last segment/method SequencePoint's endLine
    int32_t startColumn{0}; // first segment/method SequencePoint's startColumn
    int32_t endColumn{0};   // last segment/method SequencePoint's endColumn
    bool isCtor{false};     // whether method data is constructor-related

    MethodRange() = default;
    MethodRange(mdMethodDef methodToken_,
                int32_t startLine_,
                int32_t endLine_,
                int32_t startColumn_,
                int32_t endColumn_,
                bool isCtor_)
        : methodToken(methodToken_),
          startLine(startLine_),
          endLine(endLine_),
          startColumn(startColumn_),
          endColumn(endColumn_),
          isCtor(isCtor_)
    {
    }

    bool operator<(const MethodRange &other) const
    {
        return endLine < other.endLine || (endLine == other.endLine && endColumn < other.endColumn);
    }

    bool operator<(const int32_t lineNum) const
    {
        return endLine < lineNum;
    }

    bool operator==(const MethodRange &other) const
    {
        return methodToken == other.methodToken && startLine == other.startLine && endLine == other.endLine &&
               startColumn == other.startColumn && endColumn == other.endColumn;
    }

    [[nodiscard]] bool NestedInto(const MethodRange &other) const
    {
        return (startLine > other.startLine || (startLine == other.startLine && startColumn >= other.startColumn)) &&
               (endLine < other.endLine || (endLine == other.endLine && endColumn <= other.endColumn));
    }
};

struct LocalConstant
{
    WSTRING name;                   // constant name in UTF-16 encoding
    std::vector<uint8_t> signature; // constant signature blob from PDB
};

struct AsyncAwaitInfoBlock
{
    uint32_t yieldOffset{0};  // IL offset where execution yields
    uint32_t resumeOffset{0}; // IL offset where execution resumes

    AsyncAwaitInfoBlock() = default;
    AsyncAwaitInfoBlock(uint32_t yield, uint32_t resume)
        : yieldOffset(yield),
          resumeOffset(resume)
    {
    }
};

struct SequencePoint
{
    int32_t startLine{0};
    int32_t startColumn{0};
    int32_t endLine{0};
    int32_t endColumn{0};
    uint32_t ilOffset{0};
    uint32_t sourceFileIndex{0}; // Same index as returned by GetAllSourceFiles() in sourceFiles
};

struct ResolvedBreakpoint
{
    mdMethodDef methodToken{mdMethodDefNil};
    int32_t startLine{0};
    int32_t endLine{0};
    uint32_t ilOffset{0};
    ToRelease<ICorDebugModule> trModule;

    ResolvedBreakpoint(mdMethodDef method, int32_t start, int32_t end, uint32_t offset)
        : methodToken(method),
          startLine(start),
          endLine(end),
          ilOffset(offset)
    {
    }
};

struct GlobalFileIndex
{
    CORDB_ADDRESS modAddress{0};
    uint32_t sourceFileIndex{0};

    bool operator==(const GlobalFileIndex &other) const
    {
        return modAddress == other.modAddress &&
               sourceFileIndex == other.sourceFileIndex;
    }
};

struct GlobalFileIndexHash
{
    std::size_t operator()(const GlobalFileIndex &key) const
    {
        const std::size_t h1 = std::hash<CORDB_ADDRESS>{}(key.modAddress);
        const std::size_t h2 = std::hash<uint32_t>{}(key.sourceFileIndex);
        // Combine hashes using XOR and bit shifting (similar to boost::hash_combine)
        return h1 ^ (h2 << 1);
    }
};

using SourceNameMap = std::unordered_map<std::string, std::forward_list<uint32_t>>;
// properly ordered arrays of method range on each nested level in one source file
using MethodRanges = std::vector<std::vector<MethodRange>>;
using SourceMethodRanges = std::unordered_map<uint32_t, PDB::MethodRanges>;

constexpr uint8_t IDSize = 20;
// PDB ID = GUID (16 bytes) + date/time stamp (4 bytes)
using Identity = std::array<uint8_t, IDSize>;

} // namespace PDB

struct PDBInfo
{
    mdhandle_t m_pdbHandle = nullptr;
    MemoryBuffer m_memBuff;
    std::vector<uint8_t> m_embeddedPDB;
    ToRelease<ICorDebugModule> m_trModule;
    PDB::SourceNameMap m_sourceFileNameToIndices;
    PDB::SourceMethodRanges m_sourceMethodRanges;

    PDBInfo() = default;
    PDBInfo(mdhandle_t handle, MemoryBuffer &&memBuff, std::vector<uint8_t> &&embeddedPDB, ICorDebugModule *pModule,
            PDB::SourceNameMap &&sourceMap, PDB::SourceMethodRanges &&sourceMethodRanges)
        : m_pdbHandle(handle),
          m_memBuff(std::move(memBuff)),
          m_embeddedPDB(std::move(embeddedPDB)),
          m_trModule(pModule),
          m_sourceFileNameToIndices(std::move(sourceMap)),
          m_sourceMethodRanges(std::move(sourceMethodRanges))
    {
    }

    PDBInfo(PDBInfo &&other) noexcept
        : m_pdbHandle(other.m_pdbHandle),
          m_memBuff(std::move(other.m_memBuff)),
          m_embeddedPDB(std::move(other.m_embeddedPDB)),
          m_trModule(std::move(other.m_trModule)),
          m_sourceFileNameToIndices(std::move(other.m_sourceFileNameToIndices)),
          m_sourceMethodRanges(std::move(other.m_sourceMethodRanges))
    {
        other.m_pdbHandle = nullptr;
    }

    PDBInfo(const PDBInfo &) = delete;
    PDBInfo &operator=(PDBInfo &&) = delete;
    PDBInfo &operator=(const PDBInfo &) = delete;

    ~PDBInfo()
    {
        if (m_pdbHandle != nullptr)
        {
            md_destroy_handle(m_pdbHandle);
        }
    }
};

} // namespace dncdbg

#endif // DEBUGINFO_PDB_H
