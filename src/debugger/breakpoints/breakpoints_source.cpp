// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/breakpoints_source.h"
#include "debugger/breakpoints/internal_helpers.h"
#include "debugger/evalhelpers.h"
#include "debuginfo/debuginfo.h"
#include "debuginfo/pdb.h"
#include "debuginfo/sourcereference.h"
#include "metadata/helpers.h"
#include "metadata/modules.h"
#include "protocol/dapio.h"
#include "utils/hresult.h"
#include "utils/logger.h"
#include "utils/torelease.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dncdbg::SourceBreakpoints
{

namespace
{

// Hash functor for std::pair<int32_t, int32_t> (line, column) key used in unordered containers.
struct LineColumnHash
{
    std::size_t operator()(const std::pair<int32_t, int32_t> &key) const
    {
        const std::size_t h1 = std::hash<int32_t>{}(key.first);
        const std::size_t h2 = std::hash<int32_t>{}(key.second);
        return h1 ^ (h2 << 1U);
    }
};

bool &GetJustMyCode()
{
    static bool justMyCode{true};
    return justMyCode;
}

std::mutex &GetBreakpointsMutex()
{
    static std::mutex breakpointsMutex;
    return breakpointsMutex;
}

struct ManagedSourceBreakpoint
{
    uint32_t id{0};
    int32_t lineNum{0};
    int32_t columnNum{0};
    int32_t endLine{0};
    int32_t endColumn{0};
    uint32_t hitCount{0};
    std::string hitCondition;
    std::string condition;
    std::string logMessage;
    // Parsed logMessage string, each entry is a pair: <text, isExpression>.
    std::vector<std::pair<std::string, bool>> logMessageParts;
    // In case of a code line in a constructor, we could resolve multiple methods for the breakpoint.
    // For example, `MyType obj = new MyType(1);` will be added to all class constructors.
    std::vector<std::pair<ToRelease<ICorDebugFunctionBreakpoint>, CORDB_ADDRESS>> trFuncBreakpoints;

    [[nodiscard]] bool IsVerified() const
    {
        return !trFuncBreakpoints.empty();
    }

    ManagedSourceBreakpoint() = default;
    ~ManagedSourceBreakpoint();

    void ToBreakpoint(Breakpoint &breakpoint, const std::string &sourceFile, int32_t sourceReference,
                      const std::string *pAlgorithm = nullptr, const std::string *pChecksum = nullptr) const;

    ManagedSourceBreakpoint(ManagedSourceBreakpoint &&) = default;
    ManagedSourceBreakpoint(const ManagedSourceBreakpoint &) = delete;
    ManagedSourceBreakpoint &operator=(ManagedSourceBreakpoint &&) = default;
    ManagedSourceBreakpoint &operator=(const ManagedSourceBreakpoint &) = delete;
};

ManagedSourceBreakpoint::~ManagedSourceBreakpoint()
{
    for (auto &[trFuncBreakpoint, nativeAddress] : trFuncBreakpoints)
    {
        BreakpointHelpers::DeactivateManagedBreakpoint(trFuncBreakpoint);
    }
}

void ManagedSourceBreakpoint::ToBreakpoint(Breakpoint &breakpoint, const std::string &sourceFile, int32_t sourceReference,
                                           const std::string *pAlgorithm, const std::string *pChecksum) const
{
    breakpoint.id = this->id;
    breakpoint.verified = this->IsVerified();
    breakpoint.source = Source(sourceFile, sourceReference);
    if (pAlgorithm != nullptr && pChecksum != nullptr && !(*pAlgorithm).empty() && !(*pChecksum).empty())
    {
        breakpoint.source.checksums.emplace_back(*pAlgorithm, *pChecksum);
    }
    breakpoint.line = this->lineNum;
    breakpoint.column = this->columnNum;
    breakpoint.endLine = this->endLine;
    breakpoint.endColumn = this->endColumn;
}

struct ManagedSourceBreakpointMapping
{
    SourceBreakpoint breakpoint{0, 0};
    uint32_t id{0};
    PDB::GlobalFileIndex resolvedGlobalFileIndex{};
    int32_t sourceReference{0};
    std::vector<Checksum> checksums;
    int32_t resolvedLineNum{0}; // if 0 - no resolved breakpoint available in GetSourceResolvedBreakpoints()
    int32_t resolvedColumnNum{0}; // if 0 - no resolved breakpoint available in GetSourceResolvedBreakpoints()

    void Reset()
    {
        resolvedGlobalFileIndex = PDB::GlobalFileIndex{};
        resolvedLineNum = 0;
        resolvedColumnNum = 0;
    }

    ManagedSourceBreakpointMapping() = default;
    ManagedSourceBreakpointMapping(ManagedSourceBreakpointMapping &&) = default;
    ManagedSourceBreakpointMapping(const ManagedSourceBreakpointMapping &) = delete;
    ManagedSourceBreakpointMapping &operator=(ManagedSourceBreakpointMapping &&) = delete;
    ManagedSourceBreakpointMapping &operator=(const ManagedSourceBreakpointMapping &) = delete;

    ~ManagedSourceBreakpointMapping() = default;
};

// Resolved source breakpoints:
// Mapped for fast search, the mapping data is in the container below:
// resolved global source path index -> resolved (line, column) pair -> list of all ManagedSourceBreakpoint objects resolved to this line:column.
std::unordered_map<PDB::GlobalFileIndex, std::unordered_map<std::pair<int32_t, int32_t>,
                   std::list<ManagedSourceBreakpoint>, LineColumnHash>, PDB::GlobalFileIndexHash> &GetSourceResolvedBreakpoints()
{
    static std::unordered_map<PDB::GlobalFileIndex, std::unordered_map<std::pair<int32_t, int32_t>,
                              std::list<ManagedSourceBreakpoint>, LineColumnHash>, PDB::GlobalFileIndexHash> sourceResolvedBreakpoints;
    return sourceResolvedBreakpoints;
}

// Mapping for the input SourceBreakpoint array (input from protocol) to ManagedSourceBreakpoint or unresolved breakpoint.
// Note, unlike FunctionBreakpoints, for a resolved breakpoint the source path and/or line number could have changed.
// In this way we can connect new input data with previous data and properly add/remove resolved and unresolved breakpoints.
// The container has a structure for fast comparison of the current breakpoint data with the new breakpoint data from the protocol:
// path to source -> list of ManagedSourceBreakpointMapping that includes SourceBreakpoint (from protocol) and resolution-related data.
std::unordered_map<std::string, std::list<ManagedSourceBreakpointMapping>> &GetSourceBreakpointMapping()
{
    static std::unordered_map<std::string, std::list<ManagedSourceBreakpointMapping>> sourceBreakpointMapping;
    return sourceBreakpointMapping;
}

// [in] pModule - optional, provide filter by module during resolve
// [in,out] bp - breakpoint data for resolve
HRESULT ResolveSourceBreakpoint(ICorDebugModule *pModule, const ManagedSourceBreakpoint &bp,
                                const Source &source, std::vector<PDB::ResolvedBreakpoint> &resolvedPoints,
                                PDB::GlobalFileIndex &globalFileIndex)
{
    if (source.path.empty() || bp.lineNum <= 0 || bp.endLine <= 0)
    {
        return E_INVALIDARG;
    }

    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;

    if (pModule != nullptr)
    {
        IfFailRet(pModule->GetBaseAddress(&modAddress));
    }

    IfFailRet(DebugInfo::ResolveBreakpoint(modAddress, source, bp.lineNum, bp.columnNum, &globalFileIndex, resolvedPoints));
    if (resolvedPoints.empty())
    {
        return E_FAIL;
    }

    return S_OK;
}

HRESULT ActivateSourceBreakpoint(ManagedSourceBreakpoint &bp, const std::string &sourcePath,
                                 bool justMyCode, const std::vector<PDB::ResolvedBreakpoint> &resolvedPoints)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    CORDB_ADDRESS modAddressTrack = 0;
    bp.trFuncBreakpoints.reserve(resolvedPoints.size());
    for (const auto &resolvedBP : resolvedPoints)
    {
        // Note, we might have a situation with the same source path in different modules.
        // DAP and the internal debugger routine don't support this case.
        IfFailRet(resolvedBP.trModule->GetBaseAddress(&modAddressTrack));
        if ((modAddress != 0U) && (modAddress != modAddressTrack))
        {
            LOGW(log << "During breakpoint resolve, multiple modules with same source file path were detected.");
            LOGW(log << "File name: " << sourcePath);
            LOGW(log << "Breakpoint activated in module: " << Modules::GetModuleFilePath(resolvedPoints.at(0).trModule));
            LOGW(log << "Ignored module: " << Modules::GetModuleFilePath(resolvedBP.trModule));
            continue;
        }

        IfFailRet(BreakpointHelpers::SkipBreakpoint(resolvedBP.trModule, resolvedBP.methodToken, justMyCode));
        if (Status == S_SKIP)
        {
            continue;
        }

        modAddress = modAddressTrack;
        ToRelease<ICorDebugFunctionBreakpoint> trFuncBreakpoint;
        IfFailRet(BreakpointHelpers::ActivateManagedBreakpoint(modAddress, resolvedBP.methodToken, resolvedBP.ilOffset,
                                                               resolvedBP.trModule, &trFuncBreakpoint));
        CORDB_ADDRESS nativeAddress = 0;
        BreakpointHelpers::GetBreakpointNativeAddress(trFuncBreakpoint, nativeAddress);
        bp.trFuncBreakpoints.emplace_back(trFuncBreakpoint.Detach(), nativeAddress);
    }

    if (modAddress == 0)
    {
        return E_FAIL;
    }

    // No reason to leave extra space here, since a breakpoint could be set up for 1 module only (no more breakpoints will be added).
    bp.trFuncBreakpoints.shrink_to_fit();

    // The same for multiple breakpoint resolution for one module.
    bp.lineNum = resolvedPoints.at(0).startLine;
    bp.columnNum = resolvedPoints.at(0).startColumn;
    bp.endLine = resolvedPoints.at(0).endLine;
    bp.endColumn = resolvedPoints.at(0).endColumn;

    return S_OK;
}

} // unnamed namespace

