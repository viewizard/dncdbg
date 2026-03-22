// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/breakpoints/breakpoints_source.h"
#include "debugger/breakpoints/breakpoints.h"
#include "debugger/breakpoints/breakpointutils.h"
#include "debuginfo/debuginfo.h"
#include "metadata/modules.h"
#include "protocol/dapio.h"
#include "utils/logger.h"
#include <sstream>
#include <unordered_set>

namespace dncdbg
{

namespace
{

HRESULT EnableOneICorBreakpointForLine(std::list<SourceBreakpoints::ManagedSourceBreakpoint> &bList)
{
    // Same logic as provide vsdbg - only one breakpoint is active for one line.
    BOOL needEnable = TRUE;
    HRESULT Status = S_OK;
    for (auto &it : bList)
    {
        if (it.trFuncBreakpoints.empty())
        {
            continue;
        }

        for (const auto &trFuncBreakpoint : it.trFuncBreakpoints)
        {
            const HRESULT ret = trFuncBreakpoint->Activate(needEnable);
            Status = FAILED(ret) ? ret : Status;
        }
        needEnable = FALSE;
    }
    return Status;
}

// [in] pModule - optional, provide filter by module during resolve
// [in,out] bp - breakpoint data for resolve
HRESULT ResolveSourceBreakpoint(DebugInfo *pDebugInfo, ICorDebugModule *pModule, SourceBreakpoints::ManagedSourceBreakpoint &bp,
                                const std::string &bp_fullname, std::vector<DebugInfoSources::resolved_bp_t> &resolvedPoints,
                                unsigned &bp_fullname_index)
{
    if (bp_fullname.empty() || bp.linenum <= 0 || bp.endLine <= 0)
    {
        return E_INVALIDARG;
    }

    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;

    if (pModule != nullptr)
    {
        IfFailRet(pModule->GetBaseAddress(&modAddress));
    }

    IfFailRet(pDebugInfo->ResolveBreakpoint(modAddress, bp_fullname, bp_fullname_index, bp.linenum, resolvedPoints));
    if (resolvedPoints.empty())
    {
        return E_FAIL;
    }

    return S_OK;
}

HRESULT ActivateSourceBreakpoint(SourceBreakpoints::ManagedSourceBreakpoint &bp, const std::string &bp_fullname,
                                 bool justMyCode, const std::vector<DebugInfoSources::resolved_bp_t> &resolvedPoints)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    CORDB_ADDRESS modAddressTrack = 0;
    bp.trFuncBreakpoints.reserve(resolvedPoints.size());
    for (const auto &resolvedBP : resolvedPoints)
    {
        // Note, we might have situation with same source path in different modules.
        // DAP and internal debugger routine don't support this case.
        IfFailRet(resolvedBP.trModule->GetBaseAddress(&modAddressTrack));
        if ((modAddress != 0U) && (modAddress != modAddressTrack))
        {
            LOGW(log << "During breakpoint resolve, multiple modules with same source file path was detected.");
            LOGW(log << "File name: " << bp_fullname.c_str());
            LOGW(log << "Breakpoint activated in module: " << Modules::GetModuleFileName(resolvedPoints[0].trModule).c_str());
            LOGW(log << "Ignored module: " << Modules::GetModuleFileName(resolvedBP.trModule).c_str());
            continue;
        }

        IfFailRet(BreakpointUtils::SkipBreakpoint(resolvedBP.trModule, resolvedBP.methodToken, justMyCode));
        if (Status == S_SKIP)
        {
            continue;
        }

        modAddress = modAddressTrack;

        ToRelease<ICorDebugFunction> trFunc;
        IfFailRet(resolvedBP.trModule->GetFunctionFromToken(resolvedBP.methodToken, &trFunc));
        ToRelease<ICorDebugCode> trCode;
        IfFailRet(trFunc->GetILCode(&trCode));

        ToRelease<ICorDebugFunctionBreakpoint> trFuncBreakpoint;
        IfFailRet(trCode->CreateBreakpoint(resolvedBP.ilOffset, &trFuncBreakpoint));
        IfFailRet(trFuncBreakpoint->Activate(TRUE));

        bp.trFuncBreakpoints.emplace_back(trFuncBreakpoint.Detach());
    }

    if (modAddress == 0)
    {
        return E_FAIL;
    }

