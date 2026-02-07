// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/threads.h"
#include "debugger/evaluator.h"
#include "debugger/valueprint.h"
#include "utils/torelease.h"

namespace dncdbg
{

ThreadId getThreadId(ICorDebugThread *pThread)
{
    DWORD threadId = 0; // invalid value for Win32
    const HRESULT res = pThread->GetID(&threadId);
    return SUCCEEDED(res) && threadId != 0 ? ThreadId{threadId} : ThreadId{};
}

void Threads::Add(const ThreadId &threadId, bool processAttached)
{
    const WriteLock w_lock(m_userThreadsRWLock);

    m_userThreads.emplace(threadId);
    // First added user thread during start is Main thread for sure.
    if (!processAttached && !MainThread)
        MainThread = threadId;
}

void Threads::Remove(const ThreadId &threadId)
{
    const WriteLock w_lock(m_userThreadsRWLock);

    auto it = m_userThreads.find(threadId);
    if (it == m_userThreads.end())
        return;

    m_userThreads.erase(it);
}

std::string Threads::GetThreadName(ICorDebugProcess *pProcess, const ThreadId &userThread)
{
    std::string threadName = "<No name>";

    if (m_sharedEvaluator)
    {
        ToRelease<ICorDebugThread> pThread;
        ToRelease<ICorDebugValue> iCorThreadObject;
        if (SUCCEEDED(pProcess->GetThread(int(userThread), &pThread)) &&
            SUCCEEDED(pThread->GetObject(&iCorThreadObject)))
        {
            HRESULT Status = S_OK;
            m_sharedEvaluator->WalkMembers(iCorThreadObject, nullptr, FrameLevel{0}, nullptr, false,
                [&](ICorDebugType *, bool, const std::string &memberName,
                    const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *)
                {
                    // Note, only field here (not `Name` property), since we can't guarantee code execution (call property's getter),
                    // this thread can be in not consistent state for evaluation or thread could break in optimized code.
                    if (memberName != "_name")
                        return S_OK;

                    ToRelease<ICorDebugValue> iCorResultValue;
                    IfFailRet(getValue(&iCorResultValue, defaultEvalFlags));

                    BOOL isNull = TRUE;
                    ToRelease<ICorDebugValue> pValue;
                    IfFailRet(DereferenceAndUnboxValue(iCorResultValue, &pValue, &isNull));
                    if (!isNull)
                        IfFailRet(PrintStringValue(pValue, threadName));

                    return E_ABORT; // Fast exit from cycle.
                });
        }
    }

    if (MainThread && MainThread == userThread && threadName == "<No name>")
        return "Main Thread";

    return threadName;
}

// Caller should guarantee, that pProcess is not null.
HRESULT Threads::GetThreadsWithState(ICorDebugProcess *pProcess, std::vector<Thread> &threads)
{
    const ReadLock r_lock(m_userThreadsRWLock);

    HRESULT Status = S_OK;
    BOOL procRunning = FALSE;
    IfFailRet(pProcess->IsRunning(&procRunning));

    threads.reserve(m_userThreads.size());
    for (auto &userThread : m_userThreads)
    {
        // ICorDebugThread::GetUserState not available for running thread.
        threads.emplace_back(userThread, GetThreadName(pProcess, userThread), procRunning == TRUE);
    }

    return S_OK;
}

HRESULT Threads::GetThreadIds(std::vector<ThreadId> &threads)
{
    const ReadLock r_lock(m_userThreadsRWLock);

    threads.reserve(m_userThreads.size());
    for (auto &userThread : m_userThreads)
    {
        threads.emplace_back(userThread);
    }
    return S_OK;
}

void Threads::SetEvaluator(std::shared_ptr<Evaluator> &sharedEvaluator)
{
    m_sharedEvaluator = sharedEvaluator;
}

void Threads::ResetEvaluator()
{
    m_sharedEvaluator.reset();
}

} // namespace dncdbg
