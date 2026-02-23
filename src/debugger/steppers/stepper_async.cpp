// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/steppers/stepper_async.h"
#include "debugger/steppers/stepper_simple.h" // NOLINT(misc-include-cleaner)
#include "debugger/evalhelpers.h"
#include "debugger/threads.h"
#include "debugger/valueprint.h"
#include "metadata/typeprinter.h"
#include "utils/platform.h"
#include "utils/logger.h"
#include "utils/utf.h"
#include <array>
#include <vector>

namespace dncdbg
{

namespace
{

// Get '<>t__builder' field value for builder from frame.
// [in] pFrame - frame that used for get all info needed (function, module, etc);
// [out] ppValue_builder - result value.
HRESULT GetAsyncTBuilder(ICorDebugFrame *pFrame, ICorDebugValue **ppValue_builder)
{
    HRESULT Status = S_OK;

    // Find 'this'.
    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pFrame->GetFunction(&trFunction));
    ToRelease<ICorDebugModule> trModule_this;
    IfFailRet(trFunction->GetModule(&trModule_this));
    ToRelease<IUnknown> trUnknown_this;
    IfFailRet(trModule_this->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown_this));
    ToRelease<IMetaDataImport> trMDImport_this;
    IfFailRet(trUnknown_this->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport_this)));
    mdMethodDef methodDef = mdMethodDefNil;
    IfFailRet(trFunction->GetToken(&methodDef));
    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(pFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));
    ToRelease<ICorDebugValueEnum> trParamEnum;
    IfFailRet(trILFrame->EnumerateArguments(&trParamEnum));
    ULONG cParams = 0;
    IfFailRet(trParamEnum->GetCount(&cParams));
    if (cParams == 0)
    {
        return E_FAIL;
    }
    DWORD methodAttr = 0;
    IfFailRet(trMDImport_this->GetMethodProps(methodDef, nullptr, nullptr, 0, nullptr, &methodAttr,
                                              nullptr, nullptr, nullptr, nullptr));
    const bool thisParam = (methodAttr & mdStatic) == 0;
    if (!thisParam)
    {
        return E_FAIL;
    }
    // At this point, first param will be always 'this'.
    ToRelease<ICorDebugValue> trRefValue_this;
    IfFailRet(trParamEnum->Next(1, &trRefValue_this, nullptr));

    // Find '<>t__builder' field.
    ToRelease<ICorDebugValue> trValue_this;
    IfFailRet(DereferenceAndUnboxValue(trRefValue_this, &trValue_this, nullptr));
    ToRelease<ICorDebugValue2> trValue2_this;
    IfFailRet(trValue_this->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2_this)));
    ToRelease<ICorDebugType> trType_this;
    IfFailRet(trValue2_this->GetExactType(&trType_this));
    ToRelease<ICorDebugClass> trClass_this;
    IfFailRet(trType_this->GetClass(&trClass_this));
    mdTypeDef typeDef_this = mdTypeDefNil;
    IfFailRet(trClass_this->GetToken(&typeDef_this));

    ULONG numFields = 0;
    HCORENUM hEnum = nullptr;
    mdFieldDef fieldDef = mdFieldDefNil;
    ToRelease<ICorDebugValue> trRefValue_t_builder;
    while (SUCCEEDED(trMDImport_this->EnumFields(&hEnum, typeDef_this, &fieldDef, 1, &numFields)) && numFields != 0)
    {
        ULONG nameLen = 0;
        std::array<WCHAR, mdNameLen> mdName{};
        if (FAILED(trMDImport_this->GetFieldProps(fieldDef, nullptr, mdName.data(), mdNameLen, &nameLen, nullptr, nullptr,
                                                  nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        if (!str_equal(mdName.data(), W("<>t__builder")))
        {
            continue;
        }

        ToRelease<ICorDebugObjectValue> trObjValue_this;
        if (SUCCEEDED(trValue_this->QueryInterface(IID_ICorDebugObjectValue, reinterpret_cast<void **>(&trObjValue_this))))
        {
            trObjValue_this->GetFieldValue(trClass_this, fieldDef, &trRefValue_t_builder);
        }

        break;
    }
    trMDImport_this->CloseEnum(hEnum);

    if (trRefValue_t_builder == nullptr)
    {
        return E_FAIL;
    }
    IfFailRet(DereferenceAndUnboxValue(trRefValue_t_builder, ppValue_builder, nullptr));

    return S_OK;
}

// Find Async ID, in our case - reference to created by builder object,
// that could be use as unique ID for builder (state machine) on yield and resume offset breakpoints.
// [in] pThread - managed thread for evaluation (related to pFrame);
// [in] pFrame - frame that used for get all info needed (function, module, etc);
// [in] pEvalHelpers - pointer to managed debugger EvalHelpers;
// [out] ppValueAsyncIdRef - result value (reference to created by builder object).
HRESULT GetAsyncIdReference(ICorDebugThread *pThread, ICorDebugFrame *pFrame, EvalHelpers *pEvalHelpers,
                                   ICorDebugValue **ppValueAsyncIdRef)
{
    HRESULT Status = S_OK;
    ToRelease<ICorDebugValue> trValue;
    IfFailRet(GetAsyncTBuilder(pFrame, &trValue));

    // Find 'ObjectIdForDebugger' property.
    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(trValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trType->GetClass(&trClass));
    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&typeDef));

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trClass->GetModule(&trModule));
    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    mdProperty propertyDef = mdPropertyNil;
    ULONG numProperties = 0;
    HCORENUM propEnum = nullptr;
    mdMethodDef mdObjectIdForDebuggerGetter = mdMethodDefNil;
    while (SUCCEEDED(trMDImport->EnumProperties(&propEnum, typeDef, &propertyDef, 1, &numProperties)) && numProperties != 0)
    {
        ULONG propertyNameLen = 0;
        std::array<WCHAR, mdNameLen> propertyName{};
        mdMethodDef mdGetter = mdMethodDefNil;
        if (FAILED(trMDImport->GetPropertyProps(propertyDef, nullptr, propertyName.data(), mdNameLen, &propertyNameLen,
                                                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &mdGetter,
                                                nullptr, 0, nullptr)))
        {
            continue;
        }

        if (!str_equal(propertyName.data(), W("ObjectIdForDebugger")))
        {
            continue;
        }

        mdObjectIdForDebuggerGetter = mdGetter;
        break;
    }
    trMDImport->CloseEnum(propEnum);

    if (mdObjectIdForDebuggerGetter == mdMethodDefNil)
    {
        return E_FAIL;
    }

    // Call 'ObjectIdForDebugger' property getter.
    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(trModule->GetFunctionFromToken(mdObjectIdForDebuggerGetter, &trFunc));
    // Note, builder (`this` value) could be generic type - Task<TResult>, type must be provided too.
    IfFailRet(pEvalHelpers->EvalFunction(pThread, trFunc, trType.GetRef(), 1, trValue.GetRef(), 1, ppValueAsyncIdRef, true));

    return S_OK;
}

