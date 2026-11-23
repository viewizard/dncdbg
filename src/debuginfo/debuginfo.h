// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_DEBUGINFO_H
#define DEBUGINFO_DEBUGINFO_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debuginfo/pdb.h"
#include "types/types.h"
#include "types/protocol.h"
#include "utils/function_ref.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace dncdbg::DebugInfo
{

HRESULT ResolveBreakpoint(CORDB_ADDRESS modAddress, const Source &source, int32_t sourceLine, int32_t sourceColumn,
                          PDB::GlobalFileIndex *pGlobalFileIndex, std::vector<PDB::ResolvedBreakpoint> &resolvedPoints);

using PDBInfoCallback = FunctionRef<HRESULT(const PDBInfo &)>;

HRESULT GetPDBInfoImpl(CORDB_ADDRESS modAddress, PDBInfoCallback cb);
HRESULT GetEachPDBInfoImpl(PDBInfoCallback cb);

// Wraps the callable into a PDBInfoCallback; the implementation is in debuginfo.cpp.
template <typename F>
HRESULT GetPDBInfo(CORDB_ADDRESS modAddress, const F &cb)
{
    return GetPDBInfoImpl(modAddress, PDBInfoCallback(cb));
}

template <typename F>
HRESULT GetEachPDBInfo(const F &cb)
{
    return GetEachPDBInfoImpl(PDBInfoCallback(cb));
}

HRESULT GetStepRangeFromCurrentIP(ICorDebugThread *pThread, COR_DEBUG_STEP_RANGE &range);

void TryLoadModuleSymbols(ICorDebugModule *pModule, Module &module);
void UnloadModuleSymbols(ICorDebugModule *pModule);

HRESULT GetFrameNamedLocalVariable(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset,
                                   uint32_t localIndex, WSTRING &localName);

bool IsHoistedLocalInScope(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset,
                           uint32_t hoistedLocalIndex);

HRESULT GetLocalConstants(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset,
                          std::vector<PDB::LocalConstant> &constants);

HRESULT GetNextUserCodeILOffset(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset, uint32_t &ilNextOffset);
HRESULT GetNextUserCodeILOffset(ICorDebugFrame *pFrame, uint32_t &ilOffset, uint32_t &ilNextOffset);

HRESULT GetSequencePointByILOffset(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t ilOffset,
                                   PDB::SequencePoint &sequencePoint);
HRESULT GetSequencePointByFrame(ICorDebugFrame *pFrame, PDB::SequencePoint &sequencePoint,
                                PDB::GlobalFileIndex *pGlobalFileIndex = nullptr);

HRESULT GetSourceFile(const PDB::GlobalFileIndex &globalFileIndex, std::string &sourceFilePath,
                      std::string &algorithm, std::string &checksum);

bool IsStateMachineKickoffMethod(ICorDebugFunction *pFunction);
HRESULT GetStateMachineKickoffMethod(ICorDebugModule *pModule, mdMethodDef moveNextMethodToken,
                                     mdMethodDef &kickoffMethodToken);

HRESULT GetImportsAndAliases(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset,
                             std::unordered_map<PDB::ImportsKind, std::vector<PDB::Imports>> &pdbImports);

HRESULT GetSourceContent(const Source &source, std::string &sourceContent);

void GetLoadedSources(std::vector<Source> &sources);

HRESULT GetBreakpointLocations(const Source &source, const BreakpointLocation &rangeToSearch,
                               std::vector<BreakpointLocation> &locations);

// Initializes the DebugInfo internal state. See InitializeDebugSession() in manageddebugger.cpp.
void Initialize();
// Cleans up the DebugInfo internal state. See CleanupDebugSession() in manageddebugger.cpp.
void Cleanup();

} // namespace dncdbg::DebugInfo

#endif // DEBUGINFO_DEBUGINFO_H
