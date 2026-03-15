// Copyright (c) 2026 Mikhail Kurinnoi
// See the LICENSE file in the project root for more information.

#ifndef UTILS__RWLOCK_H
#define UTILS__RWLOCK_H

#include <mutex>
#include <shared_mutex>

namespace dncdbg
{

using RWLock = std::shared_mutex;
using WriteLock = std::unique_lock<RWLock>;
using ReadLock = std::shared_lock<RWLock>;

} // namespace dncdbg

#endif // UTILS__RWLOCK_H
