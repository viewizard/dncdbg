// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/threads.h"
#include "debugger/evalhelpers.h"
#include "debugger/evaluator.h"
#include "debugger/valueprint.h"
#include "utils/hresult.h"
#include "utils/rwlock.h"
#include "utils/torelease.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <map>

namespace dncdbg
{

namespace
{

std::string GetThreadName(ICorDebugThread *pThread)
{
    std::string threadName = "<No name>";

    ToRelease<ICorDebugValue> trThreadObject;
    if (pThread == nullptr ||
        FAILED(pThread->GetObject(&trThreadObject)))
    {
        return threadName;
    }

    HRESULT Status = S_OK;
    Evaluator::WalkMembers(trThreadObject, pThread, FrameLevel{0}, false, FormatSpecifier::None,
        [&](ICorDebugType *, bool, const std::string &memberName,
            const Evaluator::GetValueCallback &getValue, Evaluator::SetterData *, std::string *) -> HRESULT
        {
            // Note, only field here (not `Name` property), since we can't guarantee code execution (call property's getter),
            // this thread can be in not consistent state for evaluation or thread could break in optimized code.
            if (memberName != "_name")
            {
                return S_OK;
            }

            ToRelease<ICorDebugValue> trResultValue;
            IfFailRet(getValue(&trResultValue, nullptr));

            BOOL isNull = TRUE;
            ToRelease<ICorDebugValue> trValue;
            IfFailRet(DereferenceAndUnboxValue(trResultValue, &trValue, &isNull));
            if (isNull == FALSE)
            {
                IfFailRet(PrintStringValue(trValue, threadName));
            }

            return S_CAN_EXIT; // Fast exit from the loop.
        });

    return threadName;
}

} // unnamed namespace

ThreadId GetThreadId(ICorDebugThread *pThread)
{
    DWORD threadId = 0; // invalid value for Win32
    const HRESULT res = pThread->GetID(&threadId);
    return SUCCEEDED(res) && threadId != 0 ? ThreadId{threadId} : ThreadId{};
}

namespace Threads
{

namespace
{

RWLock &GetUserThreadsRWLock()
{
    static RWLock userThreadsRWLock;
    return userThreadsRWLock;
}

std::map<ThreadId, std::string> &GetUserThreads()
{
    static std::map<ThreadId, std::string> userThreads;
    return userThreads;
}

ThreadId &GetMainThread()
{
    static ThreadId mainThread;
    return mainThread;
}

} // unnamed namespace

void Add(ICorDebugThread *pThread, const ThreadId &threadId, bool processAttached)
{
    const WriteLock w_lock(GetUserThreadsRWLock());

    const std::string threadName = GetThreadName(pThread);

    // The first user thread added during startup is the Main thread.
    if (!processAttached && !GetMainThread())
    {
        GetMainThread() = threadId;
        if (threadName == "<No name>")
        {
            GetUserThreads().emplace(threadId, "Main Thread");
            return;
        }
    }

    GetUserThreads().emplace(threadId, threadName);
}

void ChangeName(ICorDebugThread *pThread)
{
    if (pThread == nullptr)
    {
        return;
    }

    const WriteLock w_lock(GetUserThreadsRWLock());

    const std::string threadName = GetThreadName(pThread);
    const ThreadId threadId(GetThreadId(pThread));

    auto &userThreads = GetUserThreads();
    assert(userThreads.find(threadId) != userThreads.cend());
    userThreads.at(threadId) = threadName;
}

void Remove(const ThreadId &threadId)
{
    const WriteLock w_lock(GetUserThreadsRWLock());

    auto &userThreads = GetUserThreads();
    const auto it = userThreads.find(threadId);
    if (it == userThreads.cend())
    {
        return;
    }

    userThreads.erase(it);
}

HRESULT GetThreads(std::vector<Thread> &threads)
{
    const ReadLock r_lock(GetUserThreadsRWLock());

    const auto &userThreads = GetUserThreads();
    threads.reserve(userThreads.size());
    std::transform(userThreads.cbegin(), userThreads.cend(),
                   std::back_inserter(threads), [](const auto &userThread)
                   {
                       return Thread(userThread.first, userThread.second);
                   });

    return S_OK;
}

HRESULT GetThreadIds(std::vector<ThreadId> &threads)
{
    const ReadLock r_lock(GetUserThreadsRWLock());

    const auto &userThreads = GetUserThreads();
    threads.reserve(userThreads.size());
    std::transform(userThreads.cbegin(), userThreads.cend(),
                   std::back_inserter(threads), [](const auto &userThread)
                   {
                       return userThread.first;
                   });
    return S_OK;
}

// Cleans up the Threads internal state. See ManagedDebugger::Cleanup().
void Cleanup()
{
    const WriteLock w_lock(GetUserThreadsRWLock());

    GetUserThreads().clear();
    GetMainThread() = ThreadId{};
}

} // namespace dncdbg::Threads

} // namespace dncdbg
