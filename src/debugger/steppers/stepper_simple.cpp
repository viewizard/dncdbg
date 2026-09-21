// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/steppers/stepper_simple.h"
#include "debugger/threads.h"
#include "debuginfo/debuginfo.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <mutex>

namespace dncdbg::SimpleStepper
{

namespace
{

bool &GetJustMyCode()
{
    static bool justMyCode{true};
    return justMyCode;
}

std::mutex &GetStepMutex()
{
    static std::mutex stepMutex;
    return stepMutex;
}

int &GetEnabledStepId()
{
    static int enabledStepId{0};
    return enabledStepId;
}

} // unnamed namespace

HRESULT SetupStep(ICorDebugThread *pThread, StepType stepType)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugStepper> trStepper;
    IfFailRet(pThread->CreateStepper(&trStepper));

    constexpr auto mask = static_cast<CorDebugIntercept>(INTERCEPT_ALL & ~(INTERCEPT_SECURITY | INTERCEPT_CLASS_INIT)); // NOLINT(bugprone-signed-bitwise, clang-analyzer-optin.core.EnumCastOutOfRange)
    IfFailRet(trStepper->SetInterceptMask(mask));

    const CorDebugUnmappedStop stopMask = STOP_NONE;
    IfFailRet(trStepper->SetUnmappedStopMask(stopMask));

    ToRelease<ICorDebugStepper2> trStepper2;
    IfFailRet(trStepper->QueryInterface(IID_ICorDebugStepper2, reinterpret_cast<void **>(&trStepper2)));

    // Note, we use JMC in runtime all the time (same behavior as MS vsdbg and MSVS debugger have),
    // since this is the only way to provide good speed for stepping in case "JMC disabled".
    // But in case "JMC disabled", debugger must handle different logic for exceptions/stepping/breakpoints.
    IfFailRet(trStepper2->SetJMC(TRUE));

    const ThreadId threadId(GetThreadId(pThread));

    if (stepType == StepType::STEP_OUT)
    {
        IfFailRet(trStepper->StepOut());

        const std::scoped_lock<std::mutex> lock(GetStepMutex());
        GetEnabledStepId() = static_cast<int>(threadId);

        return S_OK;
    }

    const BOOL bStepIn = (stepType == StepType::STEP_IN) ? TRUE : FALSE;

    COR_DEBUG_STEP_RANGE range;
    if (SUCCEEDED(DebugInfo::GetStepRangeFromCurrentIP(pThread, range)))
    {
        IfFailRet(trStepper->StepRange(bStepIn, &range, 1));
    }
    else
    {
        IfFailRet(trStepper->Step(bStepIn));
    }

    const std::scoped_lock<std::mutex> lock(GetStepMutex());
    GetEnabledStepId() = static_cast<int>(threadId);

    return S_OK;
}

HRESULT ManagedCallbackBreakpoint(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread)
{
    const ThreadId threadId(GetThreadId(pThread));

    const auto stepForcedIgnoreBP =
        [&]() -> bool
        {
            {
                const std::scoped_lock<std::mutex> lock(GetStepMutex());
                if (GetEnabledStepId() != static_cast<int>(threadId))
                {
                    return false;
                }
            }

            ToRelease<ICorDebugStepperEnum> trStepperEnum;
            if (FAILED(pAppDomain->EnumerateSteppers(&trStepperEnum)))
            {
                return false;
            }

            ICorDebugStepper *pCurStepper = nullptr;
            ULONG steppersFetched = 0;
            while (SUCCEEDED(trStepperEnum->Next(1, &pCurStepper, &steppersFetched)) && steppersFetched == 1)
            {
                BOOL bActive = TRUE;
                ToRelease<ICorDebugStepper> trStepper(pCurStepper);
                if (SUCCEEDED(trStepper->IsActive(&bActive)) && bActive == TRUE)
                {
                    return false;
                }
            }

            return true;
        };

    return stepForcedIgnoreBP() ? S_IGNORE : S_OK;
}

HRESULT ManagedCallbackStepComplete()
{
    // Reset simple step without real stepper release.
    const std::scoped_lock<std::mutex> lock(GetStepMutex());
    GetEnabledStepId() = 0;

    return S_OK;
}

HRESULT DisableAllSteppers(ICorDebugProcess *pProcess)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugAppDomainEnum> trAppDomainEnum;
    IfFailRet(pProcess->EnumerateAppDomains(&trAppDomainEnum));

    ICorDebugAppDomain *pCurDomain = nullptr;
    ULONG domainsFetched = 0;
    while (SUCCEEDED(trAppDomainEnum->Next(1, &pCurDomain, &domainsFetched)) && domainsFetched == 1)
    {
        ToRelease<ICorDebugAppDomain> trDomain(pCurDomain);
        ToRelease<ICorDebugStepperEnum> trStepperEnum;
        IfFailRet(trDomain->EnumerateSteppers(&trStepperEnum));

        ICorDebugStepper *pCurStepper = nullptr;
        ULONG steppersFetched = 0;
        while (SUCCEEDED(trStepperEnum->Next(1, &pCurStepper, &steppersFetched)) && steppersFetched == 1)
        {
            ToRelease<ICorDebugStepper> trStepper(pCurStepper);
            trStepper->Deactivate();
        }
    }

    const std::scoped_lock<std::mutex> lock(GetStepMutex());
    GetEnabledStepId() = 0;

    return S_OK;
}

void SetJustMyCode(bool enable)
{
    GetJustMyCode() = enable;
}

void Cleanup()
{
    // Don't reset the protocol-provided settings: JustMyCode.
    // Only the internal state related to process execution is reset here.

    const std::scoped_lock<std::mutex> lock(GetStepMutex());
    GetEnabledStepId() = 0;
}

} // namespace dncdbg::SimpleStepper