void SetJustMyCode(bool enable)
{
    GetJustMyCode() = enable;
}

HRESULT CheckBreakpointHit(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                           std::vector<uint32_t> &hitBreakpointIds)
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    HRESULT Status = S_OK;
    ToRelease<ICorDebugFunctionBreakpoint> trFunctionBreakpoint;
    IfFailRet(pBreakpoint->QueryInterface(IID_ICorDebugFunctionBreakpoint, reinterpret_cast<void **>(&trFunctionBreakpoint)));

    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(pThread->GetActiveFrame(&trFrame));
    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    PDB::SequencePoint sp;
    PDB::GlobalFileIndex globalFileIndex;
    IfFailRet(DebugInfo::GetSequencePointByFrame(trFrame, sp, &globalFileIndex));

    const auto breakpoints = GetSourceResolvedBreakpoints().find(globalFileIndex);
    if (breakpoints == GetSourceResolvedBreakpoints().cend())
    {
        return E_FAIL;
    }

    auto &breakpointsInSource = breakpoints->second;
    const auto it = breakpointsInSource.find({sp.startLine, sp.startColumn});
    if (it == breakpointsInSource.cend())
    {
        return S_FALSE; // Stopped at a break, but no breakpoints.
    }

    std::list<ManagedSourceBreakpoint> &bList = it->second;
    if (bList.empty())
    {
        return S_FALSE; // Stopped at a break, but no breakpoints.
    }

    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(trFrame->GetFunctionToken(&methodToken));

    std::string sourceFilePath;
    std::string algorithm;
    std::string checksum;
    DebugInfo::GetSourceFile(globalFileIndex, sourceFilePath, algorithm, checksum);
    int32_t sourceReference = 0;
    SourceReference::GetSourceReference(globalFileIndex, sourceReference, sourceFilePath);

    // Only one source breakpoint is active per line:column pair; iterate the list to find all
    // matching active source breakpoints and add them to hitBreakpointIds.
    for (auto &b : bList)
    {
        for (auto &[trFuncBreakpoint, nativeAddress] : b.trFuncBreakpoints)
        {
            if (FAILED(Status = BreakpointHelpers::IsSameFunctionBreakpoint(trFunctionBreakpoint, trFuncBreakpoint)) ||
                Status == S_FALSE)
            {
                continue;
            }

            CORDB_ADDRESS currentNativeAddress = 0;
            if (SUCCEEDED(BreakpointHelpers::GetBreakpointNativeAddress(trFunctionBreakpoint, currentNativeAddress)) &&
                currentNativeAddress != nativeAddress)
            {
                nativeAddress = currentNativeAddress;
                Breakpoint breakpoint;
                b.ToBreakpoint(breakpoint, sourceFilePath, sourceReference, &algorithm, &checksum);
                breakpoint.instructionReference = MetadataHelpers::AddrToString(nativeAddress);
                DAPIO::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});
            }

            if (!b.condition.empty())
            {
                std::string output;
                if (FAILED(Status = BreakpointHelpers::IsEnableByCondition(pThread, b.condition, output)) ||
                    Status == S_FALSE)
                {
                    continue;
                }

                if (!output.empty())
                {
                    Breakpoint breakpoint;
                    b.ToBreakpoint(breakpoint, sourceFilePath, sourceReference, &algorithm, &checksum);
                    breakpoint.instructionReference = MetadataHelpers::AddrToString(nativeAddress);
                    std::ostringstream ss;
                    ss << "Breakpoint error: The condition for a breakpoint failed to evaluate and will be removed. The condition was '"
                    << b.condition << "'. The error returned was '" << output << "'. - "
                    << sourceFilePath << ':' << b.lineNum << ':' << b.columnNum << "\n";
                    breakpoint.message = ss.str();
                    DAPIO::EmitOutputEvent({OutputCategory::StdErr, breakpoint.message});
                    DAPIO::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});
                    b.condition.clear();
                }
            }

            ++b.hitCount;

            if (!b.hitCondition.empty())
            {
                std::string output;
                std::ostringstream condstream;
                condstream << b.hitCount << ">" << b.hitCondition;
                if (FAILED(Status = BreakpointHelpers::IsEnableByCondition(pThread, condstream.str(), output)) ||
                    Status == S_FALSE)
                {
                    continue;
                }

                if (!output.empty())
                {
                    Breakpoint breakpoint;
                    b.ToBreakpoint(breakpoint, sourceFilePath, sourceReference, &algorithm, &checksum);
                    breakpoint.instructionReference = MetadataHelpers::AddrToString(nativeAddress);
                    std::ostringstream ss;
                    ss << "Breakpoint error: The hitCondition for a breakpoint failed to evaluate and will be removed. The hitCondition was '"
                    << b.hitCondition << "'. The error returned was '" << output << "'. - "
                    << sourceFilePath << ':' << b.lineNum << ':' << b.columnNum << "\n";
                    breakpoint.message = ss.str();
                    DAPIO::EmitOutputEvent({OutputCategory::StdErr, breakpoint.message});
                    DAPIO::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});
                    b.hitCondition.clear();
                }
            }

            if (!b.logMessage.empty())
            {
                if (b.logMessageParts.empty())
                {
                    CreateTextWithEvalParts(b.logMessage, b.logMessageParts);
                }
                std::string message;
                BuildTextWithEval(pThread, nullptr, b.logMessageParts, message);
                message += '\n';
                OutputEvent event(OutputCategory::Console, message);
                event.source = Source(sourceFilePath, sourceReference);
                if (!algorithm.empty() && !checksum.empty())
                {
                    event.source.checksums.emplace_back(algorithm, checksum);
                }
                event.line = b.lineNum;
                event.column = b.columnNum;
                DAPIO::EmitOutputEvent(event);
                continue;
            }

            hitBreakpointIds.emplace_back(b.id);
        }
    }

    return hitBreakpointIds.empty() ? S_FALSE : S_OK; // S_FALSE - stopped at a break, but the breakpoint was not found.
}

