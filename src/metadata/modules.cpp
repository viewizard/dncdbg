// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/modules.h"
#include "managed/interop.h"
#include "metadata/jmc.h"
#include "metadata/typeprinter.h"
#include "utils/logger.h"
#include <array>
#include <iomanip>
#include <sstream>
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
    ToRelease<IUnknown> pMDUnknown;
    ToRelease<IMetaDataImport> pMDImport;

    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &pMDUnknown));
    IfFailRet(pMDUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&pMDImport)));

    ULONG typesCnt = 0;
    HCORENUM fTypeEnum = nullptr;
    mdTypeDef mdType = mdTypeDefNil;

    while (SUCCEEDED(pMDImport->EnumTypeDefs(&fTypeEnum, &mdType, 1, &typesCnt)) && typesCnt != 0)
    {
        std::string typeName;
        IfFailRet(TypePrinter::NameForToken(mdType, pMDImport, typeName, false, nullptr));

        HCORENUM fFuncEnum = nullptr;
        mdMethodDef mdMethod = mdMethodDefNil;
        ULONG methodsCnt = 0;

        while (SUCCEEDED(pMDImport->EnumMethods(&fFuncEnum, mdType, &mdMethod, 1, &methodsCnt)) && methodsCnt != 0)
        {
            mdTypeDef memTypeDef = mdTypeDefNil;
            ULONG nameLen = 0;
            std::array<WCHAR, mdNameLen> szFuncName{};

            if (FAILED(pMDImport->GetMethodProps(mdMethod, &memTypeDef, szFuncName.data(), mdNameLen, &nameLen,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            // Get generic types
            ToRelease<IMetaDataImport2> pMDImport2;

            IfFailRet(pMDUnknown->QueryInterface(IID_IMetaDataImport2, reinterpret_cast<void **>(&pMDImport2)));

            HCORENUM fGenEnum = nullptr;
            mdGenericParam gp = mdGenericParamNil;
            ULONG fetched = 0;
            std::string genParams;

            while (SUCCEEDED(pMDImport2->EnumGenericParams(&fGenEnum, mdMethod, &gp, 1, &fetched)) && fetched == 1)
            {
                mdMethodDef memMethodDef = mdMethodDefNil;
                std::array<WCHAR, mdNameLen> szGenName{};
                ULONG genNameLen = 0;

                if (FAILED(pMDImport2->GetGenericParamProps(gp, nullptr, nullptr, &memMethodDef, nullptr,
                                                            szGenName.data(), mdNameLen, &genNameLen)))
                {
                    continue;
                }

                // Add comma for each element. The last one will be stripped later.
                genParams += to_utf8(szGenName.data()) + ",";
            }

            pMDImport2->CloseEnum(fGenEnum);

            std::string fullName = to_utf8(szFuncName.data());
            if (!genParams.empty())
            {
                // Last symbol is comma and it is useless, so remove
                genParams.pop_back();
                fullName += "<" + genParams + ">";
            }

            if (!functor(typeName + "." + fullName, mdMethod)) // NOLINT(performance-inefficient-string-concatenation)
            {
                pMDImport->CloseEnum(fFuncEnum);
                pMDImport->CloseEnum(fTypeEnum);
                return E_FAIL;
            }
        }

        pMDImport->CloseEnum(fFuncEnum);
    }
    pMDImport->CloseEnum(fTypeEnum);

    return S_OK;
}

std::vector<std::string> split_on_tokens(const std::string &str, const char delim)
{
    std::vector<std::string> res;
    size_t pos = 0;
    size_t prev = 0;

    while (true)
    {
        pos = str.find(delim, prev);
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

std::string GetFileName(const std::string &path)
{
    const std::size_t i = path.find_last_of("/\\");
    return i == std::string::npos ? path : path.substr(i + 1);
}

HRESULT LoadSymbols(ICorDebugModule *pModule, void **ppSymbolReaderHandle)
{
    HRESULT Status = S_OK;
    BOOL isDynamic = FALSE;
    BOOL isInMemory = FALSE;
    IfFailRet(pModule->IsDynamic(&isDynamic));
    IfFailRet(pModule->IsInMemory(&isInMemory));

    if (isDynamic == TRUE)
    {
        return E_FAIL; // Dynamic and in memory assemblies are a special case which we will ignore for now
    }

    uint64_t peAddress = 0;
    uint32_t peSize = 0;
    IfFailRet(pModule->GetBaseAddress(&peAddress));
    IfFailRet(pModule->GetSize(&peSize));

    std::vector<unsigned char> peBuf;
    uint64_t peBufAddress = 0;
    if ((isInMemory == TRUE) && (peAddress != 0) && (peSize != 0))
    {
        ToRelease<ICorDebugProcess> process;
        IfFailRet(pModule->GetProcess(&process));

        peBuf.resize(peSize);
        peBufAddress = reinterpret_cast<uint64_t>(peBuf.data());
        SIZE_T read = 0;
        IfFailRet(process->ReadMemory(peAddress, peSize, peBuf.data(), &read));
        if (read != peSize)
        {
            return E_FAIL;
        }
    }

    return Interop::LoadSymbolsForPortablePDB(
        GetModuleFileName(pModule),
        isInMemory,
        isInMemory, // isFileLayout
        peBufAddress,
        peSize,
        0,          // inMemoryPdbAddress
        0,          // inMemoryPdbSize
        ppSymbolReaderHandle
    );
}

} // unnamed namespace

ModuleInfo::~ModuleInfo() noexcept
{
    if (m_symbolReaderHandle != nullptr)
    {
        Interop::DisposeSymbols(m_symbolReaderHandle);
    }
}

void Modules::CleanupAllModules()
{
    const std::scoped_lock<std::mutex> lock(m_modulesInfoMutex);
    m_modulesInfo.clear();
}

std::string GetModuleFileName(ICorDebugModule *pModule)
{
    std::array<WCHAR, mdNameLen> name{};
    uint32_t name_len = 0;

    if (FAILED(pModule->GetName(mdNameLen, &name_len, name.data())))
    {
        return {};
    }

    std::string moduleName = to_utf8(name.data());

    // On Tizen platform module path may look like /proc/self/fd/8/bin/Xamarin.Forms.Platform.dll
    // This path is invalid in debugger process, we should change `self` to `<debugee process id>`
    static const std::string selfPrefix("/proc/self/");

    if (moduleName.compare(0, selfPrefix.size(), selfPrefix) != 0)
    {
        return moduleName;
    }

    ToRelease<ICorDebugProcess> pProcess;
    if (FAILED(pModule->GetProcess(&pProcess)))
    {
        return {};
    }

    DWORD pid = 0;

    if (FAILED(pProcess->GetID(&pid)))
    {
        return {};
    }

    std::ostringstream ss;
    ss << "/proc/" << pid << "/" << moduleName.substr(selfPrefix.size());
    return ss.str();
}

HRESULT Modules::GetModuleInfo(CORDB_ADDRESS modAddress, const ModuleInfoCallback &cb)
{
    const std::scoped_lock<std::mutex> lock(m_modulesInfoMutex);
    auto info_pair = m_modulesInfo.find(modAddress);
    return (info_pair == m_modulesInfo.end()) ? E_FAIL : cb(info_pair->second);
}

// Caller must care about m_modulesInfoMutex.
HRESULT Modules::GetModuleInfo(CORDB_ADDRESS modAddress, ModuleInfo **ppmdInfo)
{
    auto info_pair = m_modulesInfo.find(modAddress);
    if (info_pair == m_modulesInfo.end())
    {
        return E_FAIL;
    }

    *ppmdInfo = &info_pair->second;
    return S_OK;
}

HRESULT Modules::ResolveFunctionBreakpointInAny(const std::string &funcname, const ResolveFunctionBreakpointCallback &cb)
{
    const std::scoped_lock<std::mutex> lock(m_modulesInfoMutex);

    for (auto &info_pair : m_modulesInfo)
    {
        const ModuleInfo &mdInfo = info_pair.second;
        ResolveMethodInModule(mdInfo.m_iCorModule, funcname, cb);
    }

    return S_OK;
}

HRESULT Modules::ResolveFunctionBreakpointInModule(ICorDebugModule *pModule, std::string &funcname,
                                                   const ResolveFunctionBreakpointCallback &cb)
{
    return ResolveMethodInModule(pModule, funcname, cb);
}

HRESULT Modules::GetFrameILAndSequencePoint(ICorDebugFrame *pFrame, uint32_t &ilOffset,
                                            SequencePoint &sequencePoint)
{
    HRESULT Status = S_OK;

    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(pFrame->GetFunctionToken(&methodToken));

    ToRelease<ICorDebugFunction> pFunc;
    IfFailRet(pFrame->GetFunction(&pFunc));

    ToRelease<ICorDebugCode> pCode;
    IfFailRet(pFunc->GetILCode(&pCode));

    ToRelease<ICorDebugILFrame> pILFrame;
    IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&pILFrame)));

    CorDebugMappingResult mappingResult = MAPPING_NO_INFO;
    IfFailRet(pILFrame->GetIP(&ilOffset, &mappingResult));
    if (mappingResult == MAPPING_UNMAPPED_ADDRESS ||
        mappingResult == MAPPING_NO_INFO)
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugModule> pModule;
    IfFailRet(pFunc->GetModule(&pModule));

    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    return GetModuleInfo(modAddress, [&](ModuleInfo &mdInfo) -> HRESULT {
        if (mdInfo.m_symbolReaderHandle == nullptr)
        {
            return E_FAIL;
        }

        return GetSequencePointByILOffset(mdInfo.m_symbolReaderHandle, methodToken, ilOffset, &sequencePoint);
    });
}

