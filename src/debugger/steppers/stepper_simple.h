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

#include "types/types.h"
#include <mutex>
#include <memory>

namespace dncdbg
{

class DebugInfo;

class SimpleStepper
{
  public:

    SimpleStepper(std::shared_ptr<DebugInfo> &sharedDebugInfo)
        : m_sharedDebugInfo(sharedDebugInfo),
          m_justMyCode(true),
          m_enabledSimpleStepId(0)
    {}

    HRESULT SetupStep(ICorDebugThread *pThread, StepType stepType);

    // Important! Callbacks related methods must control return for succeeded return code.
    // Do not allow debugger API return succeeded (uncontrolled) return code.
    // Bad :
    //     return pThread->GetID(&threadId);
    // Good:
    //     IfFailRet(pThread->GetID(&threadId));
    //     return S_OK;
    HRESULT ManagedCallbackBreakpoint(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread);
    HRESULT ManagedCallbackStepComplete();

    HRESULT DisableAllSteppers(ICorDebugProcess *pProcess);

    void SetJustMyCode(bool enable)
    {
        m_justMyCode = enable;
    }

  private:

    std::shared_ptr<DebugInfo> m_sharedDebugInfo;
    bool m_justMyCode;

    std::mutex m_stepMutex;
    int m_enabledSimpleStepId;
};

} // namespace dncdbg
