// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/modules.h"
#include "metadata/jmc.h"
#include "protocol/dapio.h"
#include "utils/filesystem.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <cassert>
#include <iomanip>
#include <sstream>

namespace dncdbg
{

namespace
{

constexpr uint16_t g_dos_e_magic = 0x5A4D;
constexpr uint32_t g_ntsig_magic = 0x00004550;
constexpr uint16_t g_opt_header32_magic = 0x10B;
constexpr uint16_t g_opt_header64_magic = 0x20B;
constexpr uint32_t g_max_path_size = 4096;
constexpr uint32_t g_rsds_magic = 0x53445352;
constexpr uint32_t g_debug_type_codeview = 2;
constexpr uint16_t g_section_name_size = 8;
constexpr uint16_t g_max_sections_count = 96;
constexpr uint16_t g_ignored_size = 58;

#pragma pack(push, 1)
struct MemoryDosHeader
{
    uint16_t e_magic;
    uint8_t ignored[g_ignored_size]; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    uint32_t e_lfanew;
};

struct MemoryDataDirectory
{
    uint32_t virtual_address;
    uint32_t size;
};

struct MemoryRsdsHeader
{
    uint32_t signature;          // Magic "RSDS" (0x53445352)
    uint8_t  guid[g_guid_size];  // The target PDB GUID  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    uint32_t age;                // The target PDB Age (For Portable PDB, it is always 1)
    // Followed immediately by a null-terminated UTF-8 string containing the PDB Path
};

struct MemorySectionHeader
{
    char name[g_section_name_size]; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    uint32_t virtual_size;
    uint32_t virtual_address;       // RVA of section
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;   // File offset of section
    uint32_t pointer_to_relocations;
    uint32_t pointer_to_linenumbers;
    uint16_t number_of_relocations;
    uint16_t number_of_linenumbers;
    uint32_t characteristics;
};

struct MemoryDebugDirectory
{
    uint32_t characteristics;
    uint32_t time_date_stamp;
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t type;               // 2 = CODEVIEW (RSDS)
    uint32_t size_of_data;       // Size of the entire CodeView block
    uint32_t address_of_raw_data;// RVA of the CodeView data in memory (loaded layout)
    uint32_t pointer_to_raw_data;// File offset of the CodeView data (file layout)
};
#pragma pack(pop)

} // unnamed namespace

HRESULT Modules::GetModulePdbInfo(ICorDebugModule *pModule, PdbIdentity &pdbId, std::string &pathPdb)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS moduleBaseAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&moduleBaseAddress));
    ToRelease<ICorDebugProcess> trProcess;
    IfFailRet(pModule->GetProcess(&trProcess));

    auto readProcessMemory = [&](CORDB_ADDRESS addr, void *buffer, uint32_t size) -> HRESULT
    {
        SIZE_T bytesRead = 0;
        IfFailRet(trProcess->ReadMemory(addr, size, static_cast<uint8_t*>(buffer), &bytesRead));
        return (bytesRead == size) ? S_OK : E_FAIL;
    };

    // Read and validate DOS header
    MemoryDosHeader dos{};
    if (FAILED(readProcessMemory(moduleBaseAddress, &dos, sizeof(dos))) || dos.e_magic != g_dos_e_magic)
    {
        return E_FAIL;
    }

    // Read and validate NT signature
    const CORDB_ADDRESS ntHeaderAddr = moduleBaseAddress + dos.e_lfanew;
    uint32_t ntSig = 0;
    if (FAILED(readProcessMemory(ntHeaderAddr, &ntSig, sizeof(ntSig))) || ntSig != g_ntsig_magic)
    {
        return E_FAIL;
    }

    // Read optional header magic to determine PE32 vs PE32+
    const CORDB_ADDRESS optionalHeaderAddr = ntHeaderAddr + 4 + 20;
    uint16_t magic = 0;
    IfFailRet(readProcessMemory(optionalHeaderAddr, &magic, sizeof(magic)));

    if (magic != g_opt_header32_magic && magic != g_opt_header64_magic)
    {
        return E_FAIL;
    }

    // Data Directory index 6 (IMAGE_DIRECTORY_ENTRY_DEBUG) offset differs between PE32 and PE32+
    const uint32_t debugDirEntryOffset = (magic == g_opt_header32_magic) ? 144 : 160;
    const CORDB_ADDRESS debugDirEntryAddr = optionalHeaderAddr + debugDirEntryOffset;

    // Read debug directory data directory entry
    MemoryDataDirectory debugDataDir{};
    IfFailRet(readProcessMemory(debugDirEntryAddr, &debugDataDir, sizeof(debugDataDir)));

    if (debugDataDir.virtual_address == 0 || debugDataDir.size == 0)
    {
        return E_FAIL;
    }

    static constexpr uint32_t entriesCountLimit = 50;
    const uint32_t entriesCount = debugDataDir.size / sizeof(MemoryDebugDirectory);
    if (entriesCount == 0 || entriesCount > entriesCountLimit)
    {
        return E_FAIL;
    }

    // =========================================================================
    // Helper: Process debug directories and extract PDB info
    // =========================================================================
    auto processDebugDirectories = [&](const std::vector<MemoryDebugDirectory> &dirs,
                                       const std::function<CORDB_ADDRESS(const MemoryDebugDirectory&)> &getRsdsAddr) -> bool
    {
        for (const auto &dir : dirs)
        {
            if (dir.type != g_debug_type_codeview || dir.size_of_data < sizeof(MemoryRsdsHeader))
            {
                continue;
            }

            const CORDB_ADDRESS rsdsAddr = getRsdsAddr(dir);
            if (rsdsAddr == 0)
            {
                continue;
            }

            MemoryRsdsHeader rsds{};
            if (FAILED(readProcessMemory(rsdsAddr, &rsds, sizeof(rsds))) || rsds.signature != g_rsds_magic)
            {
                continue;
            }

            uint32_t age = 0;
            std::memcpy(&age, &rsds.age, sizeof(rsds.age));
            if (age != 1)
            {
                continue;
            }

            std::memcpy(pdbId.guid.data(), static_cast<void *>(rsds.guid), g_guid_size);
            std::memcpy(&pdbId.time_date_stamp, &dir.time_date_stamp, sizeof(dir.time_date_stamp));

            const uint32_t pathLength = dir.size_of_data - sizeof(MemoryRsdsHeader);
            if (pathLength == 0 || pathLength >= g_max_path_size)
            {
                continue;
            }

            std::vector<char> pathBuffer(pathLength + 1, '\0');
            if (SUCCEEDED(readProcessMemory(rsdsAddr + sizeof(MemoryRsdsHeader), pathBuffer.data(), pathLength)))
            {
                pathPdb = std::string(pathBuffer.data());
                return true;
            }
        }
        return false;
    };

    // =========================================================================
    // Try loaded layout first (ReadyToRun DLLs): RVA directly maps to VA
    // =========================================================================
    std::vector<MemoryDebugDirectory> debugDirs(entriesCount);
    if (SUCCEEDED(readProcessMemory(moduleBaseAddress + debugDataDir.virtual_address,
                                    debugDirs.data(), debugDataDir.size)))
    {
        auto getRsdsAddrLoaded = [&](const MemoryDebugDirectory &dir) -> CORDB_ADDRESS
        {
            return moduleBaseAddress + dir.address_of_raw_data;
        };

        if (processDebugDirectories(debugDirs, getRsdsAddrLoaded))
        {
            return S_OK;
        }
    }

    // =========================================================================
    // Fallback for JIT-ed DLL (file/flat layout): Convert RVA to file offset
    // =========================================================================

    // Read FILE_HEADER to get section count and optional header size
    const CORDB_ADDRESS fileHeaderAddr = ntHeaderAddr + 4;
    uint16_t numberOfSections = 0;
    uint16_t sizeOfOptionalHeader = 0;
    IfFailRet(readProcessMemory(fileHeaderAddr + 2, &numberOfSections, sizeof(numberOfSections)));
    IfFailRet(readProcessMemory(fileHeaderAddr + 16, &sizeOfOptionalHeader, sizeof(sizeOfOptionalHeader)));

    if (numberOfSections == 0 || numberOfSections > g_max_sections_count)
    {
        return E_FAIL;
    }

    // Read section headers for RVA to file offset conversion
    const CORDB_ADDRESS sectionHeadersAddr = optionalHeaderAddr + sizeOfOptionalHeader;
    std::vector<MemorySectionHeader> sections(numberOfSections);
    if (FAILED(readProcessMemory(sectionHeadersAddr, sections.data(),
                                 static_cast<uint32_t>(numberOfSections * sizeof(MemorySectionHeader)))))
    {
        return E_FAIL;
    }

    // Helper: Convert RVA to file offset using section table
    auto rvaToFileOffset = [&sections](uint32_t rva) -> uint32_t
    {
        for (const auto &section : sections)
        {
            if (rva >= section.virtual_address &&
                rva < section.virtual_address + section.size_of_raw_data)
            {
                return section.pointer_to_raw_data + (rva - section.virtual_address);
            }
        }
        return 0;
    };

    // Convert debug directory RVA to file offset
    const uint32_t debugDirFileOffset = rvaToFileOffset(debugDataDir.virtual_address);
    if (debugDirFileOffset == 0)
    {
        return E_FAIL;
    }

    // Read debug directory entries from file offset
    if (FAILED(readProcessMemory(moduleBaseAddress + debugDirFileOffset,
                                 debugDirs.data(), debugDataDir.size)))
    {
        return E_FAIL;
    }

    auto getRsdsAddrFile = [&](const MemoryDebugDirectory &dir) -> CORDB_ADDRESS
    {
        return (dir.pointer_to_raw_data != 0) ? moduleBaseAddress + dir.pointer_to_raw_data : 0;
    };

    if (processDebugDirectories(debugDirs, getRsdsAddrFile))
    {
        return S_OK;
    }

    return E_FAIL;
}

