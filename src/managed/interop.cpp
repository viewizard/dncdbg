// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "managed/interop.h"

#ifdef FEATURE_PAL
#include <dirent.h>
#include <cstdlib>
#include <sys/stat.h>
#else
#include <windows.h>
#include <palclr.h>
#endif

#include "utils/logger.h"
#include "utils/dynlibs.h"
#include "utils/filesystem.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string_view>

#ifdef FEATURE_PAL
// Suppress undefined reference
// `_invalid_parameter(char16_t const*, char16_t const*, char16_t const*, unsigned int, unsigned long)':
//      /coreclr/src/pal/inc/rt/safecrt.h:386: undefined reference to `RaiseException'
static void RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags, // NOLINT(misc-use-anonymous-namespace)
                           DWORD nNumberOfArguments, const ULONG_PTR *lpArguments)
{
}
#endif

namespace dncdbg
{

namespace
{

// This function searches *.dll files in the specified directory and adds full paths to files
// to the colon-separated list `tpaList'.
void AddFilesFromDirectoryToTpaList(const std::string &directory, std::string &tpaList)
{
#ifdef FEATURE_PAL
    static constexpr std::array<std::string_view, 4> tpaExtensions{
        ".ni.dll", // Probe for .ni.dll first so that it's preferred if ni and il coexist in the same dir
        ".dll",
        ".ni.exe",
        ".exe",
    };

    DIR *dir = opendir(directory.c_str());
    if (dir == nullptr)
    {
        return;
    }

    std::set<std::string> addedAssemblies;

    // Walk the directory for each extension separately so that we first get files with .ni.dll extension,
    // then files with .dll extension, etc.
    for (const auto &ext : tpaExtensions)
    {
        struct dirent *entry = nullptr;

        // For all entries in the directory
        while ((entry = readdir(dir)) != nullptr)
        {
            // We are interested in files only
            switch (entry->d_type)
            {
            case DT_REG:
                break;

            // Handle symlinks and file systems that do not support d_type
            case DT_LNK:
            case DT_UNKNOWN:
            {
                std::string fullFilename;

                fullFilename.append(directory);
                fullFilename += FileSystem::PathSeparator;
                fullFilename.append(entry->d_name); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

                struct stat sb{};
                if ((stat(fullFilename.c_str(), &sb) == -1) ||
                    !S_ISREG(sb.st_mode))
                {
                    continue;
                }
            }
            break;

            default:
                continue;
            }

            const std::string filename(entry->d_name); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

            if (ext.length() >= filename.length())
            {
                continue;
            }

            // Check if the extension matches the one we are looking for
            const size_t extPos = filename.length() - ext.length();
            if (filename.compare(extPos, ext.length(), ext) != 0)
            {
                continue;
            }

            const std::string filenameWithoutExt(filename.substr(0, extPos));

            // Make sure if we have an assembly with multiple extensions present,
            // we insert only one version of it.
            if (addedAssemblies.find(filenameWithoutExt) != addedAssemblies.end())
            {
                continue;
            }

            addedAssemblies.insert(filenameWithoutExt);
            tpaList.append(directory);
            tpaList += FileSystem::PathSeparator;
            tpaList.append(filename);
            tpaList.append(":");
        }

        // Rewind the directory stream to be able to iterate over it for the next extension
        rewinddir(dir);
    }

    closedir(dir);
#else
    static constexpr std::array<std::string_view, 4> tpaExtensions{
        "*.ni.dll", // Probe for .ni.dll first so that it's preferred if ni and il coexist in the same dir
        "*.dll",
        "*.ni.exe",
        "*.exe",
    };

    std::set<std::string> addedAssemblies;

    // Walk the directory for each extension separately so that we first get files with .ni.dll extension,
    // then files with .dll extension, etc.
    for (const auto &ext : tpaExtensions)
    {
        std::string assemblyPath(directory);
        assemblyPath += FileSystem::PathSeparator;
        assemblyPath.append(ext);

        WIN32_FIND_DATAA data;
        HANDLE findHandle = FindFirstFileA(assemblyPath.c_str(), &data);

        if (findHandle != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {

                    std::string filename(data.cFileName);
                    if (ext.length() >= filename.length())
                    {
                        continue;
                    }

                    const size_t extPos = filename.length() - ext.length();
                    std::string filenameWithoutExt(filename.substr(0, extPos));

                    // Make sure if we have an assembly with multiple extensions present,
                    // we insert only one version of it.
                    if (addedAssemblies.find(filenameWithoutExt) != addedAssemblies.end())
                    {
                        continue;
                    }

                    addedAssemblies.insert(filenameWithoutExt);
                    tpaList.append(directory);
                    tpaList += FileSystem::PathSeparator;
                    tpaList.append(filename);
                    tpaList.append(";");
                }
            } while (0 != FindNextFileA(findHandle, &data));

            FindClose(findHandle);
        }
    }
#endif
}

// This function unsets `CORECLR_ENABLE_PROFILING' environment variable.
void UnsetCoreCLREnv()
{
#ifdef FEATURE_PAL
    unsetenv("CORECLR_ENABLE_PROFILING");
#else
    _putenv("CORECLR_ENABLE_PROFILING=");
#endif
}

// Returns the length of a BSTR.
uint32_t SysStringLen(BSTR bstrString) // NOLINT(readability-non-const-parameter)
{
    if (bstrString == nullptr)
    {
        return 0;
    }

#ifdef FEATURE_PAL
    // https://learn.microsoft.com/en-us/previous-versions/windows/desktop/automat/bstr
    // A BSTR is a composite data type that consists of a length prefix, a data string, and a terminator.
    // A four-byte integer that contains the number of bytes in the following data string.
    // It appears immediately before the first character of the data string. This value does not include the terminator.
    return static_cast<uint32_t>(*(reinterpret_cast<uint32_t *>(bstrString) - 1) / sizeof(WCHAR));
#else
    return ::SysStringLen(bstrString);
#endif
}

// Passed to managed helper code to read in-memory PEs/PDBs.
// Returns the number of bytes read.
int ReadMemoryForSymbols(uint64_t address, char *buffer, int cb)
{
    if (address == 0 || buffer == nullptr || cb == 0)
    {
        return 0;
    }

    std::memcpy(buffer, reinterpret_cast<const void *>(address), cb);
    return cb;
}

} // unnamed namespace

