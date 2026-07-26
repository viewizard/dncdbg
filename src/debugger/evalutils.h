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
#include <type_traits>
#include <vector>

namespace dncdbg
{

class DebugInfo;
class Evaluator;
class EvalStackMachine;

enum class FormatSpecifier : uint16_t
{
    None                       = 0,
    ForceEvaluation            = 1 << 0,
    DecimalInteger             = 1 << 1,
    HexadecimalInteger         = 1 << 2,
    Dynamic                    = 1 << 3,
    EvaluatesWithNoSideEffects = 1 << 4,
    StringWithNoQuotes         = 1 << 5,
    DisplaysHiddenMembers      = 1 << 6,
    DisplaysInRawMode          = 1 << 7,
    Results                    = 1 << 8
};

inline FormatSpecifier operator | (FormatSpecifier lhs, FormatSpecifier rhs)
{
    using T = std::underlying_type_t<FormatSpecifier>;
    return static_cast<FormatSpecifier>(static_cast<T>(lhs) | static_cast<T>(rhs));
}

inline FormatSpecifier operator & (FormatSpecifier lhs, FormatSpecifier rhs)
{
    using T = std::underlying_type_t<FormatSpecifier>;
    return static_cast<FormatSpecifier>(static_cast<T>(lhs) & static_cast<T>(rhs));
}

} // namespace dncdbg

namespace dncdbg::EvalUtils
{

std::vector<std::string> ParseType(const std::string &expression, std::vector<int> &ranks);
HRESULT FindType(const std::vector<std::string> &identifiers, int &nextIdentifier, ICorDebugThread *pThread,
                 ICorDebugModule *pModule, ICorDebugType **ppType, ICorDebugModule **ppModule = nullptr);
std::vector<std::string> ParseGenericParams(const std::string &identifier, std::string &typeName);
void ParseFormatSpecifier(const std::string &expressionWithFormat, std::string &expression, FormatSpecifier &specifier);

void CreateTextWithEvalParts(const std::string &textWithEval, std::vector<std::pair<std::string, bool>> &textWithEvalParts);
void BuildTextWithEval(Evaluator *pEvaluator, EvalStackMachine *pEvalStackMachine, ICorDebugThread *pThread,
                       const std::vector<std::pair<std::string, bool>> &textWithEvalParts, std::string &output);

} // namespace dncdbg::EvalUtils

#endif // DEBUGGER_EVALUTILS_H
