// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_STEPPERS_STEPPER_ASYNC_H
#define DEBUGGER_STEPPERS_STEPPER_ASYNC_H

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "debuginfo/async_info.h"
#include "types/types.h"
#include "utils/torelease.h"
#include <mutex>

namespace dncdbg
{

class AsyncInfo;
class EvalHelpers;
class SimpleStepper;

class AsyncStepper
{
  public:

    AsyncStepper(std::shared_ptr<SimpleStepper> &simpleStepper, std::shared_ptr<DebugInfo> &sharedDebugInfo, std::shared_ptr<EvalHelpers> &sharedEvalHelpers)
        : m_simpleStepper(simpleStepper),
          m_uniqueAsyncInfo(new AsyncInfo(sharedDebugInfo)),
          m_sharedEvalHelpers(sharedEvalHelpers),
          m_asyncStep(nullptr),
          m_asyncStepNotifyDebuggerOfWaitCompletion(nullptr)
    {}

    HRESULT SetupStep(ICorDebugThread *pThread, StepType stepType);

    // Important! Callbacks related methods must control return for succeeded return code.
    // Do not allow debugger API return succeeded (uncontrolled) return code.
    // Bad :
    //     return pThread->GetID(&threadId);
    // Good:
    //     IfFailRet(pThread->GetID(&threadId));
    //     return S_OK;
    HRESULT ManagedCallbackBreakpoint(ICorDebugThread *pThread);
    HRESULT ManagedCallbackStepComplete();

    HRESULT DisableAllSteppers();

  private:

    std::shared_ptr<SimpleStepper> m_simpleStepper;
    std::unique_ptr<AsyncInfo> m_uniqueAsyncInfo;
    std::shared_ptr<EvalHelpers> m_sharedEvalHelpers;

    enum class asyncStepStatus : uint8_t
    {
        yield_offset_breakpoint,
        resume_offset_breakpoint
    };

    struct asyncBreakpoint_t
    {
        ToRelease<ICorDebugFunctionBreakpoint> trFuncBreakpoint;
        CORDB_ADDRESS modAddress{0};
        mdMethodDef methodToken{mdMethodDefNil};
        uint32_t ilOffset{0};

        asyncBreakpoint_t() = default;
        asyncBreakpoint_t(asyncBreakpoint_t &&) = delete;
        asyncBreakpoint_t(const asyncBreakpoint_t &) = delete;
        asyncBreakpoint_t &operator=(asyncBreakpoint_t &&) = delete;
        asyncBreakpoint_t &operator=(const asyncBreakpoint_t &) = delete;

        ~asyncBreakpoint_t()
        {
            if (trFuncBreakpoint != nullptr)
            {
                trFuncBreakpoint->Activate(FALSE);
            }
        }
    };

    struct asyncStep_t
    {
        ThreadId m_threadId{ThreadId::Invalid};
        StepType m_initialStepType{StepType::STEP_OVER};
        uint32_t m_resume_offset{0};
        asyncStepStatus m_stepStatus{asyncStepStatus::yield_offset_breakpoint};
        std::unique_ptr<asyncBreakpoint_t> m_Breakpoint;
        ToRelease<ICorDebugHandleValue> m_trHandleValueAsyncId;
    };

    std::mutex m_asyncStepMutex;
    // Pointer to object, that provide all active async step related data. Object will be created only in case of active async method stepping.
    std::unique_ptr<asyncStep_t> m_asyncStep;
    // System.Threading.Tasks.Task.NotifyDebuggerOfWaitCompletion() method function breakpoint data, will be configured at async method step-out setup.
    std::unique_ptr<asyncBreakpoint_t> m_asyncStepNotifyDebuggerOfWaitCompletion;

    HRESULT SetBreakpointIntoNotifyDebuggerOfWaitCompletion();
};

} // namespace dncdbg

#endif // DEBUGGER_STEPPERS_STEPPER_ASYNC_H
