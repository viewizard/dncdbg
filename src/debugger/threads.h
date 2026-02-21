// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "types/protocol.h"
#include "utils/rwlock.h"
#include <map>
#include <string>
#include <vector>

namespace dncdbg
{

class Evaluator;
ThreadId getThreadId(ICorDebugThread *pThread);

class Threads
{
  public:

    void Add(const std::shared_ptr<Evaluator> &sharedEvaluator, ICorDebugThread *pThread, const ThreadId &threadId, bool processAttached);
    void ChangeName(const std::shared_ptr<Evaluator> &sharedEvaluator, ICorDebugThread *pThread);
    void Remove(const ThreadId &threadId);
    HRESULT GetThreadsWithState(ICorDebugProcess *pProcess, std::vector<Thread> &threads);
    HRESULT GetThreadIds(std::vector<ThreadId> &threads);

  private:

    RWLock m_userThreadsRWLock;
    std::map<ThreadId, std::string> m_userThreads;
    ThreadId MainThread;
};

} // namespace dncdbg
