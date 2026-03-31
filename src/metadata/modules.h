// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef METADATA_MODULES_H
#define METADATA_MODULES_H

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "types/protocol.h"
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <vector>

namespace dncdbg
{

class Modules
{
  public:

    static HRESULT GetModuleId(ICorDebugModule *pModule, std::string &id);
    static std::string GetModuleFileName(ICorDebugModule *pModule);
    static void LoadModuleMetadata(ICorDebugModule *pModule, Module &module, bool needJMC, std::string &errorText);

    Module &GetNewModuleRef();
    HRESULT RemoveModule(ICorDebugModule *pModule, Module &removedModule);
    void GetModules(int startModule, int moduleCount, std::vector<Module> &modules, size_t &totalModules);

    static HRESULT ForEachModule(ICorDebugThread *pThread, const std::function<HRESULT(ICorDebugModule *pModule)> &cb);
    static HRESULT GetModuleWithName(ICorDebugThread *pThread, const std::string &name, ICorDebugModule **ppModule);

  private:

    std::mutex m_moduleMutex;
    std::list<Module> m_moduleList;
};

} // namespace dncdbg

#endif // METADATA_MODULES_H
