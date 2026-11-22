// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/modules.h"
#include "config/config.h"
#include "metadata/helpers.h"
#include "metadata/jmc.h"
#include "protocol/dap_events.h"
#include "utils/hresult.h"
#include "utils/int_to_string.h"
#include "utils/print.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <limits>
#include <list>
#include <mutex>
#include <string_view>
#include <utility>

namespace dncdbg::Modules
{

namespace
{

std::mutex &GetModuleMutex()
{
    static std::mutex moduleMutex;
    return moduleMutex;
}

std::list<Module> &GetModuleList()
{
    static std::list<Module> moduleList;
    return moduleList;
}

} // unnamed namespace

HRESULT GetModuleMvid(ICorDebugModule *pModule, std::string &strMvid)
{
    HRESULT Status = S_OK;

    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, reinterpret_cast<IUnknown **>(&trMDImport)));
    GUID mvid;
    IfFailRet(trMDImport->GetScopeProps(nullptr, 0, nullptr, &mvid));
    strMvid = PrintGUID(mvid);
    return S_OK;
}

std::string GetModuleFilePath(ICorDebugModule *pModule)
{
    uint32_t nameLen = 0;
    if (FAILED(pModule->GetName(0, &nameLen, nullptr)))
    {
        return {};
    }

    // Most module paths fit into the stack buffer, so the common case avoids a heap allocation.
    static constexpr uint32_t stackNameChars = 256;
    std::array<WCHAR, stackNameChars> stackName{};
    std::vector<WCHAR> heapName;
    WCHAR *wModName = stackName.data();
    if (nameLen > stackNameChars)
    {
        heapName.assign(nameLen, '\0');
        wModName = heapName.data();
    }

    if (FAILED(pModule->GetName(nameLen, nullptr, wModName)))
    {
        return {};
    }

    std::string moduleName = to_utf8(wModName);

    // On Tizen platform module path may look like /proc/self/fd/8/bin/Xamarin.Forms.Platform.dll
    // This path is invalid in debugger process, we should change `self` to `<debugee process id>`
    static constexpr std::string_view selfPrefix("/proc/self/");
    static constexpr std::string_view pidPrefix("/proc/");

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

    // A 32-bit value has at most 10 decimal digits.
    static constexpr std::size_t maxPidDigits = 10;

    // Reserve the worst-case size upfront so the path is built with a single allocation.
    std::string path;
    path.reserve(pidPrefix.size() + maxPidDigits + 1 + (moduleName.size() - selfPrefix.size()));
    path.append(pidPrefix.data(), pidPrefix.size());
    AppendInteger(path, pid);
    path += '/';
    path.append(moduleName, selfPrefix.size(), std::string::npos);
    return path;
}

void LoadModuleMetadata(ICorDebugModule *pModule, Module &module)
{
    // Query ICorDebugModule2 once and reuse it for all JIT/JMC-related calls.
    ToRelease<ICorDebugModule2> trModule2;
    const bool haveModule2 = SUCCEEDED(pModule->QueryInterface(IID_ICorDebugModule2, reinterpret_cast<void **>(&trModule2)));

    if (module.symbolStatus == SymbolStatus::Loaded)
    {
        if (haveModule2)
        {
            // Try to disable optimization for all modules with debug info.
            trModule2->SetJITCompilerFlags(CORDEBUG_JIT_DISABLE_OPTIMIZATION);

            HRESULT Status = S_OK;
            // Note, JMC status should be set regardless of the JustMyCode setting value.
            if (SUCCEEDED(Status = trModule2->SetJMCStatus(TRUE, 0, nullptr))) // If we can't enable JMC for module, there is no reason to
                                                                               // disable JMC on module's types/methods.
            {
                module.isUserCode = true;

                // Note, we use JMC in runtime all the time (same behavior as MS vsdbg and MSVS debugger have),
                // since this is the only way provide good speed for stepping in case "JMC disabled".
                // But in case "JMC disabled", debugger must care about different logic for exceptions/stepping/breakpoints.

                // https://docs.microsoft.com/en-us/visualstudio/debugger/just-my-code
                // The .NET debugger considers optimized binaries and non-loaded .pdb files to be non-user code.
                // Three compiler attributes also affect what the .NET debugger considers to be user code:
                // * DebuggerNonUserCodeAttribute tells the debugger that the code it's applied to isn't user code.
                // * DebuggerHiddenAttribute hides the code from the debugger, even if Just My Code is turned off.
                // * DebuggerStepThroughAttribute tells the debugger to step through the code it's applied to, rather
                // than step into the code. The .NET debugger considers all other code to be user code.
                if (Config::GetJustMyCode())
                {
                    DisableJMCByAttributes(pModule);
                }
            }
            else if (Status == CORDBG_E_CANT_SET_TO_JMC)
            {
                DAP::EmitOutputEvent({OutputCategory::StdErr,
                    "You are debugging a Release build of " + module.name + ". Disabling JIT "
                    "optimizations failed, in some cases (e.g. attach) this results in a "
                    "degraded debugging experience (e.g. breakpoints will not be hit).\n"});
            }
        }
    }
    else if (Config::GetSuppressJITOptimizations())
    {
        if (haveModule2)
        {
            // Try to disable optimization for all modules.
            trModule2->SetJITCompilerFlags(CORDEBUG_JIT_DISABLE_OPTIMIZATION);
        }
    }

    DWORD dwFlags = 0;
    if (haveModule2 && SUCCEEDED(trModule2->GetJITCompilerFlags(&dwFlags)))
    {
        module.isOptimized = (dwFlags & static_cast<DWORD>(CORDEBUG_JIT_DISABLE_OPTIMIZATION)) == 0U;
    }

    if (FAILED(GetModuleMvid(pModule, module.id)))
    {
        DAP::EmitOutputEvent({OutputCategory::StdErr,
            "Could not calculate module ID for module " + module.name + ".\n"});
    }

    CORDB_ADDRESS moduleBaseAddress = 0;
    uint32_t moduleSize = 0;
    if (SUCCEEDED(pModule->GetBaseAddress(&moduleBaseAddress)) &&
        SUCCEEDED(pModule->GetSize(&moduleSize)))
    {
        // "0x" + CORDB_ADDRESS (16 hex digits) + "-" + "0x" + CORDB_ADDRESS (16 hex digits).
        // AddrToString already includes the "0x" prefix, so the total length is 37 characters.
        static constexpr std::size_t addressRangeLength = 37;
        module.addressRange.clear();
        module.addressRange.reserve(addressRangeLength);
        module.addressRange += MetadataHelpers::AddrToString(moduleBaseAddress);
        module.addressRange += '-';
        module.addressRange += MetadataHelpers::AddrToString(moduleBaseAddress + moduleSize);
    }
    else
    {
        DAP::EmitOutputEvent({OutputCategory::StdErr, "Could not calculate module address range.\n"});
    }
}

