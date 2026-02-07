// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "interfaces/types.h"
#include <memory>
#include <mutex>

namespace dncdbg
{

class Modules;

class BreakBreakpoint
{
  public:

    BreakBreakpoint(std::shared_ptr<Modules> &sharedModules)
        : m_sharedModules(sharedModules)
    {
    }

    void SetLastStoppedIlOffset(ICorDebugProcess *pProcess, const ThreadId &lastStoppedThreadId);

    // Important! Callbacks related methods must control return for succeeded return code.
    // Do not allow debugger API return succeeded (uncontrolled) return code.
    // Bad :
    //     return pThread->GetID(&threadId);
    // Good:
    //     IfFailRet(pThread->GetID(&threadId));
    //     return S_OK;
    HRESULT ManagedCallbackBreak(ICorDebugThread *pThread, const ThreadId &lastStoppedThreadId);

  private:

    std::mutex m_breakMutex;
    std::shared_ptr<Modules> m_sharedModules;

    struct FullyQualifiedIlOffset_t
    {
        CORDB_ADDRESS modAddress = 0;
        mdMethodDef methodToken = mdMethodDefNil;
        ULONG32 ilOffset = 0;

        void Reset()
        {
            modAddress = 0;
            methodToken = 0;
            ilOffset = 0;
        }
    };

    FullyQualifiedIlOffset_t m_lastStoppedIlOffset;

    HRESULT GetFullyQualifiedIlOffset(ICorDebugThread *pThread, FullyQualifiedIlOffset_t &fullyQualifiedIlOffset);
};

} // namespace dncdbg
