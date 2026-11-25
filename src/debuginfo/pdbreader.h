// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_PDBREADER_H
#define DEBUGINFO_PDBREADER_H

#include "types/protocol.h"
#include "debuginfo/pdb.h"
#include "utils/utf.h"
#include <dnmd_pdb.h>
#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dncdbg::PDBReader
{

// Caller-owned, reusable storage for an array of T, such as md_sequence_points_t or md_imports_t (see the
// md_parse_*() functions). The storage is allocated with the alignment of T, which std::vector<uint8_t>
// storage does not guarantee. It grows only when a larger buffer is required (geometrically, to keep the
// number of allocations low) and is released when the object is destroyed, so it can be reused without a
// heap allocation on each call.
template <typename T>
class AlignedBuffer
{
  public:

    // Returns storage for at least `size` bytes. The buffer returned by previous calls is invalidated.
    [[nodiscard]] T *Reserve(size_t size)
    {
        if (size > m_capacity)
        {
            // The old contents are not needed, so the new storage is allocated without copying.
            const size_t grownCapacity = m_capacity + m_capacity / 2;
            const size_t newCapacity = (grownCapacity > size) ? grownCapacity : size;
            void *rawBuffer = ::operator new(newCapacity, static_cast<std::align_val_t>(alignof(T)));
            m_storage.reset(static_cast<T *>(rawBuffer));
            m_capacity = newCapacity;
        }
        return m_storage.get();
    }

  private:

    struct Deleter
    {
        void operator()(T *p) const noexcept
        {
            ::operator delete(p, static_cast<std::align_val_t>(alignof(T)));
        }
    };

    std::unique_ptr<T, Deleter> m_storage;
    size_t m_capacity{0};
};

using SequencePointsBuffer = AlignedBuffer<md_sequence_points_t>;

HRESULT OpenPDB(const std::string &pdbPath, const PDB::Identity &pdbId, MemoryBuffer &memBuffer, mdhandle_t &pdbHandle);
HRESULT GetSourceFile(mdhandle_t pdbHandle, uint32_t sourceFileIndex, std::string &sourceFilePath,
                      std::string &algorithm, std::string &checksum);
HRESULT GetAllSourceFiles(mdhandle_t pdbHandle, PDB::SourceNameMap &sourceFileNameToIndices);
HRESULT GetMethodsRanges(mdhandle_t pdbHandle, const std::unordered_set<mdMethodDef> &constrTokens,
                         std::unordered_map<uint32_t, std::vector<PDB::MethodRange>> &srcMethodRanges);
HRESULT GetLocalConstants(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                          std::vector<PDB::LocalConstant> &localConsts);
HRESULT GetLocalVariableName(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                             uint32_t localVarIndex, WSTRING &localVarName);
bool IsHoistedLocalInScope(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset, uint32_t hoistedLocalIndex);
HRESULT GetAsyncMethodSteppingInfo(mdhandle_t pdbHandle, mdMethodDef methodToken,
                                   std::vector<PDB::AsyncAwaitInfoBlock> &awaitInfos);
HRESULT GetLastIlOffset(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t &lastIlOffset);
HRESULT GetSequencePointByILOffset(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                   PDB::SequencePoint &sequencePoint);
HRESULT GetNextUserCodeILOffset(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset, uint32_t &ilNextOffset);
HRESULT GetStepRangeFromILOffset(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                 uint32_t &ilStartOffset, uint32_t &ilEndOffset);
HRESULT ResolveBreakpoints(mdhandle_t pdbHandle, const std::vector<mdMethodDef> &methodTokens, mdMethodDef nestedMethodToken,
                           uint32_t sourceFileIndex, int32_t sourceLine, int32_t sourceColumn, std::vector<PDB::ResolvedBreakpoint> &resolvedBreakpoints,
                           SequencePointsBuffer &seqPointsBuffer);
HRESULT GetStateMachineMethods(mdhandle_t pdbHandle, std::unordered_map<uint32_t, uint32_t> &moveNextToKickoff,
                               std::unordered_map<uint32_t, uint32_t> &kickoffToMoveNext);
HRESULT GetImportsAndAliases(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                             std::unordered_map<PDB::ImportsKind, std::vector<PDB::Imports>> &pdbImports);
HRESULT GetGotoTarget(mdhandle_t pdbHandle, mdMethodDef methodToken, int32_t line, int32_t column,
                      PDB::SequencePoint &sequencePoint, std::string &output);
HRESULT GetEmbeddedSource(mdhandle_t pdbHandle, uint32_t sourceFileIndex, std::string &sourceContent);
HRESULT ListEmbeddedSources(mdhandle_t pdbHandle, std::vector<std::pair<uint32_t, std::string>> &sourceFileIndexWithName);
HRESULT ListSourceLinkSources(mdhandle_t pdbHandle, std::vector<std::tuple<uint32_t, std::string, std::string>> &sourceFileIndexWithNameAndURL);
HRESULT GetBreakpointLocations(mdhandle_t pdbHandle, const std::vector<mdMethodDef> &methodTokens, uint32_t sourceFileIndex,
                               const BreakpointLocation &rangeToSearch, std::vector<BreakpointLocation> &locations, SequencePointsBuffer &seqPointsBuffer);

} // namespace dncdbg::PDBReader

#endif // DEBUGINFO_PDBREADER_H
