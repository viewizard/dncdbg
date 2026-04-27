// Copyright (c) 2020-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifdef _MSC_VER
#include <wtypes.h>
#endif

#include "debugger/managedcallback.h"
#include "debugger/manageddebugger.h"
#include "debugger/breakpoints/breakpoints.h" // NOLINT(misc-include-cleaner)
#include "debugger/callbacksqueue.h"
#include "debugger/evalstackmachine.h" // NOLINT(misc-include-cleaner)
#include "debugger/evalwaiter.h" // NOLINT(misc-include-cleaner)
#include "debugger/threads.h"
#include "debuginfo/debuginfo.h" // NOLINT(misc-include-cleaner)
#include "metadata/modules.h" // NOLINT(misc-include-cleaner)
#include "protocol/dapio.h"
#include "utils/logger.h"
#include "utils/waitpid.h"
#include "utils/utf.h"

namespace dncdbg
{

namespace
{

ExceptionCallbackType CorrectedByJMCCatchHandlerEventType(ICorDebugFrame *pFrame, bool justMyCode)
{
    if (!justMyCode)
    {
        return ExceptionCallbackType::CATCH_HANDLER_FOUND;
    }

    BOOL JMCStatus = FALSE;
    ToRelease<ICorDebugFunction> trFunction;
    ToRelease<ICorDebugFunction2> trFunction2;
    if (pFrame != nullptr && SUCCEEDED(pFrame->GetFunction(&trFunction)) &&
        SUCCEEDED(trFunction->QueryInterface(IID_ICorDebugFunction2, reinterpret_cast<void **>(&trFunction2))) &&
        SUCCEEDED(trFunction2->GetJMCStatus(&JMCStatus)) && JMCStatus == TRUE)
    {
        return ExceptionCallbackType::USER_CATCH_HANDLER_FOUND;
    }

    return ExceptionCallbackType::CATCH_HANDLER_FOUND;
}

} // unnamed namespace

ULONG ManagedCallback::GetRefCount()
{
    const std::scoped_lock<std::mutex> lock(m_refCountMutex);
    return m_refCount;
}

// IUnknown

HRESULT STDMETHODCALLTYPE ManagedCallback::QueryInterface(REFIID riid, void **ppInterface)
{
    if (riid == IID_ICorDebugManagedCallback) // NOLINT(readability-implicit-bool-conversion)
    {
        *ppInterface = static_cast<ICorDebugManagedCallback *>(this);
    }
    else if (riid == IID_ICorDebugManagedCallback2) // NOLINT(readability-implicit-bool-conversion)
    {
        *ppInterface = static_cast<ICorDebugManagedCallback2 *>(this);
    }
    else if (riid == IID_ICorDebugManagedCallback3) // NOLINT(readability-implicit-bool-conversion)
    {
        *ppInterface = static_cast<ICorDebugManagedCallback3 *>(this);
    }
    else if (riid == IID_IUnknown) // NOLINT(readability-implicit-bool-conversion)
    {
        *ppInterface = static_cast<IUnknown *>(static_cast<ICorDebugManagedCallback *>(this));
    }
    else
    {
        *ppInterface = nullptr;
        return E_NOINTERFACE;
    }

    this->AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE ManagedCallback::AddRef()
{
    const std::scoped_lock<std::mutex> lock(m_refCountMutex);
    return ++m_refCount;
}

ULONG STDMETHODCALLTYPE ManagedCallback::Release()
{
    const std::scoped_lock<std::mutex> lock(m_refCountMutex);

    assert(m_refCount > 0);

    // Note, we don't provide "delete" call for object itself for our fake "COM".
    // External holder will care about this object during debugger lifetime.

    return --m_refCount;
}

// ICorDebugManagedCallback

HRESULT STDMETHODCALLTYPE ManagedCallback::Breakpoint(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread,
                                                      ICorDebugBreakpoint *pBreakpoint)
{
    return m_sharedCallbacksQueue->AddCallbackToQueue(pAppDomain, [&]() {
        pAppDomain->AddRef();
        pThread->AddRef();
        pBreakpoint->AddRef();
        m_sharedCallbacksQueue->EmplaceBack(CallbackQueueCall::Breakpoint, pAppDomain, pThread, pBreakpoint,
                                            STEP_NORMAL, ExceptionCallbackType::FIRST_CHANCE);
    });
}

HRESULT STDMETHODCALLTYPE ManagedCallback::StepComplete(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread,
                                                        ICorDebugStepper */*pStepper*/, CorDebugStepReason reason)
{
    return m_sharedCallbacksQueue->AddCallbackToQueue(pAppDomain, [&]() {
        pAppDomain->AddRef();
        pThread->AddRef();
        m_sharedCallbacksQueue->EmplaceBack(CallbackQueueCall::StepComplete, pAppDomain, pThread, nullptr, reason,
                                            ExceptionCallbackType::FIRST_CHANCE);
    });
}

HRESULT STDMETHODCALLTYPE ManagedCallback::Break(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread)
{
    return m_sharedCallbacksQueue->AddCallbackToQueue(pAppDomain, [&]() {
        pAppDomain->AddRef();
        pThread->AddRef();
        m_sharedCallbacksQueue->EmplaceBack(CallbackQueueCall::Break, pAppDomain, pThread, nullptr, STEP_NORMAL,
                                            ExceptionCallbackType::FIRST_CHANCE);
    });
}

HRESULT STDMETHODCALLTYPE ManagedCallback::Exception(ICorDebugAppDomain *pAppDomain, ICorDebugThread */*pThread*/, BOOL /*unhandled*/)
{
    // Obsolete callback
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::EvalComplete(ICorDebugAppDomain */*pAppDomain*/, ICorDebugThread *pThread, ICorDebugEval *pEval)
{
    m_debugger.m_sharedEvalWaiter->NotifyEvalComplete(pThread, pEval);
    return S_OK; // Eval-related routine - no callbacks queue related code here.
}

HRESULT STDMETHODCALLTYPE ManagedCallback::EvalException(ICorDebugAppDomain */*pAppDomain*/, ICorDebugThread *pThread, ICorDebugEval *pEval)
{
    m_debugger.m_sharedEvalWaiter->NotifyEvalComplete(pThread, pEval);
    return S_OK; // Eval-related routine - no callbacks queue related code here.
}

// https://docs.microsoft.com/en-us/dotnet/framework/unmanaged-api/debugging/icordebugmanagedcallback-createprocess-method
// Notifies the debugger when a process has been attached or started for the first time.
// Remarks
// This method is not called until the common language runtime is initialized. Most of the ICorDebug methods will return CORDBG_E_NOTREADY before the CreateProcess callback.
HRESULT STDMETHODCALLTYPE ManagedCallback::CreateProcess(ICorDebugProcess *pProcess)
{
    // Important! Care about callback queue before NotifyProcessCreated() call.
    // In case of `attach`, NotifyProcessCreated() call will notify debugger that debuggee process attached and debugger
    // should stop debuggee process by direct `Pause()` call. From another side, callback queue has a bunch of asynchronous
    // added entries and, for example, `CreateThread()` could be called after this callback and break our debugger logic.
    ToRelease<ICorDebugAppDomainEnum> trAppDomainEnum;
    ToRelease<ICorDebugAppDomain> trAppDomain;
    ULONG domainsFetched = 0;
    if (SUCCEEDED(pProcess->EnumerateAppDomains(&trAppDomainEnum)))
    {
        // At this point we have only one domain for sure.
        if (SUCCEEDED(trAppDomainEnum->Next(1, &trAppDomain, &domainsFetched)) && domainsFetched == 1)
        {
            return m_sharedCallbacksQueue->AddCallbackToQueue(trAppDomain, [&]()
            {
                m_sharedCallbacksQueue->EmplaceBack(CallbackQueueCall::CreateProcess, trAppDomain.Detach(), nullptr, nullptr, STEP_NORMAL, ExceptionCallbackType::FIRST_CHANCE);
            });
        }
    }

    return m_sharedCallbacksQueue->ContinueProcess(pProcess);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::ExitProcess([[maybe_unused]] ICorDebugProcess *pProcess)
{
    if (m_debugger.m_sharedEvalWaiter->IsEvalRunning())
    {
        LOGW(log << "The target process exited while evaluating the function.");
    }

    m_debugger.m_sharedEvalWaiter->NotifyEvalComplete(nullptr, nullptr);

    // Linux: exit() and _exit() argument is int (signed int)
    // Windows: ExitProcess() and TerminateProcess() argument is UINT (unsigned int)
    // Windows: GetExitCodeProcess() argument is DWORD (unsigned long)
    // internal CoreCLR variable LatchedExitCode is INT32 (signed int)
    // C# Main() return values is int (signed int) or void (return 0)
    int exitCode = 0;
#ifdef FEATURE_PAL
    exitCode = WaitpidHook::GetExitCode();
#else
    HPROCESS hProcess;
    DWORD dwExitCode = 0;
    if (SUCCEEDED(pProcess->GetHandle(&hProcess)))
    {
        GetExitCodeProcess(hProcess, &dwExitCode);
        assert(dwExitCode <= static_cast<DWORD>(std::numeric_limits<int>::max()));
        exitCode = static_cast<int>(dwExitCode);
    }
#endif // FEATURE_PAL

    DAPIO::EmitExitedEvent(ExitedEvent(exitCode));
    m_debugger.NotifyProcessExited();
    DAPIO::EmitTerminatedEvent();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ManagedCallback::CreateThread(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread)
{
    if (m_debugger.m_sharedEvalWaiter->IsEvalRunning())
    {
        LOGW(log << "Thread was created by user code during evaluation with implicit user code execution.");
    }

    const ThreadId threadId(getThreadId(pThread));
    m_debugger.m_sharedThreads->Add(m_debugger.m_sharedEvaluator, pThread, threadId, m_debugger.m_startMethod == StartMethod::Attach);

    DAPIO::EmitThreadEvent(ThreadEvent(ThreadEventReason::Started, threadId));
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::ExitThread(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread)
{
    const ThreadId threadId(getThreadId(pThread));
    m_debugger.m_sharedThreads->Remove(threadId);

    m_debugger.m_sharedEvalWaiter->NotifyEvalComplete(pThread, nullptr);
    if (m_debugger.GetLastStoppedThreadId() == threadId)
    {
        m_debugger.InvalidateLastStoppedThreadId();
    }

    m_debugger.m_sharedBreakpoints->ManagedCallbackExitThread(pThread);

    DAPIO::EmitThreadEvent(ThreadEvent(ThreadEventReason::Exited, threadId));
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::LoadModule(ICorDebugAppDomain *pAppDomain, ICorDebugModule *pModule)
{
    Module &module = m_debugger.m_sharedModules->GetNewModuleRef();
    std::string errorText;
    m_debugger.m_sharedDebugInfo->TryLoadModuleSymbols(pModule, module, errorText);
    // Note, LoadModuleMetadata() must be called after debug info (symbols) load.
    Modules::LoadModuleMetadata(pModule, module, m_debugger.IsJustMyCode(), errorText);
    DAPIO::EmitModuleEvent(ModuleEvent(ModuleEventReason::New, module));
    if (!errorText.empty())
    {
        DAPIO::EmitOutputEvent({OutputCategory::StdErr, errorText});
    }

    if (module.symbolStatus == SymbolStatus::Loaded)
    {
#ifdef DEBUG_INTERNAL_TESTS
        const size_t bpCountBeforeLoad = m_debugger.m_sharedBreakpoints->GetBreakpointsCount();
#endif // DEBUG_INTERNAL_TESTS
        std::vector<BreakpointEvent> events;
        m_debugger.m_sharedBreakpoints->ManagedCallbackLoadModule(pModule, events);
        for (const BreakpointEvent &event : events)
        {
            DAPIO::EmitBreakpointEvent(event);
        }
#ifdef DEBUG_INTERNAL_TESTS
        const size_t bpCountAfterLoad = m_debugger.m_sharedBreakpoints->GetBreakpointsCount();
        m_debugger.m_sharedBreakpoints->ManagedCallbackUnloadModule(pModule, events);
        assert(bpCountBeforeLoad == m_debugger.m_sharedBreakpoints->GetBreakpointsCount());
        m_debugger.m_sharedBreakpoints->ManagedCallbackLoadModule(pModule, events);
        assert(bpCountAfterLoad == m_debugger.m_sharedBreakpoints->GetBreakpointsCount());
#endif // DEBUG_INTERNAL_TESTS
    }

    // enable Debugger.NotifyOfCrossThreadDependency after System.Private.CoreLib.dll loaded (trigger for 1 time call only)
    if (module.name == "System.Private.CoreLib.dll")
    {
        m_debugger.m_sharedEvalWaiter->SetupCrossThreadDependencyNotificationClass(pModule);
        m_debugger.m_sharedEvalStackMachine->FindPredefinedTypes(pModule);
    }

    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::UnloadModule(ICorDebugAppDomain *pAppDomain, ICorDebugModule *pModule)
{
    std::vector<BreakpointEvent> events;
    m_debugger.m_sharedBreakpoints->ManagedCallbackUnloadModule(pModule, events);
    for (const BreakpointEvent &event : events)
    {
        DAPIO::EmitBreakpointEvent(event);
    }

    Module removedModule;
    if (SUCCEEDED(m_debugger.m_sharedModules->RemoveModule(pModule, removedModule)))
    {
        DAPIO::EmitModuleEvent(ModuleEvent(ModuleEventReason::Removed, removedModule));
    }

    m_debugger.m_sharedDebugInfo->UnloadModuleSymbols(pModule);

    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::LoadClass(ICorDebugAppDomain *pAppDomain, ICorDebugClass */*pClass*/)
{
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::UnloadClass(ICorDebugAppDomain *pAppDomain, ICorDebugClass */*pClass*/)
{
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::DebuggerError(ICorDebugProcess *pProcess, HRESULT /*errorHR*/, DWORD /*errorCode*/)
{
    return m_sharedCallbacksQueue->ContinueProcess(pProcess);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::LogMessage(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread,
                                                      LONG /*lLevel*/, WCHAR */*pLogSwitchName*/, WCHAR *pMessage)
{
    if (m_debugger.m_sharedEvalWaiter->IsEvalRunning())
    {
        pAppDomain->Continue(0); // Eval-related routine - ignore callbacks queue, continue process execution.
        return S_OK;
    }

    OutputEvent event(OutputCategory::StdOut, to_utf8(pMessage));

    DWORD threadId = 0;
    pThread->GetID(&threadId);
    std::vector<StackFrame> stackFrames;
    if ((threadId != 0U) &&
        SUCCEEDED(m_debugger.GetStackTrace(ThreadId(threadId), FrameLevel(0), 0, stackFrames)))
    {
        // Find first frame with source file data (code with PDB/user code).
        auto it = std::find_if(stackFrames.begin(), stackFrames.end(),
                               [](const StackFrame &stackFrame)
                               {
                                   return !stackFrame.source.IsNull();
                               });
        if (it != stackFrames.end())
        {
            event.source = it->source;
            event.line = it->line;
            event.column = it->column;
        }
    }

    DAPIO::EmitOutputEvent(event);
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::LogSwitch(ICorDebugAppDomain *pAppDomain, ICorDebugThread */*pThread*/, LONG /*lLevel*/,
                                                     ULONG /*ulReason*/, WCHAR */*pLogSwitchName*/, WCHAR */*pParentName*/)
{
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::CreateAppDomain(ICorDebugProcess *pProcess, ICorDebugAppDomain */*pAppDomain*/)
{
    return m_sharedCallbacksQueue->ContinueProcess(pProcess);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::ExitAppDomain(ICorDebugProcess *pProcess, ICorDebugAppDomain */*pAppDomain*/)
{
    return m_sharedCallbacksQueue->ContinueProcess(pProcess);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::LoadAssembly(ICorDebugAppDomain *pAppDomain, ICorDebugAssembly */*pAssembly*/)
{
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::UnloadAssembly(ICorDebugAppDomain *pAppDomain, ICorDebugAssembly */*pAssembly*/)
{
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::ControlCTrap(ICorDebugProcess *pProcess)
{
    return m_sharedCallbacksQueue->ContinueProcess(pProcess);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::NameChange(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread)
{
    m_debugger.m_sharedThreads->ChangeName(m_debugger.m_sharedEvaluator, pThread);
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::UpdateModuleSymbols(ICorDebugAppDomain *pAppDomain, ICorDebugModule */*pModule*/,
                                                               IStream */*pSymbolStream*/)
{
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::EditAndContinueRemap(ICorDebugAppDomain *pAppDomain, ICorDebugThread */*pThread*/,
                                                                ICorDebugFunction */*pFunction*/, BOOL /*fAccurate*/)
{
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::BreakpointSetError(ICorDebugAppDomain *pAppDomain, ICorDebugThread */*pThread*/,
                                                              ICorDebugBreakpoint */*pBreakpoint*/, DWORD /*dwError*/)
{
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

// ICorDebugManagedCallback2

HRESULT STDMETHODCALLTYPE ManagedCallback::FunctionRemapOpportunity(ICorDebugAppDomain *pAppDomain, ICorDebugThread */*pThread*/,
                                                                    ICorDebugFunction */*pOldFunction*/,
                                                                    ICorDebugFunction */*pNewFunction*/, uint32_t /*oldILOffset*/)
{
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::CreateConnection(ICorDebugProcess *pProcess, CONNID /*dwConnectionId*/,
                                                            WCHAR */*pConnName*/)
{
    return m_sharedCallbacksQueue->ContinueProcess(pProcess);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::ChangeConnection(ICorDebugProcess *pProcess, CONNID /*dwConnectionId*/)
{
    return m_sharedCallbacksQueue->ContinueProcess(pProcess);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::DestroyConnection(ICorDebugProcess *pProcess, CONNID /*dwConnectionId*/)
{
    return m_sharedCallbacksQueue->ContinueProcess(pProcess);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::Exception(ICorDebugAppDomain *pAppDomain, ICorDebugThread *pThread,
                                                     ICorDebugFrame *pFrame, uint32_t /*nOffset*/,
                                                     CorDebugExceptionCallbackType dwEventType, DWORD /*dwFlags*/)
{
    return m_sharedCallbacksQueue->AddCallbackToQueue(pAppDomain, [&]() {
        // pFrame could be neutered in case of evaluation during brake, do all stuff with pFrame in callback itself.
        ExceptionCallbackType eventType = ExceptionCallbackType::UNKNOWN;
        switch (dwEventType)
        {
        case DEBUG_EXCEPTION_FIRST_CHANCE:
            eventType = ExceptionCallbackType::FIRST_CHANCE;
            break;
        case DEBUG_EXCEPTION_USER_FIRST_CHANCE:
            eventType = ExceptionCallbackType::USER_FIRST_CHANCE;
            break;
        case DEBUG_EXCEPTION_CATCH_HANDLER_FOUND:
            eventType = CorrectedByJMCCatchHandlerEventType(pFrame, m_debugger.IsJustMyCode());
            break;
        case DEBUG_EXCEPTION_UNHANDLED:
        default:
            eventType = ExceptionCallbackType::UNHANDLED;
            break;
        }

        pAppDomain->AddRef();
        pThread->AddRef();
        m_sharedCallbacksQueue->EmplaceBack(CallbackQueueCall::Exception, pAppDomain, pThread, nullptr, STEP_NORMAL, eventType);
    });
}

HRESULT STDMETHODCALLTYPE ManagedCallback::ExceptionUnwind(ICorDebugAppDomain *pAppDomain, ICorDebugThread */*pThread*/,
                                                           CorDebugExceptionUnwindCallbackType /*dwEventType*/,
                                                           DWORD /*dwFlags*/)
{
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::FunctionRemapComplete(ICorDebugAppDomain *pAppDomain,
                                                                 ICorDebugThread */*pThread*/, ICorDebugFunction */*pFunction*/)
{
    return m_sharedCallbacksQueue->ContinueAppDomain(pAppDomain);
}

HRESULT STDMETHODCALLTYPE ManagedCallback::MDANotification(ICorDebugController */*pController*/, ICorDebugThread *pThread,
                                                           ICorDebugMDA */*pMDA*/)
{
    ToRelease<ICorDebugProcess> trProcess;
    pThread->GetProcess(&trProcess);
    return m_sharedCallbacksQueue->ContinueProcess(trProcess);
}

// ICorDebugManagedCallback3

HRESULT STDMETHODCALLTYPE ManagedCallback::CustomNotification(ICorDebugThread *pThread, ICorDebugAppDomain *pAppDomain)
{
    m_debugger.m_sharedEvalWaiter->ManagedCallbackCustomNotification(pThread);
    pAppDomain->Continue(0); // Eval-related routine - ignore callbacks queue, continue process execution.
    return S_OK;
}

} // namespace dncdbg
