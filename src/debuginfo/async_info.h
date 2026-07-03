// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_ASYNC_INFO_H
#define DEBUGINFO_ASYNC_INFO_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debuginfo/pdb.h"
#include <memory>
#include <mutex>
#include <vector>

namespace dncdbg
{

class DebugInfo;

class AsyncInfo
{
  public:

    explicit AsyncInfo(std::shared_ptr<DebugInfo> &sharedDebugInfo)
        : m_sharedDebugInfo(sharedDebugInfo)
    {
    }

    bool IsMethodHaveAwait(CORDB_ADDRESS modAddress, mdMethodDef methodToken);
    bool FindNextAwaitInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t ipOffset, PDB::AsyncAwaitInfoBlock &awaitInfo);
    bool FindLastIlOffsetAwaitInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t &lastIlOffset);

  private:

    std::shared_ptr<DebugInfo> m_sharedDebugInfo;

    struct AsyncMethodInfo
    {
        CORDB_ADDRESS modAddress{0};
        mdMethodDef methodToken{mdMethodDefNil};
        HRESULT retCode{S_OK};

        std::vector<PDB::AsyncAwaitInfoBlock> awaits;
        // Part of NotifyDebuggerOfWaitCompletion magic, see ManagedDebugger::SetupAsyncStep().
        uint32_t lastIlOffset{0};
    };

    AsyncMethodInfo asyncMethodSteppingInfo;
    std::mutex m_asyncMethodSteppingInfoMutex;
    // Note, result stored into asyncMethodSteppingInfo.
    HRESULT GetAsyncMethodSteppingInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken);
};

} // namespace dncdbg

#endif // DEBUGINFO_ASYNC_INFO_H
