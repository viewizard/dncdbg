// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_DEBUGSOURCES_H
#define DEBUGINFO_DEBUGSOURCES_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "debuginfo/pdbreader.h"
#include <vector>

namespace dncdbg::DebugSources
{

HRESULT ResolveBreakpoints(const PDBInfo &pdbInfo, uint32_t sourceFileIndex, int sourceLine, std::vector<PDB::ResolvedBreakpoint> &resolvedPoints);
HRESULT FillMethodRanges(ICorDebugModule *pModule, mdhandle_t pdbHandle, std::vector<PDB::MethodRanges> &sourceMethodRanges);

} // namespace dncdbg::DebugSources

#endif // DEBUGINFO_DEBUGSOURCES_H
