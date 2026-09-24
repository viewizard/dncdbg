// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/debuginfo.h"
#include "debuginfo/asyncinfo.h"
#include "debuginfo/debugsources.h"
#include "debuginfo/pdbreader.h"
#include "debuginfo/sourcereference.h"
#include "metadata/helpers.h"
#include "metadata/modules.h"
#include "protocol/dap_events.h"
#include "utils/downloader.h"
#include "utils/filesystem.h"
#include "utils/hresult.h"
#include "utils/utftoupper.h"
#include <algorithm>
#include <cstring>
#include <forward_list>
#include <list>
#include <map>
#include <mutex>
#include <vector>

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ARCHIVE_APIS
#include <miniz/miniz.h>

namespace dncdbg::DebugInfo
{

namespace
{

constexpr uint8_t g_stamp_size = 4;
constexpr uint8_t g_guid_size = 16;
constexpr uint16_t g_dos_e_magic = 0x5A4D;
constexpr uint32_t g_ntsig_magic = 0x00004550;
constexpr uint16_t g_opt_header32_magic = 0x10B;
constexpr uint16_t g_opt_header64_magic = 0x20B;
constexpr uint16_t g_max_path_size = 4096;
constexpr uint32_t g_rsds_magic = 0x53445352;
constexpr uint8_t g_debug_type_codeview = 2;
constexpr uint8_t g_section_name_size = 8;
constexpr uint8_t g_max_sections_count = 96;
constexpr uint8_t g_ignored_size = 58;
constexpr uint8_t g_debug_type_embedded_pdb = 17;
constexpr uint32_t g_mpdb_magic = 0x4244504D;

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
    uint32_t age;                // The target PDB Age (for Portable PDB, it is always 1)
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

struct MemoryMpdbHeader
{
    uint32_t signature;
    uint32_t uncompressed_size;
};
#pragma pack(pop)

bool DecompressDeflateBuffer(const unsigned char *compressedData, size_t compressedSize, std::vector<uint8_t> &outBuffer, size_t uncompressedSize)
{
    outBuffer.resize(uncompressedSize);

    z_stream stream{};
    stream.next_in = compressedData;
    stream.avail_in = static_cast<unsigned int>(compressedSize);
    stream.next_out = outBuffer.data();
    stream.avail_out = static_cast<unsigned int>(uncompressedSize);

    // -MZ_DEFAULT_WINDOW_BITS (raw deflate/no header or footer)
    if (inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != Z_OK)
    {
        outBuffer.clear();
        return false;
    }

    const int status = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (status == Z_STREAM_END && stream.total_out == uncompressedSize)
    {
        return true;
    }

    outBuffer.clear();
    return false;
}

std::mutex &GetDebugInfoMutex()
{
    static std::mutex debugInfoMutex;
    return debugInfoMutex;
}

using DebugInfoMap = std::unordered_map<CORDB_ADDRESS, PDBInfo>;

DebugInfoMap &GetDebugInfoMap()
{
    static DebugInfoMap debugInfoMap;
    return debugInfoMap;
}

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

    ULONG fetched = 0;
    HCORENUM fTypeEnum = nullptr;
    mdTypeDef typeDef = mdTypeDefNil;