HRESULT ManagedCallbackLoadModule(ICorDebugModule *pModule)
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    for (auto &[initialPathToSource, initialBreakpoints] : GetSourceBreakpointMapping())
    {
        for (auto &initialBreakpoint : initialBreakpoints)
        {
            if (initialBreakpoint.resolvedLineNum != 0)
            {
                continue;
            }

            ManagedSourceBreakpoint bp;
            bp.id = initialBreakpoint.id;
            bp.lineNum = initialBreakpoint.breakpoint.line;
            bp.columnNum = initialBreakpoint.breakpoint.column;
            bp.endLine = initialBreakpoint.breakpoint.line;
            bp.endColumn = initialBreakpoint.breakpoint.column;
            bp.condition = initialBreakpoint.breakpoint.condition;
            bp.hitCondition = initialBreakpoint.breakpoint.hitCondition;
            bp.logMessage = initialBreakpoint.breakpoint.logMessage;
            PDB::GlobalFileIndex resolvedGlobalFileIndex;
            std::vector<PDB::ResolvedBreakpoint> resolvedPoints;
            Source source(initialPathToSource, initialBreakpoint.sourceReference);
            source.checksums = initialBreakpoint.checksums;

            if (FAILED(ResolveSourceBreakpoint(pModule, bp, source,
                                               resolvedPoints, resolvedGlobalFileIndex)) ||
                FAILED(ActivateSourceBreakpoint(bp, initialPathToSource, GetJustMyCode(), resolvedPoints)))
            {
                continue;
            }

            std::string resolvedPath;
            std::string algorithm;
            std::string checksum;
            DebugInfo::GetSourceFile(resolvedGlobalFileIndex, resolvedPath, algorithm, checksum);
            int32_t sourceReference = 0;
            SourceReference::GetSourceReference(resolvedGlobalFileIndex, sourceReference, resolvedPath);

            Breakpoint breakpoint;
            bp.ToBreakpoint(breakpoint, resolvedPath, sourceReference, &algorithm, &checksum);
            DAPIO::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});

            initialBreakpoint.resolvedGlobalFileIndex = resolvedGlobalFileIndex;
            initialBreakpoint.resolvedLineNum = bp.lineNum;
            initialBreakpoint.resolvedColumnNum = bp.columnNum;

            GetSourceResolvedBreakpoints()[resolvedGlobalFileIndex][{initialBreakpoint.resolvedLineNum, initialBreakpoint.resolvedColumnNum}].push_back(std::move(bp));
        }
    }

    return S_OK;
}

