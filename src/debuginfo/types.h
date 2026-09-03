// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGINFO_TYPES_H
#define DEBUGINFO_TYPES_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

namespace dncdbg
{

struct GotoTargetInternal
{
    uint32_t id{0};
    CORDB_ADDRESS modAddress{0};
    mdMethodDef methodToken{0};
    uint32_t ilOffset{0};
};

} // namespace dncdbg

#endif // DEBUGINFO_TYPES_H