HRESULT Modules::GetFrameILAndNextUserCodeILOffset(ICorDebugFrame *pFrame, uint32_t &ilOffset, uint32_t &ilNextOffset,
                                                   bool *noUserCodeFound)
{
    HRESULT Status = S_OK;

    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(pFrame->GetFunctionToken(&methodToken));

    ToRelease<ICorDebugFunction> pFunc;
    IfFailRet(pFrame->GetFunction(&pFunc));

    ToRelease<ICorDebugCode> pCode;
    IfFailRet(pFunc->GetILCode(&pCode));

    ToRelease<ICorDebugILFrame> pILFrame;
    IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&pILFrame)));

    CorDebugMappingResult mappingResult = MAPPING_NO_INFO;
    IfFailRet(pILFrame->GetIP(&ilOffset, &mappingResult));
    if (mappingResult == MAPPING_UNMAPPED_ADDRESS ||
        mappingResult == MAPPING_NO_INFO)
    {
        return E_FAIL;
    }

    ToRelease<ICorDebugModule> pModule;
    IfFailRet(pFunc->GetModule(&pModule));

    return GetNextUserCodeILOffsetInMethod(pModule, methodToken, ilOffset, ilNextOffset, noUserCodeFound);
}

HRESULT Modules::GetStepRangeFromCurrentIP(ICorDebugThread *pThread, COR_DEBUG_STEP_RANGE *range)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugFrame> pFrame;
    IfFailRet(pThread->GetActiveFrame(&pFrame));
    if (pFrame == nullptr)
    {
        return E_FAIL;
    }

    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(pFrame->GetFunctionToken(&methodToken));

    ToRelease<ICorDebugFunction> pFunc;
    IfFailRet(pFrame->GetFunction(&pFunc));

    ToRelease<ICorDebugCode> pCode;
    IfFailRet(pFunc->GetILCode(&pCode));

    ToRelease<ICorDebugModule> pModule;
    IfFailRet(pFunc->GetModule(&pModule));

    ToRelease<ICorDebugILFrame> pILFrame;
    IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&pILFrame)));

    uint32_t nOffset = 0;
    CorDebugMappingResult mappingResult = MAPPING_NO_INFO;
    IfFailRet(pILFrame->GetIP(&nOffset, &mappingResult));
    if (mappingResult == MAPPING_UNMAPPED_ADDRESS ||
        mappingResult == MAPPING_NO_INFO)
    {
        return E_FAIL;
    }

    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    uint32_t ilStartOffset = 0;
    uint32_t ilEndOffset = 0;

    IfFailRet(GetModuleInfo(modAddress, [&](ModuleInfo &mdInfo) -> HRESULT {
        if (mdInfo.m_symbolReaderHandle == nullptr)
        {
            return E_FAIL;
        }

        return Interop::GetStepRangesFromIP(mdInfo.m_symbolReaderHandle, nOffset, methodToken, &ilStartOffset,
                                            &ilEndOffset);
    }));

    if (ilStartOffset == ilEndOffset)
    {
        ToRelease<ICorDebugCode> pCode;
        IfFailRet(pFunc->GetILCode(&pCode));
        IfFailRet(pCode->GetSize(&ilEndOffset));
    }

    range->startOffset = ilStartOffset;
    range->endOffset = ilEndOffset;

    return S_OK;
}

