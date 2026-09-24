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
#include <functional>
#include <string>
#include <vector>

namespace dncdbg::Modules
{

HRESULT GetModuleMvid(ICorDebugModule *pModule, std::string &strMvid);
std::string GetModuleFilePath(ICorDebugModule *pModule);
void LoadModuleMetadata(ICorDebugModule *pModule, Module &module);

Module &GetNewModuleRef();
HRESULT RemoveModule(ICorDebugModule *pModule, Module &removedModule);
void GetModules(int startModule, int moduleCount, std::vector<Module> &modules, size_t &totalModules);

HRESULT ForEachModule(ICorDebugThread *pThread, const std::function<HRESULT(ICorDebugModule *pModule)> &cb);
HRESULT GetModuleWithName(ICorDebugThread *pThread, const std::string &moduleFileName, ICorDebugModule **ppModule);

// Cleans up the Modules internal state. See Cleanup() in manageddebugger.cpp.
void Cleanup();

} // namespace dncdbg::Modules

#endif // METADATA_MODULES_H