// Set notification for wait completion - call SetNotificationForWaitCompletion() method for particular builder.
// [in] pThread - managed thread for evaluation (related to pFrame);
// [in] pFrame - frame that used for get all info needed (function, module, etc);
// [in] pEvalHelpers - pointer to managed debugger EvalHelpers;
HRESULT SetNotificationForWaitCompletion(ICorDebugThread *pThread, ICorDebugValue *pBuilderValue, EvalHelpers *pEvalHelpers)
{
    HRESULT Status = S_OK;

    // Find SetNotificationForWaitCompletion() method.
    ToRelease<ICorDebugValue2> trValue2;
    IfFailRet(pBuilderValue->QueryInterface(IID_ICorDebugValue2, reinterpret_cast<void **>(&trValue2)));
    ToRelease<ICorDebugType> trType;
    IfFailRet(trValue2->GetExactType(&trType));
    ToRelease<ICorDebugClass> trClass;
    IfFailRet(trType->GetClass(&trClass));
    mdTypeDef typeDef = mdTypeDefNil;
    IfFailRet(trClass->GetToken(&typeDef));

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trClass->GetModule(&trModule));
    ToRelease<IUnknown> trUnknown;
    IfFailRet(trModule->GetMetaDataInterface(IID_IMetaDataImport, &trUnknown));
    ToRelease<IMetaDataImport> trMDImport;
    IfFailRet(trUnknown->QueryInterface(IID_IMetaDataImport, reinterpret_cast<void **>(&trMDImport)));

    ULONG numMethods = 0;
    HCORENUM hEnum = nullptr;
    mdMethodDef methodDef = mdMethodDefNil;
    mdMethodDef setNotifDef = mdMethodDefNil;
    while (SUCCEEDED(trMDImport->EnumMethods(&hEnum, typeDef, &methodDef, 1, &numMethods)) && numMethods != 0)
    {
        mdTypeDef memTypeDef = mdTypeDefNil;
        ULONG nameLen = 0;
        std::array<WCHAR, mdNameLen> szFunctionName{};
        if (FAILED(trMDImport->GetMethodProps(methodDef, &memTypeDef, szFunctionName.data(), mdNameLen, &nameLen,
                                              nullptr, nullptr, nullptr, nullptr, nullptr)))
        {
            continue;
        }

        if (!str_equal(szFunctionName.data(), W("SetNotificationForWaitCompletion")))
        {
            continue;
        }

        setNotifDef = methodDef;
        break;
    }
    trMDImport->CloseEnum(hEnum);

    if (setNotifDef == mdMethodDefNil)
    {
        return E_FAIL;
    }

    // Create boolean argument and set it to TRUE.
    ToRelease<ICorDebugEval> trEval;
    IfFailRet(pThread->CreateEval(&trEval));
    ToRelease<ICorDebugValue> trNewBoolean;
    IfFailRet(trEval->CreateValue(ELEMENT_TYPE_BOOLEAN, nullptr, &trNewBoolean));
    uint32_t cbSize = 0;
    IfFailRet(trNewBoolean->GetSize(&cbSize));
    std::vector<BYTE> rgbValue(cbSize, 0);
    ToRelease<ICorDebugGenericValue> trGenericValue;
    IfFailRet(trNewBoolean->QueryInterface(IID_ICorDebugGenericValue, reinterpret_cast<void **>(&trGenericValue)));
    IfFailRet(trGenericValue->GetValue(static_cast<void *>(rgbValue.data())));
    rgbValue[0] = 1; // TRUE
    IfFailRet(trGenericValue->SetValue(static_cast<void *>(rgbValue.data())));

    // Call this.<>t__builder.SetNotificationForWaitCompletion(TRUE).
    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(trModule->GetFunctionFromToken(setNotifDef, &trFunc));

    std::array <ICorDebugValue *, 2> ppArgsValue{pBuilderValue, trNewBoolean};
    // Note, builder (`this` value) could be generic type - Task<TResult>, type must be provided too.
    IfFailRet(pEvalHelpers->EvalFunction(pThread, trFunc, trType.GetRef(), 1, ppArgsValue.data(), 2, nullptr, true));

    return S_OK;
}

} // unnamed namespace