HRESULT GetModuleId(ICorDebugModule *pModule, std::string &id)
{
    HRESULT Status = S_OK;

    ToRelease<IUnknown> pMDUnknown;
    ToRelease<IMetaDataImport> pMDImport;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &pMDUnknown));
    IfFailRet(pMDUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&pMDImport)));

    GUID mvid;

    IfFailRet(pMDImport->GetScopeProps(nullptr, 0, nullptr, &mvid));

    static constexpr uint32_t widthMvid8 = 8;
    static constexpr uint32_t widthMvid4 = 4;
    static constexpr uint32_t widthMvid2 = 2;
    static constexpr int mvidMask = 0xFF;
    std::ostringstream ss;
    ss << std::hex
    << std::setfill('0') << std::setw(widthMvid8) << mvid.Data1 << "-"
    << std::setfill('0') << std::setw(widthMvid4) << mvid.Data2 << "-"
    << std::setfill('0') << std::setw(widthMvid4) << mvid.Data3 << "-"
    << std::setfill('0') << std::setw(widthMvid2) << (static_cast<int>(mvid.Data4[0]) & mvidMask)
    << std::setfill('0') << std::setw(widthMvid2) << (static_cast<int>(mvid.Data4[1]) & mvidMask)
    << "-";
    static constexpr uint32_t startChar = 2;
    static constexpr uint32_t endChar = 8;
    for (int i = startChar; i < endChar; i++)
    {
        ss << std::setfill('0') << std::setw(widthMvid2) << (static_cast<int>(mvid.Data4[i]) & mvidMask);
    }

    id = ss.str();

    return S_OK;
}

