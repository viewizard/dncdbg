// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_EVALHELPERS_DEBUGINFO_H
#define DEBUGGER_EVALUATION_EVALHELPERS_DEBUGINFO_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debuginfo/pdb.h"
#include "types/types.h"

namespace dncdbg::EvalDebugInfoHelpers
{

void GetImportsAndAliases(ICorDebugThread *pThread, FrameLevel frameLevel, PDB::ImportsAndAliases &pdbImports);

} // namespace dncdbg::EvalDebugInfoHelpers

#endif // DEBUGGER_EVALUATION_EVALHELPERS_DEBUGINFO_H