    // No reason leave extra space here, since breakpoint could be setup for 1 module only (no more breakpoints will be added).
    bp.trFuncBreakpoints.shrink_to_fit();

    // same for multiple breakpoint resolve for one module
    bp.linenum = resolvedPoints[0].startLine;
    bp.endLine = resolvedPoints[0].endLine;

    return S_OK;
}

} // unnamed namespace

void SourceBreakpoints::ManagedSourceBreakpoint::ToBreakpoint(Breakpoint &breakpoint, const std::string &fullname) const
{
    breakpoint.id = this->id;
    breakpoint.verified = this->IsVerified();
    breakpoint.source = Source(fullname);
    breakpoint.line = this->linenum;
    breakpoint.endLine = this->endLine;
}

void SourceBreakpoints::DeleteAll()
{
    m_breakpointsMutex.lock();
    m_sourceResolvedBreakpoints.clear();
    m_sourceBreakpointMapping.clear();
    m_breakpointsMutex.unlock();
}

HRESULT SourceBreakpoints::CheckBreakpointHit(ICorDebugThread *pThread, ICorDebugBreakpoint *pBreakpoint,
                                              std::vector<uint32_t> &hitBreakpointIds,
                                              std::vector<BreakpointEvent> &bpChangeEvents)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugFunctionBreakpoint> trFunctionBreakpoint;
    IfFailRet(pBreakpoint->QueryInterface(IID_ICorDebugFunctionBreakpoint, reinterpret_cast<void **>(&trFunctionBreakpoint)));

    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(pThread->GetActiveFrame(&trFrame));
    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    uint32_t ilOffset = 0;
    SequencePoint sp;
    IfFailRet(m_sharedDebugInfo->GetFrameILAndSequencePoint(trFrame, ilOffset, sp));

    unsigned filenameIndex = 0;
    IfFailRet(m_sharedDebugInfo->GetIndexBySourceFullPath(sp.document, filenameIndex));

    auto breakpoints = m_sourceResolvedBreakpoints.find(filenameIndex);
    if (breakpoints == m_sourceResolvedBreakpoints.end())
    {
        return E_FAIL;
    }

    auto &breakpointsInSource = breakpoints->second;
    auto it = breakpointsInSource.find(sp.startLine);
    if (it == breakpointsInSource.end())
    {
        return S_FALSE; // Stopped at break, but no breakpoints.
    }

    std::list<ManagedSourceBreakpoint> &bList = it->second;
    if (bList.empty())
    {
        return S_FALSE; // Stopped at break, but no breakpoints.
    }

    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(trFrame->GetFunctionToken(&methodToken));

    // Same logic as provide vsdbg - only one breakpoint is active for one line, find all active in the list and add to hitBreakpointIds.
    for (auto &b : bList)
    {
        for (const auto &trFuncBreakpoint : b.trFuncBreakpoints)
        {
            if (FAILED(Status = BreakpointUtils::IsSameFunctionBreakpoint(trFunctionBreakpoint, trFuncBreakpoint)) ||
                Status == S_FALSE)
            {
                continue;
            }

            if (!b.condition.empty())
            {
                std::string output;
                if (FAILED(Status = BreakpointUtils::IsEnableByCondition(b.condition, m_sharedVariables.get(), pThread, output)) ||
                    Status == S_FALSE)
                {
                    continue;
                }

                if (!output.empty())
                {
                    Breakpoint breakpoint;
                    b.ToBreakpoint(breakpoint, sp.document);
                    std::ostringstream ss;
                    ss << "Breakpoint error: The condition for a breakpoint failed to evaluate and will be removed. The condition was '"
                    << b.condition << "'. The error returned was '" << output << "'. - "
                    << sp.document << ":" << b.linenum << "\n";
                    breakpoint.message = ss.str();
                    bpChangeEvents.emplace_back(BreakpointEventReason::Changed, breakpoint);
                    b.condition.clear();
                }
            }

            ++b.hitCount;

            if (!b.hitCondition.empty())
            {
                std::string output;
                std::ostringstream condstream;
                condstream << b.hitCount << ">" << b.hitCondition;
                if (FAILED(Status = BreakpointUtils::IsEnableByCondition(condstream.str(), m_sharedVariables.get(), pThread, output)) ||
                    Status == S_FALSE)
                {
                    continue;
                }

                if (!output.empty())
                {
                    Breakpoint breakpoint;
                    b.ToBreakpoint(breakpoint, sp.document);
                    std::ostringstream ss;
                    ss << "Breakpoint error: The hitCondition for a breakpoint failed to evaluate and will be removed. The hitCondition was '"
                    << b.hitCondition << "'. The error returned was '" << output << "'. - "
                    << sp.document << ":" << b.linenum << "\n";
                    breakpoint.message = ss.str();
                    bpChangeEvents.emplace_back(BreakpointEventReason::Changed, breakpoint);
                    b.hitCondition.clear();
                }
            }

            hitBreakpointIds.emplace_back(b.id);
        }
    }

    return hitBreakpointIds.empty() ? S_FALSE : S_OK; // S_FALSE - stopped at break, but breakpoint not found.
}

