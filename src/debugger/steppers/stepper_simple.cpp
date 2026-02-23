// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/steppers/stepper_simple.h"
#include "debugger/threads.h"
#include "debuginfo/debuginfo.h" // NOLINT(misc-include-cleaner)

namespace dncdbg
{

HRESULT SimpleStepper::SetupStep(ICorDebugThread *pThread, StepType stepType)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugStepper> trStepper;
    IfFailRet(pThread->CreateStepper(&trStepper));

    constexpr auto mask = static_cast<CorDebugIntercept>(INTERCEPT_ALL & ~(INTERCEPT_SECURITY | INTERCEPT_CLASS_INIT)); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    IfFailRet(trStepper->SetInterceptMask(mask));

    const CorDebugUnmappedStop stopMask = STOP_NONE;
    IfFailRet(trStepper->SetUnmappedStopMask(stopMask));

    ToRelease<ICorDebugStepper2> trStepper2;
    IfFailRet(trStepper->QueryInterface(IID_ICorDebugStepper2, reinterpret_cast<void **>(&trStepper2)));

    // Note, we use JMC in runtime all the time (same behaviour as MS vsdbg and MSVS debugger have),
    // since this is the only way provide good speed for stepping in case "JMC disabled".
    // But in case "JMC disabled", debugger must care about different logic for exceptions/stepping/breakpoints.
    IfFailRet(trStepper2->SetJMC(TRUE));

    const ThreadId threadId(getThreadId(pThread));

    if (stepType == StepType::STEP_OUT)
    {
        IfFailRet(trStepper->StepOut());

        const std::scoped_lock<std::mutex> lock(m_stepMutex);
        m_enabledSimpleStepId = static_cast<int>(threadId);

        return S_OK;
    }

    const BOOL bStepIn = (stepType == StepType::STEP_IN) ? TRUE : FALSE;

    COR_DEBUG_STEP_RANGE range;
    if (SUCCEEDED(m_sharedDebugInfo->GetStepRangeFromCurrentIP(pThread, &range)))
    {
        IfFailRet(trStepper->StepRange(bStepIn, &range, 1));
    }
    else
    {
        IfFailRet(trStepper->Step(bStepIn));
    }

    const std::scoped_lock<std::mutex> lock(m_stepMutex);
    m_enabledSimpleStepId = static_cast<int>(threadId);

    return S_OK;
}

HRESULT SimpleStepper::ManagedCallbackBreakpoint(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread)
{
    const ThreadId threadId(getThreadId(pThread));

    auto stepForcedIgnoreBP = [&]() {
        {
            const std::scoped_lock<std::mutex> lock(m_stepMutex);
            if (m_enabledSimpleStepId != static_cast<int>(threadId))
            {
                return false;
            }
        }

        ToRelease<ICorDebugStepperEnum> trStepperEnum;
        if (FAILED(pAppDomain->EnumerateSteppers(&trStepperEnum)))
        {
            return false;
        }

        ICorDebugStepper *curStepper = nullptr;
        ULONG steppersFetched = 0;
        while (SUCCEEDED(trStepperEnum->Next(1, &curStepper, &steppersFetched)) && steppersFetched == 1)
        {
            BOOL pbActive = TRUE;
            ToRelease<ICorDebugStepper> trStepper(curStepper);
            if (SUCCEEDED(trStepper->IsActive(&pbActive)) && pbActive)
            {
                return false;
            }
        }

        return true;
    };

    if (stepForcedIgnoreBP())
    {
        return S_OK;
    }

    return S_FALSE; // S_FALSE - no error, but steppers not affect on callback
}

HRESULT SimpleStepper::ManagedCallbackStepComplete()
{
    // Reset simple step without real stepper release.
    m_stepMutex.lock();
    m_enabledSimpleStepId = 0;
    m_stepMutex.unlock();

    return S_FALSE; // S_FALSE - no error, but steppers not affect on callback
}

HRESULT SimpleStepper::DisableAllSteppers(ICorDebugProcess *pProcess)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugAppDomainEnum> trAppDomainEnum;
    IfFailRet(pProcess->EnumerateAppDomains(&trAppDomainEnum));

    ICorDebugAppDomain *curDomain = nullptr;
    ULONG domainsFetched = 0;
    while (SUCCEEDED(trAppDomainEnum->Next(1, &curDomain, &domainsFetched)) && domainsFetched == 1)
    {
        ToRelease<ICorDebugAppDomain> trDomain(curDomain);
        ToRelease<ICorDebugStepperEnum> trStepperEnum;
        IfFailRet(trDomain->EnumerateSteppers(&trStepperEnum));

        ICorDebugStepper *curStepper = nullptr;
        ULONG steppersFetched = 0;
        while (SUCCEEDED(trStepperEnum->Next(1, &curStepper, &steppersFetched)) && steppersFetched == 1)
        {
            ToRelease<ICorDebugStepper> trStepper(curStepper);
            trStepper->Deactivate();
        }
    }

    m_stepMutex.lock();
    m_enabledSimpleStepId = 0;
    m_stepMutex.unlock();

    return S_OK;
}

} // namespace dncdbg
