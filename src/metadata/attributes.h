// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include <cor.h>
#include <cordebug.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <specstrings_undef.h>
#endif

#include <string>
#include <vector>

namespace dncdbg
{

struct DebuggerAttribute
{
    static const char NonUserCode[];
    static const char StepThrough[];
    static const char Hidden[];
};

bool HasAttribute(IMetaDataImport *pMD, mdToken tok, const char *attrName);
bool HasAttribute(IMetaDataImport *pMD, mdToken tok, std::vector<std::string> &attrNames);

} // namespace dncdbg
