// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/threads.h"
#include "debugger/breakpoints/breakpoints.h"
#include "debugger/evalhelpers.h"
#include "debugger/evaluation/walkers/walkers.h"
#include "debugger/valueprint.h"
#include "utils/hresult.h"
#include "utils/rwlock.h"
#include "utils/torelease.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <map>
#include <string>

namespace dncdbg::Threads
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
    Walkers::WalkMembers(trThreadObject, pThread, FrameLevel{0}, false, FormatSpecifier::None,
        [&](ICorDebugType *, bool, const std::string &memberName,
            const Walkers::GetValueCallback &getValue, Walkers::SetterData *, std::string *) -> HRESULT
        {
            // Note, only the field here (not the `Name` property), since we can't guarantee code execution (calling the property's getter):
            // this thread can be in an inconsistent state for evaluation, or the thread could have broken in optimized code.
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

std::mutex &GetLastStoppedThreadMutex()
{
    static std::mutex lastStoppedMutex;
    return lastStoppedMutex;
}

ThreadId &GetLastStoppedThreadIdState()
{
    static ThreadId lastStoppedThreadId{ThreadId::AllThreads};
    return lastStoppedThreadId;
}

bool &GetProcessAttached()
{
    static bool processAttached{false};
    return processAttached;
}

} // unnamed namespace

ThreadId GetId(ICorDebugThread *pThread)
{
    DWORD threadId = 0; // invalid value for Win32
    const HRESULT res = pThread->GetID(&threadId);
    return SUCCEEDED(res) && threadId != 0 ? ThreadId{threadId} : ThreadId{};
}

void SetLastStoppedThread(ICorDebugThread *pThread)
{
    Breakpoints::SetLastStoppedIlOffset(pThread);

    const std::scoped_lock<std::mutex> lock(GetLastStoppedThreadMutex());

    if (pThread != nullptr)
    {
        GetLastStoppedThreadIdState() = GetId(pThread);
    }
    else
    {
        GetLastStoppedThreadIdState() = ThreadId::AllThreads;
    }
}

void SetLastStoppedThread(ICorDebugProcess *pProcess, ThreadId threadId)
{
    // Must be real thread ID or ThreadId::AllThreads.
    assert(threadId);

    ToRelease<ICorDebugThread> trThread;
    if (threadId != ThreadId::AllThreads && pProcess != nullptr)
    {
        pProcess->GetThread(static_cast<int>(threadId), &trThread);
    }

    SetLastStoppedThread(trThread);
}

void InvalidateLastStoppedThread()
{
    SetLastStoppedThread(nullptr);
}

ThreadId GetLastStoppedThreadId()
{
    const std::scoped_lock<std::mutex> lock(GetLastStoppedThreadMutex());
    return GetLastStoppedThreadIdState();
}

void Add(ICorDebugThread *pThread, const ThreadId &threadId)
{
    const WriteLock w_lock(GetUserThreadsRWLock());

    const std::string threadName = GetThreadName(pThread);

    // The first user thread added during startup is the Main thread.
    if (!GetProcessAttached() && !GetMainThread())
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

void SetProcessAttached(bool state)
{
    const WriteLock w_lock(GetUserThreadsRWLock());
    GetProcessAttached() = state;
}

void ChangeName(ICorDebugThread *pThread)
{
    if (pThread == nullptr)
    {
        return;
    }

    const WriteLock w_lock(GetUserThreadsRWLock());

    const std::string threadName = GetThreadName(pThread);
    const ThreadId threadId(Threads::GetId(pThread));

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

// Cleans up the Threads internal state. See Cleanup() in manageddebugger.cpp.
void Cleanup()
{
    {
        const WriteLock w_lock(GetUserThreadsRWLock());

        GetUserThreads().clear();
        GetMainThread() = ThreadId{};
        // Note, GetProcessAttached() is not reset here: it is set by ManagedDebugger::Attach()/Launch() for each debug session.
    }

    {
        const std::scoped_lock<std::mutex> lock(GetLastStoppedThreadMutex());
        GetLastStoppedThreadIdState() = ThreadId::AllThreads;
    }
}

} // namespace dncdbg::Threads
