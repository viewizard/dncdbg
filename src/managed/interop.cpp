// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "managed/interop.h"

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <dirent.h>
#include <cstdlib>
#include <sys/stat.h>
#elif _WIN32
#include "utils/limits.h"
#include <windows.h>
#include <palclr.h>
#endif

#include "utils/logger.h" // NOLINT(misc-include-cleaner)
#include "utils/torelease.h" // NOLINT(misc-include-cleaner)
#include "utils/dynlibs.h"
#include "utils/filesystem.h"
#include "utils/rwlock.h"
#include "utils/utf.h"
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
                           DWORD nNumberOfArguments, CONST ULONG_PTR *lpArguments)
{
}
#endif

namespace dncdbg::Interop
{

namespace // unnamed namespace
{

// This function searches *.dll files in specified directory and adds full paths to files
// to colon-separated list `tpaList'.
void AddFilesFromDirectoryToTpaList(const std::string &directory, std::string &tpaList)
{
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
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
                fullFilename.append(entry->d_name);

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

            const std::string filename(entry->d_name);

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
            if (addedAssemblies.find(filenameWithoutExt) == addedAssemblies.end())
            {
                addedAssemblies.insert(filenameWithoutExt);

                tpaList.append(directory);
                tpaList += FileSystem::PathSeparator;
                tpaList.append(filename);
                tpaList.append(":");
            }
        }

        // Rewind the directory stream to be able to iterate over it for the next extension
        rewinddir(dir);
    }

    closedir(dir);
#elif _WIN32
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
                    if (addedAssemblies.find(filenameWithoutExt) == addedAssemblies.end())
                    {
                        addedAssemblies.insert(filenameWithoutExt);

                        tpaList.append(directory);
                        tpaList += FileSystem::PathSeparator;
                        tpaList.append(filename);
                        tpaList.append(";");
                    }
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
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
    unsetenv("CORECLR_ENABLE_PROFILING");
#elif _WIN32
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

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
    return static_cast<uint32_t>((reinterpret_cast<uint32_t *>(bstrString)[-1]) / sizeof(WCHAR));
#elif _WIN32
    return ::SysStringLen(bstrString);
#endif
}

enum class RetCode : int32_t // NOLINT(performance-enum-size)
{
    OK = 0,
    Fail = 1,
    Exception = 2
};

RWLock CLRrwlock;
void *hostHandle = nullptr;
unsigned int domainId = 0;

// part of coreclrhost.h file from runtime sources
#if defined(_WIN32) && defined(_M_IX86)
#define CORECLR_CALLING_CONVENTION __stdcall
#else
#define CORECLR_CALLING_CONVENTION
#endif

#define CORECLR_HOSTING_API(function, ...) \
    extern "C" int CORECLR_CALLING_CONVENTION function(__VA_ARGS__); \
    using function##_ptr = int (CORECLR_CALLING_CONVENTION *)(__VA_ARGS__)

CORECLR_HOSTING_API(coreclr_shutdown, void *hostHandle, unsigned int domainId);
CORECLR_HOSTING_API(coreclr_initialize, const char *exePath, const char *appDomainFriendlyName, int propertyCount,
                    const char **propertyKeys, const char **propertyValues, void **hostHandle, unsigned int *domainId);
CORECLR_HOSTING_API(coreclr_create_delegate, void *hostHandle, unsigned int domainId, const char *entryPointAssemblyName,
                    const char *entryPointTypeName, const char *entryPointMethodName, void **delegate);

coreclr_shutdown_ptr shutdownCoreClr = nullptr;

