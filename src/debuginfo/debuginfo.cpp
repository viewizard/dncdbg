// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/debuginfo.h"
#include "debuginfo/debugsources.h"
#include "debuginfo/pdbreader.h"
#include "metadata/modules.h"
#include "metadata/typeprinter.h"
#include "protocol/dapio.h"
#include "utils/filesystem.h"
#include "utils/hresult.h"
#include "utils/utftoupper.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace dncdbg
{

namespace
{

bool IsTargetFunction(const std::vector<std::string> &fullName, const std::vector<std::string> &targetName)
{
    // Function should be matched by substring, i.e. received target function name should fully or partly equal with the
    // real function name. For example:
    //
    // "MethodA" matches
    // Program.ClassA.MethodA
    // Program.ClassB.MethodA
    // Program.ClassA.InnerClass.MethodA
    //
    // "ClassA.MethodB" matches
    // Program.ClassA.MethodB
    // Program.ClassB.ClassA.MethodB

    auto fullIt = fullName.rbegin();
    for (auto it = targetName.rbegin(); it != targetName.rend(); it++)
    {
        if (fullIt == fullName.rend() || *it != *fullIt)
        {
            return false;
        }

        fullIt++;
    }

    return true;
}

HRESULT ForEachMethod(ICorDebugModule *pModule, const std::function<bool(const std::string &, mdMethodDef &)> &functor)
{
    HRESULT Status = S_OK;
    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    ULONG typesCnt = 0;
    HCORENUM fTypeEnum = nullptr;
    mdTypeDef mdType = mdTypeDefNil;

    while (SUCCEEDED(trMDImport->EnumTypeDefs(&fTypeEnum, &mdType, 1, &typesCnt)) && typesCnt != 0)
    {
        std::string typeName;
        IfFailRet(TypePrinter::NameForToken(mdType, trMDImport, typeName, false, nullptr));

        HCORENUM fFuncEnum = nullptr;
        mdMethodDef mdMethod = mdMethodDefNil;
        ULONG methodsCnt = 0;

        while (SUCCEEDED(trMDImport->EnumMethods(&fFuncEnum, mdType, &mdMethod, 1, &methodsCnt)) && methodsCnt != 0)
        {
            ULONG nameLen = 0;
            if (FAILED(trMDImport->GetMethodProps(mdMethod, nullptr, nullptr, 0, &nameLen,
                                                  nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            mdTypeDef memTypeDef = mdTypeDefNil;
            std::vector<WCHAR> szFuncName(nameLen, '\0');
            if (FAILED(trMDImport->GetMethodProps(mdMethod, &memTypeDef, szFuncName.data(), nameLen, nullptr,
                                                  nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            // Get generic types
            ToRelease<IMetaDataImport2> trMDImport2;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport2, reinterpret_cast<void **>(&trMDImport2)));

            HCORENUM fGenEnum = nullptr;
            mdGenericParam gp = mdGenericParamNil;
            ULONG fetched = 0;
            std::string genParams;

            while (SUCCEEDED(trMDImport2->EnumGenericParams(&fGenEnum, mdMethod, &gp, 1, &fetched)) && fetched == 1)
            {
                ULONG genNameLen = 0;
                if (FAILED(trMDImport2->GetGenericParamProps(gp, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &genNameLen)))
                {
                    continue;
                }

                mdMethodDef memMethodDef = mdMethodDefNil;
                std::vector<WCHAR> szGenName(genNameLen, '\0');
                if (FAILED(trMDImport2->GetGenericParamProps(gp, nullptr, nullptr, &memMethodDef, nullptr,
                                                             szGenName.data(), genNameLen, nullptr)))
                {
                    continue;
                }

                // Add comma for each element. The last one will be stripped later.
                genParams += to_utf8(szGenName.data()) + ",";
            }

            trMDImport2->CloseEnum(fGenEnum);

            std::string fullName = to_utf8(szFuncName.data());
            if (!genParams.empty())
            {
                // Last symbol is comma and it is useless, so remove
                genParams.pop_back();
                fullName += "<" + genParams + ">";
            }

            fullName.insert(0, typeName + '.');
            if (!functor(fullName, mdMethod))
            {
                trMDImport->CloseEnum(fFuncEnum);
                trMDImport->CloseEnum(fTypeEnum);
                return E_FAIL;
            }
        }

        trMDImport->CloseEnum(fFuncEnum);
    }
    trMDImport->CloseEnum(fTypeEnum);

    return S_OK;
}

std::vector<std::string> split_on_tokens(const std::string &str, const char delim)
{
    std::vector<std::string> res;
    size_t prev = 0;

    while (true)
    {
        const size_t pos = str.find(delim, prev);
        if (pos == std::string::npos)
        {
            res.emplace_back(str, prev);
            break;
        }

        res.emplace_back(str, prev, pos - prev);
        prev = pos + 1;
    }

    return res;
}

HRESULT ResolveMethodInModule(ICorDebugModule *pModule, const std::string &funcName, const ResolveFunctionBreakpointCallback &cb)
{
    std::vector<std::string> splitName = split_on_tokens(funcName, '.');

    auto functor = [&](const std::string &fullName, mdMethodDef &mdMethod) -> bool
        {
            const std::vector<std::string> splitFullName = split_on_tokens(fullName, '.');

            // If we've found the target function
            if (IsTargetFunction(splitFullName, splitName))
            {
                if (FAILED(cb(pModule, mdMethod)))
                {
                    return false; // abort operation
                }
            }

            return true; // continue for other functions with matching name
        };

    return ForEachMethod(pModule, functor);
}

HRESULT LoadPDB(ICorDebugModule *pModule, mdhandle_t &pdbHandle, MemoryBuffer &memBuff, std::string &pdbFilePath, std::vector<uint8_t> &embeddedPDB)
{
    HRESULT Status = S_OK;
    PDB::Identity pdbId;
    IfFailRet(Modules::GetModulePdbInfo(pModule, pdbId, pdbFilePath, embeddedPDB));

    if (!embeddedPDB.empty())
    {
        return md_create_handle(embeddedPDB.data(), static_cast<uint32_t>(embeddedPDB.size()), &pdbHandle) ? S_OK : E_FAIL;
    }

    if (SUCCEEDED(PDBReader::OpenPDB(pdbFilePath, pdbId, memBuff, pdbHandle)))
    {
        return S_OK;
    }

    const std::string pdbFileName = GetFileName(pdbFilePath);
    const std::string modulePath = GetParentPath(Modules::GetModuleFilePath(pModule));
    pdbFilePath = modulePath + pdbFileName;

    if (SUCCEEDED(PDBReader::OpenPDB(pdbFilePath, pdbId, memBuff, pdbHandle)))
    {
        return S_OK;
    }

    const std::string dncdbgPath = GetParentPath(GetExeAbsPath());
    pdbFilePath = dncdbgPath + pdbFileName;

    if (SUCCEEDED(PDBReader::OpenPDB(pdbFilePath, pdbId, memBuff, pdbHandle)))
    {
        return S_OK;
    }

    pdbFilePath.clear();
    return COR_E_FILENOTFOUND;
}

std::string CanonicalizeFilePath(const std::string &filePath)
{
    std::string result = filePath;

    // Handle all "./" and "../".
    std::list<std::string> pathDirs;
    std::size_t i = 0;
    while ((i = result.find_first_of("/\\")) != std::string::npos)
    {
        const std::string pathElement = result.substr(0, i);
        if (pathElement == "..")
        {
            if (!pathDirs.empty())
            {
                pathDirs.pop_front();
            }
        }
        else if (pathElement != ".")
        {
            pathDirs.push_front(pathElement);
        }

        result = result.substr(i + 1);
    }

    // Set '/' as delimiter, BinaryPredicate in ResolveBreakpoint method will check both delimiters.
    for (const auto &dir : pathDirs)
    {
        result.insert(0, dir + '/');
    }

    return result;
}

} // unnamed namespace

void DebugInfo::Cleanup()
{
    const std::scoped_lock<std::mutex> lock(m_debugInfoMutex);
    m_debugInfo.clear();
}

HRESULT DebugInfo::GetPDBInfo(CORDB_ADDRESS modAddress, const PDBInfoCallback &cb)
{
    const std::scoped_lock<std::mutex> lock(m_debugInfoMutex);
    auto infoPair = m_debugInfo.find(modAddress);
    return (infoPair == m_debugInfo.end()) ? E_FAIL : cb(infoPair->second);
}

HRESULT DebugInfo::ResolveFunctionBreakpointInAny(const std::string &funcname, const ResolveFunctionBreakpointCallback &cb)
{
    const std::scoped_lock<std::mutex> lock(m_debugInfoMutex);

    for (const auto &[modAddr, pdbInfo] : m_debugInfo)
    {
        ResolveMethodInModule(pdbInfo.m_trModule, funcname, cb);
    }

    return S_OK;
}

HRESULT DebugInfo::ResolveFunctionBreakpointInModule(ICorDebugModule *pModule, const std::string &funcname,
                                                     const ResolveFunctionBreakpointCallback &cb)
{
    return ResolveMethodInModule(pModule, funcname, cb);
}

HRESULT DebugInfo::GetStepRangeFromCurrentIP(ICorDebugThread *pThread, COR_DEBUG_STEP_RANGE &range)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(pThread->GetActiveFrame(&trFrame));
    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(trFrame->GetFunctionToken(&methodToken));

    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(trFrame->GetFunction(&trFunc));

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunc->GetModule(&trModule));

    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

    uint32_t ilOffset = 0;
    CorDebugMappingResult mappingResult = MAPPING_NO_INFO;
    IfFailRet(trILFrame->GetIP(&ilOffset, &mappingResult));
    if (mappingResult == MAPPING_UNMAPPED_ADDRESS ||
        mappingResult == MAPPING_NO_INFO)
    {
        return E_FAIL;
    }

    CORDB_ADDRESS modAddress = 0;
    IfFailRet(trModule->GetBaseAddress(&modAddress));

    uint32_t ilStartOffset = 0;
    uint32_t ilEndOffset = 0;

    IfFailRet(GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            return PDBReader::GetStepRangeFromILOffset(pdbInfo.m_pdbHandle, methodToken, ilOffset, ilStartOffset, ilEndOffset);
        }));

    if (ilStartOffset == ilEndOffset)
    {
        ToRelease<ICorDebugCode> trCode;
        IfFailRet(trFunc->GetILCode(&trCode));
        IfFailRet(trCode->GetSize(&ilEndOffset));
    }

    range.startOffset = ilStartOffset;
    range.endOffset = ilEndOffset;

    return S_OK;
}

void DebugInfo::TryLoadModuleSymbols(ICorDebugModule *pModule, Module &module)
{
    mdhandle_t pdbHandle = nullptr;
    MemoryBuffer memBuff;
    std::vector<uint8_t> embeddedPDB;
    const HRESULT Status = LoadPDB(pModule, pdbHandle, memBuff, module.symbolFilePath, embeddedPDB);
    module.symbolStatus = SUCCEEDED(Status) ? SymbolStatus::Loaded : SymbolStatus::NotFound;

    if (module.symbolStatus == SymbolStatus::Loaded)
    {
        PDB::SourceNameMap sourceFileNameToIndicesMap;
        if (FAILED(PDBReader::GetAllSourceFiles(pdbHandle, sourceFileNameToIndicesMap)))
        {
            DAPIO::EmitOutputEvent({OutputCategory::StdErr,
                "Could not load source file names related info from PDB file.\n"});
        }

        PDB::SourceMethodRanges sourceMethodRanges;
        if (FAILED(DebugSources::FillMethodRanges(pModule, pdbHandle, sourceMethodRanges)))
        {
            DAPIO::EmitOutputEvent({OutputCategory::StdErr,
                "Could not load source lines related info from PDB file. Could produce failures during "
                "breakpoint's source path resolve in future.\n"});
        }

        std::unordered_map<uint32_t, uint32_t> moveNextToKickoff;
        std::unordered_map<uint32_t, uint32_t> kickoffToMoveNext;
        PDBReader::GetStateMachineMethods(pdbHandle, moveNextToKickoff, kickoffToMoveNext);

        CORDB_ADDRESS baseAddress = 0;
        if (SUCCEEDED(pModule->GetBaseAddress(&baseAddress)))
        {
            pModule->AddRef();
            PDBInfo pdbInfo{pdbHandle, std::move(memBuff), std::move(embeddedPDB), pModule,
                            std::move(sourceFileNameToIndicesMap), std::move(sourceMethodRanges),
                            std::move(moveNextToKickoff), std::move(kickoffToMoveNext)};
            const std::scoped_lock<std::mutex> lock(m_debugInfoMutex);
            m_debugInfo.insert(std::make_pair(baseAddress, std::move(pdbInfo)));
        }
        else
        {
            DAPIO::EmitOutputEvent({OutputCategory::StdErr, "Could not find module base address.\n"});
        }
    }
}

void DebugInfo::UnloadModuleSymbols(ICorDebugModule *pModule)
{
    CORDB_ADDRESS baseAddress = 0;
    if (SUCCEEDED(pModule->GetBaseAddress(&baseAddress)))
    {
        const std::scoped_lock<std::mutex> lock(m_debugInfoMutex);
        m_debugInfo.erase(baseAddress);
    }
}

HRESULT DebugInfo::GetFrameNamedLocalVariable(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset,
                                              uint32_t localIndex, WSTRING &localName)
{
    HRESULT Status = S_OK;

    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    IfFailRet(GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            return PDBReader::GetLocalVariableName(pdbInfo.m_pdbHandle, methodToken, ilOffset, localIndex, localName);
        }));

    return S_OK;
}

