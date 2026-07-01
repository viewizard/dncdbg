// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_PDBREADER_H
#define DEBUGINFO_PDBREADER_H

#include "debuginfo/pdb.h"
#include "utils/utf.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace dncdbg::PDBReader
{

HRESULT OpenPDB(const std::string &pdbPath, const PDB::Identity &pdbId, MemoryBuffer &memBuffer, mdhandle_t &pdbHandle);
HRESULT GetSourceFile(mdhandle_t pdbHandle, uint32_t sourceFileIndex, std::string &sourceFilePath);
HRESULT GetAllSourceFiles(mdhandle_t pdbHandle, PDB::SourceNameMap &sourceFileNameToIndices);
HRESULT GetMethodsRanges(mdhandle_t pdbHandle, const std::unordered_set<mdMethodDef> &constrTokens,
                         std::unordered_map<uint32_t, std::vector<PDB::MethodRange>> &srcMethodRanges);
HRESULT GetLocalConstants(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                          std::vector<PDB::LocalConstant> &localConsts);
HRESULT GetLocalVariableName(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                             uint32_t localVarIndex, WSTRING &localVarName);
bool IsHoistedLocalInScope(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset, uint32_t hoistedLocalIndex);
HRESULT GetAsyncMethodSteppingInfo(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t &catchHandlerOffset,
                                   std::vector<PDB::AsyncAwaitInfoBlock> &awaitInfos);
HRESULT GetLastIlOffset(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t &lastIlOffset);
HRESULT GetSequencePointByILOffset(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                   PDB::SequencePoint &sequencePoint);
HRESULT GetNextUserCodeILOffset(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset, uint32_t &ilNextOffset);
HRESULT GetStepRangeFromILOffset(mdhandle_t pdbHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                 uint32_t &ilStartOffset, uint32_t &ilEndOffset);
HRESULT ResolveBreakpoints(mdhandle_t pdbHandle, const std::vector<mdMethodDef> &methodTokens, mdMethodDef nestedMethodToken,
                           uint32_t sourceFileIndex, int32_t sourceLine, std::vector<PDB::ResolvedBreakpoint> &resolvedBreakpoints);
HRESULT GetStateMachineMethods(mdhandle_t pdbHandle, std::unordered_map<uint32_t, uint32_t> &moveNextToKickoff,
                               std::unordered_map<uint32_t, uint32_t> &kickoffToMoveNext);

} // namespace dncdbg::PDBReader

#endif // DEBUGINFO_PDBREADER_H
