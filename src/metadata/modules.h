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

#include "types/types.h"
#include "metadata/modules_sources.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <functional>
#include <mutex>
#include <unordered_map>

namespace dncdbg
{

HRESULT GetModuleId(ICorDebugModule *pModule, std::string &id);
std::string GetModuleFileName(ICorDebugModule *pModule);

struct ModuleInfo
{
    void *m_symbolReaderHandle = nullptr;
    ToRelease<ICorDebugModule> m_iCorModule;

    ModuleInfo(void *Handle, ICorDebugModule *Module)
      : m_symbolReaderHandle(Handle),
        m_iCorModule(Module)
    {
    }

    ModuleInfo(ModuleInfo &&other) noexcept
        : m_symbolReaderHandle(other.m_symbolReaderHandle),
          m_iCorModule(std::move(other.m_iCorModule))
    {
        other.m_symbolReaderHandle = nullptr;
    }
    ModuleInfo(const ModuleInfo &) = delete;
    ModuleInfo &operator=(ModuleInfo &&) = delete;
    ModuleInfo &operator=(const ModuleInfo &) = delete;
    ~ModuleInfo() noexcept;
};

class Modules
{
  public:

    HRESULT ResolveBreakpoint(
        /*in*/ CORDB_ADDRESS modAddress,
        /*in*/ const std::string &filename,
        /*out*/ unsigned &fullname_index,
        /*in*/ int sourceLine,
        /*out*/ std::vector<ModulesSources::resolved_bp_t> &resolvedPoints);

    HRESULT GetSourceFullPathByIndex(unsigned index, std::string &fullPath);
    HRESULT GetIndexBySourceFullPath(const std::string &fullPath, unsigned &index);

    HRESULT GetModuleWithName(const std::string &name, ICorDebugModule **ppModule);

    using ModuleInfoCallback = std::function<HRESULT(ModuleInfo &)>;
    HRESULT GetModuleInfo(CORDB_ADDRESS modAddress, const ModuleInfoCallback &cb);
    HRESULT GetModuleInfo(CORDB_ADDRESS modAddress, ModuleInfo **ppmdInfo);

    HRESULT GetFrameILAndSequencePoint(ICorDebugFrame *pFrame, uint32_t &ilOffset, SequencePoint &sequencePoint);

    HRESULT GetFrameILAndNextUserCodeILOffset(ICorDebugFrame *pFrame, uint32_t &ilOffset, uint32_t &ilNextOffset,
                                              bool *noUserCodeFound);

    HRESULT ResolveFuncBreakpointInAny(const std::string &funcname, const ResolveFuncBreakpointCallback &cb);

    static HRESULT ResolveFuncBreakpointInModule(ICorDebugModule *pModule, std::string &funcname,
                                                 const ResolveFuncBreakpointCallback &cb);

    HRESULT GetStepRangeFromCurrentIP(ICorDebugThread *pThread, COR_DEBUG_STEP_RANGE *range);

    HRESULT TryLoadModuleSymbols(ICorDebugModule *pModule, Module &module, bool needJMC, std::string &outputText);

    void CleanupAllModules();

    HRESULT GetFrameNamedLocalVariable(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t localIndex,
                                       WSTRING &localName, int32_t *pIlStart, int32_t *pIlEnd);

    HRESULT GetHoistedLocalScopes(ICorDebugModule *pModule, mdMethodDef methodToken, void **data,
                                  int32_t &hoistedLocalScopesCount);

    HRESULT GetNextUserCodeILOffsetInMethod(ICorDebugModule *pModule, mdMethodDef methodToken, uint32_t ilOffset,
                                            uint32_t &ilNextOffset, bool *noUserCodeFound = nullptr);

    HRESULT GetSequencePointByILOffset(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t ilOffset,
                                       SequencePoint &sequencePoint);

    HRESULT ForEachModule(const std::function<HRESULT(ICorDebugModule *pModule)> &cb);

  private:

    std::mutex m_modulesInfoMutex;
    std::unordered_map<CORDB_ADDRESS, ModuleInfo> m_modulesInfo;

    // Note, m_modulesSources have its own mutex for private data state sync.
    ModulesSources m_modulesSources;

    static HRESULT GetSequencePointByILOffset(void *pSymbolReaderHandle, mdMethodDef methodToken, uint32_t ilOffset,
                                              SequencePoint *sequencePoint);
};

} // namespace dncdbg