// CoreCLR use fixed size integers, don't use system/arch size dependent types for delegates.
// Important! In case of usage pointer to variable as delegate arg, make sure it have proper size for CoreCLR!
// For example, native code "int" != managed code "int", since managed code "int" is 4 byte fixed size.
using ReadMemoryDelegate = int (*)(uint64_t, char *, int32_t);
using LoadSymbolsForModuleDelegate = void *(*)(const WCHAR *, BOOL, uint64_t, int32_t, uint64_t, int32_t, ReadMemoryDelegate);
using DisposeDelegate = void (*)(void *);
using GetLocalVariableNameAndScopeDelegate = RetCode (*)(void *, int32_t, uint32_t, BSTR *, int32_t *, int32_t *);
using GetHoistedLocalScopesDelegate = RetCode (*)(void *, int32_t, void **, int32_t *);
using GetSequencePointByILOffsetDelegate = RetCode (*)(void *, mdMethodDef, uint32_t, void *);
using GetSequencePointsDelegate = RetCode (*)(void *, mdMethodDef, void **, int32_t *);
using GetNextUserCodeILOffsetDelegate = RetCode (*)(void *, mdMethodDef, uint32_t, uint32_t *, int32_t *);
using GetStepRangesFromIPDelegate = RetCode (*)(void *, uint32_t, mdMethodDef, uint32_t *, uint32_t *);
using GetModuleMethodsRangesDelegate = RetCode (*)(void *, uint32_t, void *, uint32_t, void *, void **);
using ResolveBreakPointsDelegate = RetCode (*)(void *, int32_t, void *, int32_t, int32_t, int32_t *, const WCHAR *, void **);
using GetAsyncMethodSteppingInfoDelegate = RetCode (*)(void *, mdMethodDef, void **, int32_t *, uint32_t *);
using CalculationDelegate = RetCode (*)(void *, int32_t, void *, int32_t, int32_t, int32_t *, void **, BSTR *);
using GenerateStackMachineProgramDelegate = int (*)(const WCHAR *, void **, BSTR *);
using ReleaseStackMachineProgramDelegate = void (*)(void *);
using NextStackCommandDelegate = int (*)(void *, int32_t *, void **, BSTR *);
using StringToUpperDelegate = RetCode (*)(const WCHAR *, BSTR *);
using CoTaskMemAllocDelegate = void *(*)(int32_t);
using CoTaskMemFreeDelegate = void (*)(void *);
using SysAllocStringLenDelegate = void *(*)(int32_t);
using SysFreeStringDelegate = void (*)(void *);

LoadSymbolsForModuleDelegate loadSymbolsForModuleDelegate = nullptr;
DisposeDelegate disposeDelegate = nullptr;
GetLocalVariableNameAndScopeDelegate getLocalVariableNameAndScopeDelegate = nullptr;
GetHoistedLocalScopesDelegate getHoistedLocalScopesDelegate = nullptr;
GetSequencePointByILOffsetDelegate getSequencePointByILOffsetDelegate = nullptr;
GetSequencePointsDelegate getSequencePointsDelegate = nullptr;
GetNextUserCodeILOffsetDelegate getNextUserCodeILOffsetDelegate = nullptr;
GetStepRangesFromIPDelegate getStepRangesFromIPDelegate = nullptr;
GetModuleMethodsRangesDelegate getModuleMethodsRangesDelegate = nullptr;
ResolveBreakPointsDelegate resolveBreakPointsDelegate = nullptr;
GetAsyncMethodSteppingInfoDelegate getAsyncMethodSteppingInfoDelegate = nullptr;
GenerateStackMachineProgramDelegate generateStackMachineProgramDelegate = nullptr;
ReleaseStackMachineProgramDelegate releaseStackMachineProgramDelegate = nullptr;
NextStackCommandDelegate nextStackCommandDelegate = nullptr;
StringToUpperDelegate stringToUpperDelegate = nullptr;
CoTaskMemAllocDelegate coTaskMemAllocDelegate = nullptr;
CoTaskMemFreeDelegate coTaskMemFreeDelegate = nullptr;
SysAllocStringLenDelegate sysAllocStringLenDelegate = nullptr;
SysFreeStringDelegate sysFreeStringDelegate = nullptr;
CalculationDelegate calculationDelegate = nullptr;

constexpr char ManagedPartDllName[] = "ManagedPart"; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
constexpr char SymbolReaderClassName[] = "DNCDbg.SymbolReader"; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
constexpr char EvaluationClassName[] = "DNCDbg.Evaluation"; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
constexpr char UtilsClassName[] = "DNCDbg.Utils"; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

// Pass to managed helper code to read in-memory PEs/PDBs
// Returns the number of bytes read.
int ReadMemoryForSymbols(uint64_t address, char *buffer, int cb)
{
    if (address == 0 || buffer == nullptr || cb == 0)
    {
        return 0;
    }

    std::memcpy(buffer, reinterpret_cast<const void *>(address), cb); // NOLINT(performance-no-int-to-ptr)
    return cb;
}

} // unnamed namespace

