// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/debugsources.h"
#include "metadata/enumerations.h"
#include "utils/hresult.h"
#include "utils/logger.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace dncdbg::DebugSources
{

namespace
{

struct MethodRangeWithLevel
{
    MethodRangeWithLevel(const PDB::MethodRange &entry_, const size_t level_)
        : entry(entry_),
          level(level_)
    {
    }

    PDB::MethodRange entry;
    size_t level;
};

// Work queue for AddMethodRange(). It is owned by the caller, so its storage is reused for every method
// instead of being allocated again on each call.
using MethodRangeQueue = std::vector<MethodRangeWithLevel>;

// Note, we use std::map since we need a container that will not invalidate iterators on adding new elements.
void AddMethodRange(std::map<size_t, std::set<PDB::MethodRange>> &methodRanges,
                    const PDB::MethodRange &entry, const size_t level, MethodRangeQueue &methodRangeQueue)
{
    // Entries are processed in FIFO order; `head` is the first entry that has not been processed yet.
    methodRangeQueue.clear();
    methodRangeQueue.emplace_back(entry, level);

    for (size_t head = 0; head < methodRangeQueue.size(); ++head)
    {
        // Copy the entry, since emplace_back() below may reallocate the queue storage.
        const MethodRangeWithLevel currentRange = methodRangeQueue.at(head);

        // if we are here, we need at least one nested level for sure
        if (methodRanges.empty())
        {
            methodRanges.emplace(0, std::set<PDB::MethodRange>{currentRange.entry});
            continue;
        }
        assert(currentRange.level <= methodRanges.size()); // could be increased only at 1 per recursive call
        if (currentRange.level == methodRanges.size())
        {
            methodRanges.emplace(currentRange.level, std::set<PDB::MethodRange>{currentRange.entry});
            continue;
        }

        auto &levelMethodRange = methodRanges.at(currentRange.level);

        auto it = levelMethodRange.lower_bound(currentRange.entry);
        if (it != levelMethodRange.end() && currentRange.entry.NestedInto(*it))
        {
            // Constructor parts that share the same location (for example, `int i = 0;`) are
            // nested within one another by definition (equal ranges satisfy NestedInto()), so
            // the newly added part goes to the next nested level.
            //
            // Keep the resulting level assignment independent of the input data order: the part
            // with the smaller methodToken must always be placed on the lower nested level.
            if (currentRange.entry.isCtor && it->isCtor &&
                it->startLine == currentRange.entry.startLine &&
                it->startColumn == currentRange.entry.startColumn &&
                it->endLine == currentRange.entry.endLine &&
                it->endColumn == currentRange.entry.endColumn &&
                currentRange.entry.methodToken < it->methodToken)
            {
                // Re-level the part with the larger token starting from the next nested level
                // (where it will displace the next same-location part the same way, if any),
                // then place the part with the smaller token on this level.
                const PDB::MethodRange displaced = *it;
                levelMethodRange.erase(it);
                methodRangeQueue.emplace_back(displaced, currentRange.level + 1);
                levelMethodRange.emplace(currentRange.entry);
                continue;
            }

            methodRangeQueue.emplace_back(currentRange.entry, currentRange.level + 1);
            continue;
        }

        // case with only one element on nested level, NestedInto() was already called and entry checked
        if (it == levelMethodRange.begin())
        {
            levelMethodRange.emplace(currentRange.entry);
            continue;
        }

        // move all previously added nested elements for the new entry to the level above
        while (it != levelMethodRange.begin())
        {
            it = std::prev(it);

            if (it->NestedInto(currentRange.entry))
            {
                const PDB::MethodRange tmp = *it;
                it = levelMethodRange.erase(it);
                methodRangeQueue.emplace_back(tmp, currentRange.level + 1);
            }
            else
            {
                break;
            }
        };

        levelMethodRange.emplace(currentRange.entry);
    }
}

void CompactConstructorRanges(std::map<size_t, std::set<PDB::MethodRange>> &inputMethodRanges)
{
    // Merge constructor parts into ranges on each level.
    // Note: For constructors, the stored initial input ranges represent separate sequence points
    // of the constructor. This is because during PDB data gathering, a sequence point for an
    // individual line (e.g., `int i = 5;`) cannot be distinguished from a constructor sequence point.
    for (auto &[level, methodRanges] : inputMethodRanges)
    {
        auto it = methodRanges.begin();

        while (it != methodRanges.end())
        {
            if (!it->isCtor)
            {
                ++it;
                continue;
            }

            // Extract first constructor to merge into
            auto node = methodRanges.extract(it++);

            // Merge subsequent constructors with same methodToken
            while (it != methodRanges.end() && it->isCtor && it->methodToken == node.value().methodToken)
            {
                node.value().endLine = it->endLine;
                node.value().endColumn = it->endColumn;
                it = methodRanges.erase(it);
            }

            // Insert merged constructor back
            methodRanges.insert(std::move(node));
        }
    }
}

bool GetMethodTokensByLineAndColumn(const PDB::MethodRanges &methodBpData, int32_t lineNum, int32_t columnNum,
                                    int32_t &correctedLineNum, int32_t &correctedColumnNum,
                                    std::vector<mdMethodDef> &Tokens, mdMethodDef &closestNestedToken)
{
    const PDB::MethodRange *result = nullptr;
    closestNestedToken = 0;
    correctedLineNum = lineNum;
    correctedColumnNum = columnNum;

    for (auto it = methodBpData.cbegin(); it != methodBpData.cend(); ++it)
    {
        const auto lower = std::lower_bound(it->cbegin(), it->cend(), correctedLineNum);
        if (lower == it->cend())
        {
            break; // point after last method for this nested level
        }

        // case with first line of method, for example:
        // void Method(){
        //            void Method(){ void Method(){...  <- breakpoint at this line
        if (correctedLineNum == lower->startLine)
        {
            // If two methods start on the same line, use the column to determine which one
            // the breakpoint belongs to:
            // void Method() {
            // ... code ...; void Method() {     <- breakpoint at this line
            //  };
            // When the column is not provided (columnNum == 0), the resolver decides (old behavior).
            if (result != nullptr)
            {
                if (lower->isCtor) // part of constructor
                {
                    assert(result->isCtor); // also must be part of constructor
                    Tokens.emplace_back(result->methodToken);
                    result = &(*lower);
                    continue; // need check nested level (if available)
                }

                // When the outer method is a constructor, prefer the nested method at its
                // first line. The constructor's sequence point starts at a different line
                // (e.g., the constructor declaration), so a breakpoint at the nested method's
                // first line should resolve to the nested method, not the constructor's start.
                if (result->isCtor)
                {
                    result = &(*lower);
                    continue; // need check nested level (if available)
                }

                if (columnNum == 0 || columnNum >= lower->startColumn)
                {
                    closestNestedToken = lower->methodToken;
                }
            }
            else
            {
                result = &(*lower);

                if (lower->isCtor) // part of constructor
                {
                    continue; // need check nested level (if available)
                }
            }
        }
        else if (correctedLineNum > lower->startLine && lower->endLine >= correctedLineNum)
        {
            if (result != nullptr && lower->isCtor) // part of constructor
            {
                assert(result->isCtor); // also must be part of constructor
                Tokens.emplace_back(result->methodToken);
            }

            result = &(*lower);
            continue; // need check nested level (if available)
        }
        // out of first-level method lines - forced move line to first method below, for example:
        //  <-- breakpoint at line without code (out of any methods)
        // void Method() {...}
        else if (it == methodBpData.cbegin() && correctedLineNum < lower->startLine)
        {
            correctedLineNum = lower->startLine;
            correctedColumnNum = lower->startColumn;
            result = &(*lower);

            if (lower->isCtor) // part of constructor
            {
                continue; // need check nested level (if available)
            }
        }
        // result was found on previous loop, check for closest nested method
        // need it in case of breakpoint setup at lines without code and before nested method, for example:
        // {
        //  <-- breakpoint at line without code (inside method)
        //     void Method() {...}
        // }
        else if (result != nullptr && correctedLineNum <= lower->startLine && lower->endLine <= result->endLine)
        {
            closestNestedToken = lower->methodToken;
        }

        break;
    }

    if (result != nullptr)
    {
        Tokens.emplace_back(result->methodToken);
    }

    return (result != nullptr);
}

HRESULT GetModuleConstructors(ICorDebugModule *pModule, std::unordered_set<uint32_t> &constrTokens)
{
    HRESULT Status = S_OK;
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&trMDImport)));

    IfFailRet(MetadataEnumerations::ForEachTypeDef(trMDImport,
        [&](mdTypeDef typeDef) -> HRESULT
        {
            // There can be at most one static constructor per type, so the enumeration stops after the first match.
            MetadataEnumerations::ForEachMethodWithName(trMDImport, W(".cctor"), typeDef,
                [&](mdMethodDef methodDef) -> HRESULT
                {
                    constrTokens.emplace(methodDef);
                    return S_CAN_EXIT; // Stop the enumeration after the first match.
                });

            MetadataEnumerations::ForEachMethodWithName(trMDImport, W(".ctor"), typeDef,
                [&](mdMethodDef methodDef) -> HRESULT
                {
                    constrTokens.emplace(methodDef);
                    return S_OK;
                });

            return S_OK;
        }));

    if (sizeof(std::size_t) > sizeof(uint32_t) && (constrTokens.size() > std::numeric_limits<uint32_t>::max()))
    {
        LOGE(log << "Too big token arrays.");
        return E_FAIL;
    }

    return S_OK;
}

} // unnamed namespace