HRESULT ManagedCallbackUnloadModule(ICorDebugModule *pModule)
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    std::unordered_set<uint32_t> removedIds;

    for (auto fit = GetSourceResolvedBreakpoints().begin(); fit != GetSourceResolvedBreakpoints().end();)
    {
        std::unordered_map<std::pair<int32_t, int32_t>, std::list<ManagedSourceBreakpoint>, LineColumnHash> &fileResolvedBreakpoints = fit->second;

        for (auto lit = fileResolvedBreakpoints.begin(); lit != fileResolvedBreakpoints.end();)
        {
            std::list<ManagedSourceBreakpoint> &lineResolvedBreakpoints = lit->second;

            for (auto it = lineResolvedBreakpoints.begin(); it != lineResolvedBreakpoints.end();)
            {
                ManagedSourceBreakpoint &managedSourceBreakpoint = *it;

                assert(!managedSourceBreakpoint.trFuncBreakpoints.empty());

                CORDB_ADDRESS brModAddress = 0;
                // Check only the first element, see ActivateSourceBreakpoint() code:
                // the debugger doesn't support breakpoints with the same source name in different modules.
                if (FAILED(BreakpointHelpers::GetFunctionBreakpointModAddress(managedSourceBreakpoint.trFuncBreakpoints.at(0).first, brModAddress)) ||
                    modAddress != brModAddress)
                {
                    ++it;
                }
                else
                {
                    removedIds.emplace(managedSourceBreakpoint.id);
                    it = lineResolvedBreakpoints.erase(it);
                }
            }

            if (lineResolvedBreakpoints.empty())
            {
                lit = fileResolvedBreakpoints.erase(lit);
            }
            else
            {
                ++lit;
            }
        }

        if (fileResolvedBreakpoints.empty())
        {
            fit = GetSourceResolvedBreakpoints().erase(fit);
        }
        else
        {
            ++fit;
        }
    }

    // Reset removed resolved breakpoints.
    for (auto &sourceBreakpoints : GetSourceBreakpointMapping())
    {
        for (auto &bp : sourceBreakpoints.second)
        {
            if (removedIds.find(bp.id) != removedIds.cend())
            {
                bp.Reset();

                Breakpoint breakpoint;
                breakpoint.id = bp.id;
                breakpoint.verified = false;
                breakpoint.message = "Breakpoint reset at module unload.";
                DAPIO::EmitBreakpointEvent({BreakpointEventReason::Changed, breakpoint});
            }
        }
    }

    return S_OK;
}

