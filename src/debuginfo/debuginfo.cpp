// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debuginfo/debuginfo.h"
#include "config/config.h"
#include "debuginfo/asyncinfo.h"
#include "debuginfo/debugsources.h"
#include "debuginfo/pdbreader.h"
#include "debuginfo/sourcereference.h"
#include "protocol/dap_events.h"
#include "utils/downloader.h"
#include "utils/filesystem.h"
#include "utils/hresult.h"
#include "utils/utftoupper.h"
#include <algorithm>
#include <cstring>
#include <forward_list>
#include <mutex>
#include <string_view>
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
    // The debugDataDir.size from the header may not be a multiple of sizeof(MemoryDebugDirectory), so it can be
    // larger than the storage of debugDirs (entriesCount entries). Read only the storage size; reading
    // debugDataDir.size bytes would write past the end of the vector.
    const auto debugDirsSize = static_cast<uint32_t>(debugDirs.size() * sizeof(MemoryDebugDirectory));

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
                                       auto getRawAddr) -> bool
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

            // The extra byte guarantees a terminating null after the path is read.
            std::string rsdsPath(pathLength + 1, '\0');
            if (SUCCEEDED(readProcessMemory(rsdsAddr + sizeof(MemoryRsdsHeader), rsdsPath.data(), pathLength)))
            {
                rsdsPath.resize(std::strlen(rsdsPath.c_str()));
                pathPdb = std::move(rsdsPath);
                foundCodeView = true;
            }
        }
        return foundCodeView || foundEmbedded;
    };

    // Loaded layout (ReadyToRun DLLs): RVA directly maps to VA
    const auto loadedLayout = [&]() -> HRESULT
    {
        if (SUCCEEDED(readProcessMemory(moduleBaseAddress + debugDataDir.virtual_address,
                                        debugDirs.data(), debugDirsSize)))
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
                                     debugDirs.data(), debugDirsSize)))
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

HRESULT LoadPDB(ICorDebugModule *pModule, mdhandle_t &pdbHandle, MemoryBuffer &memBuff,
                const std::string &moduleFilePath, std::string &pdbFilePath, std::vector<uint8_t> &embeddedPDB)
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
    const std::string modulePath = GetParentPath(moduleFilePath);
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
    // Handle all "./" and "../". Directories are kept in order of appearance; the last element is the most recent one.
    // The views point into filePath, so the path is split without copying the remaining part on each step.
    std::vector<std::string_view> pathDirs;
    std::string_view rest = filePath;
    std::size_t i = 0;
    while ((i = rest.find_first_of("/\\")) != std::string_view::npos)
    {
        const std::string_view pathElement = rest.substr(0, i);
        if (pathElement == "..")
        {
            if (!pathDirs.empty())
            {
                pathDirs.pop_back();
            }
        }
        else if (pathElement != ".")
        {
            pathDirs.push_back(pathElement);
        }

        rest.remove_prefix(i + 1);
    }

    // Use '/' as the delimiter; IsPathSuffixMatch() accepts both delimiters when comparing against source paths.
    std::string result;
    result.reserve(filePath.size());
    for (const auto &dir : pathDirs)
    {
        result.append(dir.data(), dir.size());
        result.push_back('/');
    }
    result.append(rest.data(), rest.size());

    return result;
}

constexpr bool IsPathDelimiter(char c) noexcept
{
    return c == '/' || c == '\\';
}

// Note: since assemblies could be built on different OSes, source file paths could use different delimiters.
constexpr bool IsSamePathChar(char a, char b) noexcept
{
    return (IsPathDelimiter(a) && IsPathDelimiter(b)) || a == b;
}