HRESULT AsyncStepper::SetupStep(ICorDebugThread *pThread, StepType stepType)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugFrame> trFrame;
    IfFailRet(pThread->GetActiveFrame(&trFrame));
    if (trFrame == nullptr)
    {
        return E_FAIL;
    }

    mdMethodDef methodToken = mdMethodDefNil;
    IfFailRet(trFrame->GetFunctionToken(&methodToken));
    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(trFrame->GetFunction(&trFunc));
    ToRelease<ICorDebugCode> trCode;
    IfFailRet(trFunc->GetILCode(&trCode));
    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunc->GetModule(&trModule));
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(trModule->GetBaseAddress(&modAddress));

    if (!m_uniqueAsyncInfo->IsMethodHaveAwait(modAddress, methodToken))
    {
        return S_FALSE; // setup simple stepper
    }

    ToRelease<ICorDebugILFrame> trILFrame;
    IfFailRet(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame)));

    uint32_t ipOffset = 0;
    CorDebugMappingResult mappingResult = MAPPING_NO_INFO;
    IfFailRet(trILFrame->GetIP(&ipOffset, &mappingResult));
    if (mappingResult == MAPPING_UNMAPPED_ADDRESS ||
        mappingResult == MAPPING_NO_INFO)
    {
        return E_FAIL;
    }

    // If we are at end of async method with await blocks and doing step-in or step-over,
    // switch to step-out, so whole NotifyDebuggerOfWaitCompletion magic happens.
    uint32_t lastIlOffset = 0;
    if (stepType != StepType::STEP_OUT &&
        m_uniqueAsyncInfo->FindLastIlOffsetAwaitInfo(modAddress, methodToken, lastIlOffset) &&
        ipOffset >= lastIlOffset)
    {
        stepType = StepType::STEP_OUT;
    }
    if (stepType == StepType::STEP_OUT)
    {
        ToRelease<ICorDebugValue> trBuilderValue;
        IfFailRet(GetAsyncTBuilder(trFrame, &trBuilderValue));

        // In case method is "async void", builder is "System.Runtime.CompilerServices.AsyncVoidMethodBuilder"
        // "If we are inside `async void` method, do normal step-out" from:
        // https://github.com/dotnet/runtime/blob/32d0360b73bd77256cc9a9314a3c4280a61ea9bc/src/mono/mono/component/debugger-engine.c#L1350
        std::string builderType;
        IfFailRet(TypePrinter::GetTypeOfValue(trBuilderValue, builderType));
        if (builderType == "System.Runtime.CompilerServices.AsyncVoidMethodBuilder")
        {
            return m_simpleStepper->SetupStep(pThread, StepType::STEP_OUT);
        }

        IfFailRet(SetNotificationForWaitCompletion(pThread, trBuilderValue, m_sharedEvalHelpers.get()));
        IfFailRet(SetBreakpointIntoNotifyDebuggerOfWaitCompletion());
        // Note, we don't create stepper here, since all we need in case of breakpoint is call Continue() from StepCommand().
        return S_OK;
    }

    AsyncInfo::AwaitInfo *awaitInfo = nullptr; // NOLINT(misc-const-correctness)
    if (m_uniqueAsyncInfo->FindNextAwaitInfo(modAddress, methodToken, ipOffset, &awaitInfo))
    {
        // We have step inside async function with await, setup breakpoint at closest await's yield_offset.
        // Two possible cases here:
        // 1. Step finished successful - await code not reached.
        // 2. Breakpoint was reached - step reached await block, so, we must switch to async step logic instead.

        const std::scoped_lock<std::mutex> lock_async(m_asyncStepMutex);

        m_asyncStep = std::make_unique<asyncStep_t>();
        m_asyncStep->m_threadId = getThreadId(pThread);
        m_asyncStep->m_initialStepType = stepType;
        m_asyncStep->m_resume_offset = awaitInfo->resume_offset;
        m_asyncStep->m_stepStatus = asyncStepStatus::yield_offset_breakpoint;

        m_asyncStep->m_Breakpoint = std::make_unique<asyncBreakpoint_t>();
        m_asyncStep->m_Breakpoint->modAddress = modAddress;
        m_asyncStep->m_Breakpoint->methodToken = methodToken;
        m_asyncStep->m_Breakpoint->ilOffset = awaitInfo->yield_offset;

        ToRelease<ICorDebugFunctionBreakpoint> trFuncBreakpoint;
        IfFailRet(trCode->CreateBreakpoint(m_asyncStep->m_Breakpoint->ilOffset, &trFuncBreakpoint));
        IfFailRet(trFuncBreakpoint->Activate(TRUE));
        m_asyncStep->m_Breakpoint->trFuncBreakpoint = trFuncBreakpoint.Detach();
    }

    return S_FALSE; // setup simple stepper
}

