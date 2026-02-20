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

namespace
{

std::string GetThreadName(const std::shared_ptr<Evaluator> &sharedEvaluator, ICorDebugThread *pThread)
{
    assert(sharedEvaluator);

    std::string threadName = "<No name>";

    ToRelease<ICorDebugValue> trThreadObject;
    if (pThread == nullptr ||
        FAILED(pThread->GetObject(&trThreadObject)))
    {
        return threadName;
    }

    HRESULT Status = S_OK;
    sharedEvaluator->WalkMembers(trThreadObject, nullptr, FrameLevel{0}, nullptr, false,
        [&](ICorDebugType *, bool, const std::string &memberName,
            const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *)
        {
            // Note, only field here (not `Name` property), since we can't guarantee code execution (call property's getter),
            // this thread can be in not consistent state for evaluation or thread could break in optimized code.
            if (memberName != "_name")
            {
                return S_OK;
            }

            ToRelease<ICorDebugValue> trResultValue;
            IfFailRet(getValue(&trResultValue, true));

            BOOL isNull = TRUE;
            ToRelease<ICorDebugValue> trValue;
            IfFailRet(DereferenceAndUnboxValue(trResultValue, &trValue, &isNull));
            if (isNull == FALSE)
            {
                IfFailRet(PrintStringValue(trValue, threadName));
            }

            return E_ABORT; // Fast exit from cycle.
        });

    return threadName;
}

} // unnamed namespace

ThreadId getThreadId(ICorDebugThread *pThread)
{
    DWORD threadId = 0; // invalid value for Win32
    const HRESULT res = pThread->GetID(&threadId);
    return SUCCEEDED(res) && threadId != 0 ? ThreadId{threadId} : ThreadId{};
}

void Threads::Add(const std::shared_ptr<Evaluator> &sharedEvaluator, ICorDebugThread *pThread, const ThreadId &threadId, bool processAttached)
{
    const WriteLock w_lock(m_userThreadsRWLock);

    const std::string threadName = GetThreadName(sharedEvaluator, pThread);

    // First added user thread during start is Main thread for sure.
    if (!processAttached && !MainThread)
    {
        MainThread = threadId;
        if (threadName == "<No name>")
        {
            m_userThreads.insert({threadId, "Main Thread"});
            return;
        }
    }

    m_userThreads.insert({threadId, threadName});
}

void Threads::ChangeName(const std::shared_ptr<Evaluator> &sharedEvaluator, ICorDebugThread *pThread)
{
    if (pThread == nullptr)
    {
        return;
    }

    const WriteLock w_lock(m_userThreadsRWLock);

    const std::string threadName = GetThreadName(sharedEvaluator, pThread);
    const ThreadId threadId(getThreadId(pThread));
    m_userThreads[threadId] = threadName;
}

void Threads::Remove(const ThreadId &threadId)
{
    const WriteLock w_lock(m_userThreadsRWLock);

    auto it = m_userThreads.find(threadId);
    if (it == m_userThreads.end())
    {
        return;
    }

    m_userThreads.erase(it);
}

// Caller should guarantee, that pProcess is not null.
HRESULT Threads::GetThreadsWithState(ICorDebugProcess *pProcess, std::vector<Thread> &threads)
{
    const ReadLock r_lock(m_userThreadsRWLock);

    HRESULT Status = S_OK;
    BOOL procRunning = FALSE;
    IfFailRet(pProcess->IsRunning(&procRunning));

    threads.reserve(m_userThreads.size());
    for (const auto &userThread : m_userThreads)
    {
        // ICorDebugThread::GetUserState not available for running thread.
        threads.emplace_back(userThread.first, userThread.second, procRunning == TRUE);
    }

    return S_OK;
}

HRESULT Threads::GetThreadIds(std::vector<ThreadId> &threads)
{
    const ReadLock r_lock(m_userThreadsRWLock);

    threads.reserve(m_userThreads.size());
    for (const auto &userThread : m_userThreads)
    {
        threads.emplace_back(userThread.first);
    }
    return S_OK;
}

} // namespace dncdbg