// Returns true if sourceFilePath is equal to fixedFilePath or ends with it on a path element boundary.
bool IsPathSuffixMatch(const std::string &fixedFilePath, const std::string &sourceFilePath)
{
    if (fixedFilePath.empty() || fixedFilePath.size() > sourceFilePath.size())
    {
        return false;
    }

    const std::size_t suffixPos = sourceFilePath.size() - fixedFilePath.size();

    // Prevent partial path matches; for example, the source "folder/source.cs" should not match the requested path "der/source.cs".
    if (suffixPos > 0 && !IsPathDelimiter(fixedFilePath.at(0)) && !IsPathDelimiter(sourceFilePath.at(suffixPos - 1)))
    {
        return false;
    }

    return std::equal(fixedFilePath.cbegin(), fixedFilePath.cend(),
                      sourceFilePath.cbegin() + static_cast<std::string::difference_type>(suffixPos), IsSamePathChar);
}

// Must be called with GetDebugInfoMutex() already locked.
// Returns nullptr in pPDBInfo if not found.
void FindPDBInfoAndSourceIndex(const Source &source, CORDB_ADDRESS modAddress, const PDBInfo *&pPDBInfo,
                               uint32_t &sourceFileIndex, PDB::GlobalFileIndex *pGlobalFileIndex)
{
    pPDBInfo = nullptr;
    sourceFileIndex = 0;

#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
    std::string fixedFilePath = ToUppercase(source.path);
#else
    std::string fixedFilePath = source.path;
#endif

    const std::string pathName = GetFileName(fixedFilePath);

    // Modules that contain the file name. Index lists are referenced rather than copied.
    struct Candidate
    {
        CORDB_ADDRESS modAddr;
        const PDBInfo *pdbInfo;
        const std::forward_list<uint32_t> *sourceIndices;
    };
    std::vector<Candidate> candidates;

    const auto addCandidate = [&](CORDB_ADDRESS modAddr, const PDBInfo &pdbInfo) -> void
    {
        const auto findName = pdbInfo.m_sourceFileNameToIndices.find(pathName);
        if (findName != pdbInfo.m_sourceFileNameToIndices.cend())
        {
            candidates.push_back(Candidate{modAddr, &pdbInfo, &findName->second});
        }
    };

    const DebugInfoMap &debugInfoMap = GetDebugInfoMap();

    if (modAddress != 0)
    {
        const auto infoPair = debugInfoMap.find(modAddress);
        if (infoPair != debugInfoMap.cend())
        {
            addCandidate(modAddress, infoPair->second);
        }
    }
    else
    {
        for (const auto &[modAddr, pdbInfo] : debugInfoMap)
        {
            addCandidate(modAddr, pdbInfo);
        }

        // Sort by module address so the result does not depend on hash map iteration order.
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate &a, const Candidate &b) -> bool
                  {
                      return a.modAddr < b.modAddr;
                  });
    }

    if (candidates.empty())
    {
        return;
    }

    fixedFilePath = CanonicalizeFilePath(fixedFilePath);

    // Checked once for all candidates: true if the request carries at least one complete checksum.
    const bool hasRequestedChecksum = std::any_of(source.checksums.cbegin(), source.checksums.cend(),
        [](const Checksum &entry) -> bool
        {
            return !entry.algorithm.empty() && !entry.checksum.empty();
        });

    // Buffers are declared outside the loops so their capacity is reused by every GetSourceFile() call.
    std::string sourceFilePath;
    std::string algorithm;
    std::string checksum;
    // Stores the matched location in the output parameters.
    const auto setMatch = [&](uint32_t sourceIndex, CORDB_ADDRESS modAddr, const PDBInfo &pdbInfo) -> void
    {
        if (pGlobalFileIndex != nullptr)
        {
            pGlobalFileIndex->sourceFileIndex = sourceIndex;
            pGlobalFileIndex->modAddress = modAddr;
        }
        sourceFileIndex = sourceIndex;
        pPDBInfo = &pdbInfo;
    };

    // The first suffix match in candidate order is kept; an exact path or checksum match overrides it.
    bool hasSuffixMatch = false;
    for (const auto &candidate : candidates)
    {
        const CORDB_ADDRESS modAddr = candidate.modAddr;
        const PDBInfo &pdbInfo = *candidate.pdbInfo;
        for (const auto &sourceIndex : *candidate.sourceIndices)
        {
            if (FAILED(PDBReader::GetSourceFile(pdbInfo.m_pdbHandle, sourceIndex, sourceFilePath, algorithm, checksum)))
            {
                continue;
            }

            if (hasRequestedChecksum && !algorithm.empty() && !checksum.empty())
            {
                // Entries with an empty algorithm or checksum never match a non-empty PDB checksum.
                const auto checksumIt = std::find_if(source.checksums.cbegin(), source.checksums.cend(),
                    [&](const Checksum &entry) -> bool
                    {
                        return entry.algorithm == algorithm && entry.checksum == checksum;
                    });
                if (checksumIt != source.checksums.cend())
                {
                    setMatch(sourceIndex, modAddr, pdbInfo);
                    return;
                }

                // Checksums are present on both sides and do not match, so the path is not compared.
                continue;
            }

#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
            // The requested path is uppercased above, so the PDB path must be uppercased to be compared with it.
            sourceFilePath = ToUppercase(sourceFilePath);
#endif

            if (fixedFilePath == sourceFilePath)
            {
                setMatch(sourceIndex, modAddr, pdbInfo);
                return;
            }

            if (!hasSuffixMatch && IsPathSuffixMatch(fixedFilePath, sourceFilePath))
            {
                hasSuffixMatch = true;
                setMatch(sourceIndex, modAddr, pdbInfo);
            }
        }
    }
}

} // unnamed namespace

