// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/steppers/stepper_simple.h"
#include "debugger/threads.h"
#include "metadata/modules.h" // NOLINT(misc-include-cleaner)

namespace dncdbg
{

HRESULT SimpleStepper::SetupStep(ICorDebugThread *pThread, StepType stepType)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugStepper> pStepper;
    IfFailRet(pThread->CreateStepper(&pStepper));

    const auto mask = static_cast<CorDebugIntercept>(INTERCEPT_ALL & ~(INTERCEPT_SECURITY | INTERCEPT_CLASS_INIT)); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    IfFailRet(pStepper->SetInterceptMask(mask));

    const CorDebugUnmappedStop stopMask = STOP_NONE;
    IfFailRet(pStepper->SetUnmappedStopMask(stopMask));

    ToRelease<ICorDebugStepper2> pStepper2;
    IfFailRet(pStepper->QueryInterface(IID_ICorDebugStepper2, reinterpret_cast<void **>(&pStepper2)));

    // Note, we use JMC in runtime all the time (same behaviour as MS vsdbg and MSVS debugger have),
    // since this is the only way provide good speed for stepping in case "JMC disabled".
    // But in case "JMC disabled", debugger must care about different logic for exceptions/stepping/breakpoints.
    IfFailRet(pStepper2->SetJMC(TRUE));

    const ThreadId threadId(getThreadId(pThread));

    if (stepType == StepType::STEP_OUT)
    {
        IfFailRet(pStepper->StepOut());

        const std::scoped_lock<std::mutex> lock(m_stepMutex);
        m_enabledSimpleStepId = static_cast<int>(threadId);

        return S_OK;
    }

    const BOOL bStepIn = (stepType == StepType::STEP_IN) ? TRUE : FALSE;

    COR_DEBUG_STEP_RANGE range;
    if (SUCCEEDED(m_sharedModules->GetStepRangeFromCurrentIP(pThread, &range)))
    {
        IfFailRet(pStepper->StepRange(bStepIn, &range, 1));
    }
    else
    {
        IfFailRet(pStepper->Step(bStepIn));
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

        ToRelease<ICorDebugStepperEnum> steppers;
        if (FAILED(pAppDomain->EnumerateSteppers(&steppers)))
            return false;

        ICorDebugStepper *curStepper = nullptr;
        ULONG steppersFetched = 0;
        while (SUCCEEDED(steppers->Next(1, &curStepper, &steppersFetched)) && steppersFetched == 1)
        {
            BOOL pbActive = TRUE;
            ToRelease<ICorDebugStepper> pStepper(curStepper);
            if (SUCCEEDED(pStepper->IsActive(&pbActive)) && pbActive)
                return false;
        }

        return true;
    };

    if (stepForcedIgnoreBP())
        return S_OK;

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

    ToRelease<ICorDebugAppDomainEnum> domains;
    IfFailRet(pProcess->EnumerateAppDomains(&domains));

    ICorDebugAppDomain *curDomain = nullptr;
    ULONG domainsFetched = 0;
    while (SUCCEEDED(domains->Next(1, &curDomain, &domainsFetched)) && domainsFetched == 1)
    {
        ToRelease<ICorDebugAppDomain> pDomain(curDomain);
        ToRelease<ICorDebugStepperEnum> steppers;
        IfFailRet(pDomain->EnumerateSteppers(&steppers));

        ICorDebugStepper *curStepper = nullptr;
        ULONG steppersFetched = 0;
        while (SUCCEEDED(steppers->Next(1, &curStepper, &steppersFetched)) && steppersFetched == 1)
        {
            ToRelease<ICorDebugStepper> pStepper(curStepper);
            pStepper->Deactivate();
        }
    }

    m_stepMutex.lock();
    m_enabledSimpleStepId = 0;
    m_stepMutex.unlock();

    return S_OK;
}

} // namespace dncdbg