    while (SUCCEEDED(trMDImport->EnumTypeDefs(&fTypeEnum, &typeDef, 1, &fetched)) && fetched != 0)
    {
        std::string displayTypeName;
        IfFailRet(MetadataHelpers::GetFQDisplayNameForToken(typeDef, trMDImport, displayTypeName, nullptr));

        HCORENUM fFuncEnum = nullptr;
        mdMethodDef mdMethod = mdMethodDefNil;
        fetched = 0;

        while (SUCCEEDED(trMDImport->EnumMethods(&fFuncEnum, typeDef, &mdMethod, 1, &fetched)) && fetched != 0)
        {
            ULONG nameLen = 0;
            if (FAILED(trMDImport->GetMethodProps(mdMethod, nullptr, nullptr, 0, &nameLen,
                                                  nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            std::vector<WCHAR> szFuncName(nameLen, '\0');
            if (FAILED(trMDImport->GetMethodProps(mdMethod, nullptr, szFuncName.data(), nameLen, nullptr,
                                                  nullptr, nullptr, nullptr, nullptr, nullptr)))
            {
                continue;
            }

            // Get generic types
            ToRelease<IMetaDataImport2> trMDImport2;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport2, reinterpret_cast<void **>(&trMDImport2)));

            HCORENUM fGenEnum = nullptr;
            mdGenericParam genParam = mdGenericParamNil;
            fetched = 0;
            std::string genParams;

            while (SUCCEEDED(trMDImport2->EnumGenericParams(&fGenEnum, mdMethod, &genParam, 1, &fetched)) && fetched == 1)
            {
                ULONG genNameLen = 0;
                if (FAILED(trMDImport2->GetGenericParamProps(genParam, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &genNameLen)))
                {
                    continue;
                }

                mdMethodDef memMethodDef = mdMethodDefNil;
                std::vector<WCHAR> szGenName(genNameLen, '\0');
                if (FAILED(trMDImport2->GetGenericParamProps(genParam, nullptr, nullptr, &memMethodDef, nullptr,
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

            fullName.insert(0, displayTypeName + '.');
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

    const auto functor = [&](const std::string &fullName, mdMethodDef &mdMethod) -> bool
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

HRESULT GetModulePdbInfo(ICorDebugModule *pModule, PDB::Identity &pdbId, std::string &pathPdb, std::vector<uint8_t> &embeddedPDB)
{
    HRESULT Status = S_OK;
    BOOL isInMemory = FALSE;
    IfFailRet(pModule->IsInMemory(&isInMemory));

    CORDB_ADDRESS moduleBaseAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&moduleBaseAddress));
    ToRelease<ICorDebugProcess> trProcess;
    IfFailRet(pModule->GetProcess(&trProcess));

    auto readProcessMemory = [&](CORDB_ADDRESS addr, void *buffer, uint32_t size) -> HRESULT
    {
        SIZE_T bytesRead = 0;
        IfFailRet(trProcess->ReadMemory(addr, size, static_cast<uint8_t *>(buffer), &bytesRead));
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
    std::vector<MemoryDebugDirectory> debugDirs(entriesCount);

    auto decompressEmbeddedPdb = [&](CORDB_ADDRESS rawDataAddr, uint32_t sizeOfData) -> bool
    {
        MemoryMpdbHeader mpdb{};
        if (FAILED(readProcessMemory(rawDataAddr, &mpdb, sizeof(mpdb))) || mpdb.signature != g_mpdb_magic)
        {
            return false;
        }

        const uint32_t compressedSize = sizeOfData - sizeof(MemoryMpdbHeader);
        std::vector<unsigned char> compressedBuffer(compressedSize);
        if (FAILED(readProcessMemory(rawDataAddr + sizeof(MemoryMpdbHeader), compressedBuffer.data(), compressedSize)))
        {
            return false;
        }

        return DecompressDeflateBuffer(compressedBuffer.data(), compressedSize, embeddedPDB, mpdb.uncompressed_size);
    };

    // Helper: Process debug directories and extract PDB info
    auto processDebugDirectories = [&](const std::vector<MemoryDebugDirectory> &dirs,
                                       const std::function<CORDB_ADDRESS(const MemoryDebugDirectory&)> &getRawAddr) -> bool
    {
        bool foundCodeView = false;
        bool foundEmbedded = false;

        for (const auto &dir : dirs)
        {
            if (dir.type == g_debug_type_embedded_pdb && dir.size_of_data > sizeof(MemoryMpdbHeader))
            {
                const CORDB_ADDRESS mpdbAddr = getRawAddr(dir);
                if (mpdbAddr != 0 && decompressEmbeddedPdb(mpdbAddr, dir.size_of_data))
                {
                    foundEmbedded = true;
                }

                continue;
            }

            if (dir.type != g_debug_type_codeview || dir.size_of_data < sizeof(MemoryRsdsHeader))
            {
                continue;
            }

            const CORDB_ADDRESS rsdsAddr = getRawAddr(dir);
            if (rsdsAddr == 0)
            {
                continue;
            }

            MemoryRsdsHeader rsds{};
            if (FAILED(readProcessMemory(rsdsAddr, &rsds, sizeof(rsds))) || rsds.signature != g_rsds_magic || rsds.age != 1)
            {
                continue;
            }

            std::memcpy(pdbId.data(), static_cast<void *>(rsds.guid), g_guid_size);
            static_assert(sizeof(dir.time_date_stamp) == g_stamp_size);
            std::memcpy(pdbId.data() + g_guid_size, &dir.time_date_stamp, g_stamp_size);

            const uint32_t pathLength = dir.size_of_data - sizeof(MemoryRsdsHeader);
            if (pathLength == 0 || pathLength >= g_max_path_size)
            {
                continue;
            }

            std::vector<char> pathBuffer(pathLength + 1, '\0');
            if (SUCCEEDED(readProcessMemory(rsdsAddr + sizeof(MemoryRsdsHeader), pathBuffer.data(), pathLength)))
            {
                pathPdb = std::string(pathBuffer.data());
                foundCodeView = true;
            }
        }
        return foundCodeView || foundEmbedded;
    };

    // Loaded layout (ReadyToRun DLLs): RVA directly maps to VA
    const auto loadedLayout = [&]() -> HRESULT
    {
        if (SUCCEEDED(readProcessMemory(moduleBaseAddress + debugDataDir.virtual_address,
                                        debugDirs.data(), debugDataDir.size)))
        {
            const auto getRsdsAddrLoaded = [&](const MemoryDebugDirectory &dir) -> CORDB_ADDRESS
            {
                return moduleBaseAddress + dir.address_of_raw_data;
            };

            if (processDebugDirectories(debugDirs, getRsdsAddrLoaded))
            {
                return S_OK;
            }
        }

        return E_FAIL;
    };

    // File layout (JIT-compiled DLL): Convert RVA to file offset
    const auto fileLayout = [&]() -> HRESULT
    {
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
        const auto rvaToFileOffset = [&sections](uint32_t rva) -> uint32_t
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

        const auto getRsdsAddrFile = [&](const MemoryDebugDirectory &dir) -> CORDB_ADDRESS
        {
            return (dir.pointer_to_raw_data != 0) ? moduleBaseAddress + dir.pointer_to_raw_data : 0;
        };

        if (processDebugDirectories(debugDirs, getRsdsAddrFile))
        {
            return S_OK;
        }

        return E_FAIL;
    };

    if (isInMemory == TRUE)
    {
        if (SUCCEEDED(loadedLayout()) ||
            SUCCEEDED(fileLayout())) // Fallback to file layout.
        {
            return S_OK;
        }
    }
    else
    {
        if (SUCCEEDED(fileLayout()) ||
            SUCCEEDED(loadedLayout())) // Fallback to loaded layout.
        {
            return S_OK;
        }
    }

    return E_FAIL;
}

HRESULT LoadPDB(ICorDebugModule *pModule, mdhandle_t &pdbHandle, MemoryBuffer &memBuff, std::string &pdbFilePath, std::vector<uint8_t> &embeddedPDB)
{
    HRESULT Status = S_OK;
    PDB::Identity pdbId;
    IfFailRet(GetModulePdbInfo(pModule, pdbId, pdbFilePath, embeddedPDB));

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

// Must be called with GetDebugInfoMutex() already locked.
// Returns nullptr in pPDBInfo if not found.
void FindPDBInfoAndSourceIndex(const Source &source, CORDB_ADDRESS modAddress, const PDBInfo *&pPDBInfo,
                               uint32_t &sourceFileIndex, PDB::GlobalFileIndex *pGlobalFileIndex)
{
    pPDBInfo = nullptr;
    sourceFileIndex = 0;

#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
    std::string fixedFilePath = to_uppercase(source.path);
#else
    std::string fixedFilePath = source.path;
#endif

    const std::string pathName = GetFileName(fixedFilePath);
    std::map<CORDB_ADDRESS, std::forward_list<uint32_t>> foundSourceIndices;

    const auto addSourceIndices = [&](CORDB_ADDRESS modAddr, const PDBInfo &pdbInfo) -> void
    {
        const auto findName = pdbInfo.m_sourceFileNameToIndices.find(pathName);
        if (findName == pdbInfo.m_sourceFileNameToIndices.cend())
        {
            return;
        }
        foundSourceIndices[modAddr].insert_after(foundSourceIndices[modAddr].before_begin(),
                                                 findName->second.cbegin(), findName->second.cend());
    };

    DebugInfoMap &debugInfoMap = GetDebugInfoMap();

    if (modAddress != 0)
    {
        const auto infoPair = debugInfoMap.find(modAddress);
        if (infoPair != debugInfoMap.cend())
        {
            addSourceIndices(modAddress, infoPair->second);
        }
    }
    else
    {
        for (const auto &[modAddr, pdbInfo] : debugInfoMap)
        {
            addSourceIndices(modAddr, pdbInfo);
        }
    }

    if (foundSourceIndices.empty())
    {
        return;
    }

    fixedFilePath = CanonicalizeFilePath(fixedFilePath);

    std::string currentResult;
    for (auto &[modAddr, sourceIndices] : foundSourceIndices)
    {
        for (const auto &sourceIndex : sourceIndices)
        {
            const auto infoPair = debugInfoMap.find(modAddr);
            if (infoPair == debugInfoMap.cend())
            {
                continue;
            }

            const PDBInfo &pdbInfo = infoPair->second;
            std::string sourceFilePath;
            std::string algorithm;
            std::string checksum;
            if (FAILED(PDBReader::GetSourceFile(pdbInfo.m_pdbHandle, sourceIndex, sourceFilePath, algorithm, checksum)))
            {
                continue;
            }

            if (!algorithm.empty() && !checksum.empty() && !source.checksums.empty())
            {
                bool hasChecksum = false;
                for (const auto &entry : source.checksums)
                {
                    if (entry.algorithm.empty() || entry.checksum.empty())
                    {
                        continue;
                    }

                    hasChecksum = true;

                    if (entry.algorithm == algorithm && entry.checksum == checksum)
                    {
                        if (pGlobalFileIndex != nullptr)
                        {
                            pGlobalFileIndex->sourceFileIndex = sourceIndex;
                            pGlobalFileIndex->modAddress = modAddr;
                        }
                        sourceFileIndex = sourceIndex;
                        pPDBInfo = &pdbInfo;
                        return;
                    }
                }

                if (hasChecksum)
                {
                    continue;
                }
            }

            if (fixedFilePath == sourceFilePath)
            {
                if (pGlobalFileIndex != nullptr)
                {
                    pGlobalFileIndex->sourceFileIndex = sourceIndex;
                    pGlobalFileIndex->modAddress = modAddr;
                }
                sourceFileIndex = sourceIndex;
                pPDBInfo = &pdbInfo;
                return;
            }

            if (fixedFilePath.size() > sourceFilePath.size())
            {
                continue;
            }

            // Prevent partial path matches; for example, the source "folder/source.cs" should not match the requested path "der/source.cs".
            if (fixedFilePath.size() < sourceFilePath.size() && fixedFilePath.at(0) != '/' && fixedFilePath.at(0) != '\\' &&
                sourceFilePath.at(sourceFilePath.size() - fixedFilePath.size() - 1) != '/' && sourceFilePath.at(sourceFilePath.size() -
                                  fixedFilePath.size() - 1) != '\\')
            {
                continue;
            }

            // Note: since assemblies could be built on different OSes, source file paths could use different delimiters.
            const auto BinaryPredicate =
                [](const char &a, const char &b) -> bool
                {
                    if ((a == '/' || a == '\\') && (b == '/' || b == '\\'))
                    {
                        return true;
                    }
                    return a == b;
                };
            if (currentResult.empty() ||
                (std::equal(fixedFilePath.cbegin(), fixedFilePath.cend(), sourceFilePath.end() -
                            static_cast<std::string::difference_type>(fixedFilePath.size()), BinaryPredicate) &&
                 currentResult.length() > fixedFilePath.length()))
            {
                currentResult = fixedFilePath;
                if (pGlobalFileIndex != nullptr)
                {
                    pGlobalFileIndex->sourceFileIndex = sourceIndex;
                    pGlobalFileIndex->modAddress = modAddr;
                }
                sourceFileIndex = sourceIndex;
                pPDBInfo = &pdbInfo;
            }
        }
    }
}

} // unnamed namespace

HRESULT GetPDBInfo(CORDB_ADDRESS modAddress, const PDBInfoCallback &cb)
{
    const std::scoped_lock<std::mutex> lock(GetDebugInfoMutex());
    const auto infoPair = GetDebugInfoMap().find(modAddress);
    return (infoPair == GetDebugInfoMap().cend()) ? E_FAIL : cb(infoPair->second);
}

HRESULT ResolveFunctionBreakpointInAny(const std::string &funcname, const ResolveFunctionBreakpointCallback &cb)
{
    const std::scoped_lock<std::mutex> lock(GetDebugInfoMutex());

    for (const auto &[modAddr, pdbInfo] : GetDebugInfoMap())
    {
        ResolveMethodInModule(pdbInfo.m_trModule, funcname, cb);
    }

    return S_OK;
}

HRESULT ResolveFunctionBreakpointInModule(ICorDebugModule *pModule, const std::string &funcname,
                                          const ResolveFunctionBreakpointCallback &cb)
{
    return ResolveMethodInModule(pModule, funcname, cb);
}

HRESULT GetStepRangeFromCurrentIP(ICorDebugThread *pThread, COR_DEBUG_STEP_RANGE &range)
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

void TryLoadModuleSymbols(ICorDebugModule *pModule, Module &module)
{
    mdhandle_t pdbHandle = nullptr;
    MemoryBuffer memBuff;
    std::vector<uint8_t> embeddedPDB;
    const HRESULT Status = LoadPDB(pModule, pdbHandle, memBuff, module.symbolFilePath, embeddedPDB);
    module.symbolStatus = SUCCEEDED(Status) ? SymbolStatus::Loaded : SymbolStatus::NotFound;

    if (module.symbolStatus != SymbolStatus::Loaded)
    {
        return;
    }

    CORDB_ADDRESS modAddress = 0;
    if (FAILED(pModule->GetBaseAddress(&modAddress)))
    {
        DAP::EmitOutputEvent({OutputCategory::StdErr, "Could not find module base address.\n"});
        return;
    }

    PDB::SourceNameMap sourceFileNameToIndicesMap;
    if (FAILED(PDBReader::GetAllSourceFiles(pdbHandle, sourceFileNameToIndicesMap)))
    {
        DAP::EmitOutputEvent({OutputCategory::StdErr,
            "Could not load source file names related info from PDB file.\n"});
    }

    PDB::SourceMethodRanges sourceMethodRanges;
    if (FAILED(DebugSources::FillMethodRanges(pModule, pdbHandle, sourceMethodRanges)))
    {
        DAP::EmitOutputEvent({OutputCategory::StdErr,
            "Could not load source lines related info from PDB file. Could produce failures during "
            "breakpoint's source path resolve in future.\n"});
    }

    std::unordered_map<uint32_t, uint32_t> moveNextToKickoff;
    std::unordered_map<uint32_t, uint32_t> kickoffToMoveNext;
    PDBReader::GetStateMachineMethods(pdbHandle, moveNextToKickoff, kickoffToMoveNext);

    std::vector<Source> newSources = SourceReference::LoadModule(pdbHandle, modAddress);

    pModule->AddRef();
    PDBInfo pdbInfo{pdbHandle, std::move(memBuff), std::move(embeddedPDB), pModule,
                    std::move(sourceFileNameToIndicesMap), std::move(sourceMethodRanges),
                    std::move(moveNextToKickoff), std::move(kickoffToMoveNext)};
    {
        const std::scoped_lock<std::mutex> lock(GetDebugInfoMutex());
        GetDebugInfoMap().insert(std::make_pair(modAddress, std::move(pdbInfo)));
    }

    // Emit events after all debugger-internal locks are released to avoid holding them during protocol I/O.
    for (auto &source : newSources)
    {
        DAP::EmitLoadedSourceEvent(LoadedSourceEvent(LoadedSourceEventReason::New, std::move(source)));
    }
}

void UnloadModuleSymbols(ICorDebugModule *pModule)
{
    CORDB_ADDRESS modAddress = 0;
    if (FAILED(pModule->GetBaseAddress(&modAddress)))
    {
        DAP::EmitOutputEvent({OutputCategory::StdErr, "Could not find module base address.\n"});
        return;
    }

    std::vector<Source> removedSources;
    GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            removedSources = SourceReference::UnloadModule(pdbInfo.m_pdbHandle, modAddress);
            return S_OK;
        });

    // Emit events after all debugger-internal locks are released to avoid holding them during protocol I/O.
    for (auto &source : removedSources)
    {
        DAP::EmitLoadedSourceEvent(LoadedSourceEvent(LoadedSourceEventReason::Removed, std::move(source)));
    }

    const std::scoped_lock<std::mutex> lock(GetDebugInfoMutex());
    GetDebugInfoMap().erase(modAddress);
}

HRESULT GetFrameNamedLocalVariable(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset,
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

bool IsHoistedLocalInScope(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset, uint32_t hoistedLocalIndex)
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

HRESULT GetNextUserCodeILOffset(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset, uint32_t &ilNextOffset)
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

HRESULT GetNextUserCodeILOffset(ICorDebugFrame *pFrame, uint32_t &ilOffset, uint32_t &ilNextOffset)
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

HRESULT GetSourceFile(const PDB::GlobalFileIndex &globalFileIndex, std::string &sourceFilePath,
                      std::string &algorithm, std::string &checksum)
{
    return GetPDBInfo(globalFileIndex.modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            return PDBReader::GetSourceFile(pdbInfo.m_pdbHandle, globalFileIndex.sourceFileIndex,
                                            sourceFilePath, algorithm, checksum);
        });
}

HRESULT GetSequencePointByILOffset(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t ilOffset,
                                   PDB::SequencePoint &sequencePoint)
{
    return GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            return PDBReader::GetSequencePointByILOffset(pdbInfo.m_pdbHandle, methodToken, ilOffset, sequencePoint);
        });
}

HRESULT GetSequencePointByFrame(ICorDebugFrame *pFrame, PDB::SequencePoint &sequencePoint,
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

HRESULT ResolveBreakpoint(CORDB_ADDRESS modAddress, const Source &source, int32_t sourceLine,
                          int32_t sourceColumn, PDB::GlobalFileIndex *pGlobalFileIndex,
                          std::vector<PDB::ResolvedBreakpoint> &resolvedPoints)
{
    if (source.sourceReference > 0)
    {
        PDB::GlobalFileIndex globalFileIndex;
        if (FAILED(SourceReference::GetGlobalIndex(source.sourceReference, globalFileIndex)))
        {
            return E_INVALIDARG;
        }

        if (pGlobalFileIndex != nullptr)
        {
            *pGlobalFileIndex = globalFileIndex;
        }

        return GetPDBInfo(globalFileIndex.modAddress,
            [&](const PDBInfo &pdbInfo) -> HRESULT
            {
                return DebugSources::ResolveBreakpoints(pdbInfo, globalFileIndex.sourceFileIndex, sourceLine, sourceColumn, resolvedPoints);
            });
    }

    const std::scoped_lock<std::mutex> lockDebugInfoInfo(GetDebugInfoMutex());

    const PDBInfo *pPDBInfo = nullptr;
    uint32_t resolvedSourceFileIndex = 0;
    FindPDBInfoAndSourceIndex(source, modAddress, pPDBInfo, resolvedSourceFileIndex, pGlobalFileIndex);

    if (pPDBInfo == nullptr)
    {
        return E_FAIL;
    }

    return DebugSources::ResolveBreakpoints(*pPDBInfo, resolvedSourceFileIndex, sourceLine, sourceColumn, resolvedPoints);
}

HRESULT GetLocalConstants(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset,
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

bool IsStateMachineKickoffMethod(ICorDebugFunction *pFunction)
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
            res = pdbInfo.m_kickoffToMoveNext.find(methodToken) != pdbInfo.m_kickoffToMoveNext.cend();
            return S_OK;
        });

    return res;
}

HRESULT GetStateMachineKickoffMethod(ICorDebugModule *pModule, mdMethodDef moveNextMethodToken,
                                     mdMethodDef &kickoffMethodToken)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    return GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            const auto find = pdbInfo.m_moveNextToKickoff.find(moveNextMethodToken);
            if (find == pdbInfo.m_moveNextToKickoff.cend())
            {
                return E_FAIL;
            }

            kickoffMethodToken = find->second;
            return S_OK;
        });
}