HRESULT LoadSymbolsForPortablePDB(const std::string &modulePath, BOOL isInMemory, BOOL isFileLayout, uint64_t peAddress,
                                  uint64_t peSize, uint64_t inMemoryPdbAddress, uint64_t inMemoryPdbSize,
                                  void **ppSymbolReaderHandle)
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

    *ppSymbolReaderHandle = loadSymbolsForModuleDelegate(szModuleName, isFileLayout, peAddress, static_cast<int>(peSize), inMemoryPdbAddress,
                                                         static_cast<int>(inMemoryPdbSize), ReadMemoryForSymbols);

    if (*ppSymbolReaderHandle == nullptr)
    {
        return E_FAIL;
    }

    return S_OK;
}

SequencePoint::~SequencePoint() noexcept
{
    Interop::SysFreeString(document);
}

void DisposeSymbols(void *pSymbolReaderHandle)
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
void Init(const std::string &coreClrPath)
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
    DLHandle coreclrLib = DLOpen(coreClrPath);
    if (coreclrLib == nullptr)
    {
        throw std::invalid_argument("Failed to load coreclr path=" + coreClrPath);
    }

    coreclr_initialize_ptr initializeCoreCLR = reinterpret_cast<coreclr_initialize_ptr>(DLSym(coreclrLib, "coreclr_initialize"));
    if (initializeCoreCLR == nullptr)
    {
        throw std::invalid_argument("coreclr_initialize not found in lib, CoreCLR path=" + coreClrPath);
    }

    std::string tpaList;
    AddFilesFromDirectoryToTpaList(clrDir, tpaList);

    const char *propertyKeys[] = { // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
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

    const char *propertyValues[] = { // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        tpaList.c_str(),                         // TRUSTED_PLATFORM_ASSEMBLIES
        exeDir.c_str(),                          // APP_PATHS
        exeDir.c_str(),                          // APP_NI_PATHS
        clrDir.c_str(),                          // NATIVE_DLL_SEARCH_DIRECTORIES
        "UseLatestBehaviorWhenTFMNotSpecified"   // AppDomainCompatSwitch
    };

    Status = initializeCoreCLR(exe.c_str(), "debugger", sizeof(propertyKeys) / sizeof(propertyKeys[0]), propertyKeys,
                               propertyValues, &hostHandle, &domainId);

    if (FAILED(Status))
    {
        throw std::runtime_error("Fail to initialize CoreCLR " + std::to_string(Status));
    }

    coreclr_create_delegate_ptr createDelegate = reinterpret_cast<coreclr_create_delegate_ptr>(DLSym(coreclrLib, "coreclr_create_delegate"));
    if (createDelegate == nullptr)
    {
        throw std::runtime_error("coreclr_create_delegate not found");
    }

    shutdownCoreClr = reinterpret_cast<coreclr_shutdown_ptr>(DLSym(coreclrLib, "coreclr_shutdown"));
    if (shutdownCoreClr == nullptr)
    {
        throw std::runtime_error("coreclr_shutdown not found");
    }

    const bool allDelegatesCreated = 
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, SymbolReaderClassName, "LoadSymbolsForModule", reinterpret_cast<void **>(&loadSymbolsForModuleDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, SymbolReaderClassName, "Dispose", reinterpret_cast<void **>(&disposeDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, SymbolReaderClassName, "GetLocalVariableNameAndScope", reinterpret_cast<void **>(&getLocalVariableNameAndScopeDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, SymbolReaderClassName, "GetHoistedLocalScopes", reinterpret_cast<void **>(&getHoistedLocalScopesDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, SymbolReaderClassName, "GetSequencePointByILOffset", reinterpret_cast<void **>(&getSequencePointByILOffsetDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, SymbolReaderClassName, "GetSequencePoints", reinterpret_cast<void **>(&getSequencePointsDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, SymbolReaderClassName, "GetNextUserCodeILOffset", reinterpret_cast<void **>(&getNextUserCodeILOffsetDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, SymbolReaderClassName, "GetStepRangesFromIP", reinterpret_cast<void **>(&getStepRangesFromIPDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, SymbolReaderClassName, "GetModuleMethodsRanges", reinterpret_cast<void **>(&getModuleMethodsRangesDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, SymbolReaderClassName, "ResolveBreakPoints", reinterpret_cast<void **>(&resolveBreakPointsDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, SymbolReaderClassName, "GetAsyncMethodSteppingInfo", reinterpret_cast<void **>(&getAsyncMethodSteppingInfoDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, EvaluationClassName, "Calculation", reinterpret_cast<void **>(&calculationDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, EvaluationClassName, "GenerateStackMachineProgram", reinterpret_cast<void **>(&generateStackMachineProgramDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, EvaluationClassName, "ReleaseStackMachineProgram", reinterpret_cast<void **>(&releaseStackMachineProgramDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, EvaluationClassName, "NextStackCommand", reinterpret_cast<void **>(&nextStackCommandDelegate))) &&
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, UtilsClassName, "StringToUpper", reinterpret_cast<void **>(&stringToUpperDelegate)));
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, UtilsClassName, "CoTaskMemAlloc", reinterpret_cast<void **>(&coTaskMemAllocDelegate)));
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, UtilsClassName, "CoTaskMemFree", reinterpret_cast<void **>(&coTaskMemFreeDelegate)));
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, UtilsClassName, "SysAllocStringLen", reinterpret_cast<void **>(&sysAllocStringLenDelegate)));
        SUCCEEDED(Status = createDelegate(hostHandle, domainId, ManagedPartDllName, UtilsClassName, "SysFreeString", reinterpret_cast<void **>(&sysFreeStringDelegate)));

    if (!allDelegatesCreated)
    {
        throw std::runtime_error("createDelegate failed with status: " + std::to_string(Status));
    }

    const bool allDelegatesInited = (loadSymbolsForModuleDelegate != nullptr) &&
                                    (disposeDelegate != nullptr) &&
                                    (getLocalVariableNameAndScopeDelegate != nullptr) &&
                                    (getHoistedLocalScopesDelegate != nullptr) &&
                                    (getSequencePointByILOffsetDelegate != nullptr) &&
                                    (getSequencePointsDelegate != nullptr) &&
                                    (getNextUserCodeILOffsetDelegate != nullptr) &&
                                    (getStepRangesFromIPDelegate != nullptr) &&
                                    (getModuleMethodsRangesDelegate != nullptr) &&
                                    (resolveBreakPointsDelegate != nullptr) &&
                                    (getAsyncMethodSteppingInfoDelegate != nullptr) &&
                                    (generateStackMachineProgramDelegate != nullptr) &&
                                    (releaseStackMachineProgramDelegate != nullptr) &&
                                    (nextStackCommandDelegate != nullptr) &&
                                    (stringToUpperDelegate != nullptr) &&
                                    (coTaskMemAllocDelegate != nullptr) &&
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
void Shutdown()
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
        LOGE("coreclr_shutdown failed - status: 0x%08x", Status);
    }

    shutdownCoreClr = nullptr;
    loadSymbolsForModuleDelegate = nullptr;
    disposeDelegate = nullptr;
    getLocalVariableNameAndScopeDelegate = nullptr;
    getHoistedLocalScopesDelegate = nullptr;
    getSequencePointByILOffsetDelegate = nullptr;
    getSequencePointsDelegate = nullptr;
    getNextUserCodeILOffsetDelegate = nullptr;
    getStepRangesFromIPDelegate = nullptr;
    getModuleMethodsRangesDelegate = nullptr;
    resolveBreakPointsDelegate = nullptr;
    getAsyncMethodSteppingInfoDelegate = nullptr;
    stringToUpperDelegate = nullptr;
    coTaskMemAllocDelegate = nullptr;
    coTaskMemFreeDelegate = nullptr;
    sysAllocStringLenDelegate = nullptr;
    sysFreeStringDelegate = nullptr;
    calculationDelegate = nullptr;
}

HRESULT GetSequencePointByILOffset(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t ilOffset,
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

HRESULT GetSequencePoints(void *pSymbolReaderHandle, mdMethodDef methodToken, SequencePoint **sequencePoints, int32_t &Count)
{
    const ReadLock read_lock(CLRrwlock);
    if ((getSequencePointsDelegate == nullptr) || (pSymbolReaderHandle == nullptr))
    {
        return E_FAIL;
    }

    const RetCode retCode = getSequencePointsDelegate(pSymbolReaderHandle, static_cast<int32_t>(methodToken),
                                                      reinterpret_cast<void **>(sequencePoints), &Count);

    return retCode == RetCode::OK ? S_OK : E_FAIL;
}

HRESULT GetNextUserCodeILOffset(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t ilOffset,
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

HRESULT GetStepRangesFromIP(void *pSymbolReaderHandle, uint32_t ip, mdMethodDef MethodToken, uint32_t *ilStartOffset, uint32_t *ilEndOffset)
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

HRESULT GetNamedLocalVariableAndScope(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t localIndex,
                                      WCHAR *localName, uint32_t localNameLen, int32_t *pIlStart, int32_t *pIlEnd)
{
    ReadLock read_lock(CLRrwlock);
    if ((getLocalVariableNameAndScopeDelegate == nullptr) || (pSymbolReaderHandle == nullptr) ||
        (localName == nullptr) || (pIlStart == nullptr) || (pIlEnd == nullptr))
    {
        return E_FAIL;
    }

    BSTR wszLocalName = Interop::SysAllocStringLen(mdNameLen);
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

    wcscpy_s(localName, localNameLen, wszLocalName);
    Interop::SysFreeString(wszLocalName);

    return S_OK;
}

HRESULT GetHoistedLocalScopes(void *pSymbolReaderHandle, mdMethodDef methodToken, void **data, int32_t &hoistedLocalScopesCount)
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

HRESULT Calculation(void *firstOp, int32_t firstType, void *secondOp, int32_t secondType, int32_t operationType,
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

HRESULT GetModuleMethodsRanges(void *pSymbolReaderHandle, uint32_t constrTokensNum, void *constrTokens,
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

HRESULT ResolveBreakPoints(void *pSymbolReaderHandle, int32_t tokenNum, void *Tokens, int32_t sourceLine,
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

HRESULT GetAsyncMethodSteppingInfo(void *pSymbolReaderHandle, mdMethodDef methodToken,
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

HRESULT GenerateStackMachineProgram(const std::string &expr, void **ppStackProgram, std::string &textOutput)
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

void ReleaseStackMachineProgram(void *pStackProgram)
{
    const ReadLock read_lock(CLRrwlock);
    if ((releaseStackMachineProgramDelegate == nullptr) || (pStackProgram == nullptr))
    {
        return;
    }

    releaseStackMachineProgramDelegate(pStackProgram);
}

// Note, managed part will release Ptr unmanaged memory at object finalizer call after ReleaseStackMachineProgram() call.
// Native part must not release Ptr memory, allocated by managed part.
HRESULT NextStackCommand(void *pStackProgram, int32_t &Command, void **Ptr, std::string &textOutput)
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

void *AllocString(const std::string &str)
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

    memmove(bstr, wstr.data(), wstr.size() * sizeof(decltype(wstr[0])));
    return bstr;
}

HRESULT StringToUpper(std::string &String)
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

BSTR SysAllocStringLen(int32_t size)
{
    const ReadLock read_lock(CLRrwlock);
    if (sysAllocStringLenDelegate == nullptr)
    {
        return nullptr;
    }

    return reinterpret_cast<BSTR>(sysAllocStringLenDelegate(size));
}

void SysFreeString(BSTR ptrBSTR)
{
    const ReadLock read_lock(CLRrwlock);
    if (sysFreeStringDelegate == nullptr)
    {
        return;
    }

    sysFreeStringDelegate(ptrBSTR);
}

void *CoTaskMemAlloc(int32_t size)
{
    const ReadLock read_lock(CLRrwlock);
    if (coTaskMemAllocDelegate == nullptr)
    {
        return nullptr;
    }

    return coTaskMemAllocDelegate(size);
}

void CoTaskMemFree(void *ptr)
{
    const ReadLock read_lock(CLRrwlock);
    if (coTaskMemFreeDelegate == nullptr)
    {
        return;
    }

    coTaskMemFreeDelegate(ptr);
}

} // namespace dncdbg::Interop