HRESULT Modules::TryLoadModuleSymbols(ICorDebugModule *pModule, Module &module, bool needJMC, std::string &outputText)
{
    HRESULT Status = S_OK;

    module.path = GetModuleFileName(pModule);
    module.name = GetFileName(module.path);

    void *pSymbolReaderHandle = nullptr;
    LoadSymbols(pModule, &pSymbolReaderHandle);
    module.symbolStatus = pSymbolReaderHandle != nullptr ? SymbolStatus::Loaded : SymbolStatus::NotFound;

    if (module.symbolStatus == SymbolStatus::Loaded)
    {
        ToRelease<ICorDebugModule2> pModule2;
        if (SUCCEEDED(pModule->QueryInterface(IID_ICorDebugModule2, reinterpret_cast<void **>(&pModule2))))
        {
            if (!needJMC)
            {
                pModule2->SetJITCompilerFlags(CORDEBUG_JIT_DISABLE_OPTIMIZATION);
            }

            if (SUCCEEDED(Status = pModule2->SetJMCStatus(TRUE, 0, nullptr))) // If we can't enable JMC for module, no reason
                                                                              // disable JMC on module's types/methods.
            {
                // Note, we use JMC in runtime all the time (same behaviour as MS vsdbg and MSVS debugger have),
                // since this is the only way provide good speed for stepping in case "JMC disabled".
                // But in case "JMC disabled", debugger must care about different logic for exceptions/stepping/breakpoints.

                // https://docs.microsoft.com/en-us/visualstudio/debugger/just-my-code
                // The .NET debugger considers optimized binaries and non-loaded .pdb files to be non-user code.
                // Three compiler attributes also affect what the .NET debugger considers to be user code:
                // * DebuggerNonUserCodeAttribute tells the debugger that the code it's applied to isn't user code.
                // * DebuggerHiddenAttribute hides the code from the debugger, even if Just My Code is turned off.
                // * DebuggerStepThroughAttribute tells the debugger to step through the code it's applied to, rather
                // than step into the code. The .NET debugger considers all other code to be user code.
                if (needJMC)
                {
                    DisableJMCByAttributes(pModule);
                }
            }
            else if (Status == CORDBG_E_CANT_SET_TO_JMC)
            {
                if (needJMC)
                {
                    outputText = "You are debugging a Release build of " + module.name +
                                 ". Using Just My Code with Release builds using compiler optimizations results in a "
                                 "degraded debugging experience (e.g. breakpoints will not be hit).";
                }
                else
                {
                    outputText = "You are debugging a Release build of " + module.name +
                                 ". Without Just My Code Release builds try not to use compiler optimizations, but in "
                                 "some cases (e.g. attach) this still results in a degraded debugging experience (e.g. "
                                 "breakpoints will not be hit).";
                }
            }
        }

        ToRelease<IUnknown> pMDUnknown;
        ToRelease<IMetaDataImport> pMDImport;
        IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &pMDUnknown));
        IfFailRet(pMDUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&pMDImport)));

        if (FAILED(m_modulesSources.FillSourcesCodeLinesForModule(pModule, pMDImport, pSymbolReaderHandle)))
        {
            LOGE("Could not load source lines related info from PDB file. Could produce failures during breakpoint's "
                 "source path resolve in future.");
        }
    }

    IfFailRet(GetModuleId(pModule, module.id));

    CORDB_ADDRESS baseAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&baseAddress));

    pModule->AddRef();
    ModuleInfo mdInfo{pSymbolReaderHandle, pModule};
    const std::scoped_lock<std::mutex> lock(m_modulesInfoMutex);
    m_modulesInfo.insert(std::make_pair(baseAddress, std::move(mdInfo)));

    return S_OK;
}

