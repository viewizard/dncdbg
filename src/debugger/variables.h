// Copyright (c) 2021-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_VARIABLES_H
#define DEBUGGER_VARIABLES_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include "types/protocol.h"

namespace dncdbg::Variables
{

// The caller must guarantee that pProcess is not null for every function that takes it.

HRESULT GetVariables(ICorDebugProcess *pProcess, uint32_t variablesReference, std::vector<Variable> &variables);

HRESULT SetVariable(ICorDebugProcess *pProcess, const std::string &name, const std::string &value, uint32_t ref,
                    std::string &output);

HRESULT SetExpression(ICorDebugProcess *pProcess, FrameId frameId, const std::string &expressionWithFormat,
                      const std::string &value, std::string &output);

HRESULT GetScopes(ICorDebugProcess *pProcess, FrameId frameId, std::vector<Scope> &scopes);

HRESULT Evaluate(ICorDebugProcess *pProcess, FrameId frameId, const std::string &expressionWithFormat,
                 Variable &variable, std::string &output);

void Cleanup();

} // namespace dncdbg::Variables

#endif // DEBUGGER_VARIABLES_H