HRESULT GetImportsAndAliases(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset,
                             std::unordered_map<PDB::ImportsKind, std::vector<PDB::Imports>> &pdbImports)
{
    HRESULT Status = S_OK;
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&modAddress));

    return GetPDBInfo(modAddress,
        [&](const PDBInfo &pdbInfo) -> HRESULT
        {
            return PDBReader::GetImportsAndAliases(pdbInfo.m_pdbHandle, methodToken, ilOffset, pdbImports);
        });
}

HRESULT GetSourceContent(const Source &source, std::string &sourceContent)
{
    if (source.sourceReference > 0)
    {
        PDB::GlobalFileIndex globalFileIndex;
        if (FAILED(SourceReference::GetGlobalIndex(source.sourceReference, globalFileIndex)))
        {
            return E_INVALIDARG;
        }

        std::string url;
        if (SUCCEEDED(SourceReference::GetSourceURL(globalFileIndex, url)))
        {
            return DownloadSource(url, sourceContent) ? S_OK : E_FAIL;
        }

        return GetPDBInfo(globalFileIndex.modAddress,
            [&](const PDBInfo &pdbInfo) -> HRESULT
            {
                return PDBReader::GetEmbeddedSource(pdbInfo.m_pdbHandle, globalFileIndex.sourceFileIndex, sourceContent);
            });
    }

    if (source.path.empty())
    {
        return E_INVALIDARG;
    }

    const std::scoped_lock<std::mutex> lockDebugInfoInfo(GetDebugInfoMutex());

    const PDBInfo *pPDBInfo = nullptr;
    uint32_t resolvedSourceFileIndex = 0;
    FindPDBInfoAndSourceIndex(source, 0, pPDBInfo, resolvedSourceFileIndex, nullptr);

    if (pPDBInfo == nullptr)
    {
        return E_FAIL;
    }

    CORDB_ADDRESS modAddress = 0;
    std::string url;
    if (pPDBInfo->m_trModule != nullptr &&
        SUCCEEDED(pPDBInfo->m_trModule->GetBaseAddress(&modAddress)) &&
        SUCCEEDED(SourceReference::GetSourceURL({modAddress, resolvedSourceFileIndex}, url)))
    {
        return DownloadSource(url, sourceContent) ? S_OK : E_FAIL;
    }

    return PDBReader::GetEmbeddedSource(pPDBInfo->m_pdbHandle, resolvedSourceFileIndex, sourceContent);
}

