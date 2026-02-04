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

#include "debugger/manageddebugger.h"
#include "metadata/async_info.h"
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

    AsyncStepper(std::shared_ptr<SimpleStepper> simpleStepper, std::shared_ptr<Modules> &sharedModules, std::shared_ptr<EvalHelpers> &sharedEvalHelpers)
        : m_simpleStepper(simpleStepper),
          m_uniqueAsyncInfo(new AsyncInfo(sharedModules)),
          m_sharedEvalHelpers(sharedEvalHelpers),
          m_asyncStep(nullptr),
          m_asyncStepNotifyDebuggerOfWaitCompletion(nullptr)
    {}

    HRESULT SetupStep(ICorDebugThread *pThread, ManagedDebugger::StepType stepType);

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

    enum class asyncStepStatus
    {
        yield_offset_breakpoint,
        resume_offset_breakpoint
    };

    struct asyncBreakpoint_t
    {
        ToRelease<ICorDebugFunctionBreakpoint> iCorFuncBreakpoint;
        CORDB_ADDRESS modAddress;
        mdMethodDef methodToken;
        ULONG32 ilOffset;

        asyncBreakpoint_t()
            : iCorFuncBreakpoint(nullptr),
              modAddress(0),
              methodToken(0),
              ilOffset(0)
        {}

        ~asyncBreakpoint_t()
        {
            if (iCorFuncBreakpoint)
                iCorFuncBreakpoint->Activate(FALSE);
        }
    };

    struct asyncStep_t
    {
        ThreadId m_threadId;
        ManagedDebugger::StepType m_initialStepType;
        uint32_t m_resume_offset;
        asyncStepStatus m_stepStatus;
        std::unique_ptr<asyncBreakpoint_t> m_Breakpoint;
        ToRelease<ICorDebugHandleValue> m_iCorHandleValueAsyncId;

        asyncStep_t()
            : m_threadId(ThreadId::Invalid),
              m_initialStepType(ManagedDebugger::StepType::STEP_OVER),
              m_resume_offset(0),
              m_stepStatus(asyncStepStatus::yield_offset_breakpoint),
              m_Breakpoint(nullptr),
              m_iCorHandleValueAsyncId(nullptr)
        {}
    };

    std::mutex m_asyncStepMutex;
    // Pointer to object, that provide all active async step related data. Object will be created only in case of active async method stepping.
    std::unique_ptr<asyncStep_t> m_asyncStep;
    // System.Threading.Tasks.Task.NotifyDebuggerOfWaitCompletion() method function breakpoint data, will be configured at async method step-out setup.
    std::unique_ptr<asyncBreakpoint_t> m_asyncStepNotifyDebuggerOfWaitCompletion;

    HRESULT SetBreakpointIntoNotifyDebuggerOfWaitCompletion();
};

} // namespace dncdbg
