// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/goto.h"
#include "debugger/evaluation/evalhelpers/metadata.h"
#include "debuginfo/debuginfo.h"
#include "debuginfo/pdbreader.h"
#include "metadata/helpers.h"
#include "utils/hresult.h"

namespace dncdbg::Goto
{

namespace
{

uint32_t &GetGotoTargetId()
{
    static uint32_t gotoTargetId = 0;
    return gotoTargetId;
}

}

HRESULT GetTarget(const Source &source, int32_t line, int32_t column, std::vector<GotoTarget> &targets,
                  std::vector<TargetInternal> &intTargets, std::string &output)
{
    HRESULT Status = S_OK;

    // Reuse breakpoint related logic in order to find MethodToken and Module.
    std::vector<PDB::ResolvedBreakpoint> resolvedPoints;
    IfFailRet(DebugInfo::ResolveBreakpoint(0, source, line, column, nullptr, resolvedPoints));

    uint32_t &gotoTargetId = GetGotoTargetId();

    for (const auto &bp : resolvedPoints)
    {
        CORDB_ADDRESS modAddress = 0;
        IfFailRet(bp.trModule->GetBaseAddress(&modAddress));

        PDB::SequencePoint sequencePoint;
        if (FAILED(DebugInfo::GetPDBInfo(modAddress,
            [&](const PDBInfo &pdbInfo) -> HRESULT
            {
                IfFailRet(PDBReader::GetGotoTarget(pdbInfo.m_pdbHandle, bp.methodToken, line, column,
                                                   sequencePoint, output));
                return S_OK;
            })))
        {
            continue;
        }

        gotoTargetId++;

        targets.emplace_back();
        auto &target = targets.back();
        target.id = gotoTargetId;
        target.line = sequencePoint.startLine;
        target.column = sequencePoint.startColumn;
        target.endLine = sequencePoint.endLine;
        target.endColumn = sequencePoint.endColumn;

        if (FAILED(EvalMetadataHelpers::GetFQDisplayRealCodeMethodName(bp.trModule, bp.methodToken, target.label)))
        {
            target.label = std::to_string(gotoTargetId);
        }

        ToRelease<ICorDebugFunction> trFunction;
        IfFailRet(bp.trModule->GetFunctionFromToken(bp.methodToken, &trFunction));

        CORDB_ADDRESS nativeAddress = 0;
        MetadataHelpers::GetNativeAddress(trFunction, sequencePoint.ilOffset, nativeAddress);
        if (nativeAddress != 0)
        {
            target.instructionPointerReference = MetadataHelpers::AddrToString(nativeAddress);
        }

        intTargets.emplace_back();
        auto &intTarget = intTargets.back();
        intTarget.id = gotoTargetId;
        intTarget.modAddress = modAddress;
        intTarget.methodToken = bp.methodToken;
        intTarget.ilOffset = sequencePoint.ilOffset;
    }

    return targets.empty() ? E_FAIL : S_OK;
}

void Cleanup()
{
    // Don't reset GetGotoTargetId since goto target IDs must remain unique across debug sessions.
}

} // namespace dncdbg::Goto
