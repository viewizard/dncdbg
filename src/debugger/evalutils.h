// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUTILS_H
#define DEBUGGER_EVALUTILS_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include <string>
#include <vector>

namespace dncdbg
{

class DebugInfo;

} // namespace dncdbg

namespace dncdbg::EvalUtils
{

std::vector<std::string> ParseType(const std::string &expression, std::vector<int> &ranks);
HRESULT FindType(const std::vector<std::string> &identifiers, int &nextIdentifier, ICorDebugThread *pThread,
                 ICorDebugModule *pModule, ICorDebugType **ppType, ICorDebugModule **ppModule = nullptr);
std::vector<std::string> ParseGenericParams(const std::string &identifier, std::string &typeName);

} // namespace dncdbg::EvalUtils

#endif // DEBUGGER_EVALUTILS_H
