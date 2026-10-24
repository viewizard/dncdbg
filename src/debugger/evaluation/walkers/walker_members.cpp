// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/walkers/walkers.h"
#include "config/config.h"
#include "debugger/evaluation/evalhelpers/evalexec.h"
#include "debugger/evaluation/evalhelpers/metadata.h"
#include "debugger/evaluation/evalhelpers/systemtypes.h"
#include "debugger/evaluation/evalhelpers/typeproxy.h"
#include "debugger/evaluation/evalhelpers/value.h"
#include "debugger/evaluation/primitivetypes/types.h"
#include "debugger/frames.h"
#include "metadata/attributes.h"
#include "metadata/helpers.h"
#include "metadata/modules.h"
#include "metadata/sigparse.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <array>
#include <cassert>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dncdbg::Walkers
{

namespace
{

std::mutex &GetMembersMutex()
{
    static std::mutex membersMutex;
    return membersMutex;
}

std::unordered_set<CORDB_ADDRESS> &GetInitializedTypesAddr()
{
    static std::unordered_set<CORDB_ADDRESS> initializedTypesAddr;
    return initializedTypesAddr;
}

std::unordered_map<std::string, bool> &GetModulePlainILImageCache()
{
    // Cached per module MVID: true when the module is a confirmed plain IL image, false for
    // ReadyToRun ones and when the image could not be inspected (see IsModulePlainILImage()).
    static std::unordered_map<std::string, bool> plainILImageCache;
    return plainILImageCache;
}

void IncIndices(const std::vector<uint32_t> &dims, std::vector<uint32_t> &ind)
{
    assert(ind.size() <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));
    int i = static_cast<int32_t>(ind.size()) - 1;

    while (i >= 0)
    {
        ind.at(i) += 1;
        if (ind.at(i) < dims.at(i))
        {
            return;
        }
        ind.at(i) = 0;
        --i;
    }
}

std::string IndicesToStr(const std::vector<uint32_t> &ind, const std::vector<uint32_t> &base)
{
    const size_t ind_size = ind.size();
    if (ind_size < 1 || base.size() != ind_size)
    {
        return {};
    }

    std::ostringstream ss;
    const char *sep = "";
    for (size_t i = 0; i < ind_size; ++i)
    {
        ss << sep;
        sep = ", ";
        ss << (base.at(i) + ind.at(i));
    }
    return ss.str();
}

constexpr uint16_t g_dos_e_magic = 0x5A4D;
constexpr uint32_t g_ntsig_magic = 0x00004550;
constexpr uint16_t g_opt_header32_magic = 0x10B;
constexpr uint16_t g_opt_header64_magic = 0x20B;
constexpr uint8_t g_section_name_size = 8;
constexpr uint8_t g_max_sections_count = 96;
constexpr uint8_t g_ignored_size = 58;
// 'RTR' signature of the ReadyToRun header (READYTORUN_SIGNATURE).
constexpr uint32_t g_rtr_magic = 0x00525452;
// Size of the CLR (COR20) header in the PE image; see MemoryCor20Header below.
constexpr uint8_t g_cor20_header_size = 72;

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

// CLR (COR20) header, see IMAGE_COR20_HEADER in corhdr.h.
struct MemoryCor20Header
{
    uint32_t cb;
    uint16_t major_runtime_version;
    uint16_t minor_runtime_version;
    MemoryDataDirectory metadata;
    uint32_t flags;
    uint32_t entry_point_token;
    MemoryDataDirectory resources;
    MemoryDataDirectory strong_name_signature;
    MemoryDataDirectory code_manager_table;
    MemoryDataDirectory vtable_fixups;
    MemoryDataDirectory export_address_table_jumps;
    // Null for ordinary IL images; points to a ReadyToRun header ('RTR') for ReadyToRun images.
    MemoryDataDirectory managed_native_header;
};
#pragma pack(pop)
// The ManagedNativeHeader data directory occupies the last 8 bytes of the CLR header.
static_assert(sizeof(MemoryCor20Header) == g_cor20_header_size);

// Detect whether the module is a plain IL image, i.e. not a ReadyToRun (R2R) one. The PE image
// headers are read from the debuggee's memory (see GetModulePdbInfo() in debuginfo.cpp for the
// general approach): the ManagedNativeHeader data directory of the CLR (COR20) header is null
// for ordinary IL images, while ReadyToRun images point it to a READYTORUN_HEADER ('RTR').
// The check matters because in ReadyToRun images all methods, including the static constructor,
// are precompiled, so the JIT-based initialization detection is not applicable.
// Returns S_OK only when the module is confirmed to be a plain IL image; returns a failure
// HRESULT for ReadyToRun modules and when the image could not be inspected, so that the caller
// switches to the fallback type-initialization detection logic.
HRESULT IsModulePlainILImage(ICorDebugModule *pModule)
{
    HRESULT Status = S_OK;

    CORDB_ADDRESS moduleBaseAddress = 0;
    IfFailRet(pModule->GetBaseAddress(&moduleBaseAddress));

    std::string mvid;
    IfFailRet(Modules::GetModuleMvid(pModule, mvid));

    std::unordered_map<std::string, bool> &plainILImageCache = GetModulePlainILImageCache();
    {
        const std::scoped_lock<std::mutex> lock(GetMembersMutex());

        const auto iter = plainILImageCache.find(mvid);
        if (iter != plainILImageCache.cend())
        {
            return iter->second ? S_OK : E_FAIL;
        }
    }

    ToRelease<ICorDebugProcess> trProcess;
    IfFailRet(pModule->GetProcess(&trProcess));

    const auto readProcessMemory = [&](CORDB_ADDRESS addr, void *buffer, uint32_t size) -> HRESULT
    {
        // The callers may pass an unmapped address (e.g. an RVA that is not covered by any section
        // in the file layout), and reading at address 0 is never valid.
        if (addr == 0)
        {
            return E_FAIL;
        }

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

    // Data Directory index 14 (IMAGE_DIRECTORY_ENTRY_COMHEADER) offset differs between PE32 and PE32+
    const uint32_t comDirEntryOffset = (magic == g_opt_header32_magic) ? 208 : 224;

    // Read CLR (COR20) header data directory entry
    MemoryDataDirectory comDataDir{};
    IfFailRet(readProcessMemory(optionalHeaderAddr + comDirEntryOffset, &comDataDir, sizeof(comDataDir)));

    if (comDataDir.virtual_address == 0 || comDataDir.size < sizeof(MemoryCor20Header))
    {
        return E_FAIL;
    }

    bool isR2RDetected = false;

    // Helper: Inspect the CLR (COR20) header via the given RVA-to-debuggee-address mapping.
    // Returns S_OK when the image looks like a plain IL one in this layout, E_FAIL when it is
    // a ReadyToRun one (then isR2RDetected is set) or when it could not be inspected via this mapping.
    const auto inspectCor20Header = [&](const std::function<CORDB_ADDRESS(uint32_t rva)> &rvaToAddr) -> HRESULT
    {
        MemoryCor20Header cor20{};
        if (FAILED(readProcessMemory(rvaToAddr(comDataDir.virtual_address), &cor20, sizeof(cor20))) ||
            cor20.cb < sizeof(MemoryCor20Header) || cor20.metadata.virtual_address == 0)
        {
            return E_FAIL; // Not a valid CLR header in this layout.
        }

        // ManagedNativeHeader is null for ordinary IL images.
        if (cor20.managed_native_header.virtual_address == 0)
        {
            return S_OK;
        }

        // Validate the ReadyToRun header signature so that misread data (e.g. when the image
        // is mapped in the other layout) is not mistaken for a ReadyToRun header.
        uint32_t signature = 0;
        if (FAILED(readProcessMemory(rvaToAddr(cor20.managed_native_header.virtual_address), &signature, sizeof(signature))) ||
            signature != g_rtr_magic)
        {
            return E_FAIL;
        }

        isR2RDetected = true;
        return E_FAIL;
    };

    // Loaded layout (ReadyToRun DLLs): RVA directly maps to VA
    const auto loadedLayout = [&]() -> HRESULT
    {
        return inspectCor20Header([&moduleBaseAddress](uint32_t rva) -> CORDB_ADDRESS
        {
            return moduleBaseAddress + rva;
        });
    };

    // File layout (plain IL images are mapped as a raw file): convert RVA to file offset via the section table.
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

        // Read section headers for RVA-to-file-offset conversion
        const CORDB_ADDRESS sectionHeadersAddr = optionalHeaderAddr + sizeOfOptionalHeader;
        std::vector<MemorySectionHeader> sections(numberOfSections);
        if (FAILED(readProcessMemory(sectionHeadersAddr, sections.data(),
                                     static_cast<uint32_t>(numberOfSections * sizeof(MemorySectionHeader)))))
        {
            return E_FAIL;
        }

        // Helper: Convert RVA to file offset using the section table
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

        const uint32_t corHeaderFileOffset = rvaToFileOffset(comDataDir.virtual_address);
        if (corHeaderFileOffset == 0)
        {
            return E_FAIL;
        }

        return inspectCor20Header([&moduleBaseAddress, &rvaToFileOffset](uint32_t rva) -> CORDB_ADDRESS
        {
            const uint32_t fileOffset = rvaToFileOffset(rva);
            return (fileOffset != 0) ? moduleBaseAddress + fileOffset : 0;
        });
    };

    // Inspect the image in both layouts: ReadyToRun images are mapped by the runtime
    // section-by-section (RVA directly maps to VA), while plain IL images are mapped as a raw
    // file (addresses map to file offsets), so the working layout depends on the module kind.
    const HRESULT loadedLayoutStatus = loadedLayout();
    const HRESULT fileLayoutStatus = fileLayout();

    const std::scoped_lock<std::mutex> lock(GetMembersMutex());

    // The 'RTR' signature is validated, so the ReadyToRun detection takes precedence over
    // possibly misread plain IL layout data.
    if (isR2RDetected || (FAILED(loadedLayoutStatus) && FAILED(fileLayoutStatus)))
    {
        plainILImageCache.emplace(mvid, false);
        return E_FAIL; // ReadyToRun module or undetermined image.
    }

    plainILImageCache.emplace(mvid, true);
    return S_OK; // Plain IL module confirmed by at least one layout inspection.
}

HRESULT InitializeStaticFields(ICorDebugThread *pThread, ICorDebugType *pType, FormatSpecifier specifier)
{
    HRESULT Status = S_OK;

    // Detect whether the class is initialized (its static constructor .cctor has run). For plain
    // IL modules, the static constructor is considered to have run if it has already been
    // JIT-compiled; for ReadyToRun modules everything is precompiled, so we read the MethodTable
    // initialization flag directly from the debuggee's memory instead. COR_TYPEID.token1 is the
    // MethodTable address. MethodTable contains the m_pAuxiliaryData pointer, and
    // MethodTableAuxiliaryData has m_dwFlags, where bit 0 (enum_flag_Initialized = 0x0001)
    // indicates whether the class constructor has run.
    static constexpr DWORD enum_flag_Initialized = 0x0001;

    bool isClassInitialized = false; // Assume not initialized by default.

    std::unordered_set<CORDB_ADDRESS> &initializedTypesAddr = GetInitializedTypesAddr();

    // Get the MethodTable address via ICorDebugType2::GetTypeID().
    ToRelease<ICorDebugType2> trType2;
    COR_TYPEID typeID{0, 0};
    if (SUCCEEDED(pType->QueryInterface(IID_ICorDebugType2, reinterpret_cast<void **>(&trType2))) &&
        SUCCEEDED(trType2->GetTypeID(&typeID)) && typeID.token1 != 0)
    {
        // typeID.token1 is the MethodTable address.
        const CORDB_ADDRESS methodTableAddr = typeID.token1;

        {
            const std::scoped_lock<std::mutex> lock(GetMembersMutex());

            // Nothing to do if initialization for this type was already ensured
            // during a previous evaluation (see the emplace() at the end of this function).
            if (initializedTypesAddr.find(methodTableAddr) != initializedTypesAddr.cend())
            {
                return S_OK;
            }
        }

        ToRelease<ICorDebugClass> trClass;
        IfFailRet(pType->GetClass(&trClass));
        ToRelease<ICorDebugModule> trModule;
        IfFailRet(trClass->GetModule(&trModule));
        mdTypeDef typeDef = mdTypeDefNil;
        IfFailRet(trClass->GetToken(&typeDef));
        ToRelease<IUnknown> trUnknown;
        IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
        ToRelease<IMetaDataImport> trMDImport;
        IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

        ULONG numMethods = 0;
        HCORENUM cEnum = nullptr;
        mdMethodDef methodDef = mdMethodDefNil;
        // There can be at most one static constructor per type, so a single fetch is sufficient.
        const bool hasStaticCtor = S_OK == trMDImport->EnumMethodsWithName(&cEnum, typeDef, W(".cctor"), &methodDef, 1, &numMethods) &&
                                   numMethods == 1;
        trMDImport->CloseEnum(cEnum);

        if (!hasStaticCtor)
        {
            // The type does not have a static constructor.
            const std::scoped_lock<std::mutex> lock(GetMembersMutex());
            // typeID.token1 is the MethodTable address.
            initializedTypesAddr.emplace(typeID.token1);
            return S_OK;
        }

        if (SUCCEEDED(IsModulePlainILImage(trModule)))
        {
            ToRelease<ICorDebugFunction> trFunction;
            ToRelease<ICorDebugCode> trNativeCode;
            if (SUCCEEDED(trModule->GetFunctionFromToken(methodDef, &trFunction)) &&
                SUCCEEDED(trFunction->GetNativeCode(&trNativeCode)) && trNativeCode != nullptr)
            {
                // The static constructor has been JIT-compiled; since it is compiled only when
                // invoked, the class has already been initialized.
                isClassInitialized = true;
            }
        }
        else
        {
            ToRelease<ICorDebugProcess> trProcess;
            IfFailRet(pThread->GetProcess(&trProcess));
            if (trProcess == nullptr)
            {
                return E_FAIL;
            }

            // Read the MethodTable to get the m_pAuxiliaryData pointer.
            // The offset of m_pAuxiliaryData within MethodTable depends on the pointer size,
            // so it is computed from the layout below. We rely on m_dwFlags being at offset 0
            // in MethodTableAuxiliaryData, with the Initialized flag in bit 0.

            // Read a sufficient portion of the MethodTable to locate the m_pAuxiliaryData.
            // MethodTable layout (from the runtime sources):
            // - m_dwFlags (DWORD) at offset 0
            // - m_BaseSize (DWORD) at offset 4
            // - m_dwFlags2 (DWORD) at offset 8
            // - m_wNumVirtuals (WORD) at offset 12
            // - m_wNumInterfaces (WORD) at offset 14
            // - m_pParentMethodTable (pointer) at offset 16
            // - m_pModule (pointer) at offset 16 + sizeof(pointer)
            // - m_pAuxiliaryData (pointer) at offset 16 + 2 * sizeof(pointer)
            static constexpr size_t auxDataOffset = (sizeof(DWORD) * 3) + (sizeof(WORD) * 2) + (sizeof(void *) * 2);
            static constexpr size_t readSize = auxDataOffset + sizeof(void *);
            std::array<uint8_t, readSize> buffer{0};
            SIZE_T bytesRead = 0;

            if (SUCCEEDED(trProcess->ReadMemory(methodTableAddr, readSize, buffer.data(), &bytesRead)) && bytesRead >= readSize)
            {
                // Get the auxiliary data pointer.
                uintptr_t auxDataAddr = 0;
                std::memcpy(&auxDataAddr, buffer.data() + auxDataOffset, sizeof(uintptr_t));
                if (auxDataAddr != 0)
                {
                    // Read m_dwFlags from MethodTableAuxiliaryData (at offset 0).
                    DWORD auxFlags = 0;
                    if (SUCCEEDED(trProcess->ReadMemory(static_cast<CORDB_ADDRESS>(auxDataAddr), sizeof(DWORD),
                                                        reinterpret_cast<uint8_t *>(&auxFlags), &bytesRead)))
                    {
                        isClassInitialized = (auxFlags & enum_flag_Initialized) != 0;
                    }
                }
            }
        }
    }

    if (!isClassInitialized)
    {
        // Force the static constructor to run.
        // Note, when the initialization state cannot be determined (e.g. the class was not loaded yet,
        // so it has no MethodTable in the debuggee memory and ICorDebugType2::GetTypeID() failed),
        // the CallStaticConstructor() below also loads the class, since the object allocation inside
        // it triggers the class load and the static constructor run. Without this, GetStaticFieldValue()
        // fails for a class that is not loaded yet.
        IfFailRet(EvalExec::CallStaticConstructor(pThread, pType, specifier));
    }

    if (typeID.token1 == 0)
    {
        trType2.Free();
        if (FAILED(pType->QueryInterface(IID_ICorDebugType2, reinterpret_cast<void **>(&trType2))) ||
            FAILED(trType2->GetTypeID(&typeID)) ||
            typeID.token1 == 0)
        {
            return S_OK;
        }
    }

    const std::scoped_lock<std::mutex> lock(GetMembersMutex());
    // typeID.token1 is the MethodTable address.
    initializedTypesAddr.emplace(typeID.token1);

    return S_OK;
}

// ICorDebugType::GetStaticFieldValue() for a static Nullable<T> field (and similar cases) returns
// a value whose fully-qualified name is the underlying primitive type (e.g., "System.Int32"), while
// its CorElementType is not the expected ELEMENT_TYPE_I4, which breaks the GetType()-based logic
// downstream. Recreate such values as proper primitive ICorDebugValues; all other values (including
// null ones) are passed through unchanged (the passed-in value is AddRef'd for the caller).
HRESULT RecreateAsPrimitiveValue(ICorDebugThread *pThread, ICorDebugValue *pValue, ICorDebugValue **ppResultValue)
{
    if (pValue == nullptr)
    {
        // Nothing to recreate.
        *ppResultValue = nullptr;
        return S_OK;
    }

    HRESULT Status = S_OK;
    CorElementType valueElemType = ELEMENT_TYPE_MAX;
    IfFailRet(pValue->GetType(&valueElemType));
    if (valueElemType != ELEMENT_TYPE_VALUETYPE)
    {
        // Already a proper primitive value, nothing to recreate.
        pValue->AddRef();
        *ppResultValue = pValue;
        return S_OK;
    }

    std::string metadataTypeName;
    IfFailRet(MetadataHelpers::GetFQMDTypeNameByICorValue(pValue, metadataTypeName));

    static const std::unordered_map<std::string, CorElementType> typeMap{
        {"System.Boolean", ELEMENT_TYPE_BOOLEAN},
        {"System.Char",    ELEMENT_TYPE_CHAR},
        {"System.SByte",   ELEMENT_TYPE_I1},
        {"System.Byte",    ELEMENT_TYPE_U1},
        {"System.Double",  ELEMENT_TYPE_R8},
        {"System.Single",  ELEMENT_TYPE_R4},
        {"System.Int32",   ELEMENT_TYPE_I4},
        {"System.UInt32",  ELEMENT_TYPE_U4},
        {"System.Int64",   ELEMENT_TYPE_I8},
        {"System.UInt64",  ELEMENT_TYPE_U8},
        {"System.Int16",   ELEMENT_TYPE_I2},
        {"System.UInt16",  ELEMENT_TYPE_U2},
        // nuint? and nint? cannot be declared const field
    };

    const auto typeIter = typeMap.find(metadataTypeName);
    if (typeIter == typeMap.cend())
    {
        pValue->AddRef();
        *ppResultValue = pValue;
        return S_OK;
    }

    uint64_t data = 0;
    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(pValue->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    IfFailRet(trGenericValue->GetValue(&data));
    return PrimitiveTypes::CreateICorValue(pThread, typeIter->second, &data, ppResultValue);
}

} // unnamed namespace

// Note, could return S_CAN_EXIT for fast exit.
HRESULT WalkMembers(ICorDebugValue *pInputValue, ICorDebugThread *pThread, FrameLevel frameLevel,
                    bool provideSetterData, FormatSpecifier specifier, const WalkMembersCallback &cb)
{
    // Same behavior as MS vsdbg and the MSVS C# debugger: don't show enumeration members.
    if (EvalValueHelpers::IsEnumeration(pInputValue))
    {
        return S_OK;
    }

    HRESULT Status = S_OK;
    bool showInRaw = (specifier & FormatSpecifier::DisplaysInRawMode) != FormatSpecifier::None ||
                     (Config::GetEvalFlags() & Config::EVAL_SHOWRAWVALUES) != 0U;
    bool showHidden = (specifier & FormatSpecifier::DisplaysHiddenMembers) != FormatSpecifier::None;
    bool walkContainer = (specifier & FormatSpecifier::WalkContainerMembers) != FormatSpecifier::None;

    struct WalkValue
    {
        ToRelease<ICorDebugValue> trValue;
        bool isTypeProxyValue = false;
        bool walkContainerMembers = false;

        WalkValue(ICorDebugValue *pValue, bool isTypeProxyValue_, bool walkContainerMembers_ = false)
            : trValue(pValue),
              isTypeProxyValue(isTypeProxyValue_),
              walkContainerMembers(walkContainerMembers_)
        {
        }
    };

    // Queue of fields/properties to process. Includes elements with
    // DebuggerBrowsableState.RootHidden, to unwrap their members during the walk.
    std::list<WalkValue> trWalkQueue;
    pInputValue->AddRef();
    trWalkQueue.emplace_back(pInputValue, false);

    // Number of members unwrapped during this walk; a zero limit disables unwrapping.
    uint32_t walkRootCount = 0;
    const uint32_t rootHiddenWalkLimit = Config::GetRootHiddenWalkLimit();

    // Unwraps a RootHidden member by queuing its value for the walk. Circular references
    // would refill the queue endlessly, so every unwrap counts against the walk limit.
    const auto queueRootHiddenMemberValue = [&](ICorDebugValue *pValue) -> void
    {
        if (walkRootCount >= rootHiddenWalkLimit)
        {
            return; // Too many unwraps in this walk; stop unwrapping to prevent an infinite walk.
        }

        ++walkRootCount;

        pValue->AddRef();
        trWalkQueue.emplace_back(pValue, false);
    };

    const auto walkNext = [&](ICorDebugValue *pFrontValue, bool isTypeProxyValue, bool walkContainerMembers) -> HRESULT
    {
        BOOL isNull = FALSE;
        ToRelease<ICorDebugValue> trValue;
        IfFailRet(EvalValueHelpers::DereferenceAndUnboxValue(pFrontValue, &trValue, &isNull));
        if (trValue == nullptr)
        {
            return isNull == TRUE ? S_OK : E_FAIL;
        }

        CorElementType inputElemType = ELEMENT_TYPE_MAX;
        IfFailRet(pFrontValue->GetType(&inputElemType));
        if (inputElemType == ELEMENT_TYPE_PTR)
        {
            const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
            {
                trValue->AddRef();
                *ppResultValue = trValue;
                return S_OK;
            };

            IfFailRet(cb(nullptr, false, "", getValue, nullptr, nullptr));
            if (Status == S_CAN_EXIT)
            {
                return S_CAN_EXIT; // Fast exit from the loop.
            }
            return S_OK;
        }

        ToRelease<ICorDebugArrayValue> trArrayValue;
        if (!walkContainer &&
            SUCCEEDED(trValue->QueryInterface(IID_ICorDebugArrayValue, reinterpret_cast<void **>(&trArrayValue))))
        {
            uint32_t nRank = 0;
            IfFailRet(trArrayValue->GetRank(&nRank));

            uint32_t cElements = 0;
            IfFailRet(trArrayValue->GetCount(&cElements));

            std::vector<uint32_t> dims(nRank, 0);
            IfFailRet(trArrayValue->GetDimensions(nRank, dims.data()));

            std::vector<uint32_t> base(nRank, 0);
            BOOL hasBaseIndices = FALSE;
            if (SUCCEEDED(trArrayValue->HasBaseIndicies(&hasBaseIndices)) && (hasBaseIndices == TRUE))
            {
                IfFailRet(trArrayValue->GetBaseIndicies(nRank, base.data()));
            }

            std::vector<uint32_t> ind(nRank, 0);

            for (uint32_t i = 0; i < cElements; ++i)
            {
                const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
                {
                    IfFailRet(trArrayValue->GetElementAtPosition(i, ppResultValue));
                    return S_OK;
                };

                IfFailRet(cb(nullptr, false, "[" + IndicesToStr(ind, base) + "]", getValue, nullptr, nullptr));
                if (Status == S_CAN_EXIT)
                {
                    return S_CAN_EXIT; // Fast exit from the loop.
                }
                IncIndices(dims, ind);
            }

            return S_OK;
        }

        ToRelease<ICorDebugType> trType;
        {
            ToRelease<ICorDebugValue2> trValue2;
            IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
            IfFailRet(trValue2->GetExactType(&trType));
        }
        if (trType == nullptr)
        {
            return E_FAIL;
        }

        while (trType != nullptr)
        {
            std::string metadataTypeName;
            MetadataHelpers::GetFQMDTypeNameByICorType(trType, metadataTypeName);
            if (metadataTypeName == "System.Decimal")
            {
                return S_OK;
            }

            if (!walkContainerMembers && metadataTypeName == "System.Nullable`1") // System.Nullable<T>
            {
                ToRelease<ICorDebugValue> trValueValue;
                bool hasValue = false;
                IfFailRet(EvalValueHelpers::GetNullableValue(trValue, &trValueValue, hasValue));

                if (walkContainer)
                {
                    // Walk the Nullable<T> object itself.
                    pFrontValue->AddRef();
                    trWalkQueue.emplace_back(pFrontValue, false, true);
                }

                if (hasValue)
                {
                    CorElementType elemType = ELEMENT_TYPE_MAX;
                    IfFailRet(trValueValue->GetType(&elemType));

                    trValue.Free();
                    trValue = trValueValue.Detach();
                    ToRelease<ICorDebugValue2> trValue2;
                    IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
                    trType.Free();
                    IfFailRet(trValue2->GetExactType(&trType));

                    continue;
                }

                return S_OK;
            }

            CorElementType elemType = ELEMENT_TYPE_MAX;
            IfFailRet(trType->GetType(&elemType));
            if (elemType == ELEMENT_TYPE_STRING)
            {
                if (walkContainer)
                {
                    // In case of a string, the input value is always a reference (ELEMENT_TYPE_CLASS),
                    // so query the exact type from it to walk the string members.
                    trValue.Free();
                    ToRelease<ICorDebugValue2> trValue2;
                    IfFailRet(pFrontValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
                    trType.Free();
                    IfFailRet(trValue2->GetExactType(&trType));
                }
                else
                {
                    return S_OK;
                }
            }
            else if (elemType == ELEMENT_TYPE_SZARRAY || elemType == ELEMENT_TYPE_ARRAY)
            {
                // Create proper System.Array type in order to walk members.
                trType.Free();
                ToRelease<ICorDebugClass> trClass;
                IfFailRet(SystemTypes::GetClass(SystemTypes::SystemType::Array, &trClass));
                ToRelease<ICorDebugClass2> trClass2;
                IfFailRet(trClass->QueryInterface(IID_ICorDebugClass2, reinterpret_cast<void **>(&trClass2)));
                IfFailRet(trClass2->GetParameterizedType(ELEMENT_TYPE_CLASS, 0, nullptr, &trType));
            }
            else if (elemType != ELEMENT_TYPE_CLASS && elemType != ELEMENT_TYPE_VALUETYPE)
            {
                return S_OK;
            }

            ToRelease<ICorDebugClass> trClass;
            IfFailRet(trType->GetClass(&trClass));
            ToRelease<ICorDebugModule> trModule;
            IfFailRet(trClass->GetModule(&trModule));
            mdTypeDef currentTypeDef = mdTypeDefNil;
            IfFailRet(trClass->GetToken(&currentTypeDef));

            if (!showInRaw && isNull == FALSE && !isTypeProxyValue)
            {
                ToRelease<ICorDebugValue> trTypeProxyValue;
                if (SUCCEEDED(TypeProxy::GetDebuggerTypeProxyValue(pThread, trModule, pFrontValue, trType,
                                                                   currentTypeDef, &trTypeProxyValue)))
                {
                    trWalkQueue.emplace_front(trTypeProxyValue.Detach(), true);
                    if (!walkContainer)
                    {
                        return S_OK;
                    }
                }
            }

            ToRelease<IUnknown> trUnknown;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

            bool staticFieldsInitializationChecked = false;
            HRESULT staticFieldsInitializationStatus = S_OK;

            IfFailRet(EvalMetadataHelpers::ForEachFields(trMDImport, currentTypeDef,
                [&](mdFieldDef fieldDef) -> HRESULT
                {
                    const DebuggerBrowsableState browsableState = showInRaw ? DebuggerBrowsableState::Collapsed :
                                                                              GetDebuggerBrowsableAttributeState(trMDImport, fieldDef);
                    if (browsableState == DebuggerBrowsableState::Never)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    ULONG nameLen = 0;
                    DWORD fieldAttr = 0;
                    if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen, &fieldAttr,
                                                         nullptr, nullptr, nullptr, nullptr, nullptr)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    if (isTypeProxyValue && !showHidden &&
                        (fieldAttr & fdFieldAccessMask) != fdPublic)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    WSTRING mdName(nameLen, '\0');
                    PCCOR_SIGNATURE pSig = nullptr;
                    ULONG cbSig = 0;
                    UVCP_CONSTANT pRawValue = nullptr;
                    ULONG rawValueLength = 0;
                    if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr, nullptr,
                                                         &pSig, &cbSig, nullptr, &pRawValue, &rawValueLength)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    // Remove the null terminator that was included in the length
                    if (!mdName.empty() && mdName.back() == '\0')
                    {
                        mdName.pop_back();
                    }

                    // Prevent access to internal compiler-added fields (without a visible name).
                    // They should be accessed by debugger routines only and hidden from the user/IDE.
                    // More about compiler-generated names in the Roslyn sources:
                    // https://github.com/dotnet/roslyn/blob/315c2e149ba7889b0937d872274c33fcbfe9af5f/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNames.cs
                    // Note, uncontrolled access to an internal compiler-added field or its properties may break debugger work.
                    if (!showHidden && MetadataHelpers::IsSynthesizedLocalName(mdName))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    const bool isStatic = (fieldAttr & fdStatic);
                    if (isNull == TRUE && !isStatic)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    const std::string name = to_utf8(mdName.c_str());

                    const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *pFallbackTypeName) -> HRESULT
                    {
                        if (fieldAttr & fdLiteral)
                        {
                            SigConstant sigConstant;
                            IfFailRet(ParseLiteralFieldSig(trMDImport, pSig, pSig + cbSig, sigConstant));

                            std::string realDisplayTypeName;
                            IfFailRet(EvalExec::CreateLiteralFieldValue(pThread, sigConstant, pRawValue,
                                                                        rawValueLength, ppResultValue, realDisplayTypeName));

                            if (pFallbackTypeName != nullptr)
                            {
                                *pFallbackTypeName = std::move(realDisplayTypeName);
                            }
                        }
                        else if (fieldAttr & fdStatic)
                        {
                            if (pThread == nullptr)
                            {
                                return E_FAIL;
                            }

                            ToRelease<ICorDebugFrame> trFrame;
                            IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
                            if (trFrame == nullptr)
                            {
                                return E_FAIL;
                            }

                            if (!staticFieldsInitializationChecked)
                            {
                                staticFieldsInitializationChecked = true;
                                staticFieldsInitializationStatus = InitializeStaticFields(pThread, trType, specifier);
                            }
                            IfFailRet(staticFieldsInitializationStatus);

                            ToRelease<ICorDebugValue> resultValue;
                            Status = trType->GetStaticFieldValue(fieldDef, trFrame, &resultValue);
                            if (Status == CORDBG_E_STATIC_VAR_NOT_AVAILABLE)
                            {
                                SigElementType sigElementType;
                                IfFailRet(ParseFieldSig(trMDImport, pSig, pSig + cbSig, sigElementType));
                                std::string realDisplayTypeName;
                                IfFailRet(EvalExec::CreateStaticFieldDefaultValue(pThread, sigElementType, ppResultValue, realDisplayTypeName));
                                if (pFallbackTypeName != nullptr)
                                {
                                    *pFallbackTypeName = std::move(realDisplayTypeName);
                                }
                                return S_OK;
                            }
                            IfFailRet(Status);

                            IfFailRet(RecreateAsPrimitiveValue(pThread, resultValue, ppResultValue));
                        }
                        else
                        {
                            // Re-acquire trValue from pFrontValue, since it could be neutered by an eval call in `cb` on a previous iteration.
                            trValue.Free();
                            IfFailRet(EvalValueHelpers::DereferenceAndUnboxValue(pFrontValue, &trValue, &isNull));
                            ToRelease<ICorDebugObjectValue> trObjValue;
                            IfFailRet(trValue->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue)));
                            IfFailRet(trObjValue->GetFieldValue(trClass, fieldDef, ppResultValue));
                        }

                        return S_OK;
                    };

                    if (browsableState == DebuggerBrowsableState::RootHidden)
                    {
                        ToRelease<ICorDebugValue> trResultValue;
                        if (SUCCEEDED(getValue(&trResultValue, nullptr)))
                        {
                            queueRootHiddenMemberValue(trResultValue);
                        }
                        if (!walkContainer)
                        {
                            return S_OK; // Return success to continue walking.
                        }
                    }

                    std::string textWithEval;
                    HasDebuggerAttribute(trMDImport, fieldDef, DebuggerAttribute::Display, textWithEval);

                    IfFailRet(cb(trType, isStatic, name, getValue, nullptr, &textWithEval));
                    if (Status == S_CAN_EXIT)
                    {
                        return S_CAN_EXIT; // Fast exit from the loop.
                    }

                    return S_OK; // Return success to continue walking.
                }));
            if (Status == S_CAN_EXIT)
            {
                return S_CAN_EXIT;
            }
            Status = EvalMetadataHelpers::ForEachProperties(trMDImport, currentTypeDef,
                [&](mdProperty propertyDef) -> HRESULT
                {
                    const DebuggerBrowsableState browsableState = showInRaw ? DebuggerBrowsableState::Collapsed :
                                                                              GetDebuggerBrowsableAttributeState(trMDImport, propertyDef);
                    if (browsableState == DebuggerBrowsableState::Never)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    ULONG propertyNameLen = 0;
                    if (FAILED(trMDImport->GetPropertyProps(propertyDef, nullptr, nullptr, 0, &propertyNameLen,
                                                            nullptr, nullptr, nullptr, nullptr, nullptr,
                                                            nullptr, nullptr, nullptr, nullptr, 0, nullptr)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    mdMethodDef mdGetter = mdMethodDefNil;
                    mdMethodDef mdSetter = mdMethodDefNil;
                    std::vector<WCHAR> propertyName(propertyNameLen, '\0');
                    if (FAILED(trMDImport->GetPropertyProps(propertyDef, nullptr, propertyName.data(), propertyNameLen,
                                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                                            nullptr, &mdSetter, &mdGetter, nullptr, 0, nullptr)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    DWORD getterAttr = 0;
                    PCCOR_SIGNATURE pSig = nullptr;
                    ULONG cbSig = 0;
                    if (FAILED(trMDImport->GetMethodProps(mdGetter, nullptr, nullptr, 0, nullptr, &getterAttr,
                                                          &pSig, &cbSig, nullptr, nullptr)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    if (isTypeProxyValue && !showHidden &&
                        (getterAttr & mdMemberAccessMask) != mdPublic)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    bool isStatic = (getterAttr & mdStatic);
                    if (isNull == TRUE && !isStatic)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    // A bit hacky, but a fast way to detect an indexer:
                    // a property getter that takes arguments is an indexer for sure.
                    // Note, a static parameterized property (not supported in C#, but possible in other .NET
                    // languages) has a getter that takes arguments too, so it is skipped by the same check.
                    uint32_t argCount = 0;
                    if (SUCCEEDED(GetMethodArgCount(pSig, pSig + cbSig, argCount)) &&
                        argCount > 0)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    const std::string name = to_utf8(propertyName.data());

                    const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
                    {
                        if (pThread == nullptr)
                        {
                            return E_FAIL;
                        }

                        ToRelease<ICorDebugFunction> trFunc;
                        IfFailRet(trModule->GetFunctionFromToken(mdGetter, &trFunc));

                        return EvalExec::CallFunction(pThread, trFunc, trType.GetPtr(), nullptr,
                                                      isStatic ? nullptr : &pFrontValue, isStatic ? 0 : 1,
                                                      specifier, ppResultValue);
                    };

                    if (browsableState == DebuggerBrowsableState::RootHidden)
                    {
                        ToRelease<ICorDebugValue> trResultValue;
                        if (SUCCEEDED(getValue(&trResultValue, nullptr)))
                        {
                            queueRootHiddenMemberValue(trResultValue);
                        }
                        if (!walkContainer)
                        {
                            return S_OK; // Return success to continue walking.
                        }
                    }

                    std::string textWithEval;
                    HasDebuggerAttribute(trMDImport, propertyDef, DebuggerAttribute::Display, textWithEval);

                    if (provideSetterData)
                    {
                        ToRelease<ICorDebugFunction> trFuncSetter;
                        if (FAILED(trModule->GetFunctionFromToken(mdSetter, &trFuncSetter)))
                        {
                            trFuncSetter.Free();
                        }
                        SetterData setterData(isStatic ? nullptr : pFrontValue, trType, trFuncSetter);
                        IfFailRet(cb(trType, isStatic, name, getValue, &setterData, &textWithEval));
                        if (Status == S_CAN_EXIT)
                        {
                            return S_CAN_EXIT; // Fast exit from the loop.
                        }
                    }
                    else
                    {
                        IfFailRet(cb(trType, isStatic, name, getValue, nullptr, &textWithEval));
                        if (Status == S_CAN_EXIT)
                        {
                            return S_CAN_EXIT; // Fast exit from the loop.
                        }
                    }

                    return S_OK; // Return success to continue walking.
                });
            // Note: The code above was moved out of IfFailRet() due to MSVC error C2121.
            IfFailRet(Status);
            if (Status == S_CAN_EXIT)
            {
                return S_CAN_EXIT;
            }

            std::string metadataBaseTypeName;
            ToRelease<ICorDebugType> trBaseType;
            if (SUCCEEDED(trType->GetBase(&trBaseType)) && trBaseType != nullptr &&
                SUCCEEDED(MetadataHelpers::GetFQMDTypeNameByICorType(trBaseType, metadataBaseTypeName)))
            {
                trType.Free();

                if (metadataBaseTypeName != "System.Object" &&
                    metadataBaseTypeName != "System.ValueType")
                {
                    // Add fields of the base class.
                    trType = trBaseType.Detach();
                }
            }
            else
            {
                trType.Free();
            }
        }

        return S_OK;
    };

    while (!trWalkQueue.empty())
    {
        const ToRelease<ICorDebugValue> trFrontValue(trWalkQueue.front().trValue.Detach());
        const bool isTypeProxyValue = trWalkQueue.front().isTypeProxyValue;
        const bool walkContainerMembers = trWalkQueue.front().walkContainerMembers;
        trWalkQueue.pop_front();

        IfFailRet(walkNext(trFrontValue, isTypeProxyValue, walkContainerMembers));
        if (Status == S_CAN_EXIT)
        {
            return S_OK;
        }
    }

    return S_OK;
}

// Note, could return S_CAN_EXIT for fast exit.
HRESULT WalkStaticMembers(ICorDebugType *pInputType, ICorDebugThread *pThread, FrameLevel frameLevel,
                          bool provideSetterData, FormatSpecifier specifier, const WalkStaticMembersCallback &cb)
{
    HRESULT Status = S_OK;
    CorElementType elemType = ELEMENT_TYPE_MAX;
    IfFailRet(pInputType->GetType(&elemType));
    if (elemType != ELEMENT_TYPE_CLASS && elemType != ELEMENT_TYPE_VALUETYPE)
    {
        return S_OK;
    }

    // Same behavior as MS vsdbg and the MSVS C# debugger: don't show enumeration members.
    if (EvalValueHelpers::IsEnumeration(pInputType))
    {
        return S_OK;
    }

    bool showInRaw = (specifier & FormatSpecifier::DisplaysInRawMode) != FormatSpecifier::None ||
                     (Config::GetEvalFlags() & Config::EVAL_SHOWRAWVALUES) != 0U;
    bool showHidden = (specifier & FormatSpecifier::DisplaysHiddenMembers) != FormatSpecifier::None;
    bool walkContainer = (specifier & FormatSpecifier::WalkContainerMembers) != FormatSpecifier::None;

    // Queue of types to process. Also includes the types of members marked with
    // DebuggerBrowsableState.RootHidden, so those members are unwrapped during the walk.
    std::list<ToRelease<ICorDebugType>> trWalkQueue;
    pInputType->AddRef();
    trWalkQueue.emplace_back(pInputType);

    // Number of members unwrapped during this walk; a zero limit disables unwrapping.
    uint32_t walkRootCount = 0;
    const uint32_t rootHiddenWalkLimit = Config::GetRootHiddenWalkLimit();

    const auto queueRootHiddenMemberType = [&](ICorDebugValue *pValue) -> void
    {
        if (pValue == nullptr)
        {
            return; // Nothing to unwrap.
        }

        BOOL isNull = FALSE;
        ToRelease<ICorDebugValue> trDerefValue;
        if (FAILED(EvalValueHelpers::DereferenceAndUnboxValue(pValue, &trDerefValue, &isNull)) ||
            trDerefValue == nullptr || isNull == TRUE)
        {
            return; // Nothing to unwrap.
        }

        ToRelease<ICorDebugValue2> trValue2;
        ToRelease<ICorDebugType> trValueType;
        if (FAILED(trDerefValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2))) ||
            FAILED(trValue2->GetExactType(&trValueType)) || trValueType == nullptr)
        {
            return; // Nothing to unwrap.
        }

        CorElementType valueElemType = ELEMENT_TYPE_MAX;
        if (FAILED(trValueType->GetType(&valueElemType)) ||
            (valueElemType != ELEMENT_TYPE_CLASS && valueElemType != ELEMENT_TYPE_VALUETYPE))
        {
            return; // Nothing to unwrap.
        }

        if (walkRootCount >= rootHiddenWalkLimit)
        {
            return; // Too many unwraps in this walk; stop unwrapping to prevent an infinite walk.
        }

        ++walkRootCount;

        trWalkQueue.emplace_back(trValueType.Detach());
    };

    const auto walkNext = [&](ICorDebugType *pFrontType) -> HRESULT
    {
        pFrontType->AddRef();
        ToRelease<ICorDebugType> trType(pFrontType);

        while (trType != nullptr)
        {
            ToRelease<ICorDebugClass> trClass;
            IfFailRet(trType->GetClass(&trClass));
            ToRelease<ICorDebugModule> trModule;
            IfFailRet(trClass->GetModule(&trModule));
            mdTypeDef currentTypeDef = mdTypeDefNil;
            IfFailRet(trClass->GetToken(&currentTypeDef));

            ToRelease<IUnknown> trUnknown;
            IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
            ToRelease<IMetaDataImport> trMDImport;
            IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

            bool staticFieldsInitializationChecked = false;
            HRESULT staticFieldsInitializationStatus = S_OK;

            IfFailRet(EvalMetadataHelpers::ForEachFields(trMDImport, currentTypeDef,
                [&](mdFieldDef fieldDef) -> HRESULT
                {
                    const DebuggerBrowsableState browsableState = showInRaw ? DebuggerBrowsableState::Collapsed :
                                                                              GetDebuggerBrowsableAttributeState(trMDImport, fieldDef);
                    if (browsableState == DebuggerBrowsableState::Never)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    ULONG nameLen = 0;
                    DWORD fieldAttr = 0;
                    if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, nullptr, 0, &nameLen, &fieldAttr,
                                                         nullptr, nullptr, nullptr, nullptr, nullptr)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    if ((fieldAttr & fdStatic) == 0)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    WSTRING mdName(nameLen, '\0');
                    PCCOR_SIGNATURE pSig = nullptr;
                    ULONG cbSig = 0;
                    UVCP_CONSTANT pRawValue = nullptr;
                    ULONG rawValueLength = 0;
                    if (FAILED(trMDImport->GetFieldProps(fieldDef, nullptr, mdName.data(), nameLen, nullptr, nullptr,
                                                         &pSig, &cbSig, nullptr, &pRawValue, &rawValueLength)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    // Remove the null terminator that was included in the length
                    if (!mdName.empty() && mdName.back() == '\0')
                    {
                        mdName.pop_back();
                    }

                    // Prevent access to internal compiler-added fields (without a visible name).
                    // They should be accessed by debugger routines only and hidden from the user/IDE.
                    // More about compiler-generated names in the Roslyn sources:
                    // https://github.com/dotnet/roslyn/blob/315c2e149ba7889b0937d872274c33fcbfe9af5f/src/Compilers/CSharp/Portable/Symbols/Synthesized/GeneratedNames.cs
                    // Note, uncontrolled access to an internal compiler-added field or its properties may break debugger work.
                    if (!showHidden && MetadataHelpers::IsSynthesizedLocalName(mdName))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    const std::string name = to_utf8(mdName.c_str());

                    const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *pFallbackTypeName) -> HRESULT
                    {
                        if (fieldAttr & fdLiteral)
                        {
                            SigConstant sigConstant;
                            IfFailRet(ParseLiteralFieldSig(trMDImport, pSig, pSig + cbSig, sigConstant));

                            std::string realDisplayTypeName;
                            IfFailRet(EvalExec::CreateLiteralFieldValue(pThread, sigConstant, pRawValue,
                                                                        rawValueLength, ppResultValue, realDisplayTypeName));

                            if (pFallbackTypeName != nullptr)
                            {
                                *pFallbackTypeName = std::move(realDisplayTypeName);
                            }
                        }
                        else
                        {
                            if (pThread == nullptr)
                            {
                                return E_FAIL;
                            }

                            ToRelease<ICorDebugFrame> trFrame;
                            IfFailRet(GetFrameAt(pThread, frameLevel, &trFrame));
                            if (trFrame == nullptr)
                            {
                                return E_FAIL;
                            }

                            if (!staticFieldsInitializationChecked)
                            {
                                staticFieldsInitializationChecked = true;
                                staticFieldsInitializationStatus = InitializeStaticFields(pThread, trType, specifier);
                            }
                            IfFailRet(staticFieldsInitializationStatus);

                            ToRelease<ICorDebugValue> resultValue;
                            Status = trType->GetStaticFieldValue(fieldDef, trFrame, &resultValue);
                            if (Status == CORDBG_E_STATIC_VAR_NOT_AVAILABLE)
                            {
                                SigElementType sigElementType;
                                IfFailRet(ParseFieldSig(trMDImport, pSig, pSig + cbSig, sigElementType));
                                std::string realDisplayTypeName;
                                IfFailRet(EvalExec::CreateStaticFieldDefaultValue(pThread, sigElementType, ppResultValue, realDisplayTypeName));
                                if (pFallbackTypeName != nullptr)
                                {
                                    *pFallbackTypeName = std::move(realDisplayTypeName);
                                }
                                return S_OK;
                            }
                            IfFailRet(Status);

                            IfFailRet(RecreateAsPrimitiveValue(pThread, resultValue, ppResultValue));
                        }

                        return S_OK;
                    };

                    if (browsableState == DebuggerBrowsableState::RootHidden)
                    {
                        ToRelease<ICorDebugValue> trResultValue;
                        if (SUCCEEDED(getValue(&trResultValue, nullptr)))
                        {
                            queueRootHiddenMemberType(trResultValue);
                        }
                        if (!walkContainer)
                        {
                            return S_OK; // Return success to continue walking.
                        }
                    }

                    std::string textWithEval;
                    HasDebuggerAttribute(trMDImport, fieldDef, DebuggerAttribute::Display, textWithEval);

                    IfFailRet(cb(trType, name, getValue, nullptr, &textWithEval));
                    if (Status == S_CAN_EXIT)
                    {
                        return S_CAN_EXIT; // Fast exit from the loop.
                    }

                    return S_OK; // Return success to continue walking.
                }));
            if (Status == S_CAN_EXIT)
            {
                return S_CAN_EXIT;
            }
            Status = EvalMetadataHelpers::ForEachProperties(trMDImport, currentTypeDef,
                [&](mdProperty propertyDef) -> HRESULT
                {
                    const DebuggerBrowsableState browsableState = showInRaw ? DebuggerBrowsableState::Collapsed :
                                                                              GetDebuggerBrowsableAttributeState(trMDImport, propertyDef);
                    if (browsableState == DebuggerBrowsableState::Never)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    ULONG propertyNameLen = 0;
                    if (FAILED(trMDImport->GetPropertyProps(propertyDef, nullptr, nullptr, 0, &propertyNameLen,
                                                            nullptr, nullptr, nullptr, nullptr, nullptr,
                                                            nullptr, nullptr, nullptr, nullptr, 0, nullptr)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    mdMethodDef mdGetter = mdMethodDefNil;
                    mdMethodDef mdSetter = mdMethodDefNil;
                    std::vector<WCHAR> propertyName(propertyNameLen, '\0');
                    if (FAILED(trMDImport->GetPropertyProps(propertyDef, nullptr, propertyName.data(), propertyNameLen,
                                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                                            nullptr, &mdSetter, &mdGetter, nullptr, 0, nullptr)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    DWORD getterAttr = 0;
                    PCCOR_SIGNATURE pSig = nullptr;
                    ULONG cbSig = 0;
                    if (FAILED(trMDImport->GetMethodProps(mdGetter, nullptr, nullptr, 0, nullptr, &getterAttr,
                                                          &pSig, &cbSig, nullptr, nullptr)))
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    if ((getterAttr & mdStatic) == 0)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    // A bit hacky, but a fast way to skip a static parameterized property:
                    // a static property getter that takes arguments cannot be evaluated without them.
                    uint32_t argCount = 0;
                    if (SUCCEEDED(GetMethodArgCount(pSig, pSig + cbSig, argCount)) &&
                        argCount > 0)
                    {
                        return S_OK; // Return success to continue walking.
                    }

                    const std::string name = to_utf8(propertyName.data());

                    const auto getValue = [&](ICorDebugValue **ppResultValue, std::string *) -> HRESULT
                    {
                        if (pThread == nullptr)
                        {
                            return E_FAIL;
                        }

                        ToRelease<ICorDebugFunction> trFunc;
                        IfFailRet(trModule->GetFunctionFromToken(mdGetter, &trFunc));

                        return EvalExec::CallFunction(pThread, trFunc, trType.GetPtr(), nullptr,
                                                      nullptr, 0, specifier, ppResultValue);
                    };

                    if (browsableState == DebuggerBrowsableState::RootHidden)
                    {
                        ToRelease<ICorDebugValue> trResultValue;
                        if (SUCCEEDED(getValue(&trResultValue, nullptr)))
                        {
                            queueRootHiddenMemberType(trResultValue);
                        }
                        if (!walkContainer)
                        {
                            return S_OK; // Return success to continue walking.
                        }
                    }

                    std::string textWithEval;
                    HasDebuggerAttribute(trMDImport, propertyDef, DebuggerAttribute::Display, textWithEval);

                    if (provideSetterData)
                    {
                        ToRelease<ICorDebugFunction> trFuncSetter;
                        if (FAILED(trModule->GetFunctionFromToken(mdSetter, &trFuncSetter)))
                        {
                            trFuncSetter.Free();
                        }
                        SetterData setterData(nullptr, trType, trFuncSetter);
                        IfFailRet(cb(trType, name, getValue, &setterData, &textWithEval));
                        if (Status == S_CAN_EXIT)
                        {
                            return S_CAN_EXIT; // Fast exit from the loop.
                        }
                    }
                    else
                    {
                        IfFailRet(cb(trType, name, getValue, nullptr, &textWithEval));
                        if (Status == S_CAN_EXIT)
                        {
                            return S_CAN_EXIT; // Fast exit from the loop.
                        }
                    }

                    return S_OK; // Return success to continue walking.
                });
            // Note: The code above was moved out of IfFailRet() due to MSVC error C2121.
            IfFailRet(Status);
            if (Status == S_CAN_EXIT)
            {
                return S_CAN_EXIT;
            }

            std::string metadataBaseTypeName;
            ToRelease<ICorDebugType> trBaseType;
            if (SUCCEEDED(trType->GetBase(&trBaseType)) && trBaseType != nullptr &&
                SUCCEEDED(MetadataHelpers::GetFQMDTypeNameByICorType(trBaseType, metadataBaseTypeName)))
            {
                trType.Free();

                if (metadataBaseTypeName != "System.Object" &&
                    metadataBaseTypeName != "System.ValueType")
                {
                    // Add fields of the base class.
                    trType = trBaseType.Detach();
                }
            }
            else
            {
                trType.Free();
            }
        }

        return S_OK;
    };

    while (!trWalkQueue.empty())
    {
        const ToRelease<ICorDebugType> trFrontType(trWalkQueue.front().Detach());
        trWalkQueue.pop_front();

        IfFailRet(walkNext(trFrontType));
        if (Status == S_CAN_EXIT)
        {
            return S_OK;
        }
    }

    return S_OK;
}

// Cleans up the Walkers internal state. See Cleanup() in evaluation.cpp.
void WalkMembersCleanup()
{
    const std::scoped_lock<std::mutex> lock(GetMembersMutex());
    GetInitializedTypesAddr().clear();
    GetModulePlainILImageCache().clear();
}

} // namespace dncdbg::Walkers
