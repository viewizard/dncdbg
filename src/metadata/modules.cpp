// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/modules.h"
#include "config/config.h"
#include "metadata/helpers.h"
#include "metadata/jmc.h"
#include "protocol/dap_events.h"
#include "utils/filesystem.h"
#include "utils/hresult.h"
#include "utils/print.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <cassert>
#include <cstring>
#include <iterator>
#include <limits>
#include <list>
#include <mutex>
#include <sstream>

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

    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));
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

    std::vector<WCHAR> wModName(nameLen, '\0');
    if (FAILED(pModule->GetName(nameLen, nullptr, wModName.data())))
    {
        return {};
    }

    std::string moduleName = to_utf8(wModName.data());

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

void LoadModuleMetadata(ICorDebugModule *pModule, Module &module)
{
    if (module.symbolStatus == SymbolStatus::Loaded)
    {
        ToRelease<ICorDebugModule2> trModule2;
        if (SUCCEEDED(pModule->QueryInterface(IID_ICorDebugModule2, reinterpret_cast<void **>(&trModule2))))
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
        module.addressRange = MetadataHelpers::AddrToString(moduleBaseAddress) + '-' +
                              MetadataHelpers::AddrToString(moduleBaseAddress + moduleSize);
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
    for (auto it = moduleList.begin(); it != moduleList.end();)
    {
        if (it->id == id)
        {
            removedModule = *it;
            moduleList.erase(it);
            return S_OK;
        }
        else
        {
            ++it;
        }
    }

    return E_INVALIDARG;
}

void GetModules(int startModule, int moduleCount, std::vector<Module> &modules, size_t &totalModules)
{
    const std::scoped_lock<std::mutex> lock(GetModuleMutex());

    const auto &moduleList = GetModuleList();
    totalModules = moduleList.size();

    assert(moduleList.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
    if (startModule >= static_cast<int>(moduleList.size()))
    {
        return;
    }

    const auto startIt = std::next(moduleList.cbegin(), startModule);
    auto endIt = moduleList.cend();
    if (moduleCount != 0 &&
        startModule + moduleCount < static_cast<int>(moduleList.size()))
    {
        endIt = std::next(startIt, moduleCount);
    }

    for (auto it = startIt; it != endIt; it = std::next(it))
    {
        modules.emplace_back(*it);
    }
}

HRESULT ForEachModule(ICorDebugThread *pThread, const std::function<HRESULT(ICorDebugModule *pModule)> &cb)
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

            if (GetFileName(path) == moduleFileName)
            {
                pModule->AddRef();
                *ppModule = pModule;
                return S_CAN_EXIT; // Fast exit from the loop.
            }

            return S_OK; // Return S_OK to continue iteration.
        }));

    return *ppModule != nullptr ? S_OK : E_FAIL;
}

// Cleans up the Modules internal state. See Cleanup() in manageddebugger.cpp.
void Cleanup()
{
    const std::scoped_lock<std::mutex> lock(GetModuleMutex());
    GetModuleList().clear();
}

} // namespace dncdbg::Modules
