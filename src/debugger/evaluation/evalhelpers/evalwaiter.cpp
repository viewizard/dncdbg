// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/evalhelpers/evalwaiter.h"
#include "utils/hresult.h"
#include "utils/logger.h"
#include "utils/torelease.h"
#include "utils/utf.h"
#include <cassert>
#include <chrono>
#include <future>
#include <iomanip>
#include <memory>
#include <mutex>
#include <utility>

namespace dncdbg::EvalWaiter
{

namespace
{

struct evalResultData_t
{
    ToRelease<ICorDebugValue> trEval;
    HRESULT Status = E_FAIL;
};

struct evalResult_t
{
    evalResult_t() = delete;
    evalResult_t(DWORD threadId_, ICorDebugEval *pEval_, const std::promise<std::unique_ptr<evalResultData_t>> &promiseValue_) = delete;
    evalResult_t(const evalResult_t &B) = delete;
    evalResult_t &operator=(const evalResult_t &B) = delete;
    evalResult_t &operator=(evalResult_t &&B) = delete;

    evalResult_t(DWORD threadId_,
                 ICorDebugEval *pEval_,
                 std::promise<std::unique_ptr<evalResultData_t>> &&promiseValue_)
        : threadId(threadId_),
          pEval(pEval_),
          promiseValue(std::move(promiseValue_))
    {
    }
    evalResult_t(evalResult_t &&B) noexcept
        : threadId(B.threadId),
          pEval(B.pEval),
          promiseValue(std::move(B.promiseValue))
    {
    }

    ~evalResult_t() = default;

