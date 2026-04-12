// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_BREAKPOINTS_BREAKPOINTS_EXCEPTION_H
#define DEBUGGER_BREAKPOINTS_BREAKPOINTS_EXCEPTION_H

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "types/protocol.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dncdbg
{

class Evaluator;

class ExceptionBreakpoints
{
  public:

    ExceptionBreakpoints(std::shared_ptr<Evaluator> &sharedEvaluator)
        : m_sharedEvaluator(sharedEvaluator),
          m_exceptionBreakpoints(static_cast<size_t>(ExceptionBreakpointFilter::Size))
    {
    }

    void SetJustMyCode(bool enable)
    {
        m_justMyCode = enable;
    };
    void DeleteAll();
    HRESULT SetExceptionBreakpoints(const std::vector<ExceptionBreakpoint> &exceptionBreakpoints,
                                    std::vector<Breakpoint> &breakpoints, const std::function<uint32_t()> &getId);
    HRESULT GetExceptionInfo(ICorDebugThread *pThread, ExceptionInfo &exceptionInfo);
    bool CoveredByFilter(ExceptionBreakpointFilter filterId, const std::string &excType, ExceptionCategory excCategory);

    // Important! Callbacks related methods must control return for succeeded return code.
    // Do not allow debugger API return succeeded (uncontrolled) return code.
    // Bad :
    //     return pThread->GetID(&threadId);
    // Good:
    //     IfFailRet(pThread->GetID(&threadId));
    //     return S_OK;
    HRESULT ManagedCallbackException(ICorDebugThread *pThread, ExceptionCallbackType eventType);
    HRESULT ManagedCallbackExitThread(ICorDebugThread *pThread);

  private:

    std::shared_ptr<Evaluator> m_sharedEvaluator;
    bool m_justMyCode{true};

    std::mutex m_threadsExceptionMutex;
    std::unordered_map<DWORD, ExceptionCallbackType> m_threadsExceptionCallbackType;
    // Note: Exception callbacks are called with different exception callback types,
    // and we need to know the exception type related to the current stop event.
    std::unordered_map<DWORD, ExceptionBreakMode> m_threadsExceptionBreakMode;

    HRESULT GetExceptionDetails(ICorDebugThread *pThread, ICorDebugValue *pExceptionValue, ExceptionDetails *pDetails);

    struct ManagedExceptionBreakpoint
    {
        uint32_t id{0};
        ExceptionCategory categoryHint{ExceptionCategory::ANY};
        std::unordered_set<std::string> condition; // Note, only exception type related conditions allowed for now.
        bool negativeCondition{false};

        ManagedExceptionBreakpoint() = default;
        void ToBreakpoint(Breakpoint &breakpoint) const;

        ManagedExceptionBreakpoint(ManagedExceptionBreakpoint &&) = default;
        ManagedExceptionBreakpoint(const ManagedExceptionBreakpoint &) = delete;
        ManagedExceptionBreakpoint &operator=(ManagedExceptionBreakpoint &&) = default;
        ManagedExceptionBreakpoint &operator=(const ManagedExceptionBreakpoint &) = delete;
        ~ManagedExceptionBreakpoint() = default;
    };

    std::mutex m_breakpointsMutex;
    std::vector<std::unordered_multimap<size_t, ManagedExceptionBreakpoint>> m_exceptionBreakpoints;
};

} // namespace dncdbg

#endif // DEBUGGER_BREAKPOINTS_BREAKPOINTS_EXCEPTION_H