bool DebugInfo::IsHoistedLocalInScope(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset, uint32_t hoistedLocalIndex)
{
    CORDB_ADDRESS modAddress = 0;
    if (FAILED(pModule->GetBaseAddress(&modAddress)))
    {
        return true; // Fail-open: show variable if we can't check scope
    }

    bool result = true; // Default to showing variable (fail-open)
    GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            result = PDBReader::IsHoistedLocalInScope(pdbInfo.m_pdbHandle, methodToken, ilOffset, hoistedLocalIndex);
            return S_OK;
        });

    return result;
}

HRESULT DebugInfo::GetNextUserCodeILOffset(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset, uint32_t &ilNextOffset)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    return GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            return PDBReader::GetNextUserCodeILOffset(pdbInfo.m_pdbHandle, methodToken, ilOffset, ilNextOffset);
        });
}

HRESULT DebugInfo::GetNextUserCodeILOffset(ICorDebugFrame *pFrame, uint32_t &ilOffset, uint32_t &ilNextOffset)
{
    HRESULT Status = S_OK;

    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(pFrame->GetFunctionToken(&methodToken));

    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(pFrame->GetFunction(&trFunc));

    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

    CorDebugMappingResult mappingResult = MAPPING_NO_INFO;
    IfFailRet(trILFrame->GetIP(&ilOffset, &mappingResult));
    if (mappingResult == MAPPING_UNMAPPED_ADDRESS ||
        mappingResult == MAPPING_NO_INFO)
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunc->GetModule(&trModule));

    return GetNextUserCodeILOffset(trModule, methodToken, ilOffset, ilNextOffset);
}

