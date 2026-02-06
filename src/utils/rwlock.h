// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#pragma once

#include <mutex>
#include <shared_mutex>

namespace dncdbg
{

typedef std::shared_mutex RWLock;
typedef std::unique_lock<RWLock> WriteLock;
typedef std::shared_lock<RWLock> ReadLock;

} // namespace dncdbg
