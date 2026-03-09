// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "types/protocol.h"
#include <list>
#include <string>

namespace dncdbg
{

class Modules
{
  public:

    static HRESULT GetModuleId(ICorDebugModule *pModule, std::string &id);
    static std::string GetModuleFileName(ICorDebugModule *pModule);

    // Note, methods below must be called from ManagedCallback only,
    // since we don't provide any synchronization for m_moduleList.
    Module &GetNewModuleRef();
    HRESULT RemoveModule(ICorDebugModule *pModule, Module &removedModule);

  private:

    std::list<Module> m_moduleList;
};

} // namespace dncdbg