HRESULT SetSourceBreakpoints(bool haveProcess, const Source &source, const std::vector<SourceBreakpoint> &sourceBreakpoints,
                             std::vector<Breakpoint> &breakpoints, const std::function<uint32_t()> &getId)
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    const auto RemoveResolvedByInitialBreakpoint =
        [&](ManagedSourceBreakpointMapping &initialBreakpoint)
        {
            if (initialBreakpoint.resolvedLineNum == 0) // if 0 - no resolved breakpoint available in GetSourceResolvedBreakpoints()
            {
                return S_OK;
            }

            const auto bMap_it = GetSourceResolvedBreakpoints().find(initialBreakpoint.resolvedGlobalFileIndex);
            if (bMap_it == GetSourceResolvedBreakpoints().cend())
            {
                return E_FAIL;
            }

            const auto bList_it = bMap_it->second.find({initialBreakpoint.resolvedLineNum, initialBreakpoint.resolvedColumnNum});
            if (bList_it == bMap_it->second.cend())
            {
                return E_FAIL;
            }

            for (auto itList = bList_it->second.begin(); itList != bList_it->second.end();)
            {
                if ((*itList).id == initialBreakpoint.id)
                {
                    itList = bList_it->second.erase(itList);
                    break;
                }
                else
                {
                    ++itList;
                }
            }

            if (bList_it->second.empty())
            {
                bMap_it->second.erase(bList_it);
            }

            return S_OK;
        };

    HRESULT Status = S_OK;
    if (sourceBreakpoints.empty())
    {
        const auto it = GetSourceBreakpointMapping().find(source.path);
        if (it != GetSourceBreakpointMapping().cend())
        {
            for (auto &initialBreakpoint : it->second)
            {
                Breakpoint breakpoint;
                breakpoint.id = initialBreakpoint.id;
                breakpoint.verified = initialBreakpoint.resolvedLineNum != 0;
                const BreakpointEvent event(BreakpointEventReason::Removed, breakpoint);
                DAPIO::EmitBreakpointEvent(event);

                IfFailRet(RemoveResolvedByInitialBreakpoint(initialBreakpoint));
            }
            GetSourceBreakpointMapping().erase(it);
        }
        return S_OK;
    }

    auto &breakpointsInSource = GetSourceBreakpointMapping()[source.path];
    // Note, unlike before column support was added, an IDE may provide multiple breakpoints
    // on one line (with different columns). Key by the (line, column) pair to distinguish them.
    std::unordered_map<std::pair<int32_t, int32_t>, ManagedSourceBreakpointMapping *, LineColumnHash> breakpointsInSourceMap;

    // Remove old breakpoints
    std::unordered_set<std::pair<int32_t, int32_t>, LineColumnHash> funcBreakpointLinesColumns;
    for (const auto &sb : sourceBreakpoints)
    {
        funcBreakpointLinesColumns.emplace(sb.line, sb.column);
    }
    for (auto it = breakpointsInSource.begin(); it != breakpointsInSource.end();)
    {
        ManagedSourceBreakpointMapping &initialBreakpoint = *it;
        // Note, we don't remove the breakpoint in case `condition`, `hitCondition` or `logMessage` changed,
        // we only change these fields in the resolved breakpoint.
        if (funcBreakpointLinesColumns.find({initialBreakpoint.breakpoint.line, initialBreakpoint.breakpoint.column}) == funcBreakpointLinesColumns.cend())
        {
            Breakpoint breakpoint;
            breakpoint.id = initialBreakpoint.id;
            breakpoint.verified = initialBreakpoint.resolvedLineNum != 0;
            const BreakpointEvent event(BreakpointEventReason::Removed, breakpoint);
            DAPIO::EmitBreakpointEvent(event);

            IfFailRet(RemoveResolvedByInitialBreakpoint(initialBreakpoint));
            it = breakpointsInSource.erase(it);
        }
        else
        {
            assert(breakpointsInSourceMap.find({initialBreakpoint.breakpoint.line, initialBreakpoint.breakpoint.column}) == breakpointsInSourceMap.cend());
            breakpointsInSourceMap.emplace(std::make_pair(initialBreakpoint.breakpoint.line, initialBreakpoint.breakpoint.column), &initialBreakpoint);
            ++it;
        }
    }

    // Note, DAP requires that "sourceBreakpoints" and "functionBreakpoints" must have the same indexes for the same breakpoints.

    for (const auto &sb : sourceBreakpoints)
    {
        const int32_t line = sb.line;
        const int32_t column = sb.column;
        Breakpoint breakpoint;

        const auto b = breakpointsInSourceMap.find({line, column});
        if (b == breakpointsInSourceMap.cend())
        {
            ManagedSourceBreakpointMapping initialBreakpoint;
            initialBreakpoint.breakpoint = sb;
            initialBreakpoint.id = getId();
            initialBreakpoint.sourceReference = source.sourceReference;
            initialBreakpoint.checksums = source.checksums;

            // New breakpoint
            ManagedSourceBreakpoint bp;
            bp.id = initialBreakpoint.id;
            bp.lineNum = line;
            bp.columnNum = column;
            bp.endLine = line;
            bp.endColumn = column;
            bp.condition = initialBreakpoint.breakpoint.condition;
            bp.hitCondition = initialBreakpoint.breakpoint.hitCondition;
            bp.logMessage = initialBreakpoint.breakpoint.logMessage;
            PDB::GlobalFileIndex resolvedGlobalFileIndex;
            std::vector<PDB::ResolvedBreakpoint> resolvedPoints;

            if (haveProcess &&
                SUCCEEDED(ResolveSourceBreakpoint(nullptr, bp, source, resolvedPoints, resolvedGlobalFileIndex)) &&
                SUCCEEDED(ActivateSourceBreakpoint(bp, source.path, GetJustMyCode(), resolvedPoints)))
            {
                initialBreakpoint.resolvedGlobalFileIndex = resolvedGlobalFileIndex;
                initialBreakpoint.resolvedLineNum = bp.lineNum;
                initialBreakpoint.resolvedColumnNum = bp.columnNum;

                std::string resolvedPath;
                std::string algorithm;
                std::string checksum;
                DebugInfo::GetSourceFile(resolvedGlobalFileIndex, resolvedPath, algorithm, checksum);
                int32_t sourceReference = 0;
                SourceReference::GetSourceReference(resolvedGlobalFileIndex, sourceReference, resolvedPath);

                bp.ToBreakpoint(breakpoint, resolvedPath, sourceReference, &algorithm, &checksum);
                GetSourceResolvedBreakpoints()[resolvedGlobalFileIndex][{initialBreakpoint.resolvedLineNum, initialBreakpoint.resolvedColumnNum}].push_back(std::move(bp));
            }
            else
            {
                bp.ToBreakpoint(breakpoint, source.path, source.sourceReference);
                if (!haveProcess)
                {
                    breakpoint.message = "The breakpoint is pending and will be resolved when debugging starts.";
                }
                else
                {
                    breakpoint.message = "The breakpoint will not currently be hit. No symbols have been loaded for this document.";
                }
            }

            breakpointsInSource.push_back(std::move(initialBreakpoint));
        }
        else
        {
            ManagedSourceBreakpointMapping &initialBreakpoint = *b->second;
            initialBreakpoint.breakpoint.condition = sb.condition;
            initialBreakpoint.breakpoint.hitCondition = sb.hitCondition;
            initialBreakpoint.breakpoint.logMessage = sb.logMessage;

            if (initialBreakpoint.resolvedLineNum != 0)
            {
                const auto bMap_it = GetSourceResolvedBreakpoints().find(initialBreakpoint.resolvedGlobalFileIndex);
                if (bMap_it == GetSourceResolvedBreakpoints().cend())
                {
                    return E_FAIL;
                }

                const auto bList_it = bMap_it->second.find({initialBreakpoint.resolvedLineNum, initialBreakpoint.resolvedColumnNum});
                if (bList_it == bMap_it->second.cend())
                {
                    return E_FAIL;
                }

                for (auto &bp : bList_it->second)
                {
                    if (initialBreakpoint.id != bp.id)
                    {
                        continue;
                    }

                    // Existing breakpoint
                    const bool changedCondition = bp.condition != initialBreakpoint.breakpoint.condition;
                    const bool changedHitCondition = bp.hitCondition != initialBreakpoint.breakpoint.hitCondition;
                    const bool changedLogMessage = bp.logMessage != initialBreakpoint.breakpoint.logMessage;
                    bp.condition = initialBreakpoint.breakpoint.condition;
                    bp.hitCondition = initialBreakpoint.breakpoint.hitCondition;
                    bp.logMessage = initialBreakpoint.breakpoint.logMessage;
                    if (changedLogMessage)
                    {
                        bp.logMessageParts.clear();
                    }
                    std::string resolvedPath;
                    std::string algorithm;
                    std::string checksum;
                    DebugInfo::GetSourceFile(initialBreakpoint.resolvedGlobalFileIndex, resolvedPath, algorithm, checksum);
                    int32_t sourceReference = 0;
                    SourceReference::GetSourceReference(initialBreakpoint.resolvedGlobalFileIndex, sourceReference, resolvedPath);

                    bp.ToBreakpoint(breakpoint, resolvedPath, sourceReference, &algorithm, &checksum);
                    if (changedCondition || changedHitCondition || changedLogMessage)
                    {
                        std::string changed;

                        if (changedCondition)
                        {
                            changed = "condition";
                        }
                        if (changedHitCondition)
                        {
                            if (changedCondition)
                            {
                                changed += ", ";
                            }
                            changed += "hitCondition";
                        }
                        if (changedLogMessage)
                        {
                            if (changedCondition || changedHitCondition)
                            {
                                changed += ", ";
                            }
                            changed += "logMessage";
                        }

                        breakpoint.message = "Breakpoint " + changed + " changed.";
                        const BreakpointEvent event(BreakpointEventReason::Changed, breakpoint);
                        DAPIO::EmitBreakpointEvent(event);
                        breakpoint.message.clear();
                    }
                    break;
                }
            }
            else
            {
                // Was already added, but was not yet resolved.
                ManagedSourceBreakpoint bp;
                bp.id = initialBreakpoint.id;
                bp.lineNum = line;
                bp.columnNum = column;
                bp.endLine = line;
                bp.endColumn = column;
                bp.condition = initialBreakpoint.breakpoint.condition;
                bp.hitCondition = initialBreakpoint.breakpoint.hitCondition;
                bp.logMessage = initialBreakpoint.breakpoint.logMessage;
                bp.ToBreakpoint(breakpoint, source.path, source.sourceReference);
                if (!haveProcess)
                {
                    breakpoint.message = "The breakpoint is pending and will be resolved when debugging starts.";
                }
                else
                {
                    breakpoint.message = "The breakpoint will not currently be hit. No symbols have been loaded for this document.";
                }
            }
        }

        breakpoints.push_back(breakpoint);
    }

    return S_OK;
}

#ifdef DEBUG_INTERNAL_TESTS
size_t GetBreakpointsCount()
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    size_t count = 0;

    for (const auto &fileResolvedBreakpoints : GetSourceResolvedBreakpoints())
    {
        for (const auto &lineResolvedBreakpoints : fileResolvedBreakpoints.second)
        {
            for (const auto &managedSourceBreakpoint : lineResolvedBreakpoints.second)
            {
                count += managedSourceBreakpoint.trFuncBreakpoints.size();
            }
        }
    }

    return count;
}
#endif // DEBUG_INTERNAL_TESTS

void Cleanup()
{
    const std::scoped_lock<std::mutex> lock(GetBreakpointsMutex());

    GetSourceResolvedBreakpoints().clear();
    // Reset the resolved breakpoints state.
    for (auto &sourceBreakpoints : GetSourceBreakpointMapping())
    {
        for (auto &bp : sourceBreakpoints.second)
        {
            bp.Reset();
        }
    }
}

} // namespace dncdbg::SourceBreakpoints