HRESULT AsyncStepper::ManagedCallbackStepComplete()
{
    // In case we have async method and first await breakpoint (yield_offset) was enabled, but not reached.
    m_asyncStepMutex.lock();
    if (m_asyncStep)
    {
        m_asyncStep.reset(nullptr);
    }
    m_asyncStepMutex.unlock();

    return S_FALSE; // S_FALSE - no error, but steppers not affect on callback
}

HRESULT AsyncStepper::DisableAllSteppers()
{
    m_asyncStepMutex.lock();
    if (m_asyncStep)
    {
        m_asyncStep.reset(nullptr);
    }
    if (m_asyncStepNotifyDebuggerOfWaitCompletion)
    {
        m_asyncStepNotifyDebuggerOfWaitCompletion.reset(nullptr);
    }
    m_asyncStepMutex.unlock();

    return S_OK;
}

// Setup breakpoint into System.Threading.Tasks.Task.NotifyDebuggerOfWaitCompletion() method, that will be
// called at wait completion if notification was enabled by SetNotificationForWaitCompletion().
// Note, NotifyDebuggerOfWaitCompletion() will be called only once, since notification flag
// will be automatically disabled inside NotifyDebuggerOfWaitCompletion() method itself.
HRESULT AsyncStepper::SetBreakpointIntoNotifyDebuggerOfWaitCompletion()
{
    HRESULT Status = S_OK;
    static const std::string assemblyName("System.Private.CoreLib.dll");
    static const WSTRING className(W("System.Threading.Tasks.Task"));
    static const WSTRING methodName(W("NotifyDebuggerOfWaitCompletion"));
    ToRelease<ICorDebugFunction> trFunc;
    IfFailRet(m_sharedEvalHelpers->FindMethodInModule(assemblyName, className, methodName, &trFunc));

    ToRelease<ICorDebugModule> trModule;
    IfFailRet(trFunc->GetModule(&trModule));
    CORDB_ADDRESS modAddress = 0;
    IfFailRet(trModule->GetBaseAddress(&modAddress));
    mdMethodDef methodDef = mdMethodDefNil;
    IfFailRet(trFunc->GetToken(&methodDef));

    ToRelease<ICorDebugCode> trCode;
    IfFailRet(trFunc->GetILCode(&trCode));

    ToRelease<ICorDebugFunctionBreakpoint> trFuncBreakpoint;
    IfFailRet(trCode->CreateBreakpoint(0, &trFuncBreakpoint));
    IfFailRet(trFuncBreakpoint->Activate(TRUE));

    const std::scoped_lock<std::mutex> lock_async(m_asyncStepMutex);
    m_asyncStepNotifyDebuggerOfWaitCompletion = std::make_unique<asyncBreakpoint_t>();
    m_asyncStepNotifyDebuggerOfWaitCompletion->trFuncBreakpoint = trFuncBreakpoint.Detach();
    m_asyncStepNotifyDebuggerOfWaitCompletion->modAddress = modAddress;
    m_asyncStepNotifyDebuggerOfWaitCompletion->methodToken = methodDef;

    return S_OK;
}