HRESULT SourceBreakpoints::ManagedCallbackLoadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events)
{
    const std::scoped_lock<std::mutex> lock(m_breakpointsMutex);

    for (auto &initialBreakpoints : m_sourceBreakpointMapping)
    {
        for (auto &initialBreakpoint : initialBreakpoints.second)
        {
            if (initialBreakpoint.resolved_linenum != 0)
            {
                continue;
            }

            ManagedSourceBreakpoint bp;
            bp.id = initialBreakpoint.id;
            bp.linenum = initialBreakpoint.breakpoint.line;
            bp.endLine = initialBreakpoint.breakpoint.line;
            bp.condition = initialBreakpoint.breakpoint.condition;
            bp.hitCondition = initialBreakpoint.breakpoint.hitCondition;
            unsigned resolved_fullname_index = 0;
            std::vector<DebugInfoSources::resolved_bp_t> resolvedPoints;

            if (FAILED(ResolveSourceBreakpoint(m_sharedDebugInfo.get(), pModule, bp, initialBreakpoints.first,
                                               resolvedPoints, resolved_fullname_index)) ||
                FAILED(ActivateSourceBreakpoint(bp, initialBreakpoints.first, m_justMyCode, resolvedPoints)))
            {
                continue;
            }

            std::string resolved_fullname;
            m_sharedDebugInfo->GetSourceFullPathByIndex(resolved_fullname_index, resolved_fullname);

            Breakpoint breakpoint;
            bp.ToBreakpoint(breakpoint, resolved_fullname);
            events.emplace_back(BreakpointEventReason::Changed, breakpoint);

            initialBreakpoint.resolved_fullname_index = resolved_fullname_index;
            initialBreakpoint.resolved_linenum = bp.linenum;

            m_sourceResolvedBreakpoints[resolved_fullname_index][initialBreakpoint.resolved_linenum].push_back(std::move(bp));
            EnableOneICorBreakpointForLine(m_sourceResolvedBreakpoints[resolved_fullname_index][initialBreakpoint.resolved_linenum]);
        }
    }

    return S_OK;
}