RWLock Interop::CLRrwlock;
void *Interop::hostHandle = nullptr;
unsigned int Interop::domainId = 0;
coreclr_shutdown_ptr Interop::shutdownCoreClr = nullptr;

Interop::LoadSymbolsForModuleDelegate Interop::loadSymbolsForModuleDelegate = nullptr;
Interop::DisposeDelegate Interop::disposeDelegate = nullptr;
Interop::GetLocalVariableNameAndScopeDelegate Interop::getLocalVariableNameAndScopeDelegate = nullptr;
Interop::GetHoistedLocalScopesDelegate Interop::getHoistedLocalScopesDelegate = nullptr;
Interop::GetSequencePointByILOffsetDelegate Interop::getSequencePointByILOffsetDelegate = nullptr;
Interop::GetNextUserCodeILOffsetDelegate Interop::getNextUserCodeILOffsetDelegate = nullptr;
Interop::GetStepRangesFromIPDelegate Interop::getStepRangesFromIPDelegate = nullptr;
Interop::GetModuleMethodsRangesDelegate Interop::getModuleMethodsRangesDelegate = nullptr;
Interop::ResolveBreakPointsDelegate Interop::resolveBreakPointsDelegate = nullptr;
Interop::GetAsyncMethodSteppingInfoDelegate Interop::getAsyncMethodSteppingInfoDelegate = nullptr;
Interop::GetLocalConstantsDelegate Interop::getLocalConstantsDelegate = nullptr;
Interop::GenerateStackMachineProgramDelegate Interop::generateStackMachineProgramDelegate = nullptr;
Interop::ReleaseStackMachineProgramDelegate Interop::releaseStackMachineProgramDelegate = nullptr;
Interop::NextStackCommandDelegate Interop::nextStackCommandDelegate = nullptr;
Interop::StringToUpperDelegate Interop::stringToUpperDelegate = nullptr;
Interop::CoTaskMemFreeDelegate Interop::coTaskMemFreeDelegate = nullptr;
Interop::SysAllocStringLenDelegate Interop::sysAllocStringLenDelegate = nullptr;
Interop::SysFreeStringDelegate Interop::sysFreeStringDelegate = nullptr;
Interop::CalculationDelegate Interop::calculationDelegate = nullptr;