HRESULT Modules::GetFrameNamedLocalVariable(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t localIndex,
                                            WSTRING &localName, int32_t *pIlStart, int32_t *pIlEnd)
{
    HRESULT Status = S_OK;

    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    std::array<WCHAR, mdNameLen> wLocalName{};

    IfFailRet(GetModuleInfo(modAddress,
        [&](ModuleInfo &mdInfo) -> HRESULT
        {
            if (mdInfo.m_symbolReaderHandle == nullptr)
            {
                return E_FAIL;
            }

            return Interop::GetNamedLocalVariableAndScope(mdInfo.m_symbolReaderHandle, methodToken, localIndex,
                                                          wLocalName.data(), mdNameLen, pIlStart, pIlEnd);
        }));

    localName = wLocalName.data();

    return S_OK;
}

HRESULT Modules::GetHoistedLocalScopes(ICorDebugModule *pModule, mdMethodDef methodToken, void **data,
                                       int32_t &hoistedLocalScopesCount)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    return GetModuleInfo(modAddress,
        [&](ModuleInfo &mdInfo) -> HRESULT
        {
            if (mdInfo.m_symbolReaderHandle == nullptr)
            {
                return E_FAIL;
            }

            return Interop::GetHoistedLocalScopes(mdInfo.m_symbolReaderHandle, methodToken, data, hoistedLocalScopesCount);
        });
}

