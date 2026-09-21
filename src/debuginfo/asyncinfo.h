// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_ASYNCINFO_H
#define DEBUGINFO_ASYNCINFO_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debuginfo/pdb.h"
#include <cstdint>

namespace dncdbg::AsyncInfo
{

// Check if the method has an await block; this is how we detect async methods with awaits.
bool IsMethodHaveAwait(CORDB_ADDRESS modAddress, mdMethodDef methodToken);

// Find an await block after the IL offset in a particular async method and return the await info, if present.
// For async stepping, we need await info from the PDB to set up breakpoints in the proper places (yield and resume offsets).
bool FindNextAwaitInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t ipOffset, PDB::AsyncAwaitInfoBlock &awaitInfo);

// Find the last IL offset for user code in an async method, if present.
// For step-in and step-over, we must detect the last user code line in order to "emulate"
// step-out (NotifyDebuggerOfWaitCompletion magic) instead.
bool FindLastIlOffsetAwaitInfo(CORDB_ADDRESS modAddress, mdMethodDef methodToken, uint32_t &lastIlOffset);

// Cleans up the AsyncInfo internal state. See DebugInfo::Cleanup().
void Cleanup();

} // namespace dncdbg::AsyncInfo

#endif // DEBUGINFO_ASYNCINFO_H