HRESULT SourceBreakpoints::ManagedCallbackUnloadModule(ICorDebugModule *pModule, std::vector<BreakpointEvent> &events)
{
    const std::scoped_lock<std::mutex> lock(m_breakpointsMutex);

    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    std::unordered_set<uint32_t> removedIds;

    for (auto fit = m_sourceResolvedBreakpoints.begin(); fit != m_sourceResolvedBreakpoints.end();)
    {
        std::unordered_map<int, std::list<ManagedSourceBreakpoint>> &fileResolvedBreakpoints = fit->second;

        for (auto lit = fileResolvedBreakpoints.begin(); lit != fileResolvedBreakpoints.end();)
        {
            std::list<ManagedSourceBreakpoint> &lineResolvedBreakpoints = lit->second;

            for (auto it = lineResolvedBreakpoints.begin(); it != lineResolvedBreakpoints.end();)
            {
                ManagedSourceBreakpoint &managedSourceBreakpoint = *it;

                assert(!managedSourceBreakpoint.trFuncBreakpoints.empty());

                CORDB_ADDRESS brModAddress = 0;
                // Check only first element, see ActivateSourceBreakpoint() code,
                // debugger don't support breakpoint with same source name in different modules.
                if (FAILED(BreakpointUtils::GetFunctionBreakpointModAddress(managedSourceBreakpoint.trFuncBreakpoints[0], brModAddress)) ||
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
            fit = m_sourceResolvedBreakpoints.erase(fit);
        }
        else
        {
            ++fit;
        }
    }

    // Reset removed resolved breakpoints.
    for (auto &sourceBreakpoins : m_sourceBreakpointMapping)
    {
        for (auto &bp : sourceBreakpoins.second)
        {
            if (removedIds.find(bp.id) != removedIds.end())
            {
                bp.resolved_fullname_index = 0;
                bp.resolved_linenum = 0;

                Breakpoint breakpoint;
                breakpoint.id = bp.id;
                breakpoint.verified = false;
                breakpoint.message = "Breakpoint reset at module unload.";
                events.emplace_back(BreakpointEventReason::Changed, breakpoint);
            }
        }
    }

    return S_OK;
}

HRESULT SourceBreakpoints::SetSourceBreakpoints(bool haveProcess, const std::string &filename, const std::vector<SourceBreakpoint> &sourceBreakpoints,
                                                std::vector<Breakpoint> &breakpoints, const std::function<uint32_t()> &getId)
{
    const std::scoped_lock<std::mutex> lock(m_breakpointsMutex);

    auto RemoveResolvedByInitialBreakpoint =
        [&](ManagedSourceBreakpointMapping &initialBreakpoint)
        {
            if (initialBreakpoint.resolved_linenum == 0) // if 0 - no resolved breakpoint available in m_sourceResolvedBreakpoints
            {
                return S_OK;
            }

            auto bMap_it = m_sourceResolvedBreakpoints.find(initialBreakpoint.resolved_fullname_index);
            if (bMap_it == m_sourceResolvedBreakpoints.end())
            {
                return E_FAIL;
            }

            auto bList_it = bMap_it->second.find(initialBreakpoint.resolved_linenum);
            if (bList_it == bMap_it->second.end())
            {
                return E_FAIL;
            }

            for (auto itList = bList_it->second.begin(); itList != bList_it->second.end();)
            {
                if ((*itList).id == initialBreakpoint.id)
                {
                    itList = bList_it->second.erase(itList);
                    EnableOneICorBreakpointForLine(bList_it->second);
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
        auto it = m_sourceBreakpointMapping.find(filename);
        if (it != m_sourceBreakpointMapping.end())
        {
            for (auto &initialBreakpoint : it->second)
            {
                Breakpoint breakpoint;
                breakpoint.id = initialBreakpoint.id;
                breakpoint.verified = initialBreakpoint.resolved_linenum != 0;
                const BreakpointEvent event(BreakpointEventReason::Removed, breakpoint);
                DAPIO::EmitBreakpointEvent(event);

                IfFailRet(RemoveResolvedByInitialBreakpoint(initialBreakpoint));
            }
            m_sourceBreakpointMapping.erase(it);
        }
        return S_OK;
    }

    auto &breakpointsInSource = m_sourceBreakpointMapping[filename];
    std::unordered_map<int, ManagedSourceBreakpointMapping *> breakpointsInSourceMap;

    // Remove old breakpoints
    std::unordered_set<int> funcBreakpointLines;
    for (const auto &sb : sourceBreakpoints)
    {
        funcBreakpointLines.insert(sb.line);
    }
    for (auto it = breakpointsInSource.begin(); it != breakpointsInSource.end();)
    {
        ManagedSourceBreakpointMapping &initialBreakpoint = *it;
        // Note, we don't remove breakpoint in case changed `condition` or `hitCondition`,
        // only change this fields in resolved breakpoint.
        if (funcBreakpointLines.find(initialBreakpoint.breakpoint.line) == funcBreakpointLines.end())
        {
            Breakpoint breakpoint;
            breakpoint.id = initialBreakpoint.id;
            breakpoint.verified = initialBreakpoint.resolved_linenum != 0;
            const BreakpointEvent event(BreakpointEventReason::Removed, breakpoint);
            DAPIO::EmitBreakpointEvent(event);

            IfFailRet(RemoveResolvedByInitialBreakpoint(initialBreakpoint));
            it = breakpointsInSource.erase(it);
        }
        else
        {
            // Note, debugger assume, that IDE can provide only one breakpoint for one line.
            assert(breakpointsInSourceMap.find(initialBreakpoint.breakpoint.line) == breakpointsInSourceMap.end());
            breakpointsInSourceMap[initialBreakpoint.breakpoint.line] = &initialBreakpoint;
            ++it;
        }
    }

    // Note, DAP require, that "sourceBreakpoints" and "functionBreakpoints" must have same indexes for same breakpoints.

    for (const auto &sb : sourceBreakpoints)
    {
        const int line = sb.line;
        Breakpoint breakpoint;

        auto b = breakpointsInSourceMap.find(line);
        if (b == breakpointsInSourceMap.end())
        {
            ManagedSourceBreakpointMapping initialBreakpoint;
            initialBreakpoint.breakpoint = sb;
            initialBreakpoint.id = getId();

            // New breakpoint
            ManagedSourceBreakpoint bp;
            bp.id = initialBreakpoint.id;
            bp.linenum = line;
            bp.endLine = line;
            bp.condition = initialBreakpoint.breakpoint.condition;
            bp.hitCondition = initialBreakpoint.breakpoint.hitCondition;
            unsigned resolved_fullname_index = 0;
            std::vector<DebugInfoSources::resolved_bp_t> resolvedPoints;

            if (haveProcess &&
                SUCCEEDED(ResolveSourceBreakpoint(m_sharedDebugInfo.get(), nullptr, bp, filename, resolvedPoints, resolved_fullname_index)) &&
                SUCCEEDED(ActivateSourceBreakpoint(bp, filename, m_justMyCode, resolvedPoints)))
            {
                initialBreakpoint.resolved_fullname_index = resolved_fullname_index;
                initialBreakpoint.resolved_linenum = bp.linenum;
                std::string resolved_fullname;
                m_sharedDebugInfo->GetSourceFullPathByIndex(resolved_fullname_index, resolved_fullname);
                bp.ToBreakpoint(breakpoint, resolved_fullname);
                m_sourceResolvedBreakpoints[resolved_fullname_index][initialBreakpoint.resolved_linenum].push_back(std::move(bp));
                EnableOneICorBreakpointForLine(m_sourceResolvedBreakpoints[resolved_fullname_index][initialBreakpoint.resolved_linenum]);
            }
            else
            {
                bp.ToBreakpoint(breakpoint, filename);
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

            if (initialBreakpoint.resolved_linenum != 0)
            {
                auto bMap_it = m_sourceResolvedBreakpoints.find(initialBreakpoint.resolved_fullname_index);
                if (bMap_it == m_sourceResolvedBreakpoints.end())
                {
                    return E_FAIL;
                }

                auto bList_it = bMap_it->second.find(initialBreakpoint.resolved_linenum);
                if (bList_it == bMap_it->second.end())
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
                    bp.condition = initialBreakpoint.breakpoint.condition;
                    bp.hitCondition = initialBreakpoint.breakpoint.hitCondition;
                    std::string resolved_fullname;
                    m_sharedDebugInfo->GetSourceFullPathByIndex(initialBreakpoint.resolved_fullname_index, resolved_fullname);
                    bp.ToBreakpoint(breakpoint, resolved_fullname);
                    if (changedCondition || changedHitCondition)
                    {
                        if (changedCondition)
                        {
                            breakpoint.message = "Breakpoint condition changed.";
                        }
                        else
                        {
                            breakpoint.message = "Breakpoint hitCondition changed.";
                        }
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
                bp.linenum = line;
                bp.endLine = line;
                bp.condition = initialBreakpoint.breakpoint.condition;
                bp.hitCondition = initialBreakpoint.breakpoint.hitCondition;
                bp.ToBreakpoint(breakpoint, filename);
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
size_t SourceBreakpoints::GetBreakpointsCount()
{
    const std::scoped_lock<std::mutex> lock(m_breakpointsMutex);

    size_t count = 0;

    for (auto &fileResolvedBreakpoints : m_sourceResolvedBreakpoints)
    {
        for (auto &lineResolvedBreakpoints : fileResolvedBreakpoints.second)
        {
            for (auto &managedSourceBreakpoint : lineResolvedBreakpoints.second)
            {
                count += managedSourceBreakpoint.trFuncBreakpoints.size();
            }
        }
    }

    return count;
}
#endif // DEBUG_INTERNAL_TESTS

} // namespace dncdbg
