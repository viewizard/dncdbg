// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_GOTO_H
#define DEBUGGER_GOTO_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/types.h"
#include "types/protocol.h"
#include <vector>
#include <string>

namespace dncdbg::Goto
{

struct TargetInternal
{
    uint32_t id{0};
    CORDB_ADDRESS modAddress{0};
    mdMethodDef methodToken{0};
    uint32_t ilOffset{0};
};

HRESULT GetTarget(const Source &source, int32_t line, int32_t column, std::vector<GotoTarget> &targets,
                  std::vector<TargetInternal> &intTargets, std::string &output);

// Cleans up the Goto internal state. See Cleanup() in manageddebugger.cpp.
void Cleanup();

} // namespace dncdbg::Goto

#endif // DEBUGGER_GOTO_H
