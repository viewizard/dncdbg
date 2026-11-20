// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_EVALHELPERS_TEXTWITHEVAL_H
#define DEBUGGER_EVALUATION_EVALHELPERS_TEXTWITHEVAL_H

#include <cor.h>
#include <cordebug.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include <string>
#include <utility>
#include <vector>

namespace dncdbg::EvalTextHelpers
{

// Split text containing `{expression}` parts into pairs of literal text and expression parts.
void CreateTextWithEvalParts(const std::string &textWithEval, std::vector<std::pair<std::string, bool>> &textWithEvalParts);

// Rewrite C# interpolation format strings in the expression parts (`{value:format}`) into
// `(value).ToString("format"),nq`, as in an interpolated string. A conditional operator must be
// parenthesized, the same as in C#.
void ApplyInterpolationFormats(std::vector<std::pair<std::string, bool>> &textWithEvalParts);

// Build the final text by evaluating the expression parts (with format specifiers) and
// appending the literal text parts as is.
void BuildTextWithEval(ICorDebugThread *pThread, ICorDebugValue *pForcedThisValue,
                       const std::vector<std::pair<std::string, bool>> &textWithEvalParts, std::string &output);

} // namespace dncdbg::EvalTextHelpers

#endif // DEBUGGER_EVALUATION_EVALHELPERS_TEXTWITHEVAL_H
