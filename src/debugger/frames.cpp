// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/frames.h"
#include "debugger/evalhelpers.h"
#include "debugger/threads.h"
#include "debuginfo/debuginfo.h"
#include "metadata/typeprinter.h"
#include "utils/hresult.h"
#include "utils/torelease.h"
#include <algorithm>
#include <iterator>
#include <list>
#include <vector>

namespace dncdbg
{

namespace
{

uintptr_t GetSP(const CONTEXT *context)
{
#if defined(_TARGET_AMD64_) // NOLINT(readability-use-concise-preprocessor-directives)
    return context->Rsp;
#elif defined(_TARGET_X86_)
    return context->Esp;
#elif defined(_TARGET_ARM_)
    return context->Sp;
#elif defined(_TARGET_ARM64_)
    return context->Sp;
#elif defined(_TARGET_RISCV64_)
    return context->Sp;
#elif defined(_TARGET_LOONGARCH64_)
    return context->Sp;
#else
    static_assert(false, "Unsupported platform");
#endif
}

uintptr_t GetFP(const CONTEXT *context)
{
#if defined(_TARGET_AMD64_) // NOLINT(readability-use-concise-preprocessor-directives)
    return context->Rbp;
#elif defined(_TARGET_X86_)
    return context->Ebp;
#elif defined(_TARGET_ARM_)
    return context->R11;
#elif defined(_TARGET_ARM64_)
    return context->Fp;
#elif defined(_TARGET_RISCV64_)
    return context->Fp;
#elif defined(_TARGET_LOONGARCH64_)
    return context->Fp;
#else
    static_assert(false, "Unsupported platform");
#endif
}

void SetFP(CONTEXT *context, uintptr_t value)
{
#if defined(_TARGET_AMD64_) // NOLINT(readability-use-concise-preprocessor-directives)
    context->Rbp = value;
#elif defined(_TARGET_X86_)
    context->Ebp = value;
#elif defined(_TARGET_ARM_)
    context->R11 = value;
#elif defined(_TARGET_ARM64_)
    context->Fp = value;
#elif defined(_TARGET_RISCV64_)
    context->Fp = value;
#elif defined(_TARGET_LOONGARCH64_)
    context->Fp = value;
#else
    static_assert(false, "Unsupported platform");
#endif
}

void UnwindNativeFrames(ICorDebugThread */*pThread*/, bool /*firstFrame*/, CONTEXT */*pStartContext*/,
                                  CONTEXT */*pEndContext*/, const WalkFramesCallback &/*cb*/)
{
    // In case of a non-interop build we merge "CoreCLR native frame" and "user's native frame" into "[Native Frames]".
}

HRESULT GetActiveInternalFrames(const ToRelease<ICorDebugThread3> &trThread3, std::list<ToRelease<ICorDebugInternalFrame2>> &trInternalFrames)
{
    HRESULT Status = S_OK;
    uint32_t cInternalFrames = 0;
    IfFailRet(trThread3->GetActiveInternalFrames(0, &cInternalFrames, nullptr));

    uint32_t fetchedFrames = 0;
    std::vector<ICorDebugInternalFrame2 *> pInternalFrames(cInternalFrames);
    if (SUCCEEDED(trThread3->GetActiveInternalFrames(cInternalFrames, &fetchedFrames, pInternalFrames.data())) &&
        fetchedFrames == cInternalFrames)
    {
        std::transform(pInternalFrames.begin(), pInternalFrames.end(),
                       std::back_inserter(trInternalFrames), [](ICorDebugInternalFrame2 *p)
                       {
                           return ToRelease<ICorDebugInternalFrame2>(p);
                       });
    }
    else
    {
        return E_FAIL;
    }

    return S_OK;
}

bool AllowInternalFrame(ICorDebugInternalFrame2 *pInternalFrame2)
{
    ToRelease<ICorDebugInternalFrame> trInternalFrame;
    CorDebugInternalFrameType corFrameType = STUBFRAME_NONE;
    if (SUCCEEDED(pInternalFrame2->QueryInterface(IID_ICorDebugInternalFrame, reinterpret_cast<void **>(&trInternalFrame))))
    {
        trInternalFrame->GetFrameType(&corFrameType);
    }
    else
    {
        return false;
    }

    switch (corFrameType)
    {
    case STUBFRAME_M2U:
    case STUBFRAME_U2M:
    case STUBFRAME_APPDOMAIN_TRANSITION:
    case STUBFRAME_LIGHTWEIGHT_FUNCTION:
    case STUBFRAME_FUNC_EVAL:
    case STUBFRAME_INTERNALCALL:
    case STUBFRAME_CLASS_INIT:
    case STUBFRAME_SECURITY:
    case STUBFRAME_JIT_COMPILATION:
        return true;
    case STUBFRAME_EXCEPTION: // no reason to add `[Exception]` frame on the top of stacktrace
    case STUBFRAME_NONE:
        return false;
    default:
        assert(false);
        return false;
    }
}

HRESULT GetILOffsetFromNativeAddress(ICorDebugModule *pModule, mdMethodDef methodToken, CORDB_ADDRESS nativeAddress, uint32_t &ilOffset)
{
    if (pModule == nullptr)
    {
        return E_INVALIDARG;
    }

    HRESULT Status = S_OK;
    ToRelease<ICorDebugFunction> trFunction;
    IfFailRet(pModule->GetFunctionFromToken(methodToken, &trFunction));
    ToRelease<ICorDebugCode> trNativeCode;
    IfFailRet(trFunction->GetNativeCode(&trNativeCode));
    CORDB_ADDRESS nativeBaseAddress = 0;
    IfFailRet(trNativeCode->GetAddress(&nativeBaseAddress));
    if (nativeAddress < nativeBaseAddress)
    {
        return E_FAIL; // The address belongs to a different memory region
    }
    auto nativeOffset = static_cast<ULONG32>(nativeAddress - nativeBaseAddress);

    ULONG32 mapElementsCount = 0;
    IfFailRet(trNativeCode->GetILToNativeMapping(0, &mapElementsCount, nullptr));
    if (mapElementsCount == 0)
    {
        return E_FAIL; // Mapping data is unavailable (e.g., lightweight/dynamic methods)
    }

    std::vector<COR_DEBUG_IL_TO_NATIVE_MAP> mapping(mapElementsCount);
    IfFailRet(trNativeCode->GetILToNativeMapping(mapElementsCount, &mapElementsCount, mapping.data()));

    // Search the JIT map to find which IL offset corresponds to our native offset
    ilOffset = 0;

    for (ULONG32 i = 0; i < mapElementsCount; ++i)
    {
        const auto &entry = mapping.at(i);

        // Skip internal CLR runtime markers that do not map to real IL code:
        // - NO_MAPPING (0xffffffff): Code generated by CLR (GC checks, security blocks)
        // - PROLOG     (0xfffffffe): Method initialization code
        // - EPILOG     (0xfffffffd): Method return/cleanup code
        if (entry.ilOffset == static_cast<uint32_t>(NO_MAPPING) ||
            entry.ilOffset == static_cast<uint32_t>(PROLOG) ||
            entry.ilOffset == static_cast<uint32_t>(EPILOG))
        {
            continue;
        }

        // Exact match: The native address points precisely to the start of an instruction boundary
        if (entry.nativeStartOffset == nativeOffset)
        {
            ilOffset = entry.ilOffset;
            break;
        }

        // Closest match: The native address most likely points inside an assembly instruction sequence.
        // We keep track of the closest preceding boundary (the 'left' bound of the instruction sequence).
        if (entry.nativeStartOffset <= nativeOffset)
        {
            ilOffset = entry.ilOffset;
        }
    }

    return S_OK;
}

} // unnamed namespace

HRESULT WalkFrames(ICorDebugThread *pThread, DebugInfo *pDebugInfo, const WalkFramesCallback &cb)
{
    HRESULT Status = S_OK;

    auto exceptionStackTrace = [&]() -> HRESULT
    {
        if (!Threads::IsUnhandledExceptionStatus(pThread))
        {
            return S_OK;
        }

        ToRelease<ICorDebugValue> trExceptionValueRef;
        IfFailRet(pThread->GetCurrentException(&trExceptionValueRef));
        if (trExceptionValueRef == nullptr)
        {
            return E_FAIL;
        }

        ToRelease<ICorDebugValue> trExceptionValue;
        IfFailRet(DereferenceAndUnboxValue(trExceptionValueRef, &trExceptionValue));
        ToRelease<ICorDebugExceptionObjectValue> trExceptionObjectValue;
        IfFailRet(trExceptionValue->QueryInterface(IID_ICorDebugExceptionObjectValue, reinterpret_cast<void **>(&trExceptionObjectValue)));

        CorDebugExceptionObjectStackFrame exceptionObjectStackFrame;
        ULONG fetched = 0;
        ToRelease<ICorDebugExceptionObjectCallStackEnum> trExceptionObjectCallStackEnum;
        IfFailRet(trExceptionObjectValue->EnumerateExceptionCallStack(&trExceptionObjectCallStackEnum));

        int lastForeignExceptionFrameIndex = -1;
        int index = -1;
        while (SUCCEEDED(trExceptionObjectCallStackEnum->Next(1, &exceptionObjectStackFrame, &fetched)) && fetched == 1)
        {
            exceptionObjectStackFrame.pModule->Release();

            index++;
            if (exceptionObjectStackFrame.isLastForeignExceptionFrame == TRUE)
            {
                lastForeignExceptionFrameIndex = index;
            }
        }
        // Release the first enumerator before reusing the variable for a fresh enumeration.
        trExceptionObjectCallStackEnum.Free();

        // The exception thread was not changed (exception was not rethrown), normal unwind will work without issues.
        if (lastForeignExceptionFrameIndex == -1)
        {
            return S_OK;
        }

        IfFailRet(trExceptionObjectValue->EnumerateExceptionCallStack(&trExceptionObjectCallStackEnum));

        index = -1;
        while (SUCCEEDED(trExceptionObjectCallStackEnum->Next(1, &exceptionObjectStackFrame, &fetched)) && fetched == 1)
        {
            ToRelease<ICorDebugModule> trModule(exceptionObjectStackFrame.pModule);

            index++;
            // Don't unwind stack frames that belong to current thread, normal unwind will do this.
            if (lastForeignExceptionFrameIndex < index)
            {
                break;
            }

            CORDB_ADDRESS modAddress = 0;
            uint32_t ilOffset = 0;
            PDB::SequencePoint sequencePoint;
            std::string sourceFilePath;
            if (FAILED(trModule->GetBaseAddress(&modAddress)) ||
                FAILED(GetILOffsetFromNativeAddress(trModule, exceptionObjectStackFrame.methodDef, exceptionObjectStackFrame.ip, ilOffset)))
            {
                continue;
            }

            std::string methodName;
            if (FAILED(TypePrinter::GetFullyQualifiedMethodName(trModule, exceptionObjectStackFrame.methodDef, pDebugInfo, methodName)))
            {
                methodName = "Unnamed method in optimized code";
            }

            if (SUCCEEDED(pDebugInfo->GetSequencePointByILOffset(modAddress, exceptionObjectStackFrame.methodDef, ilOffset, sequencePoint)) &&
                SUCCEEDED(pDebugInfo->GetSourceFile({modAddress, sequencePoint.sourceFileIndex}, sourceFilePath)))
            {
                if (cb(FrameType::CLRManagedExceptionUser, nullptr, &sequencePoint, &methodName, &sourceFilePath) == S_CAN_EXIT)
                {
                    return S_CAN_EXIT;
                }
            }
            // Make sure that the last foreign exception frame itself is added to the stack trace.
            else if (exceptionObjectStackFrame.isLastForeignExceptionFrame == TRUE)
            {
                if (cb(FrameType::CLRManagedException, nullptr, nullptr, &methodName, nullptr) == S_CAN_EXIT)
                {
                    return S_CAN_EXIT;
                }
            }
        }

        return S_OK;
    };
    // Exception stack trace first, since foreign exception frames must be upper frames of the stack trace.
    if (exceptionStackTrace() == S_CAN_EXIT)
    {
        return S_OK;
    }

    // From https://github.com/SymbolSource/Microsoft.Samples.Debugging/blob/master/src/debugger/mdbgeng/FrameFactory.cs
    ToRelease<ICorDebugThread3> trThread3;
    IfFailRet(pThread->QueryInterface(IID_ICorDebugThread3, reinterpret_cast<void **>(&trThread3)));
    ToRelease<ICorDebugStackWalk> trStackWalk;
    IfFailRet(trThread3->CreateStackWalk(&trStackWalk));

    static constexpr uint32_t ctxFlags = static_cast<uint32_t>(CONTEXT_CONTROL) | static_cast<uint32_t>(CONTEXT_INTEGER);
    CONTEXT ctxUnmanagedChain;
    bool ctxUnmanagedChainValid = false;
    CONTEXT currentCtx;
    uint32_t contextSize = 0;
    memset(reinterpret_cast<void *>(&ctxUnmanagedChain), 0, sizeof(CONTEXT));
    memset(reinterpret_cast<void *>(&currentCtx), 0, sizeof(CONTEXT));

    std::list<ToRelease<ICorDebugInternalFrame2>> trInternalFrames;
    GetActiveInternalFrames(trThread3, trInternalFrames);

    int level = -1;
    static constexpr bool firstFrame = true;

    for (Status = S_OK; ; Status = trStackWalk->Next())
    {
        if (Status == CORDBG_S_AT_END_OF_STACK ||
            FAILED(Status))
        {
            break;
        }

        level++;

        ToRelease<ICorDebugFrame> trFrame;
        if (FAILED(Status = trStackWalk->GetFrame(&trFrame)))
        {
            continue;
        }

        if (trFrame != nullptr && !trInternalFrames.empty())
        {
            BOOL isCloser = TRUE;
            for (auto it = trInternalFrames.begin(); it != trInternalFrames.end(); )
            {
                if (SUCCEEDED((*it)->IsCloserToLeaf(trFrame, &isCloser)) &&
                    isCloser == TRUE)
                {
                    ToRelease<ICorDebugFrame> trIntFrame;
                    if (SUCCEEDED((*it)->QueryInterface(IID_ICorDebugInternalFrame, reinterpret_cast<void **>(&trIntFrame))) &&
                        AllowInternalFrame(*it))
                    {
                        if (cb(FrameType::CLRInternal, trIntFrame, nullptr, nullptr, nullptr) == S_CAN_EXIT)
                        {
                            return S_OK;
                        }
                    }
                    it = trInternalFrames.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        if (Status == S_FALSE) // S_FALSE - The current frame is a native stack frame.
        {
            // We've hit a native frame, we need to store the CONTEXT
            memset(reinterpret_cast<void *>(&ctxUnmanagedChain), 0, sizeof(CONTEXT));
            if (FAILED(trStackWalk->GetContext(ctxFlags, sizeof(CONTEXT), &contextSize, reinterpret_cast<uint8_t *>(&ctxUnmanagedChain))))
            {
                memset(reinterpret_cast<void *>(&ctxUnmanagedChain), 0, sizeof(CONTEXT));
            }
            ctxUnmanagedChainValid = true;
            continue;
        }

        // At this point (Status == S_OK).
        // According to CoreCLR sources, S_OK could be with nulled trFrame, that must be skipped.
        // Related to `FrameType::kExplicitFrame` in runtime (skipped frame function with no-frame transition is represented)
        if (trFrame == nullptr)
        {
            continue;
        }

        // If we get a RuntimeUnwindableFrame, then the stackwalker is also stopped at a native
        // stack frame, but it's a native stack frame which requires special unwinding help from
        // the runtime. When a debugger gets a RuntimeUnwindableFrame, it should use the runtime
        // to unwind, but it has to do inspection on its own. It can call
        // ICorDebugStackWalk::GetContext() to retrieve the context of the native stack frame.
        ToRelease<ICorDebugRuntimeUnwindableFrame> trRuntimeUnwindableFrame;
        if (SUCCEEDED(trFrame->QueryInterface(IID_ICorDebugRuntimeUnwindableFrame, reinterpret_cast<void **>(&trRuntimeUnwindableFrame))))
        {
            continue;
        }

        // We need to store the CONTEXT when we're at a managed frame.
        memset(reinterpret_cast<void *>(&currentCtx), 0, sizeof(CONTEXT));
        if (FAILED(trStackWalk->GetContext(ctxFlags, sizeof(CONTEXT), &contextSize, reinterpret_cast<uint8_t *>(&currentCtx))))
        {
            memset(reinterpret_cast<void *>(&currentCtx), 0, sizeof(CONTEXT));
        }
        // Note, we don't change top managed frame FP in case we don't have SP (for example, registers context related issue)
        // or CoreCLR was able to restore it. This case could happen only with "managed" top frame (`GetFrame()` return `S_OK`),
        // where real top frame is native (for example, optimized managed code with inlined pinvoke or CoreCLR native frame).
        if (level == 0 && GetSP(&currentCtx) != 0 && GetFP(&currentCtx) == 0)
        {
            SetFP(&currentCtx, GetSP(&currentCtx));
            trStackWalk->SetContext(SET_CONTEXT_FLAG_UNWIND_FRAME, sizeof(CONTEXT), reinterpret_cast<uint8_t *>(&currentCtx));
        }

        // Check if we have native frames to unwind
        if (ctxUnmanagedChainValid)
        {
            UnwindNativeFrames(pThread, !firstFrame, &ctxUnmanagedChain, &currentCtx, cb);
            level++;
            // Clear out the CONTEXT
            memset(reinterpret_cast<void *>(&ctxUnmanagedChain), 0, sizeof(CONTEXT));
            ctxUnmanagedChainValid = false;
        }

        // Return the managed frame
        ToRelease<ICorDebugFunction> trFunction;
        if (SUCCEEDED(trFrame->GetFunction(&trFunction)))
        {
            ToRelease<ICorDebugILFrame> trILFrame;
            uint32_t nOffset = 0;
            CorDebugMappingResult mappingResult = MAPPING_NO_INFO;
            if (SUCCEEDED(trFrame->QueryInterface(IID_ICorDebugILFrame, reinterpret_cast<void **>(&trILFrame))) &&
                SUCCEEDED(trILFrame->GetIP(&nOffset, &mappingResult)))
            {
                if (mappingResult == MAPPING_UNMAPPED_ADDRESS ||
                    mappingResult == MAPPING_NO_INFO)
                {
                    // Related to ICorDebugInternalFrame, ignore.
                    continue;
                }

                // Hide state machine related frames for both JMC and non-JMC cases.
                std::string methodName;
                if ((pDebugInfo != nullptr && pDebugInfo->IsStateMachineKickoffMethod(trFunction)) ||
                    (pDebugInfo != nullptr && SUCCEEDED(TypePrinter::GetFullyQualifiedMethodName(trFrame, pDebugInfo, methodName)) &&
                     // Note: starts_with() is C++20, use rfind() for compatibility
                     (methodName.rfind("System.Runtime.CompilerServices.AsyncMethodBuilderCore", 0) == 0 ||
                      methodName.rfind("System.Runtime.CompilerServices.AsyncTaskMethodBuilder", 0) == 0)))
                {
                    continue;
                }

                if (cb(FrameType::CLRManaged, trFrame, nullptr, nullptr, nullptr) == S_CAN_EXIT)
                {
                    return S_OK;
                }
            }
            else
            {
                if (cb(FrameType::Unknown, trFrame, nullptr, nullptr, nullptr) == S_CAN_EXIT)
                {
                    return S_OK;
                }
            }
            continue;
        }

        ToRelease<ICorDebugNativeFrame> trNativeFrame;
        if (FAILED(trFrame->QueryInterface(IID_ICorDebugNativeFrame, reinterpret_cast<void **>(&trNativeFrame))))
        {
            if (cb(FrameType::Unknown, trFrame, nullptr, nullptr, nullptr) == S_CAN_EXIT)
            {
                return S_OK;
            }
            continue;
        }
        // If the first frame is CoreCLR native frame then we might be in a call to unmanaged code.
        // Note, in case of starting unwinding from native code we get CoreCLR native frame first, not a native frame at the top,
        // since CoreCLR debug API doesn't track native code execution and doesn't really "see" native code at the beginning of unwinding.
        if (level == 0)
        {
            UnwindNativeFrames(pThread, firstFrame, nullptr, &currentCtx, cb);
        }
        if (cb(FrameType::CLRNative, nullptr, nullptr, nullptr, nullptr) == S_CAN_EXIT)
        {
            return S_OK;
        }
    }

    for (const auto &trIntFrame2 : trInternalFrames)
    {
        ToRelease<ICorDebugFrame> trIntFrame;
        if (SUCCEEDED(trIntFrame2->QueryInterface(IID_ICorDebugInternalFrame, reinterpret_cast<void **>(&trIntFrame))) &&
            AllowInternalFrame(trIntFrame2))
        {
            if (cb(FrameType::CLRInternal, trIntFrame, nullptr, nullptr, nullptr) == S_CAN_EXIT)
            {
                return S_OK;
            }
        }
    }

    // We may have native frames at the end of the stack
    if (ctxUnmanagedChainValid)
    {
        if (level == 0) // in case this is first and last frame - unwind all
        {
            UnwindNativeFrames(pThread, firstFrame, nullptr, nullptr, cb);
        }
        else
        {
            UnwindNativeFrames(pThread, !firstFrame, &ctxUnmanagedChain, nullptr, cb);
        }
    }

    return S_OK;
}

HRESULT GetFrameAt(ICorDebugThread *pThread, FrameLevel level, DebugInfo *pDebugInfo, ICorDebugFrame **ppFrame)
{
    auto foreignExceptionFrameDetected = [&]() -> bool
    {
        if (!Threads::IsUnhandledExceptionStatus(pThread))
        {
            return false;
        }

        ToRelease<ICorDebugValue> trExceptionValueRef;
        if (FAILED(pThread->GetCurrentException(&trExceptionValueRef)) ||
            trExceptionValueRef == nullptr)
        {
            return false;
        }

        ToRelease<ICorDebugValue> trExceptionValue;
        ToRelease<ICorDebugExceptionObjectValue> trExceptionObjectValue;
        if (FAILED(DereferenceAndUnboxValue(trExceptionValueRef, &trExceptionValue)) ||
            FAILED(trExceptionValue->QueryInterface(IID_ICorDebugExceptionObjectValue, reinterpret_cast<void **>(&trExceptionObjectValue))))
        {
            return false;
        }

        CorDebugExceptionObjectStackFrame exceptionObjectStackFrame;
        ULONG fetched = 0;
        ToRelease<ICorDebugExceptionObjectCallStackEnum> trExceptionObjectCallStackEnum;
        if (FAILED(trExceptionObjectValue->EnumerateExceptionCallStack(&trExceptionObjectCallStackEnum)))
        {
            return false;
        }

        while (SUCCEEDED(trExceptionObjectCallStackEnum->Next(1, &exceptionObjectStackFrame, &fetched)) && fetched == 1)
        {
            exceptionObjectStackFrame.pModule->Release();

            if (exceptionObjectStackFrame.isLastForeignExceptionFrame == TRUE)
            {
                return true;
            }
        }

        return false;
    };

    // Try to get 0 (current active) frame in an efficient way, if possible.
    if (static_cast<int>(level) == 0 && !foreignExceptionFrameDetected() && SUCCEEDED(pThread->GetActiveFrame(ppFrame)) && *ppFrame != nullptr)
    {
        return S_OK;
    }

    int currentFrame = -1;

    WalkFrames(pThread, pDebugInfo,
        [&](FrameType frameType, ICorDebugFrame *pFrame, const PDB::SequencePoint *, const std::string *, const std::string *) -> HRESULT
        {
            currentFrame++;

            if (currentFrame < static_cast<int>(level))
            {
                return S_OK; // Continue walk.
            }
            else if (currentFrame > static_cast<int>(level) ||
                     frameType != FrameType::CLRManaged)
            {
                return S_CAN_EXIT; // Fast exit from loop.
            }

            pFrame->AddRef();
            *ppFrame = pFrame;
            return S_CAN_EXIT; // Fast exit from loop.
        });

    return *ppFrame != nullptr ? S_OK : E_FAIL;
}

const char *GetInternalTypeName(CorDebugInternalFrameType frameType)
{
    switch (frameType)
    {
    case STUBFRAME_M2U:
        return "Managed to Native Transition";
    case STUBFRAME_U2M:
        return "Native to Managed Transition";
    case STUBFRAME_APPDOMAIN_TRANSITION:
        return "Appdomain Transition";
    case STUBFRAME_LIGHTWEIGHT_FUNCTION:
        return "Lightweight function";
    case STUBFRAME_FUNC_EVAL:
        return "Func Eval";
    case STUBFRAME_INTERNALCALL:
        return "Internal Call";
    case STUBFRAME_CLASS_INIT:
        return "Class Init";
    case STUBFRAME_EXCEPTION:
        return "Exception";
    case STUBFRAME_SECURITY:
        return "Security";
    case STUBFRAME_JIT_COMPILATION:
        return "JIT Compilation";
    case STUBFRAME_NONE:
        return "Unknown";
    default:
        assert(false);
        return "Unknown";
    }
}

} // namespace dncdbg
