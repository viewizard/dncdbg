// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/modules.h"
#include "metadata/jmc.h"
#include "utils/filesystem.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <array>
#include <iomanip>
#include <sstream>

namespace dncdbg
{

HRESULT Modules::GetModuleId(ICorDebugModule *pModule, std::string &id)
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

std::string Modules::GetModuleFileName(ICorDebugModule *pModule)
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

void Modules::LoadModuleMetadata(ICorDebugModule *pModule, Module &module, bool needJMC, std::string &errorText)
{
    module.path = Modules::GetModuleFileName(pModule);
    module.name = GetFileName(module.path);

    if (module.symbolStatus == SymbolStatus::Loaded)
    {
        ToRelease<ICorDebugModule2> trModule2;
        if (SUCCEEDED(pModule->QueryInterface(IID_ICorDebugModule2, reinterpret_cast<void **>(&trModule2))))
        {
            if (!needJMC)
            {
                trModule2->SetJITCompilerFlags(CORDEBUG_JIT_DISABLE_OPTIMIZATION);
            }

            HRESULT Status = S_OK;
            // Note, JMC status should be set for any needJMC value.
            if (SUCCEEDED(Status = trModule2->SetJMCStatus(TRUE, 0, nullptr))) // If we can't enable JMC for module, no reason
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
                if (needJMC)
                {
                    errorText += "You are debugging a Release build of " + module.name +
                                 ". Using Just My Code with Release builds using compiler optimizations results in a "
                                 "degraded debugging experience (e.g. breakpoints will not be hit).";
                }
                else
                {
                    errorText += "You are debugging a Release build of " + module.name +
                                 ". Without Just My Code Release builds try not to use compiler optimizations, but in "
                                 "some cases (e.g. attach) this still results in a degraded debugging experience (e.g. "
                                 "breakpoints will not be hit).";
                }
            }
        }
    }

    ToRelease<ICorDebugModule2> trModule2;
    DWORD dwFlags = 0;
    if (SUCCEEDED(pModule->QueryInterface(IID_ICorDebugModule2, reinterpret_cast<void **>(&trModule2))) &&
        SUCCEEDED(trModule2->GetJITCompilerFlags(&dwFlags)))
    {
        module.isOptimized = (dwFlags & 2UL) == 0;
    }

    if (FAILED(Modules::GetModuleId(pModule, module.id)))
    {
        errorText += "Could not calculate module ID for module" + module.name + ".";
    }
}

Module &Modules::GetNewModuleRef()
{
    m_moduleList.emplace_back();
    return m_moduleList.back();
}

HRESULT Modules::RemoveModule(ICorDebugModule *pModule, Module &removedModule)
{
    HRESULT Status = S_OK;
    std::string id;
    IfFailRet(GetModuleId(pModule, id));

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

} // namespace dncdbg