Module &GetNewModuleRef()
{
    const std::scoped_lock<std::mutex> lock(GetModuleMutex());

    auto &moduleList = GetModuleList();
    moduleList.emplace_back();
    return moduleList.back();
}

HRESULT RemoveModule(ICorDebugModule *pModule, Module &removedModule)
{
    HRESULT Status = S_OK;
    std::string id;
    IfFailRet(GetModuleMvid(pModule, id));

    const std::scoped_lock<std::mutex> lock(GetModuleMutex());

    auto &moduleList = GetModuleList();
    const auto it = std::find_if(moduleList.begin(), moduleList.end(),
                                 [&id](const Module &candidate) { return candidate.id == id; });
    if (it == moduleList.end())
    {
        return E_INVALIDARG;
    }

    removedModule = std::move(*it);
    moduleList.erase(it);
    return S_OK;
}

void GetModules(int startModule, int moduleCount, std::vector<Module> &modules, size_t &totalModules)
{
    const std::scoped_lock<std::mutex> lock(GetModuleMutex());

    const auto &moduleList = GetModuleList();
    totalModules = moduleList.size();

    assert(moduleList.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()));
    if (startModule < 0 || startModule >= static_cast<int>(moduleList.size()))
    {
        return;
    }

    // moduleCount <= 0 means "all modules starting from startModule".
    // The range is computed in size_t arithmetic, so that a large moduleCount cannot overflow int.
    const std::size_t remaining = moduleList.size() - static_cast<std::size_t>(startModule);
    const std::size_t count = (moduleCount > 0) ? std::min(remaining, static_cast<std::size_t>(moduleCount)) : remaining;

    const auto startIt = std::next(moduleList.cbegin(), startModule);
    const auto endIt = std::next(startIt, static_cast<std::ptrdiff_t>(count));

    modules.reserve(modules.size() + count);
    for (auto it = startIt; it != endIt; ++it)
    {
        modules.emplace_back(*it);
    }
}

HRESULT ForEachModuleImpl(ICorDebugThread *pThread, ModuleCallback cb)
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

    ICorDebugAssembly *pCurAssembly = nullptr;
    ULONG assemblyFetched = 0;
    while (SUCCEEDED(trAssemblyEnum->Next(1, &pCurAssembly, &assemblyFetched)) && assemblyFetched == 1)
    {
        ToRelease<ICorDebugAssembly> trAssembly(pCurAssembly);
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

HRESULT GetModuleWithName(ICorDebugThread *pThread, const std::string &moduleFileName, ICorDebugModule **ppModule)
{
    HRESULT Status = S_OK;
    *ppModule = nullptr;

    IfFailRet(ForEachModule(pThread,
        [&](ICorDebugModule *pModule) -> HRESULT
        {
            const std::string path = GetModuleFilePath(pModule);
            const std::size_t separatorPos = path.find_last_of("/\\");
            const std::string_view fileName = (separatorPos == std::string::npos) ?
                std::string_view(path) : std::string_view(path).substr(separatorPos + 1);

            if (fileName == std::string_view(moduleFileName))
            {
                pModule->AddRef();
                *ppModule = pModule;
                return S_CAN_EXIT; // Fast exit from the loop.
            }

            return S_OK; // Return S_OK to continue iteration.
        }));

    return *ppModule != nullptr ? S_OK : E_FAIL;
}

// Cleans up the Modules internal state. See CleanupDebugSession() in manageddebugger.cpp.
void Cleanup()
{
    const std::scoped_lock<std::mutex> lock(GetModuleMutex());
    GetModuleList().clear();
}

} // namespace dncdbg::Modules
