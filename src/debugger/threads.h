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
#include <vector>

namespace dncdbg::Threads
{

ThreadId GetId(ICorDebugThread *pThread);

void SetLastStoppedThread(ICorDebugThread *pThread);
void SetLastStoppedThread(ICorDebugProcess *pProcess, ThreadId threadId);
void InvalidateLastStoppedThread();
ThreadId GetLastStoppedThreadId();

void Add(ICorDebugThread *pThread, const ThreadId &threadId);
void SetProcessAttached(bool state);
void ChangeName(ICorDebugThread *pThread);
void Remove(const ThreadId &threadId);
HRESULT GetThreads(std::vector<Thread> &threads);

// Cleans up the Threads internal state. See ManagedDebugger::Cleanup().
void Cleanup();

} // namespace dncdbg::Threads

#endif // DEBUGGER_THREADS_H
