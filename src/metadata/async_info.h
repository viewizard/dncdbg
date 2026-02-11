// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include <memory>
#include <mutex>
#include <vector>

namespace dncdbg
{

class Modules;

class AsyncInfo
{
  public:

    AsyncInfo(std::shared_ptr<Modules> &sharedModules) : m_sharedModules(sharedModules)
    {
    }

    struct AwaitInfo
    {
        uint32_t yield_offset;
        uint32_t resume_offset;

        AwaitInfo()
          : yield_offset(0),
            resume_offset(0)
        {
        };
        AwaitInfo(uint32_t offset1, uint32_t offset2)
          : yield_offset(offset1),
            resume_offset(offset2)
        {
        };
    };

    bool IsMethodHaveAwait(CORDB_ADDRESS modAddress, mdMethodDef methodToken);
    bool FindNextAwaitInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t ipOffset, AwaitInfo **awaitInfo);
    bool FindLastIlOffsetAwaitInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t &lastIlOffset);

  private:

    std::shared_ptr<Modules> m_sharedModules;

    struct AsyncMethodInfo
    {
        CORDB_ADDRESS modAddress = 0;
        mdMethodDef methodToken = mdMethodDefNil;
        HRESULT retCode = S_OK;

        std::vector<AwaitInfo> awaits;
        // Part of NotifyDebuggerOfWaitCompletion magic, see ManagedDebugger::SetupAsyncStep().
        uint32_t lastIlOffset = 0;

        AsyncMethodInfo()
          : modAddress(0),
            methodToken(mdMethodDefNil),
            retCode(S_OK),
            awaits(),
            lastIlOffset(0)
        {
        };
    };

    AsyncMethodInfo asyncMethodSteppingInfo;
    std::mutex m_asyncMethodSteppingInfoMutex;
    // Note, result stored into asyncMethodSteppingInfo.
    HRESULT GetAsyncMethodSteppingInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken);
};

} // namespace dncdbg