HRESULT Interop::LoadSymbolsForPortablePDB(const std::string &modulePath, BOOL isInMemory, BOOL isFileLayout, uint64_t peAddress,
                                           uint64_t peSize, uint64_t inMemoryPdbAddress, uint64_t inMemoryPdbSize,
                                           void **ppSymbolReaderHandle, std::string &pdbPath)
{
    const ReadLock read_lock(CLRrwlock);
    if ((loadSymbolsForModuleDelegate == nullptr) || (ppSymbolReaderHandle == nullptr))
    {
        return E_FAIL;
    }

    // The module name needs to be null for in-memory PE's.
    const WCHAR *szModuleName = nullptr;
    auto wModulePath = to_utf16(modulePath);
    if ((isInMemory == FALSE) && !modulePath.empty())
    {
        szModuleName = wModulePath.c_str();
    }
    BSTR pdbPathBSTR = nullptr;

    *ppSymbolReaderHandle = loadSymbolsForModuleDelegate(szModuleName, isFileLayout, peAddress, static_cast<int>(peSize), inMemoryPdbAddress,
                                                         static_cast<int>(inMemoryPdbSize), ReadMemoryForSymbols, &pdbPathBSTR);

    if (*ppSymbolReaderHandle == nullptr)
    {
        return E_FAIL;
    }

    if (pdbPathBSTR != nullptr)
    {
        pdbPath = to_utf8(pdbPathBSTR);
        SysFreeString(pdbPathBSTR);
    }

    return S_OK;
}

Interop::SequencePoint::~SequencePoint() noexcept
{
    Interop::SysFreeString(document);
}

void Interop::DisposeSymbols(void *pSymbolReaderHandle)
{
    const ReadLock read_lock(CLRrwlock);
    if ((disposeDelegate == nullptr) || (pSymbolReaderHandle == nullptr))
    {
        return;
    }

    disposeDelegate(pSymbolReaderHandle);
}