HRESULT Modules::GetModuleMvid(ICorDebugModule *pModule, std::string &strMvid)
{
    HRESULT Status = S_OK;

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
    GUID mvid;
    IfFailRet(trMDImport->GetScopeProps(nullptr, 0, nullptr, &mvid));

    static constexpr uint32_t widthMvid8 = 8;
    static constexpr uint32_t widthMvid4 = 4;
    static constexpr uint32_t widthMvid2 = 2;
    static constexpr int mvidMask = 0xFF;
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(widthMvid8) << mvid.Data1 << "-"
       << std::setw(widthMvid4) << mvid.Data2 << "-"
       << std::setw(widthMvid4) << mvid.Data3 << "-"
       << std::setw(widthMvid2) << (static_cast<int>(mvid.Data4[0]) & mvidMask)
       << std::setw(widthMvid2) << (static_cast<int>(mvid.Data4[1]) & mvidMask) << "-";
    static constexpr uint32_t startChar = 2;
    static constexpr uint32_t endChar = 8;
    for (int i = startChar; i < endChar; i++)
    {
        ss << std::setw(widthMvid2) << (static_cast<int>(mvid.Data4[i]) & mvidMask); // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    }

    strMvid = ss.str();

    return S_OK;
}

std::string Modules::GetModuleFileName(ICorDebugModule *pModule)
{
    uint32_t nameLen = 0;
    if (FAILED(pModule->GetName(0, &nameLen, nullptr)))
    {
        return {};
    }

    WSTRING wModName(nameLen - 1, '\0'); // nameLen includes null terminator
    if (FAILED(pModule->GetName(nameLen, nullptr, wModName.data())))
    {
        return {};
    }

    std::string moduleName = to_utf8(wModName.c_str());

    // On Tizen platform module path may look like /proc/self/fd/8/bin/Xamarin.Forms.Platform.dll
    // This path is invalid in debugger process, we should change `self` to `<debugee process id>`
    static const std::string selfPrefix("/proc/self/");

    if (moduleName.compare(0, selfPrefix.size(), selfPrefix) != 0)
    {
        return moduleName;
    }

    ToRelease<ICorDebugProcess> trProcess;
    if (FAILED(pModule->GetProcess(&trProcess)))
    {
        return {};
    }

    DWORD pid = 0;

    if (FAILED(trProcess->GetID(&pid)))
    {
        return {};
    }

    std::ostringstream ss;
    ss << "/proc/" << pid << "/" << moduleName.substr(selfPrefix.size());
    return ss.str();
}

void Modules::LoadModuleMetadata(ICorDebugModule *pModule, Module &module, bool needJMC, bool suppressJITOptimizations)
{
    module.path = Modules::GetModuleFileName(pModule);
    module.name = GetFileName(module.path);

    if (module.symbolStatus == SymbolStatus::Loaded)
    {
        ToRelease<ICorDebugModule2> trModule2;
        if (SUCCEEDED(pModule->QueryInterface(IID_ICorDebugModule2, reinterpret_cast<void **>(&trModule2))))
        {
            // Try to disable optimization for all modules with debug info.
            trModule2->SetJITCompilerFlags(CORDEBUG_JIT_DISABLE_OPTIMIZATION);

            HRESULT Status = S_OK;
            // Note, JMC status should be set for any needJMC value.
            if (SUCCEEDED(Status = trModule2->SetJMCStatus(TRUE, 0, nullptr))) // If we can't enable JMC for module, there is no reason to
                                                                               // disable JMC on module's types/methods.
            {
                module.isUserCode = true;

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
                DAPIO::EmitOutputEvent({OutputCategory::StdErr,
                    "You are debugging a Release build of " + module.name + ". Disabling JIT "
                    "optimizations failed, in some cases (e.g. attach) this results in a "
                    "degraded debugging experience (e.g. breakpoints will not be hit).\n"});
            }
        }
    }
    else if (suppressJITOptimizations)
    {
        ToRelease<ICorDebugModule2> trModule2;
        if (SUCCEEDED(pModule->QueryInterface(IID_ICorDebugModule2, reinterpret_cast<void **>(&trModule2))))
        {
            // Try to disable optimization for all modules.
            trModule2->SetJITCompilerFlags(CORDEBUG_JIT_DISABLE_OPTIMIZATION);
        }
    }

    ToRelease<ICorDebugModule2> trModule2;
    DWORD dwFlags = 0;
    if (SUCCEEDED(pModule->QueryInterface(IID_ICorDebugModule2, reinterpret_cast<void **>(&trModule2))) &&
        SUCCEEDED(trModule2->GetJITCompilerFlags(&dwFlags)))
    {
        module.isOptimized = (dwFlags & 2UL) == 0;
    }

    if (FAILED(Modules::GetModuleMvid(pModule, module.id)))
    {
        DAPIO::EmitOutputEvent({OutputCategory::StdErr,
            "Could not calculate module ID for module " + module.name + ".\n"});
    }

    CORDB_ADDRESS moduleBaseAddress = 0;
    uint32_t moduleSize = 0;
    if (SUCCEEDED(pModule->GetBaseAddress(&moduleBaseAddress)) &&
        SUCCEEDED(pModule->GetSize(&moduleSize)))
    {
        static constexpr int32_t addrSize = 16; // CORDB_ADDRESS is ULONG64 for all arches.
        std::ostringstream ss;
        ss << "0x" << std::hex << std::setfill('0')
           << std::setw(addrSize) << moduleBaseAddress << "-0x"
           << std::setw(addrSize) << moduleBaseAddress + moduleSize;
        module.addressRange = ss.str();
    }
    else
    {
        DAPIO::EmitOutputEvent({OutputCategory::StdErr, "Could not calculate module address range.\n"});
    }
}

Module &Modules::GetNewModuleRef()
{
    const std::scoped_lock<std::mutex> lock(m_moduleMutex);

    m_moduleList.emplace_back();
    return m_moduleList.back();
}

HRESULT Modules::RemoveModule(ICorDebugModule *pModule, Module &removedModule)
{
    HRESULT Status = S_OK;
    std::string id;
    IfFailRet(GetModuleMvid(pModule, id));

    const std::scoped_lock<std::mutex> lock(m_moduleMutex);

    for (auto it = m_moduleList.begin(); it != m_moduleList.end();)
    {
        if (it->id == id)
        {
            removedModule = *it;
            m_moduleList.erase(it);
            return S_OK;
        }
        else
        {
            ++it;
        }
    }

    return E_INVALIDARG;
}

void Modules::GetModules(int startModule, int moduleCount, std::vector<Module> &modules, size_t &totalModules)
{
    const std::scoped_lock<std::mutex> lock(m_moduleMutex);

    totalModules = m_moduleList.size();

    assert(m_moduleList.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
    if (startModule >= static_cast<int>(m_moduleList.size()))
    {
        return;
    }

    auto startIt = std::next(m_moduleList.begin(), startModule);
    auto endIt = m_moduleList.end();
    if (moduleCount != 0 &&
        startModule + moduleCount < static_cast<int>(m_moduleList.size()))
    {
        endIt = std::next(startIt, moduleCount);
    }

    for (auto it = startIt; it != endIt; it = std::next(it))
    {
        modules.emplace_back(*it);
    }
}

HRESULT Modules::ForEachModule(ICorDebugThread *pThread, const std::function<HRESULT(ICorDebugModule *pModule)> &cb)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugProcess> trProcess;
    IfFailRet(pThread->GetProcess(&trProcess));
    ToRelease<ICorDebugAppDomainEnum> trAppDomainEnum;
    IfFailRet(trProcess->EnumerateAppDomains(&trAppDomainEnum));
    // At this moment, the debugger supports only one application domain per process.
    ToRelease<ICorDebugAppDomain> trAppDomain;
    ULONG domainsFetched = 0;
    IfFailRet(trAppDomainEnum->Next(1, &trAppDomain, &domainsFetched));
    IfFailRet(domainsFetched == 1 ? S_OK : E_FAIL);
    ToRelease<ICorDebugAssemblyEnum> trAssemblyEnum;
    IfFailRet(trAppDomain->EnumerateAssemblies(&trAssemblyEnum));

    ICorDebugAssembly *curAssembly = nullptr;
    ULONG assemblyFetched = 0;
    while (SUCCEEDED(trAssemblyEnum->Next(1, &curAssembly, &assemblyFetched)) && assemblyFetched == 1)
    {
        ToRelease<ICorDebugAssembly> trAssembly(curAssembly);
        // Only one module per assembly is supported.
        ToRelease<ICorDebugModuleEnum> trModuleEnum;
        IfFailRet(trAssembly->EnumerateModules(&trModuleEnum));
        ToRelease<ICorDebugModule> trModule;
        ULONG moduleFetched = 0;
        IfFailRet(trModuleEnum->Next(1, &trModule, &moduleFetched));
        IfFailRet(moduleFetched == 1 ? S_OK : E_FAIL);

        if (FAILED(Status = cb(trModule)))
        {
            break;
        }
        else if (Status == S_CAN_EXIT)
        {
            Status = S_OK;
            break;
        }
    }

    return Status;
}

HRESULT Modules::GetModuleWithName(ICorDebugThread *pThread, const std::string &name, ICorDebugModule **ppModule)
{
    HRESULT Status = S_OK;
    *ppModule = nullptr;

    IfFailRet(Modules::ForEachModule(pThread,
        [&](ICorDebugModule *pModule) -> HRESULT
        {
            const std::string path = Modules::GetModuleFileName(pModule);

            if (GetFileName(path) == name)
            {
                pModule->AddRef();
                *ppModule = pModule;
                return S_CAN_EXIT; // Fast exit from loop.
            }

            return S_OK; // Return S_OK to continue the iteration.
        }));

    return *ppModule != nullptr ? S_OK : E_FAIL;
}

} // namespace dncdbg