HRESULT DebugInfo::GetSourceFile(const PDB::GlobalFileIndex &globalFileIndex, std::string &sourceFilePath)
{
    return GetPDBInfo(globalFileIndex.modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            return PDBReader::GetSourceFile(pdbInfo.m_pdbHandle, globalFileIndex.sourceFileIndex, sourceFilePath);
        });
}

HRESULT DebugInfo::GetSequencePointByILOffset(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t ilOffset,
                                              PDB::SequencePoint &sequencePoint)
{
    return GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            return PDBReader::GetSequencePointByILOffset(pdbInfo.m_pdbHandle, methodToken, ilOffset, sequencePoint);
        });
}

HRESULT DebugInfo::GetSequencePointByFrame(ICorDebugFrame *pFrame, PDB::SequencePoint &sequencePoint,
                                           PDB::GlobalFileIndex *pGlobalFileIndex)
{
    HRESULT Status = S_OK;

    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(pFrame->GetFunctionToken(&methodToken));

    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(pFrame->GetFunction(&trFunc));

    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

    uint32_t ilOffset = 0;
    CorDebugMappingResult mappingResult = MAPPING_NO_INFO;
    IfFailRet(trILFrame->GetIP(&ilOffset, &mappingResult));
    if (mappingResult == MAPPING_UNMAPPED_ADDRESS ||
        mappingResult == MAPPING_NO_INFO)
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunc->GetModule(&trModule));

    CORDB_ADDRESS modAddress = 0;
    IfFailRet(trModule->GetBaseAddress(&modAddress));

    IfFailRet(GetSequencePointByILOffset(modAddress, methodToken, ilOffset, sequencePoint));

    if (pGlobalFileIndex != nullptr)
    {
        pGlobalFileIndex->modAddress = modAddress;
        pGlobalFileIndex->sourceFileIndex = sequencePoint.sourceFileIndex;
    }
    return S_OK;
}