HRESULT FillMethodRanges(ICorDebugModule *pModule, mdhandle_t pdbHandle, PDB::SourceMethodRanges &sourceMethodRanges)
{
    HRESULT Status = S_OK;

    // Array of tokens for constructors (.ctor/.cctor), which could have segmented code.
    std::unordered_set<uint32_t> constrTokens;
    IfFailRet(GetModuleConstructors(pModule, constrTokens));

    std::unordered_map<uint32_t, std::vector<PDB::MethodRange>> pdbMethodRanges;
    IfFailRet(PDBReader::GetMethodsRanges(pdbHandle, constrTokens, pdbMethodRanges));
    if (pdbMethodRanges.empty())
    {
        return S_OK;
    }

    sourceMethodRanges.clear();
    sourceMethodRanges.reserve(pdbMethodRanges.size());

    // Work queue shared by all AddMethodRange() calls, so its storage is allocated once.
    MethodRangeQueue methodRangeQueue;

    for (auto &[sourceIndex, fileMethodRanges] : pdbMethodRanges)
    {
#ifdef DEBUG_INTERNAL_TESTS
        // Add in reverse for testing the AddMethodRange() method to build proper nested levels.
        std::map<size_t, std::set<PDB::MethodRange>> inputMethodRanges;
        for (auto it = fileMethodRanges.rbegin(); it != fileMethodRanges.rend(); ++it)
        {
            const auto &methodRange = *it;
            AddMethodRange(inputMethodRanges, methodRange, 0, methodRangeQueue);
        }
#else
        // Note, don't reorder input data, since it has almost ideal order for us.
        // For example, for Private.CoreLib (about 45518 methods) only 4 relocations were made.
        std::map<size_t, std::set<PDB::MethodRange>> inputMethodRanges;
        for (const auto &methodRange : fileMethodRanges)
        {
            AddMethodRange(inputMethodRanges, methodRange, 0, methodRangeQueue);
        }
#endif // DEBUG_INTERNAL_TESTS

        // The flat input for this file is no longer needed; release its memory before building the result.
        std::vector<PDB::MethodRange>().swap(fileMethodRanges);

        CompactConstructorRanges(inputMethodRanges);

        PDB::MethodRanges &methodRanges = sourceMethodRanges[sourceIndex];
        methodRanges.reserve(inputMethodRanges.size());
        for (const auto &levelEntry : inputMethodRanges)
        {
            methodRanges.emplace_back(levelEntry.second.cbegin(), levelEntry.second.cend());
        }
    }

    return S_OK;
}