// WARNING! Due to CoreCLR limitations, Init() / Shutdown() sequence can be used only once during process execution.
// Note, init in case of error will throw exception, since this is fatal for debugger (CoreCLR can't be re-init).
//
// Important! The coreclr_initialize is supposed to be called on the main thread only.
// On Linux with musl libc, calling coreclr_initialize from a non-main thread will cause SIGSEGV.
// For more info, see: https://github.com/dotnet/runtime/issues/103741
void Interop::Init(const std::string &coreClrPath)
{
    const WriteLock write_lock(CLRrwlock);

    // If we have shutdownCoreClr initialized, we already initialized all managed part.
    if (shutdownCoreClr != nullptr)
    {
        return;
    }

    const std::string clrDir = coreClrPath.substr(0, coreClrPath.rfind(DIRECTORY_SEPARATOR_CHAR_A));

    HRESULT Status = S_OK;

    UnsetCoreCLREnv();

    // Pin the module - CoreCLR.so/dll does not support being unloaded.
    // "CoreCLR does not support reinitialization or unloading. Do not call `coreclr_initialize` again or unload the
    // CoreCLR library." https://docs.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting
    DLHandle coreclrLib = DLOpen(coreClrPath.c_str());
    if (coreclrLib == nullptr)
    {
        throw std::invalid_argument("Failed to load coreclr path=" + coreClrPath);
    }

    auto initializeCoreCLR = reinterpret_cast<coreclr_initialize_ptr>(DLSym(coreclrLib, "coreclr_initialize"));
    if (initializeCoreCLR == nullptr)
    {
        throw std::invalid_argument("coreclr_initialize not found in lib, CoreCLR path=" + coreClrPath);
    }

    std::string tpaList;
    AddFilesFromDirectoryToTpaList(clrDir, tpaList);

    const std::array<const char *, 5> propertyKeys = {
        "TRUSTED_PLATFORM_ASSEMBLIES",
        "APP_PATHS",
        "APP_NI_PATHS",
        "NATIVE_DLL_SEARCH_DIRECTORIES",
        "AppDomainCompatSwitch"
    };

    const std::string exe = GetExeAbsPath();
    if (exe.empty())
    {
        throw std::runtime_error("Unable to detect exe path");
    }

    const std::size_t dirSepIndex = exe.rfind(DIRECTORY_SEPARATOR_CHAR_A);
    if (dirSepIndex == std::string::npos)
    {
        throw std::runtime_error("Can't find directory separator in string returned by GetExeAbsPath");
    }

    const std::string exeDir = exe.substr(0, dirSepIndex);

    const std::array<const char *, 5> propertyValues = {
        tpaList.c_str(),                         // TRUSTED_PLATFORM_ASSEMBLIES
        exeDir.c_str(),                          // APP_PATHS
        exeDir.c_str(),                          // APP_NI_PATHS
        clrDir.c_str(),                          // NATIVE_DLL_SEARCH_DIRECTORIES
        "UseLatestBehaviorWhenTFMNotSpecified"   // AppDomainCompatSwitch
    };

    if (FAILED(Status = initializeCoreCLR(exe.c_str(), "debugger", static_cast<int>(propertyKeys.size()),
                                          const_cast<const char**>(propertyKeys.data()),
                                          const_cast<const char**>(propertyValues.data()),
                                          &hostHandle, &domainId)))
    {
        throw std::runtime_error("Fail to initialize CoreCLR " + std::to_string(Status));
    }

    auto createDelegate = reinterpret_cast<coreclr_create_delegate_ptr>(DLSym(coreclrLib, "coreclr_create_delegate"));
    if (createDelegate == nullptr)
    {
        throw std::runtime_error("coreclr_create_delegate not found");
    }

    shutdownCoreClr = reinterpret_cast<coreclr_shutdown_ptr>(DLSym(coreclrLib, "coreclr_shutdown"));
    if (shutdownCoreClr == nullptr)
    {
        throw std::runtime_error("coreclr_shutdown not found");
    }

    static const char *managedPartDllName = "ManagedPart";
    static const char *symbolReaderClassName = "DNCDbg.SymbolReader";
    static const char *evaluationClassName = "DNCDbg.Evaluation";
    static const char *utilsClassName = "DNCDbg.Utils";

    const bool allDelegatesCreated =
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, symbolReaderClassName, "LoadSymbolsForModule", reinterpret_cast<void **>(&loadSymbolsForModuleDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, symbolReaderClassName, "Dispose", reinterpret_cast<void **>(&disposeDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, symbolReaderClassName, "GetLocalVariableNameAndScope", reinterpret_cast<void **>(&getLocalVariableNameAndScopeDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, symbolReaderClassName, "GetHoistedLocalScopes", reinterpret_cast<void **>(&getHoistedLocalScopesDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, symbolReaderClassName, "GetSequencePointByILOffset", reinterpret_cast<void **>(&getSequencePointByILOffsetDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, symbolReaderClassName, "GetNextUserCodeILOffset", reinterpret_cast<void **>(&getNextUserCodeILOffsetDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, symbolReaderClassName, "GetStepRangesFromIP", reinterpret_cast<void **>(&getStepRangesFromIPDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, symbolReaderClassName, "GetModuleMethodsRanges", reinterpret_cast<void **>(&getModuleMethodsRangesDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, symbolReaderClassName, "ResolveBreakPoints", reinterpret_cast<void **>(&resolveBreakPointsDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, symbolReaderClassName, "GetAsyncMethodSteppingInfo", reinterpret_cast<void **>(&getAsyncMethodSteppingInfoDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, symbolReaderClassName, "GetLocalConstants", reinterpret_cast<void **>(&getLocalConstantsDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, evaluationClassName, "Calculation", reinterpret_cast<void **>(&calculationDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, evaluationClassName, "GenerateStackMachineProgram", reinterpret_cast<void **>(&generateStackMachineProgramDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, evaluationClassName, "ReleaseStackMachineProgram", reinterpret_cast<void **>(&releaseStackMachineProgramDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, evaluationClassName, "NextStackCommand", reinterpret_cast<void **>(&nextStackCommandDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, utilsClassName, "StringToUpper", reinterpret_cast<void **>(&stringToUpperDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, utilsClassName, "CoTaskMemFree", reinterpret_cast<void **>(&coTaskMemFreeDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, utilsClassName, "SysAllocStringLen", reinterpret_cast<void **>(&sysAllocStringLenDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, managedPartDllName, utilsClassName, "SysFreeString", reinterpret_cast<void **>(&sysFreeStringDelegate)));

    if (!allDelegatesCreated)
    {
        throw std::runtime_error("createDelegate failed with status: " + std::to_string(Status));
    }

    const bool allDelegatesInited = (loadSymbolsForModuleDelegate != nullptr) &&
                                    (disposeDelegate != nullptr) &&
                                    (getLocalVariableNameAndScopeDelegate != nullptr) &&
                                    (getHoistedLocalScopesDelegate != nullptr) &&
                                    (getSequencePointByILOffsetDelegate != nullptr) &&
                                    (getNextUserCodeILOffsetDelegate != nullptr) &&
                                    (getStepRangesFromIPDelegate != nullptr) &&
                                    (getModuleMethodsRangesDelegate != nullptr) &&
                                    (resolveBreakPointsDelegate != nullptr) &&
                                    (getAsyncMethodSteppingInfoDelegate != nullptr) &&
                                    (getLocalConstantsDelegate != nullptr) &&
                                    (generateStackMachineProgramDelegate != nullptr) &&
                                    (releaseStackMachineProgramDelegate != nullptr) &&
                                    (nextStackCommandDelegate != nullptr) &&
                                    (stringToUpperDelegate != nullptr) &&
                                    (coTaskMemFreeDelegate != nullptr) &&
                                    (sysAllocStringLenDelegate != nullptr) &&
                                    (sysFreeStringDelegate != nullptr) &&
                                    (calculationDelegate != nullptr);

    if (!allDelegatesInited)
    {
        throw std::runtime_error("Some delegates nulled");
    }
}

// WARNING! Due to CoreCLR limitations, Shutdown() can't be called out of the Main() scope, for example, from global object destructor.
void Interop::Shutdown()
{
    const WriteLock write_lock(CLRrwlock);
    if (shutdownCoreClr == nullptr)
    {
        return;
    }

    // "Warm up Roslyn" thread still could be running at this point, let `coreclr_shutdown` care about this.
    HRESULT Status = S_OK;
    if (FAILED(Status = shutdownCoreClr(hostHandle, domainId)))
    {
        LOGE(log << "coreclr_shutdown failed - status: 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
    }

    shutdownCoreClr = nullptr;
    loadSymbolsForModuleDelegate = nullptr;
    disposeDelegate = nullptr;
    getLocalVariableNameAndScopeDelegate = nullptr;
    getHoistedLocalScopesDelegate = nullptr;
    getSequencePointByILOffsetDelegate = nullptr;
    getNextUserCodeILOffsetDelegate = nullptr;
    getStepRangesFromIPDelegate = nullptr;
    getModuleMethodsRangesDelegate = nullptr;
    resolveBreakPointsDelegate = nullptr;
    getAsyncMethodSteppingInfoDelegate = nullptr;
    getLocalConstantsDelegate = nullptr;
    stringToUpperDelegate = nullptr;
    coTaskMemFreeDelegate = nullptr;
    sysAllocStringLenDelegate = nullptr;
    sysFreeStringDelegate = nullptr;
    calculationDelegate = nullptr;
}

HRESULT Interop::GetSequencePointByILOffset(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                            SequencePoint *sequencePoint)
{
    const ReadLock read_lock(CLRrwlock);
    if ((getSequencePointByILOffsetDelegate == nullptr) || (pSymbolReaderHandle == nullptr) || (sequencePoint == nullptr))
    {
        return E_FAIL;
    }

    // Sequence points with startLine equal to 0xFEEFEE marker are filtered out on the managed side.
    const RetCode retCode = getSequencePointByILOffsetDelegate(pSymbolReaderHandle, static_cast<int32_t>(methodToken),
                                                               ilOffset, sequencePoint);

    return retCode == RetCode::OK ? S_OK : E_FAIL;
}

HRESULT Interop::GetNextUserCodeILOffset(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                         uint32_t &ilNextOffset, bool *noUserCodeFound)
{
    const ReadLock read_lock(CLRrwlock);
    if ((getNextUserCodeILOffsetDelegate == nullptr) || (pSymbolReaderHandle == nullptr))
    {
        return E_FAIL;
    }

    int32_t NoUserCodeFound = 0;

    // Sequence points with startLine equal to 0xFEEFEE marker are filtered out on the managed side.
    const RetCode retCode = getNextUserCodeILOffsetDelegate(pSymbolReaderHandle, static_cast<int32_t>(methodToken),
                                                            ilOffset, &ilNextOffset, &NoUserCodeFound);

    if (noUserCodeFound != nullptr)
    {
        *noUserCodeFound = NoUserCodeFound == 1;
    }

    return retCode == RetCode::OK ? S_OK : E_FAIL;
}

HRESULT Interop::GetStepRangesFromIP(void *pSymbolReaderHandle, uint32_t ip, mdMethodDef MethodToken, uint32_t *ilStartOffset, uint32_t *ilEndOffset)
{
    const ReadLock read_lock(CLRrwlock);
    if ((getStepRangesFromIPDelegate == nullptr) || (pSymbolReaderHandle == nullptr) ||
        (ilStartOffset == nullptr) || (ilEndOffset == nullptr))
    {
        return E_FAIL;

    }
    const RetCode retCode = getStepRangesFromIPDelegate(pSymbolReaderHandle, ip, static_cast<int32_t>(MethodToken),
                                                        ilStartOffset, ilEndOffset);
    return retCode == RetCode::OK ? S_OK : E_FAIL;
}

HRESULT Interop::GetNamedLocalVariableAndScope(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t localIndex,
                                               WSTRING &localName, int32_t *pIlStart, int32_t *pIlEnd)
{
    ReadLock read_lock(CLRrwlock);
    if ((getLocalVariableNameAndScopeDelegate == nullptr) || (pSymbolReaderHandle == nullptr) ||
        (pIlStart == nullptr) || (pIlEnd == nullptr))
    {
        return E_FAIL;
    }

    static constexpr uint32_t mdNameLen = 2048;
    BSTR wszLocalName = Interop::SysAllocStringLen(static_cast<int32_t>(mdNameLen));
    if (SysStringLen(wszLocalName) == 0)
    {
        return E_OUTOFMEMORY;
    }

    const RetCode retCode = getLocalVariableNameAndScopeDelegate(pSymbolReaderHandle, static_cast<int32_t>(methodToken), localIndex,
                                                                 &wszLocalName, pIlStart, pIlEnd);
    read_lock.unlock();

    if (retCode != RetCode::OK)
    {
        Interop::SysFreeString(wszLocalName);
        return E_FAIL;
    }

    localName = wszLocalName;
    Interop::SysFreeString(wszLocalName);

    return S_OK;
}

HRESULT Interop::GetHoistedLocalScopes(void *pSymbolReaderHandle, mdMethodDef methodToken, void **data, int32_t &hoistedLocalScopesCount)
{
    const ReadLock read_lock(CLRrwlock);
    if ((getHoistedLocalScopesDelegate == nullptr) || (pSymbolReaderHandle == nullptr))
    {
        return E_FAIL;
    }

    const RetCode retCode = getHoistedLocalScopesDelegate(pSymbolReaderHandle, static_cast<int32_t>(methodToken),
                                                          data, &hoistedLocalScopesCount);
    return retCode == RetCode::OK ? S_OK : E_FAIL;
}

HRESULT Interop::Calculation(void *firstOp, int32_t firstType, void *secondOp, int32_t secondType, int32_t operationType,
                             int32_t &resultType, void **data, std::string &errorText)
{
    ReadLock read_lock(CLRrwlock);
    if (calculationDelegate == nullptr)
    {
        return E_FAIL;
    }

    BSTR werrorText = nullptr;
    const RetCode retCode = calculationDelegate(firstOp, firstType, secondOp, secondType, operationType,
                                                &resultType, data, &werrorText);
    read_lock.unlock();

    if (retCode != RetCode::OK)
    {
        errorText = to_utf8(werrorText);
        Interop::SysFreeString(werrorText);
        return E_FAIL;
    }

    return S_OK;
}

HRESULT Interop::GetModuleMethodsRanges(void *pSymbolReaderHandle, uint32_t constrTokensNum, void *constrTokens,
                                        uint32_t normalTokensNum, void *normalTokens, void **data)
{
    const ReadLock read_lock(CLRrwlock);
    if ((getModuleMethodsRangesDelegate == nullptr) || (pSymbolReaderHandle == nullptr) || ((constrTokensNum != 0U) && (constrTokens == nullptr)) ||
        ((normalTokensNum != 0U) && (normalTokens == nullptr)) || (data == nullptr))
    {
        return E_FAIL;
    }

    const RetCode retCode = getModuleMethodsRangesDelegate(pSymbolReaderHandle, constrTokensNum, constrTokens,
                                                           normalTokensNum, normalTokens, data);
    return retCode == RetCode::OK ? S_OK : E_FAIL;
}

HRESULT Interop::ResolveBreakPoints(void *pSymbolReaderHandle, int32_t tokenNum, void *Tokens, int32_t sourceLine,
                                    int32_t nestedToken, int32_t &Count, const std::string &sourcePath, void **data)
{
    const ReadLock read_lock(CLRrwlock);
    if ((resolveBreakPointsDelegate == nullptr) || (pSymbolReaderHandle == nullptr) || (Tokens == nullptr) || (data == nullptr))
    {
        return E_FAIL;
    }

    const RetCode retCode = resolveBreakPointsDelegate(pSymbolReaderHandle, tokenNum, Tokens, sourceLine, nestedToken, &Count,
                                                       to_utf16(sourcePath).c_str(), data);
    return retCode == RetCode::OK ? S_OK : E_FAIL;
}

HRESULT Interop::GetAsyncMethodSteppingInfo(void *pSymbolReaderHandle, mdMethodDef methodToken,
                                            std::vector<AsyncAwaitInfoBlock> &AsyncAwaitInfo, uint32_t *ilOffset)
{
    ReadLock read_lock(CLRrwlock);
    if ((getAsyncMethodSteppingInfoDelegate == nullptr) || (pSymbolReaderHandle == nullptr) || (ilOffset == nullptr))
    {
        return E_FAIL;
    }

    AsyncAwaitInfoBlock *allocatedAsyncInfo = nullptr;
    int32_t asyncInfoCount = 0;

    const RetCode retCode = getAsyncMethodSteppingInfoDelegate(pSymbolReaderHandle, static_cast<int32_t>(methodToken),
                                                               reinterpret_cast<void **>(&allocatedAsyncInfo),
                                                               &asyncInfoCount, ilOffset);
    read_lock.unlock();

    if (retCode != RetCode::OK)
    {
        return E_FAIL;
    }

    if (asyncInfoCount == 0)
    {
        assert(allocatedAsyncInfo == nullptr);
        return S_OK;
    }

    AsyncAwaitInfo.assign(allocatedAsyncInfo, allocatedAsyncInfo + asyncInfoCount);

    Interop::CoTaskMemFree(allocatedAsyncInfo);
    return S_OK;
}

HRESULT Interop::GetLocalConstants(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                   void **data, int32_t &constantCount)
{
    ReadLock read_lock(CLRrwlock);
    if ((getLocalConstantsDelegate == nullptr) || (pSymbolReaderHandle == nullptr) || (data == nullptr))
    {
        return E_FAIL;
    }

    const RetCode retCode = getLocalConstantsDelegate(pSymbolReaderHandle, static_cast<int32_t>(methodToken),
                                                      ilOffset, data, &constantCount);
    read_lock.unlock();

    if (retCode != RetCode::OK)
    {
        return E_FAIL;
    }

    return S_OK;
}

HRESULT Interop::GenerateStackMachineProgram(const std::string &expr, void **ppStackProgram, std::string &textOutput)
{
    ReadLock read_lock(CLRrwlock);
    if ((generateStackMachineProgramDelegate == nullptr) || (ppStackProgram == nullptr))
    {
        return E_FAIL;
    }

    textOutput = "";
    BSTR wTextOutput = nullptr;
    const HRESULT Status = generateStackMachineProgramDelegate(to_utf16(expr).c_str(), ppStackProgram, &wTextOutput);
    read_lock.unlock();

    if (wTextOutput != nullptr)
    {
        textOutput = to_utf8(wTextOutput);
        SysFreeString(wTextOutput);
    }

    return Status;
}

void Interop::ReleaseStackMachineProgram(void *pStackProgram)
{
    const ReadLock read_lock(CLRrwlock);
    if ((releaseStackMachineProgramDelegate == nullptr) || (pStackProgram == nullptr))
    {
        return;
    }

    releaseStackMachineProgramDelegate(pStackProgram);
}

// Note: the managed part will release Ptr unmanaged memory at object finalizer call after ReleaseStackMachineProgram() call.
// The native part must not release Ptr memory allocated by the managed part.
HRESULT Interop::NextStackCommand(void *pStackProgram, int32_t &Command, void **Ptr, std::string &textOutput)
{
    ReadLock read_lock(CLRrwlock);
    if ((nextStackCommandDelegate == nullptr) || (pStackProgram == nullptr))
    {
        return E_FAIL;
    }

    textOutput = "";
    BSTR wTextOutput = nullptr;
    const HRESULT Status = nextStackCommandDelegate(pStackProgram, &Command, Ptr, &wTextOutput);
    read_lock.unlock();

    if (wTextOutput != nullptr)
    {
        textOutput = to_utf8(wTextOutput);
        SysFreeString(wTextOutput);
    }

    return Status;
}

void *Interop::AllocString(const std::string &str)
{
    if (str.empty())
    {
        return nullptr;
    }

    auto wstr = to_utf16(str);
    BSTR bstr = Interop::SysAllocStringLen(static_cast<int32_t>(wstr.size()));
    if (SysStringLen(bstr) == 0)
    {
        return nullptr;
    }

    memmove(bstr, wstr.data(), wstr.size() * sizeof(decltype(wstr.at(0))));
    return bstr;
}

HRESULT Interop::StringToUpper(std::string &String)
{
    ReadLock read_lock(CLRrwlock);
    if (stringToUpperDelegate == nullptr)
    {
        return E_FAIL;
    }

    BSTR wString = nullptr;
    const RetCode retCode = stringToUpperDelegate(to_utf16(String).c_str(), &wString);
    read_lock.unlock();

    if ((retCode != RetCode::OK) || (wString == nullptr))
    {
        return E_FAIL;
    }

    String = to_utf8(wString);
    Interop::SysFreeString(wString);

    return S_OK;
}

BSTR Interop::SysAllocStringLen(int32_t size)
{
    const ReadLock read_lock(CLRrwlock);
    if (sysAllocStringLenDelegate == nullptr)
    {
        return nullptr;
    }

    return reinterpret_cast<BSTR>(sysAllocStringLenDelegate(size));
}

void Interop::SysFreeString(BSTR ptrBSTR)
{
    const ReadLock read_lock(CLRrwlock);
    if (sysFreeStringDelegate == nullptr)
    {
        return;
    }

    sysFreeStringDelegate(ptrBSTR);
}

void Interop::CoTaskMemFree(void *ptr)
{
    const ReadLock read_lock(CLRrwlock);
    if (coTaskMemFreeDelegate == nullptr)
    {
        return;
    }

    coTaskMemFreeDelegate(ptr);
}

} // namespace dncdbg