    DWORD threadId;
    ICorDebugEval *pEval;
    std::promise<std::unique_ptr<evalResultData_t>> promiseValue;
};

bool &GetEvalCanceled()
{
    static bool evalCanceled{false};
    return evalCanceled;
}

bool &GetEvalCrossThreadDependency()
{
    static bool evalCrossThreadDependency{false};
    return evalCrossThreadDependency;
}

ToRelease<ICorDebugClass> &GetCrossThreadDependencyNotification()
{
    static ToRelease<ICorDebugClass> trCrossThreadDependencyNotification;
    return trCrossThreadDependencyNotification;
}

std::unique_ptr<evalResult_t> &GetEvalResult()
{
    static std::unique_ptr<evalResult_t> evalResult;
    return evalResult;
}

std::mutex &GetWaitEvalResultMutex()
{
    static std::mutex waitEvalResultMutex;
    return waitEvalResultMutex;
}

std::mutex &GetEvalResultMutex()
{
    static std::mutex evalResultMutex;
    return evalResultMutex;
}

ICorDebugEval *FindEvalForThread(ICorDebugThread *pThread)
{
    const std::scoped_lock<std::mutex> lock(GetEvalResultMutex());

    DWORD threadId = 0;
    if (FAILED(pThread->GetID(&threadId)) || !GetEvalResult())
    {
        return nullptr;
    }

    return GetEvalResult()->threadId == threadId ? GetEvalResult()->pEval : nullptr;
}

std::future<std::unique_ptr<evalResultData_t>> RunEval(HRESULT &Status,
                                                       ICorDebugProcess *pProcess,
                                                       ICorDebugThread *pThread,
                                                       ICorDebugEval *pEval,
                                                       const WaitEvalResultCallback &cbSetupEval)
{
    std::promise<std::unique_ptr<evalResultData_t>> p;
    auto f = p.get_future();
    if (!f.valid())
    {
        LOGE(log << "get_future() returns not valid promise object");
    }

    DWORD threadId = 0;
    pThread->GetID(&threadId);

    const std::scoped_lock<std::mutex> lock(GetEvalResultMutex());
    assert(!GetEvalResult()); // We can have only 1 eval, and the previous one must be completed.
    GetEvalResult() = std::make_unique<evalResult_t>(threadId, pEval, std::move(p));

    // We don't have an easy way to abort the eval setup in case of some error in the debugger API,
    // so try to set up the eval only if all is OK right before we run the process.
    if (FAILED(Status = cbSetupEval(pEval)))
    {
        LOGE(log << "Setup eval failed, 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
        GetEvalResult().reset(nullptr);
    }
    else if (FAILED(Status = pProcess->Continue(0)))
    {
        LOGE(log << "Continue() failed, 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
        GetEvalResult().reset(nullptr);
    }

    return f;
}

HRESULT SetEnableCustomNotification(ICorDebugProcess *pProcess, BOOL fEnable)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugProcess3> trProcess3;
    IfFailRet(pProcess->QueryInterface(IID_ICorDebugProcess3, reinterpret_cast<void **>(&trProcess3)));
    return trProcess3->SetEnableCustomNotification(GetCrossThreadDependencyNotification(), fEnable);
}

} // unnamed namespace

HRESULT WaitEvalResult(ICorDebugThread *pThread, ICorDebugValue **ppEvalResult, const WaitEvalResultCallback &cbSetupEval)
{
    // Important! Evaluation should be performed for 1 thread only.
    const std::scoped_lock<std::mutex> lock(GetWaitEvalResultMutex());

    // During evaluation, user code could be executed implicitly, which could trigger callbacks such as breakpoints, exceptions, etc.
    // Make sure all managed callbacks ignore the standard logic during evaluation and don't pause/interrupt managed code execution.

    HRESULT Status = S_OK;
    ToRelease<ICorDebugProcess> trProcess;
    IfFailRet(pThread->GetProcess(&trProcess));
    if (trProcess == nullptr)
    {
        return E_FAIL;
    }
    DWORD evalThreadId = 0;
    IfFailRet(pThread->GetID(&evalThreadId));

    // Note, we need to suspend all managed threads that are not used for eval during the eval (delegates, reverse pinvokes, managed threads).
    auto ChangeThreadsState = [&](CorDebugThreadState state)
    {
        ToRelease<ICorDebugThreadEnum> trThreadEnum;
        trProcess->EnumerateThreads(&trThreadEnum);
        ULONG fetched = 0;
        ToRelease<ICorDebugThread> trThread;
        while (SUCCEEDED(trThreadEnum->Next(1, &trThread, &fetched)) && fetched == 1)
        {
            DWORD tid = 0;
            if (SUCCEEDED(trThread->GetID(&tid)) &&
                evalThreadId != tid &&
                FAILED(trThread->SetDebugState(state)))
            {
                if (state == THREAD_SUSPEND)
                {
                    LOGW(log << "SetDebugState(THREAD_SUSPEND) during eval setup failed. "
                             << "This may change the state of the process and any breakpoints and exceptions encountered will be skipped.");
                }
                else
                {
                    LOGW(log << "SetDebugState(THREAD_RUN) during eval failed. Process state was not restored.");
                }
            }
            trThread.Free();
        }
    };

    bool evalTimeOut = false;
    const auto WaitResult = [&]() -> HRESULT
    {
        ChangeThreadsState(THREAD_SUSPEND);

        ToRelease<ICorDebugEval> trEval;
        IfFailRet(pThread->CreateEval(&trEval));

        try
        {
            auto f = RunEval(Status, trProcess, pThread, trEval, cbSetupEval);
            IfFailRet(Status);

            if (!f.valid())
            {
                return E_FAIL;
            }

            // Note:
            // MSVS 2017 debugger and newer use the config file
            // C:\Program Files (x86)\Microsoft Visual Studio\YYYY\VERSION\Common7\IDE\Profiles\CSharp.vssettings
            // by default NormalEvalTimeout is 5000 milliseconds
            //
            // TODO add timeout configuration feature (care about VS Code, MSVS with Tizen plugin, standalone usage)

            static constexpr uint32_t normalEvalTimeout = 5000; // TODO config
            std::future_status timeoutStatus = f.wait_for(std::chrono::milliseconds(normalEvalTimeout));
            if (timeoutStatus == std::future_status::timeout)
            {
                LOGW(log << "Evaluation timed out.");
                LOGW(log << "To prevent an unsafe abort when evaluating, all threads were allowed to run. "
                         << "This may have changed the state of the process and any breakpoints and exceptions encountered have been skipped.");

                // Note:
                // All CoreCLR releases up to at least version 3.1.3 don't have a proper x86 implementation of ICorDebugEval::Abort().
                // The issue is that CoreCLR terminates managed process execution instead of aborting the evaluation.

                // In this case we have the same behavior as MS vsdbg and the MSVS C# debugger - run all managed threads and try to abort the eval at any cost.
                // Ignore errors here, this is our last chance to prevent debugger hangs.
                trProcess->Stop(0);
                ChangeThreadsState(THREAD_RUN);

                if (FAILED(trEval->Abort()))
                {
                    ToRelease<ICorDebugEval2> trEval2;
                    if (SUCCEEDED(trEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2))))
                    {
                        trEval2->RudeAbort();
                    }
                }

                evalTimeOut = true;
                trProcess->Continue(0);
            }
            // Wait for 5 more seconds, give `Abort()` a chance.
            static constexpr uint32_t abortEvalTimeout = 5000; // TODO config
            timeoutStatus = f.wait_for(std::chrono::milliseconds(abortEvalTimeout));
            if (timeoutStatus == std::future_status::timeout)
            {
                // Looks like it can't be aborted; this is a fatal error for the debugger (the debuggee has an inconsistent state now).
                trProcess->Stop(0);
                GetEvalResultMutex().lock();
                GetEvalResult().reset(nullptr);
                GetEvalResultMutex().unlock();
                LOGE(log << "Fatal error, eval abort failed.");
                return E_UNEXPECTED;
            }

            auto evalResult = f.get();
            IfFailRet(evalResult->Status);

            if (ppEvalResult == nullptr)
            {
                return S_OK;
            }

            *ppEvalResult = evalResult->trEval.Detach();
            return evalResult->Status;
        }
        catch (const std::future_error &)
        {
            return E_FAIL;
        }
    };

    SetEnableCustomNotification(trProcess, TRUE);

    GetEvalCanceled() = false;
    GetEvalCrossThreadDependency() = false;
    HRESULT ret = WaitResult();

    SetEnableCustomNotification(trProcess, FALSE);