HRESULT DebugInfo::ResolveBreakpoint(CORDB_ADDRESS modAddress, const std::string &filePath,
                                     int sourceLine, PDB::GlobalFileIndex &globalFileIndex,
                                     std::vector<PDB::ResolvedBreakpoint> &resolvedPoints)
{
#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
    std::string fixedFilePath = to_uppercase(filePath);
#else
    std::string fixedFilePath = filePath;
#endif

    const std::scoped_lock<std::mutex> lockDebugInfoInfo(m_debugInfoMutex);

    const std::string pathName = GetFileName(fixedFilePath);
    std::map<CORDB_ADDRESS, std::forward_list<uint32_t>> foundSourceIndices;

    auto addSourceIndices = [&](CORDB_ADDRESS modAddr, const PDBInfo &pdbInfo) -> void
    {
            auto findName = pdbInfo.m_sourceFileNameToIndices.find(pathName);
            if (findName == pdbInfo.m_sourceFileNameToIndices.end())
            {
                return;
            }
            foundSourceIndices[modAddr].insert_after(foundSourceIndices[modAddr].before_begin(),
                                                     findName->second.begin(), findName->second.end());
    };

    if (modAddress != 0)
    {
        auto infoPair = m_debugInfo.find(modAddress);
        if (infoPair != m_debugInfo.end())
        {
            const PDBInfo &pdbInfo = infoPair->second;
            addSourceIndices(modAddress, pdbInfo);
        };
    }
    else
    {
        for (const auto &[modAddr, pdbInfo] : m_debugInfo)
        {
            addSourceIndices(modAddr, pdbInfo);
        }
    }

    if (foundSourceIndices.empty())
    {
        return E_FAIL;
    }

    fixedFilePath = CanonicalizeFilePath(fixedFilePath);

    const PDBInfo *pPDBInfo = nullptr;
    auto findPDBInfoAndIndex = [&]()
    {
        std::string currentResult;
        for (auto &[modAddr, sourceIndices] : foundSourceIndices)
        {
            for (auto &sourceIndex : sourceIndices)
            {
                auto infoPair = m_debugInfo.find(modAddr);
                if (infoPair == m_debugInfo.end())
                {
                    continue;
                }

                const PDBInfo &pdbInfo = infoPair->second;
                std::string sourceFilePath;
                if (FAILED(PDBReader::GetSourceFile(pdbInfo.m_pdbHandle, sourceIndex, sourceFilePath)))
                {
                    continue;
                }

                if (fixedFilePath == sourceFilePath)
                {
                    globalFileIndex.sourceFileIndex = sourceIndex;
                    globalFileIndex.modAddress = modAddr;
                    pPDBInfo = &pdbInfo;
                    return;
                }

                if (fixedFilePath.size() > sourceFilePath.size())
                {
                    continue;
                }

                // Note, since assemblies could be built in different OSes, we could have different delimiters in source files paths.
                auto BinaryPredicate =
                    [](const char &a, const char &b) -> bool
                    {
                        if ((a == '/' || a == '\\') && (b == '/' || b == '\\'))
                        {
                            return true;
                        }
                        return a == b;
                    };

                // Prevent partial path matches, for example: source "folder/source.cs" should not match requested path "der/source.cs".
                if (fixedFilePath.size() < sourceFilePath.size() && fixedFilePath.at(0) != '/' && fixedFilePath.at(0) != '\\' &&
                    sourceFilePath.at(sourceFilePath.size() - fixedFilePath.size() - 1) != '/' && sourceFilePath.at(sourceFilePath.size() -
                                     fixedFilePath.size() - 1) != '\\')
                {
                    continue;
                }
                if (currentResult.empty() ||
                    (std::equal(fixedFilePath.begin(), fixedFilePath.end(), sourceFilePath.end() -
                                static_cast<std::string::difference_type>(fixedFilePath.size()), BinaryPredicate) &&
                     currentResult.length() > fixedFilePath.length()))
                {
                    currentResult = fixedFilePath;
                    globalFileIndex.sourceFileIndex = sourceIndex;
                    globalFileIndex.modAddress = modAddr;
                    pPDBInfo = &pdbInfo;
                }
            }
        }
    };
    findPDBInfoAndIndex();

    if (pPDBInfo == nullptr)
    {
        return E_FAIL;
    }

    return DebugSources::ResolveBreakpoints(*pPDBInfo, globalFileIndex.sourceFileIndex, sourceLine, resolvedPoints);
}

