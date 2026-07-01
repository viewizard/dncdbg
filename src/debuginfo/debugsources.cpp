// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/debugsources.h"
#include "utils/hresult.h"
#include "utils/logger.h"
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace dncdbg::DebugSources
{

namespace
{

// Note, we use std::map since we need a container that will not invalidate iterators on adding new elements.
void AddMethodRange(std::map<size_t, std::set<PDB::MethodRange>> &methodRanges,
                    const PDB::MethodRange &entry, const size_t level)
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

    std::list<MethodRangeWithLevel> methodRangeQueue;
    methodRangeQueue.emplace_back(entry, level);

    while (!methodRangeQueue.empty())
    {
        MethodRangeWithLevel currentRange = methodRangeQueue.front();
        methodRangeQueue.pop_front();

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
            methodRangeQueue.emplace_back(currentRange.entry, currentRange.level + 1);
            continue;
        }

        // case with only one element on nested level, NestedInto() was already called and entry checked
        if (it == levelMethodRange.begin())
        {
            levelMethodRange.emplace(currentRange.entry);
            continue;
        }

        // in case these are parts of constructor with same location (for example, `int i = 0;`)
        if (it != levelMethodRange.end() && *it == currentRange.entry && currentRange.entry.isCtor)
        {
            assert(it->isCtor); // also must be part of constructor
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

bool GetMethodTokensByLineNumber(const PDB::MethodRanges &methodBpData, int32_t lineNum, int32_t &correctedLineNum,
                                 std::vector<mdMethodDef> &Tokens, mdMethodDef &closestNestedToken)
{
    const PDB::MethodRange *result = nullptr;
    closestNestedToken = 0;
    correctedLineNum = lineNum;

    for (auto it = methodBpData.cbegin(); it != methodBpData.cend(); ++it)
    {
        auto lower = std::lower_bound(it->cbegin(), it->cend(), correctedLineNum);
        if (lower == it->cend())
        {
            break; // point after last method for this nested level
        }

        // case with first line of method, for example:
        // void Method(){
        //            void Method(){ void Method(){...  <- breakpoint at this line
        if (correctedLineNum == lower->startLine)
        {
            // At this point we can't check this case, let managed part decide (since it sees Columns):
            // void Method() {
            // ... code ...; void Method() {     <- breakpoint at this line
            //  };
            if (result != nullptr)
            {
                if (lower->isCtor) // part of constructor
                {
                    assert(result->isCtor); // also must be part of constructor
                    Tokens.emplace_back(result->methodToken);
                    result = &(*lower);
                    continue; // need check nested level (if available)
                }

                closestNestedToken = lower->methodToken;
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
    ToRelease<IUnknown> trUnknown;
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    ULONG numTypedefs = 0;
    HCORENUM hEnum = nullptr;
    mdTypeDef typeDef = mdTypeDefNil;
    while (SUCCEEDED(trMDImport->EnumTypeDefs(&hEnum, &typeDef, 1, &numTypedefs)) && numTypedefs != 0)
    {
        ULONG numMethods = 0;
        HCORENUM fEnum = nullptr;
        mdMethodDef methodDef = mdMethodDefNil;
        while (SUCCEEDED(trMDImport->EnumMethods(&fEnum, typeDef, &methodDef, 1, &numMethods)) && numMethods != 0)
        {
            ULONG funcNameLen = 0;
            DWORD methodAttr = 0;
            if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, nullptr, 0, &funcNameLen,
                                                  &methodAttr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            static constexpr DWORD ctorMask = mdRTSpecialName | mdSpecialName; // ".ctor", ".cctor" or "Finalize"
            if ((methodAttr & ctorMask) != ctorMask)
            {
                continue;
            }

            WSTRING funcName(funcNameLen, '\0');
            if (FAILED(trMDImport->GetMethodProps(methodDef, nullptr, funcName.data(), funcNameLen, nullptr,
                                                  nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            // Remove null terminator that was included in the length
            if (!funcName.empty() && funcName.back() == '\0')
            {
                funcName.pop_back();
            }

            if (funcName == W(".ctor") || funcName == W(".cctor"))
            {
                constrTokens.emplace(methodDef);
            }
        }
        trMDImport->CloseEnum(fEnum);
    }
    trMDImport->CloseEnum(hEnum);

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

    // Array of tokens for constructors (.ctor/.cctor, that could have segmented code).
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

    for (const auto &[sourceIndex, fileMethodRanges] : pdbMethodRanges)
    {
#ifdef DEBUG_INTERNAL_TESTS
        // Add in reverse for testing AddMethodRange() method to build proper nested levels.
        std::map<size_t, std::set<PDB::MethodRange>> inputMethodRanges;
        for (auto it = fileMethodRanges.rbegin(); it != fileMethodRanges.rend(); ++it)
        {
            const auto &methodRange = *it;
            AddMethodRange(inputMethodRanges, methodRange, 0);
        }
#else
        // Note, don't reorder input data, since it has almost ideal order for us.
        // For example, for Private.CoreLib (about 45518 methods) only 4 relocations were made.
        std::map<size_t, std::set<PDB::MethodRange>> inputMethodRanges;
        for (const auto &methodRange : fileMethodRanges)
        {
            AddMethodRange(inputMethodRanges, methodRange, 0);
        }
#endif // DEBUG_INTERNAL_TESTS

        CompactConstructorRanges(inputMethodRanges);

        PDB::MethodRanges &methodRanges = sourceMethodRanges[sourceIndex];
        methodRanges.resize(inputMethodRanges.size());
        for (uint32_t j = 0; j < inputMethodRanges.size(); ++j)
        {
            methodRanges.at(j).resize(inputMethodRanges.at(j).size());
            std::copy(inputMethodRanges.at(j).begin(), inputMethodRanges.at(j).end(), methodRanges.at(j).begin());
        }
    }

    return S_OK;
}

HRESULT ResolveBreakpoints(const PDBInfo &pdbInfo, uint32_t sourceFileIndex, int sourceLine, std::vector<PDB::ResolvedBreakpoint> &resolvedPoints)
{
    std::vector<mdMethodDef> methodTokens;
    // In case the line doesn't belong to any method, if possible, will be "moved" to the first line of the method below sourceLine.
    int32_t correctedStartLine = 0;
    mdMethodDef closestNestedToken = mdMethodDefNil;
    auto methodRanges = pdbInfo.m_sourceMethodRanges.find(sourceFileIndex);
    if (methodRanges == pdbInfo.m_sourceMethodRanges.end() ||
        !GetMethodTokensByLineNumber(methodRanges->second, sourceLine, correctedStartLine, methodTokens, closestNestedToken))
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
                                            sourceFileIndex, correctedStartLine, resolvedPoints));

    for (auto &entry : resolvedPoints)
    {
        pdbInfo.m_trModule->AddRef();
        entry.trModule = pdbInfo.m_trModule.GetPtr();
    }

    return S_OK;
}

} // namespace dncdbg::DebugSources
