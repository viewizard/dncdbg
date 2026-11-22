// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef METADATA_MODULES_H
#define METADATA_MODULES_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/protocol.h"
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace dncdbg::Modules
{

// A non-owning reference to a callable. Unlike std::function, it never allocates.
// The callable must outlive the ModuleCallback that refers to it.
class ModuleCallback
{
  public:
    template <typename F, typename = std::enable_if_t<!std::is_same_v<F, ModuleCallback> &&
                                                      std::is_invocable_r_v<HRESULT, const F &, ICorDebugModule *>>>
    explicit ModuleCallback(const F &callable) noexcept
        : m_target(std::addressof(callable)),
          m_invoker(&Invoke<F>)
    {
    }

    HRESULT operator()(ICorDebugModule *pModule) const
    {
        return m_invoker(m_target, pModule);
    }

  private:
    using Invoker = HRESULT (*)(const void *, ICorDebugModule *);

    template <typename F>
    static HRESULT Invoke(const void *target, ICorDebugModule *pModule)
    {
        return (*static_cast<const F *>(target))(pModule);
    }

    const void *m_target;
    Invoker m_invoker;
};

HRESULT GetModuleMvid(ICorDebugModule *pModule, std::string &strMvid);
std::string GetModuleFilePath(ICorDebugModule *pModule);
void LoadModuleMetadata(ICorDebugModule *pModule, Module &module);

Module &GetNewModuleRef();
HRESULT RemoveModule(ICorDebugModule *pModule, Module &removedModule);
void GetModules(int startModule, int moduleCount, std::vector<Module> &modules, size_t &totalModules);

HRESULT ForEachModuleImpl(ICorDebugThread *pThread, ModuleCallback cb);

// Calls `cb` for each module in the thread's application domain. The walk stops when `cb` returns
// a failure code, which is returned to the caller, or S_CAN_EXIT, which stops the walk and returns S_OK.
template <typename F>
HRESULT ForEachModule(ICorDebugThread *pThread, const F &cb)
{
    return ForEachModuleImpl(pThread, ModuleCallback(cb));
}

HRESULT GetModuleWithName(ICorDebugThread *pThread, const std::string &moduleFileName, ICorDebugModule **ppModule);

// Cleans up the Modules internal state. See CleanupDebugSession() in manageddebugger.cpp.
void Cleanup();

} // namespace dncdbg::Modules

#endif // METADATA_MODULES_H