// Check if breakpoint is part of async stepping routine and do next action for async stepping if need.
// [in] pThread - object that represents the thread that contains the breakpoint.
HRESULT AsyncStepper::ManagedCallbackBreakpoint(ICorDebugThread *pThread)
{
    ToRelease<ICorDebugFrame> trFrame;
    mdMethodDef methodToken = mdMethodDefNil;
    if (FAILED(pThread->GetActiveFrame(&trFrame)) ||
        trFrame == nullptr ||
        FAILED(trFrame->GetFunctionToken(&methodToken)))
    {
        LOGE("Failed receive function token for async step");
        return S_FALSE;
    }
    CORDB_ADDRESS modAddress = 0;
    ToRelease<ICorDebugFunction> trFunc;
    ToRelease<ICorDebugModule> trModule;
    if (FAILED(trFrame->GetFunction(&trFunc)) ||
        FAILED(trFunc->GetModule(&trModule)) ||
        FAILED(trModule->GetBaseAddress(&modAddress)))
    {
        LOGE("Failed receive module address for async step");
        return S_FALSE;
    }

    const std::scoped_lock<std::mutex> lock_async(m_asyncStepMutex);

    if (!m_asyncStep)
    {
        // Care special case here, when we step-out from async method with await blocks
        // and NotifyDebuggerOfWaitCompletion magic happens with breakpoint in this method.
        // Note, if we hit NotifyDebuggerOfWaitCompletion breakpoint, it's our no matter which thread.

        if (!m_asyncStepNotifyDebuggerOfWaitCompletion ||
            modAddress != m_asyncStepNotifyDebuggerOfWaitCompletion->modAddress ||
            methodToken != m_asyncStepNotifyDebuggerOfWaitCompletion->methodToken)
        {
            return S_FALSE;
        }

        m_asyncStepNotifyDebuggerOfWaitCompletion.reset(nullptr);
        // Note, notification flag will be reseted automatically in NotifyDebuggerOfWaitCompletion() method,
        // no need call SetNotificationForWaitCompletion() with FALSE arg (at least, mono acts in the same way).

        // Update stepping request to new thread/frame_count that we are continuing on
        // so continuing with normal step-out works as expected.
        m_simpleStepper->SetupStep(pThread, StepType::STEP_OUT);
        return S_OK;
    }

    if (modAddress != m_asyncStep->m_Breakpoint->modAddress ||
        methodToken != m_asyncStep->m_Breakpoint->methodToken)
    {
        // Async step was breaked by another breakpoint, remove async step related breakpoint.
        // Same behavior as MS vsdbg have for stepping interrupted by breakpoint.
        m_asyncStep.reset(nullptr);
        return S_FALSE;
    }

    ToRelease<ICorDebugILFrame> trILFrame;
    uint32_t ipOffset = 0;
    CorDebugMappingResult mappingResult = MAPPING_NO_INFO;
    if (FAILED(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame))) ||
        FAILED(trILFrame->GetIP(&ipOffset, &mappingResult)) ||
        mappingResult == MAPPING_UNMAPPED_ADDRESS ||
        mappingResult == MAPPING_NO_INFO)
    {
        LOGE("Failed receive current IP offset for async step");
        return S_FALSE;
    }

    if (ipOffset != m_asyncStep->m_Breakpoint->ilOffset)
    {
        // Async step was breaked by another breakpoint, remove async step related breakpoint.
        // Same behavior as MS vsdbg have for stepping interrupted by breakpoint.
        m_asyncStep.reset(nullptr);
        return S_FALSE;
    }

    if (m_asyncStep->m_stepStatus == asyncStepStatus::yield_offset_breakpoint)
    {
        // Note, in case of first breakpoint for async step, we must have same thread.
        if (m_asyncStep->m_threadId != getThreadId(pThread))
        {
            // Parallel thread execution, skip it and continue async step routine.
            return S_OK;
        }

        HRESULT Status = S_OK;
        ToRelease<ICorDebugProcess> trProcess;
        IfFailRet(pThread->GetProcess(&trProcess));
        m_simpleStepper->DisableAllSteppers(trProcess);

        m_asyncStep->m_stepStatus = asyncStepStatus::resume_offset_breakpoint;

        ToRelease<ICorDebugCode> trCode;
        ToRelease<ICorDebugFunctionBreakpoint> trFuncBreakpoint;
        if (FAILED(trFunc->GetILCode(&trCode)) ||
            FAILED(trCode->CreateBreakpoint(m_asyncStep->m_resume_offset, &trFuncBreakpoint)) ||
            FAILED(trFuncBreakpoint->Activate(TRUE)))
        {
            LOGE("Could not setup second breakpoint (resume_offset) for await block");
            return S_FALSE;
        }

        m_asyncStep->m_Breakpoint->trFuncBreakpoint->Activate(FALSE);
        m_asyncStep->m_Breakpoint->trFuncBreakpoint = trFuncBreakpoint.Detach();
        m_asyncStep->m_Breakpoint->ilOffset = m_asyncStep->m_resume_offset;

        CorDebugHandleType handleType = CorDebugHandleType::HANDLE_PINNED;
        ToRelease<ICorDebugValue> trValue;
        if (FAILED(GetAsyncIdReference(pThread, trFrame, m_sharedEvalHelpers.get(), &trValue)) ||
            FAILED(trValue->QueryInterface(IID_ICorDebugHandleValue, reinterpret_cast<void **>(&m_asyncStep->m_trHandleValueAsyncId))) ||
            FAILED(m_asyncStep->m_trHandleValueAsyncId->GetHandleType(&handleType)) ||
            handleType != CorDebugHandleType::HANDLE_STRONG) // Note, we need only strong handle here, that will not invalidated on continue-break.
        {
            m_asyncStep->m_trHandleValueAsyncId.Free();
            LOGE("Could not setup handle with async ID for await block");
        }
    }
    else
    {
        // For second breakpoint we could have 3 cases:
        // 1. We still have initial thread, so, no need spend time and check asyncId.
        // 2. We have another thread with same asyncId - same execution of async method.
        // 3. We have another thread with different asyncId - parallel execution of async method.
        if (m_asyncStep->m_threadId == getThreadId(pThread))
        {
            m_simpleStepper->SetupStep(pThread, m_asyncStep->m_initialStepType);
            m_asyncStep.reset(nullptr);
            return S_OK;
        }

        ToRelease<ICorDebugValue> trValueRef;
        CORDB_ADDRESS currentAsyncId = 0;
        ToRelease<ICorDebugValue> trValue;
        BOOL isNull = FALSE;
        if (SUCCEEDED(GetAsyncIdReference(pThread, trFrame, m_sharedEvalHelpers.get(), &trValueRef)) &&
            SUCCEEDED(DereferenceAndUnboxValue(trValueRef, &trValue, &isNull)) && (isNull == FALSE))
        {
            trValue->GetAddress(&currentAsyncId);
        }
        else
        {
            LOGE("Could not calculate current async ID for await block");
        }

        CORDB_ADDRESS prevAsyncId = 0;
        ToRelease<ICorDebugValue> trDereferencedValue;
        ToRelease<ICorDebugValue> trValueAsyncId;
        if ((m_asyncStep->m_trHandleValueAsyncId != nullptr) && // Note, we could fail with m_trHandleValueAsyncId on previous breakpoint by some reason.
            SUCCEEDED(m_asyncStep->m_trHandleValueAsyncId->Dereference(&trDereferencedValue)) &&
            SUCCEEDED(DereferenceAndUnboxValue(trDereferencedValue, &trValueAsyncId, &isNull)) && (isNull == FALSE))
        {
            trValueAsyncId->GetAddress(&prevAsyncId);
        }
        else
        {
            LOGE("Could not calculate previous async ID for await block");
        }

        // Note, 'currentAsyncId' and 'prevAsyncId' is 64 bit addresses, in our case can't be 0.
        // If we can't detect proper thread - continue stepping for this thread.
        if (currentAsyncId == prevAsyncId || currentAsyncId == 0 || prevAsyncId == 0)
        {
            m_simpleStepper->SetupStep(pThread, m_asyncStep->m_initialStepType);
            m_asyncStep.reset(nullptr);
        }
    }

    return S_OK;
}

} // namespace dncdbg