HRESULT ResolveBreakpoints(const PDBInfo &pdbInfo, uint32_t sourceFileIndex, int32_t sourceLine,
                           int32_t sourceColumn, std::vector<PDB::ResolvedBreakpoint> &resolvedPoints,
                           PDBReader::SequencePointsBuffer &seqPointsBuffer)
{
    std::vector<mdMethodDef> methodTokens;
    // In case the line doesn't belong to any method, it will be "moved", if possible, to the first line of the method below sourceLine.
    int32_t correctedStartLine = 0;
    int32_t correctedStartColumn = 0;
    mdMethodDef closestNestedToken = mdMethodDefNil;
    const auto methodRanges = pdbInfo.m_sourceMethodRanges.find(sourceFileIndex);
    if (methodRanges == pdbInfo.m_sourceMethodRanges.cend() ||
        !GetMethodTokensByLineAndColumn(methodRanges->second, sourceLine, sourceColumn, correctedStartLine,
                                        correctedStartColumn, methodTokens, closestNestedToken))
    {
        return E_FAIL;
    }
    if (methodTokens.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
    {
        LOGE(log << "Too big token arrays.");
        return E_FAIL;
    }

    HRESULT Status = S_OK;
    IfFailRet(PDBReader::ResolveBreakpoints(pdbInfo.m_pdbHandle, methodTokens, closestNestedToken,
                                            sourceFileIndex, correctedStartLine, correctedStartColumn, resolvedPoints, seqPointsBuffer));

    for (auto &entry : resolvedPoints)
    {
        pdbInfo.m_trModule->AddRef();
        entry.trModule = pdbInfo.m_trModule.GetPtr();
    }

    return S_OK;
}

HRESULT FindMethodsInRange(const PDBInfo &pdbInfo, uint32_t sourceFileIndex, const BreakpointLocation &rangeToSearch,
                           std::vector<mdMethodDef> &methodTokens)
{
    // Returns `methodTokens` with the tokens of all methods that intersect `rangeToSearch`,
    // for later use in GetBreakpointLocations() (src/debuginfo/pdbreader.cpp).
    methodTokens.clear();

    if (rangeToSearch.line <= 0)
    {
        return E_INVALIDARG;
    }

    const auto methodRanges = pdbInfo.m_sourceMethodRanges.find(sourceFileIndex);
    if (methodRanges == pdbInfo.m_sourceMethodRanges.cend())
    {
        return E_FAIL;
    }

    // Normalize the search range the same way as GetBreakpointLocations() does:
    // when the end of the range is not provided (zero), the range covers the rest of
    // the start line (DAP sends a single line for gutter breakpoints).
    const int32_t rangeStartLine = rangeToSearch.line;
    const int32_t rangeStartColumn = rangeToSearch.column;
    const int32_t rangeEndLine = (rangeToSearch.endLine != 0) ? rangeToSearch.endLine : rangeToSearch.line;
    const int32_t rangeEndColumn =
        (rangeToSearch.endColumn != 0) ? rangeToSearch.endColumn : std::numeric_limits<int32_t>::max();

    // The method ranges are stored per nested level (see FillMethodRanges()); a method
    // could be covered by the range on any level, so check all levels.
    // Ranges on each level are sorted by their end position, so the search starts at the first
    // method that ends at or after the start of the range; the methods before it cannot intersect the range.
    const PDB::MethodRange rangeStart{mdMethodDefNil, 0, rangeStartLine, 0, rangeStartColumn, false};
    for (const auto &levelMethodRanges : methodRanges->second)
    {
        for (auto it = std::lower_bound(levelMethodRanges.cbegin(), levelMethodRanges.cend(), rangeStart);
             it != levelMethodRanges.cend(); ++it)
        {
            const PDB::MethodRange &methodRange = *it;
            // Skip methods that do not intersect the requested range.
            if (methodRange.startLine > rangeEndLine ||
                (methodRange.startLine == rangeEndLine && methodRange.startColumn > rangeEndColumn))
            {
                continue;
            }

            methodTokens.push_back(methodRange.methodToken);
        }
    }

    // A method can be present on several nested levels, so the tokens are made unique.
    std::sort(methodTokens.begin(), methodTokens.end());
    methodTokens.erase(std::unique(methodTokens.begin(), methodTokens.end()), methodTokens.end());

    return S_OK;
}

} // namespace dncdbg::DebugSources
