// Copyright (c) 2022-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#pragma once

#include "cor.h"
#include "cordebug.h"
#include <unordered_set>
#include <string>

namespace dncdbg
{

class Modules;
class Evaluator;
class EvalHelpers;

namespace HotReloadHelpers
{

    // Call all ClearCache() and UpdateApplication() methods from UpdateHandlerTypes.
    HRESULT UpdateApplication(ICorDebugThread *pThread, Modules *pModules, Evaluator *pEvaluator, EvalHelpers *pEvalHelpers,
                              const std::string &updatedDLL, const std::unordered_set<mdTypeDef> &updatedTypeTokens);

} // namespace HotReloadHelpers

} // namespace dncdbg