HRESULT Modules::GetModuleWithName(const std::string &name, ICorDebugModule **ppModule)
{
    const std::scoped_lock<std::mutex> lock(m_modulesInfoMutex);

    for (auto &info_pair : m_modulesInfo)
    {
        const ModuleInfo &mdInfo = info_pair.second;

        const std::string path = GetModuleFileName(mdInfo.m_iCorModule);

        if (GetFileName(path) == name)
        {
            mdInfo.m_iCorModule->AddRef();
            *ppModule = mdInfo.m_iCorModule;
            return S_OK;
        }
    }
    return E_FAIL;
}

HRESULT Modules::GetNextUserCodeILOffsetInMethod(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset,
                                                 uint32_t &ilNextOffset, bool *noUserCodeFound)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    return GetModuleInfo(modAddress,
        [&](ModuleInfo &mdInfo) -> HRESULT
        {
            if (mdInfo.m_symbolReaderHandle == nullptr)
            {
                return E_FAIL;
            }

            return Interop::GetNextUserCodeILOffset(mdInfo.m_symbolReaderHandle, methodToken,
                                                    ilOffset, ilNextOffset, noUserCodeFound);
        });
}

HRESULT Modules::GetSequencePointByILOffset(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                            SequencePoint *sequencePoint)
{
    Interop::SequencePoint symSequencePoint;

    if (FAILED(Interop::GetSequencePointByILOffset(pSymbolReaderHandle, methodToken, ilOffset, &symSequencePoint)))
    {
        return E_FAIL;
    }

    sequencePoint->document = to_utf8(symSequencePoint.document);
    sequencePoint->startLine = symSequencePoint.startLine;
    sequencePoint->startColumn = symSequencePoint.startColumn;
    sequencePoint->endLine = symSequencePoint.endLine;
    sequencePoint->endColumn = symSequencePoint.endColumn;
    sequencePoint->offset = symSequencePoint.offset;

    return S_OK;
}

HRESULT Modules::GetSequencePointByILOffset(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t ilOffset,
                                            SequencePoint &sequencePoint)
{
    return GetModuleInfo(modAddress,
        [&](ModuleInfo &mdInfo) -> HRESULT
        {
            if (mdInfo.m_symbolReaderHandle == nullptr)
            {
                return E_FAIL;
            }

            return GetSequencePointByILOffset(mdInfo.m_symbolReaderHandle, methodToken, ilOffset, &sequencePoint);
        });
}

HRESULT Modules::ForEachModule(const std::function<HRESULT(ICorDebugModule *pModule)> &cb)
{
    HRESULT Status = S_OK;
    const std::scoped_lock<std::mutex> lock(m_modulesInfoMutex);

    for (auto &info_pair : m_modulesInfo)
    {
        const ModuleInfo &mdInfo = info_pair.second;
        IfFailRet(cb(mdInfo.m_iCorModule));
    }
    return S_OK;
}

HRESULT Modules::ResolveBreakpoint(/*in*/ CORDB_ADDRESS modAddress,
#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
                                   /*in*/ const std::string &filename_,
#else
                                   /*in*/ const std::string &filename,
#endif
                                   /*out*/ unsigned &fullname_index,
                                   /*in*/ int sourceLine,
                                   /*out*/ std::vector<ModulesSources::resolved_bp_t> &resolvedPoints)
{
#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
    HRESULT Status = S_OK;
    std::string filename = filename_;
    IfFailRet(Interop::StringToUpper(filename));
#endif

    // Note, in all code we use m_modulesInfoMutex > m_sourcesInfoMutex lock sequence.
    const std::scoped_lock<std::mutex> lockModulesInfo(m_modulesInfoMutex);
    return m_modulesSources.ResolveBreakpoint(this, modAddress, filename, fullname_index, sourceLine, resolvedPoints);
}

HRESULT Modules::GetSourceFullPathByIndex(unsigned index, std::string &fullPath)
{
    return m_modulesSources.GetSourceFullPathByIndex(index, fullPath);
}

HRESULT Modules::GetIndexBySourceFullPath(const std::string &fullPath, unsigned &index)
{
    return m_modulesSources.GetIndexBySourceFullPath(fullPath, index);
}

} // namespace dncdbg
