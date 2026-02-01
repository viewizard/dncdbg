// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>

#include "interfaces/types.h"
#include "metadata/modules_sources.h"
#include "utils/string_view.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace dncdbg
{

HRESULT GetModuleId(ICorDebugModule *pModule, std::string &id);
std::string GetModuleFileName(ICorDebugModule *pModule);
HRESULT IsModuleHaveSameName(ICorDebugModule *pModule, const std::string &Name, bool isFullPath);

struct ModuleInfo
{
    PVOID m_symbolReaderHandle = nullptr;
    ToRelease<ICorDebugModule> m_iCorModule;

    ModuleInfo(PVOID Handle, ICorDebugModule *Module)
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

    struct SequencePoint
    {
        int32_t startLine;
        int32_t startColumn;
        int32_t endLine;
        int32_t endColumn;
        int32_t offset;
        std::string document;
    };

    HRESULT ResolveBreakpoint(
        /*in*/ CORDB_ADDRESS modAddress,
        /*in*/ std::string filename,
        /*out*/ unsigned &fullname_index,
        /*in*/ int sourceLine,
        /*out*/ std::vector<ModulesSources::resolved_bp_t> &resolvedPoints);

    HRESULT GetSourceFullPathByIndex(unsigned index, std::string &fullPath);
    HRESULT GetIndexBySourceFullPath(std::string fullPath, unsigned &index);

    HRESULT GetModuleWithName(const std::string &name, ICorDebugModule **ppModule);

    typedef std::function<HRESULT(ModuleInfo &)> ModuleInfoCallback;
    HRESULT GetModuleInfo(CORDB_ADDRESS modAddress, ModuleInfoCallback cb);
    HRESULT GetModuleInfo(CORDB_ADDRESS modAddress, ModuleInfo **ppmdInfo);

    HRESULT GetFrameILAndSequencePoint(ICorDebugFrame *pFrame, ULONG32 &ilOffset, SequencePoint &sequencePoint);

    HRESULT GetFrameILAndNextUserCodeILOffset(ICorDebugFrame *pFrame, ULONG32 &ilOffset, ULONG32 &ilNextOffset,
                                              bool *noUserCodeFound);

    HRESULT ResolveFuncBreakpointInAny(const std::string &module, bool &module_checked, const std::string &funcname,
                                       ResolveFuncBreakpointCallback cb);

    HRESULT ResolveFuncBreakpointInModule(ICorDebugModule *pModule, const std::string &module, bool &module_checked,
                                          std::string &funcname, ResolveFuncBreakpointCallback cb);

    HRESULT GetStepRangeFromCurrentIP(ICorDebugThread *pThread, COR_DEBUG_STEP_RANGE *range);

    HRESULT TryLoadModuleSymbols(ICorDebugModule *pModule, Module &module, bool needJMC, std::string &outputText);

    void CleanupAllModules();

    HRESULT GetFrameNamedLocalVariable(ICorDebugModule *pModule, mdMethodDef methodToken, ULONG localIndex,
                                       WSTRING &localName, ULONG32 *pIlStart, ULONG32 *pIlEnd);

    HRESULT GetHoistedLocalScopes(ICorDebugModule *pModule, mdMethodDef methodToken, PVOID *data,
                                  int32_t &hoistedLocalScopesCount);

    HRESULT GetNextUserCodeILOffsetInMethod(ICorDebugModule *pModule, mdMethodDef methodToken, ULONG32 ilOffset,
                                            ULONG32 &ilNextOffset, bool *noUserCodeFound = nullptr);

    HRESULT GetSequencePointByILOffset(CORDB_ADDRESS modAddress, mdMethodDef methodToken, ULONG32 ilOffset,
                                       SequencePoint &sequencePoint);

    HRESULT ForEachModule(std::function<HRESULT(ICorDebugModule *pModule)> cb);

  private:

    std::mutex m_modulesInfoMutex;
    std::unordered_map<CORDB_ADDRESS, ModuleInfo> m_modulesInfo;

    // Note, m_modulesSources have its own mutex for private data state sync.
    ModulesSources m_modulesSources;

    HRESULT GetSequencePointByILOffset(PVOID pSymbolReaderHandle, mdMethodDef methodToken, ULONG32 ilOffset,
                                       SequencePoint *sequencePoint);
};

} // namespace dncdbg