HRESULT GetPDBInfo(CORDB_ADDRESS modAddress, PDBInfoCallback cb)
{
    const std::scoped_lock<std::mutex> lock(GetDebugInfoMutex());
    const DebugInfoMap &debugInfoMap = GetDebugInfoMap();
    const auto infoPair = debugInfoMap.find(modAddress);
    return (infoPair == debugInfoMap.cend()) ? E_FAIL : cb(infoPair->second);
}

HRESULT GetEachPDBInfo(PDBInfoCallback cb)
{
    HRESULT Status = S_OK;

    const std::scoped_lock<std::mutex> lock(GetDebugInfoMutex());

    for (const auto &entry : GetDebugInfoMap())
    {
        IfFailRet(cb(entry.second));
    }

    return S_OK;
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
    const HRESULT Status = LoadPDB(pModule, pdbHandle, memBuff, module.path, module.symbolFilePath, embeddedPDB);
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

    // Release memory-mapped PDB pages once module symbol initialization reads are done, per the
    // configured policy, so the pages are re-read from the backing file on demand instead of
    // staying resident. The metadata handle references memory inside the mapped view, and Flush()
    // may change the address of the mapped data (the Windows implementation recreates the mapped
    // view), so re-create the handle if the mapped data location has changed.
    if (Config::GetPdbMmapPolicy() == Config::pdbMmapFlushAfterInit)
    {
        const void *mappedDataBeforeFlush = memBuff.Data();
        memBuff.Flush();
        if (memBuff.Data() != mappedDataBeforeFlush)
        {
            md_destroy_handle(pdbHandle);
            pdbHandle = nullptr;
            if (!md_create_handle(memBuff.Data(), static_cast<uint32_t>(memBuff.Size()), &pdbHandle))
            {
                DAP::EmitOutputEvent({OutputCategory::StdErr,
                    "Could not re-create the PDB handle after flushing memory-mapped data.\n"});
                return;
            }
        }
    }

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

void Initialize()
{
    AsyncInfo::Cleanup();
    SourceReference::Initialize();

    const std::scoped_lock<std::mutex> lock(GetDebugInfoMutex());

    GetDebugInfoMap().clear();
}

void Cleanup()
{
    AsyncInfo::Cleanup();
    SourceReference::Cleanup();

    const std::scoped_lock<std::mutex> lock(GetDebugInfoMutex());

    GetDebugInfoMap().clear();
}

} // namespace dncdbg::DebugInfo
