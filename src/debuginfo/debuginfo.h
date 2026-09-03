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
#include "debuginfo/types.h"
#include "types/types.h"
#include "types/protocol.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace dncdbg
{

using ResolveFunctionBreakpointCallback = std::function<HRESULT(ICorDebugModule *, mdMethodDef &)>;

class DebugInfo
{
  public:

    HRESULT ResolveBreakpoint(CORDB_ADDRESS modAddress, const Source &source, int32_t sourceLine, int32_t sourceColumn,
                              PDB::GlobalFileIndex *pGlobalFileIndex, std::vector<PDB::ResolvedBreakpoint> &resolvedPoints);

    using PDBInfoCallback = std::function<HRESULT(const PDBInfo &)>;
    HRESULT GetPDBInfo(CORDB_ADDRESS modAddress, const PDBInfoCallback &cb);

    HRESULT ResolveFunctionBreakpointInAny(const std::string &funcname, const ResolveFunctionBreakpointCallback &cb);

    static HRESULT ResolveFunctionBreakpointInModule(ICorDebugModule *pModule, const std::string &funcname,
                                                     const ResolveFunctionBreakpointCallback &cb);

    HRESULT GetStepRangeFromCurrentIP(ICorDebugThread *pThread, COR_DEBUG_STEP_RANGE &range);

    void TryLoadModuleSymbols(ICorDebugModule *pModule, Module &module);
    void UnloadModuleSymbols(ICorDebugModule *pModule);

    void Cleanup();

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

    HRESULT GetGotoTarget(const Source &source, int32_t line, int32_t column, std::vector<GotoTarget> &targets,
                          std::vector<GotoTargetInternal> &intTargets, std::string &output);

  private:

    std::mutex m_debugInfoMutex;
    std::unordered_map<CORDB_ADDRESS, PDBInfo> m_debugInfo;

    uint32_t m_gotoTargetId = 0;
};

} // namespace dncdbg

#endif // DEBUGINFO_DEBUGINFO_H