void GetLoadedSources(std::vector<Source> &sources)
{
    const std::scoped_lock<std::mutex> lock(GetDebugInfoMutex());

    for (const auto &[modAddr, pdbInfo] : GetDebugInfoMap())
    {
        SourceReference::AddLoadedSourcesForModule(pdbInfo.m_pdbHandle, modAddr, sources);
    }
}

HRESULT GetBreakpointLocations(const Source &source, const BreakpointLocation &rangeToSearch,
                               std::vector<BreakpointLocation> &locations)
{
    HRESULT Status = S_OK;
    locations.clear();

    if (source.sourceReference > 0)
    {
        PDB::GlobalFileIndex globalFileIndex;
        if (FAILED(SourceReference::GetGlobalIndex(source.sourceReference, globalFileIndex)))
        {
            return E_INVALIDARG;
        }

        return GetPDBInfo(globalFileIndex.modAddress,
            [&](const PDBInfo &pdbInfo) -> HRESULT
            {
                std::vector<mdMethodDef> methodTokens;
                IfFailRet(DebugSources::FindMethodsInRange(pdbInfo, globalFileIndex.sourceFileIndex, rangeToSearch,
                                                           methodTokens));

                return PDBReader::GetBreakpointLocations(pdbInfo.m_pdbHandle, methodTokens,
                                                         globalFileIndex.sourceFileIndex, rangeToSearch, locations);
            });
    }

    const std::scoped_lock<std::mutex> lockDebugInfoInfo(GetDebugInfoMutex());

    const PDBInfo *pPDBInfo = nullptr;
    uint32_t resolvedSourceFileIndex = 0;
    FindPDBInfoAndSourceIndex(source, 0, pPDBInfo, resolvedSourceFileIndex, nullptr);

    if (pPDBInfo == nullptr)
    {
        return E_FAIL;
    }

    std::vector<mdMethodDef> methodTokens;
    IfFailRet(DebugSources::FindMethodsInRange(*pPDBInfo, resolvedSourceFileIndex, rangeToSearch, methodTokens));

    return PDBReader::GetBreakpointLocations(pPDBInfo->m_pdbHandle, methodTokens,
                                             resolvedSourceFileIndex, rangeToSearch, locations);
}

void Cleanup()
{
    AsyncInfo::Cleanup();
    SourceReference::Cleanup();

    const std::scoped_lock<std::mutex> lock(GetDebugInfoMutex());

    GetDebugInfoMap().clear();
}

} // namespace dncdbg::DebugInfo
