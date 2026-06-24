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
#include "managed/interop.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <array>
#include <cstdint>
#include <dnmd.h>
#include <vector>

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

    ResolvedBreakpoint(mdMethodDef method, int32_t start, int32_t end, uint32_t offset)
        : methodToken(method),
          startLine(start),
          endLine(end),
          ilOffset(offset)
    {
    }
};

constexpr uint8_t IDSize = 20;
// PDB ID = GUID (16 bytes) + date/time stamp (4 bytes)
using Identity = std::array<uint8_t, IDSize>;

} // namespace PDB

struct PDBInfo
{
    void *m_symbolReaderHandle = nullptr;
    mdhandle_t m_pdbHandle = nullptr;
    MemoryBuffer m_memBuff;
    ToRelease<ICorDebugModule> m_trModule;

    PDBInfo() = default;
    PDBInfo(void *symHandle, mdhandle_t handle, MemoryBuffer &&memBuff, ICorDebugModule *pModule)
        : m_symbolReaderHandle(symHandle),
          m_pdbHandle(handle),
          m_memBuff(std::move(memBuff)),
          m_trModule(pModule)
    {
    }

    PDBInfo(PDBInfo &&other) noexcept
        : m_symbolReaderHandle(other.m_symbolReaderHandle),
          m_pdbHandle(other.m_pdbHandle),
          m_memBuff(std::move(other.m_memBuff)),
          m_trModule(std::move(other.m_trModule))
    {
        other.m_symbolReaderHandle = nullptr;
        other.m_pdbHandle = nullptr;
    }

    PDBInfo(const PDBInfo &) = delete;
    PDBInfo &operator=(PDBInfo &&) = delete;
    PDBInfo &operator=(const PDBInfo &) = delete;

    ~PDBInfo()
    {
        if (m_symbolReaderHandle != nullptr)
        {
            Interop::DisposeSymbols(m_symbolReaderHandle);
        }

        if (m_pdbHandle != nullptr)
        {
            md_destroy_handle(m_pdbHandle);
        }
    }
};

} // namespace dncdbg

#endif // DEBUGINFO_PDB_H
