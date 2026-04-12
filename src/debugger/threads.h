// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_THREADS_H
#define DEBUGGER_THREADS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
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
    HRESULT GetThreads(std::vector<Thread> &threads);
    HRESULT GetThreadIds(std::vector<ThreadId> &threads);

  private:

    RWLock m_userThreadsRWLock;
    std::map<ThreadId, std::string> m_userThreads;
    ThreadId MainThread;
};

} // namespace dncdbg

#endif // DEBUGGER_THREADS_H