HRESULT DebugInfo::GetLocalConstants(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset,
                                     std::vector<PDB::LocalConstant> &constants)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    return GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            return PDBReader::GetLocalConstants(pdbInfo.m_pdbHandle, methodToken, ilOffset, constants);
        });
}

bool DebugInfo::IsStateMachineKickoffMethod(ICorDebugFunction *pFunction)
{
    mdMethodDef methodToken = mdMethodDefNil;
    ToRelease<ICorDebugModule> trModule;
    CORDB_ADDRESS modAddress = 0;
    if (pFunction == nullptr ||
        FAILED(pFunction->GetToken(&methodToken)) ||
        FAILED(pFunction->GetModule(&trModule)) ||
        FAILED(trModule->GetBaseAddress(&modAddress)))
    {
        return false;
    }

    bool res = false;
    GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            res = pdbInfo.m_kickoffToMoveNext.find(methodToken) != pdbInfo.m_kickoffToMoveNext.end();
            return S_OK;
        });

    return res;
}

HRESULT DebugInfo::GetStateMachineKickoffMethod(ICorDebugModule *pModule, mdMethodDef moveNextMethodToken,
                                                mdMethodDef &kickoffMethodToken)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    return GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            auto find = pdbInfo.m_moveNextToKickoff.find(moveNextMethodToken);
            if (find == pdbInfo.m_moveNextToKickoff.end())
            {
                return E_FAIL;
            }

            kickoffMethodToken = find->second;
            return S_OK;
        });
}

} // namespace dncdbg
