// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/modules.h"
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

Module &Modules::GetNewModuleRef()
{
    m_moduleList.emplace_back();
    return m_moduleList.back();
}

} // namespace dncdbg
