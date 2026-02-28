// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/frames.h"
#include "utils/torelease.h"
#include <list>
#include <vector>

namespace dncdbg
{

namespace
{

uintptr_t GetSP(CONTEXT *context)
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
#error "Unsupported platform"
#endif
}

uintptr_t GetFP(CONTEXT *context)
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
#error "Unsupported platform"
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
#error "Unsupported platform"
#endif
}

void UnwindNativeFrames(ICorDebugThread */*pThread*/, bool /*firstFrame*/, CONTEXT */*pStartContext*/,
                                  CONTEXT */*pEndContext*/, const WalkFramesCallback &/*cb*/)
{
    // In case not interop build we merge "CoreCLR native frame" and "user's native frame" into "[Native Frames]".
}

HRESULT GetActiveInternalFrames(const ToRelease<ICorDebugThread3> &trThread3, std::list<ToRelease<ICorDebugInternalFrame2>> &trInternalFrames)
{
    HRESULT Status = S_OK;
    uint32_t cInternalFrames = 0;
    IfFailRet(trThread3->GetActiveInternalFrames(0, &cInternalFrames, nullptr));

    uint32_t fetchedFrames = 0;
    std::vector<ICorDebugInternalFrame2*> pInternalFrames(cInternalFrames);
    if (SUCCEEDED(trThread3->GetActiveInternalFrames(cInternalFrames, &fetchedFrames, pInternalFrames.data())) &&
        fetchedFrames == cInternalFrames)
    {
        for (const auto &entry : pInternalFrames)
        {
            trInternalFrames.emplace_back(entry);
        }
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
    case STUBFRAME_EXCEPTION: // no reason add `[Exception]` frame on the top of stacktrace
    case STUBFRAME_NONE:
    default:
        return false;
    }
}

} // unnamed namespace

// From https://github.com/SymbolSource/Microsoft.Samples.Debugging/blob/master/src/debugger/mdbgeng/FrameFactory.cs
HRESULT WalkFrames(ICorDebugThread *pThread, const WalkFramesCallback &cb)
{
    HRESULT Status = S_OK;

    ToRelease<ICorDebugThread3> trThread3;
    IfFailRet(pThread->QueryInterface(IID_ICorDebugThread3, reinterpret_cast<void **>(&trThread3)));
    ToRelease<ICorDebugStackWalk> trStackWalk;
    IfFailRet(trThread3->CreateStackWalk(&trStackWalk));

    static constexpr uint32_t ctxFlags = static_cast<uint32_t>(CONTEXT_CONTROL) | static_cast<uint32_t>(CONTEXT_INTEGER);
    CONTEXT ctxUnmanagedChain;
    bool ctxUnmanagedChainValid = false;
    CONTEXT currentCtx;
    uint32_t contextSize = 0;
    memset((void *)(&ctxUnmanagedChain), 0, sizeof(CONTEXT));
    memset((void *)&currentCtx, 0, sizeof(CONTEXT));

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
                        if (cb(FrameType::CLRInternal, trIntFrame) == S_FALSE)
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
            memset((void *)&ctxUnmanagedChain, 0, sizeof(CONTEXT));
            if (FAILED(trStackWalk->GetContext(ctxFlags, sizeof(CONTEXT), &contextSize, reinterpret_cast<uint8_t *>(&ctxUnmanagedChain))))
            {
                memset((void *)&ctxUnmanagedChain, 0, sizeof(CONTEXT));
            }
            ctxUnmanagedChainValid = true;
            continue;
        }

        // At this point (Status == S_OK).
        // Accordingly to CoreCLR sources, S_OK could be with nulled trFrame, that must be skipped.
        // Related to `FrameType::kExplicitFrame` in runtime (skipped frame function with no-frame transition represents)
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
        memset((void *)&currentCtx, 0, sizeof(CONTEXT));
        if (FAILED(trStackWalk->GetContext(ctxFlags, sizeof(CONTEXT), &contextSize, reinterpret_cast<uint8_t *>(&currentCtx))))
        {
            memset((void *)&currentCtx, 0, sizeof(CONTEXT));
        }
        // Note, we don't change top managed frame FP in case we don't have SP (for example, registers context related issue)
        // or CoreCLR was able to restore it. This case could happens only with "managed" top frame (`GetFrame()` return `S_OK`),
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
            memset((void *)&ctxUnmanagedChain, 0, sizeof(CONTEXT));
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

                if (cb(FrameType::CLRManaged, trFrame) == S_FALSE)
                {
                    return S_OK;
                }
            }
            else
            {
                if (cb(FrameType::Unknown, trFrame) == S_FALSE)
                {
                    return S_OK;
                }
            }
            continue;
        }

        ToRelease<ICorDebugNativeFrame> trNativeFrame;
        if (FAILED(trFrame->QueryInterface(IID_ICorDebugNativeFrame, reinterpret_cast<void **>(&trNativeFrame))))
        {
            if (cb(FrameType::Unknown, trFrame) == S_FALSE)
            {
                return S_OK;
            }
            continue;
        }
        // If the first frame is CoreCLR native frame then we might be in a call to unmanaged code.
        // Note, in case start unwinding from native code we get CoreCLR native frame first, not some native frame at the top,
        // since CoreCLR debug API don't track native code execution and don't really "see" native code at the beginning of unwinding.
        if (level == 0)
        {
            UnwindNativeFrames(pThread, firstFrame, nullptr, &currentCtx, cb);
        }
        if (cb(FrameType::CLRNative, nullptr) == S_FALSE)
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
            if (cb(FrameType::CLRInternal, trIntFrame) == S_FALSE)
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

HRESULT GetFrameAt(ICorDebugThread *pThread, FrameLevel level, ICorDebugFrame **ppFrame)
{
    // Try get 0 (current active) frame in fast way, if possible.
    if (static_cast<int>(level) == 0 && SUCCEEDED(pThread->GetActiveFrame(ppFrame)) && *ppFrame != nullptr)
    {
        return S_OK;
    }

    int currentFrame = -1;

    WalkFrames(pThread,
        [&](FrameType frameType, ICorDebugFrame *pFrame) -> HRESULT
        {
            currentFrame++;

            if (currentFrame < static_cast<int>(level))
            {
                return S_OK; // Continue walk.
            }
            else if (currentFrame > static_cast<int>(level) ||
                    frameType != FrameType::CLRManaged)
            {
                return S_FALSE; // Fast exit from cycle.
            }

            pFrame->AddRef();
            *ppFrame = pFrame;
            return S_FALSE; // Fast exit from cycle.
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
    default:
        return "Unknown";
    }
}

} // namespace dncdbg