    if (ret == CORDBG_S_FUNC_EVAL_ABORTED)
    {
        if (GetEvalCrossThreadDependency())
        {
            ret = CORDBG_E_CANT_CALL_ON_THIS_THREAD;
        }
        else
        {
            ret = GetEvalCanceled() ? COR_E_OPERATIONCANCELED : COR_E_TIMEOUT;
        }
    }
    // In this case we have the same behavior as MS vsdbg and the MSVS C# debugger - in case it was aborted by timeout, show a proper error.
    else if (evalTimeOut)
    {
        ret = (ret == E_UNEXPECTED) ? E_UNEXPECTED : COR_E_TIMEOUT;
    }

    ChangeThreadsState(THREAD_RUN);
    return ret;
}

void NotifyEvalComplete(ICorDebugThread *pThread, ICorDebugEval *pEval)
{
    const std::scoped_lock<std::mutex> lock(GetEvalResultMutex());
    if (pThread == nullptr)
    {
        GetEvalResult().reset(nullptr);
        return;
    }

    DWORD threadId = 0;
    pThread->GetID(&threadId);

    std::unique_ptr<evalResultData_t> uniqueEvalResult = std::make_unique<evalResultData_t>();
    if (pEval != nullptr)
    {
        // CORDBG_S_FUNC_EVAL_HAS_NO_RESULT: Some Func evals will lack a return value, such as those whose return type is void.
        (*uniqueEvalResult).Status = pEval->GetResult(&(*uniqueEvalResult).trEval);
    }

    if (!GetEvalResult() || GetEvalResult()->threadId != threadId)
    {
        return;
    }

    GetEvalResult()->promiseValue.set_value(std::move(uniqueEvalResult));
    GetEvalResult().reset(nullptr);
}

HRESULT ManagedCallbackCustomNotification(ICorDebugThread *pThread)
{
    // Note:
    // All CoreCLR releases up to at least version 3.1.3 don't have a proper x86 implementation of ICorDebugEval::Abort().
    // The issue is that CoreCLR terminates managed process execution instead of aborting the evaluation.

    // Note, there could be only one eval running, but we need to ignore custom notifications from threads created during eval.
    // In this case we have the same behavior as the MSVS C# debugger (ATM vsdbg doesn't support Debugger.NotifyOfCrossThreadDependency).
    ICorDebugEval *pEval = FindEvalForThread(pThread);
    if (pEval == nullptr)
    {
        return S_OK;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugEval2> trEval2;
    if (FAILED(Status = pEval->Abort()) &&
        (FAILED(Status = pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2))) ||
         FAILED(Status = trEval2->RudeAbort())))
    {
        LOGE(log << "Can't abort evaluation in custom notification callback, 0x" << std::setw(hexErrWidth) << std::setfill('0') << std::hex << Status);
        return Status;
    }

    GetEvalCrossThreadDependency() = true;
    return S_OK;
}

HRESULT SetupCrossThreadDependencyNotificationClass(ICorDebugModule *pModule)
{
    HRESULT Status = S_OK;
    ToRelease<IUnknown> trUnknown;
    IfFailRet(pModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    // In order to make the code simple and clear, we don't check enclosing classes with recursion here;
    // since we know the behavior for sure, just find "System.Diagnostics.Debugger" first.
    mdTypeDef typeDefParent = mdTypeDefNil;
    static const WSTRING strParentTypeDef(W("System.Diagnostics.Debugger"));
    IfFailRet(trMDImport->FindTypeDefByName(strParentTypeDef.c_str(), mdTypeDefNil, &typeDefParent));

    mdTypeDef typeDef = mdTypeDefNil;
    static const WSTRING strTypeDef(W("CrossThreadDependencyNotification"));
    IfFailRet(trMDImport->FindTypeDefByName(strTypeDef.c_str(), typeDefParent, &typeDef));

    GetCrossThreadDependencyNotification().Free(); // allow re-setup if needed
    return pModule->GetClassFromToken(typeDef, &GetCrossThreadDependencyNotification());
}

bool IsEvalRunning()
{
    const std::scoped_lock<std::mutex> lock(GetEvalResultMutex());
    return (GetEvalResult() != nullptr);
}

void CancelEvalRunning()
{
    const std::scoped_lock<std::mutex> lock(GetEvalResultMutex());

    if (!GetEvalResult())
    {
        return;
    }

    ToRelease<ICorDebugEval2> trEval2;
    if (SUCCEEDED(GetEvalResult()->pEval->Abort()) ||
        (SUCCEEDED(GetEvalResult()->pEval->QueryInterface(IID_ICorDebugEval2, reinterpret_cast<void **>(&trEval2))) &&
         SUCCEEDED(trEval2->RudeAbort())))
    {
        GetEvalCanceled() = true;
    }
}

void Cleanup()
{
    GetCrossThreadDependencyNotification().Free(); // allow re-setup if needed

    GetEvalCanceled() = false;
    GetEvalCrossThreadDependency() = false;

    const std::scoped_lock<std::mutex> lock(GetEvalResultMutex());
    GetEvalResult().reset(nullptr);
}

} // namespace dncdbg::EvalWaiter
