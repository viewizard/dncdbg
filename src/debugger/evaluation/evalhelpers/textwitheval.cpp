// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/evalhelpers/textwitheval.h"
#include "debugger/evaluation/evalhelpers/formatspecifier.h"
#include "debugger/evaluation/stackmachine/stackmachine.h"
#include "debugger/valueprint.h"
#include "utils/torelease.h"
#include <algorithm>

namespace dncdbg::EvalTextHelpers
{

void CreateTextWithEvalParts(const std::string &textWithEval, std::vector<std::pair<std::string, bool>> &textWithEvalParts)
{
    // Each '{' yields at most 2 parts (literal text + expression); reserve the worst-case size
    // upfront, so the parts are built without vector reallocation.
    const size_t braceCount = static_cast<size_t>(std::count(textWithEval.cbegin(), textWithEval.cend(), '{'));
    textWithEvalParts.reserve(textWithEvalParts.size() + 2 * braceCount + 1);

    size_t pos = 0;
    size_t prevPos = 0;

    while ((pos = textWithEval.find('{', prevPos)) != std::string::npos)
    {
        // Add text before the '{' (if any) as literal text.
        if (pos > prevPos)
        {
            textWithEvalParts.emplace_back(textWithEval.substr(prevPos, pos - prevPos), false);
        }

        // Find the matching closing '}' by counting brace depth.
        size_t endPos = pos + 1;
        int braceDepth = 1;
        while (endPos < textWithEval.length() && braceDepth > 0)
        {
            if (textWithEval.at(endPos) == '{')
            {
                braceDepth++;
            }
            else if (textWithEval.at(endPos) == '}')
            {
                braceDepth--;
            }
            endPos++;
        }

        if (braceDepth > 0)
        {
            // No matching closing brace found; treat everything from '{' to the end as literal text.
            textWithEvalParts.emplace_back(textWithEval.substr(pos), false);
            prevPos = textWithEval.length();
            break;
        }

        // Add the expression inside braces (without the braces themselves) as an expression.
        // endPos points to the position right after the matching '}', so the expression is [pos+1, endPos-1).
        textWithEvalParts.emplace_back(textWithEval.substr(pos + 1, endPos - pos - 2), true);

        prevPos = endPos;
    }

    // Add the remaining text after the last '}' (or the entire string if no braces were found) as literal text.
    if (prevPos < textWithEval.length())
    {
        textWithEvalParts.emplace_back(textWithEval.substr(prevPos), false);
    }
}

void ApplyInterpolationFormats(std::vector<std::pair<std::string, bool>> &textWithEvalParts)
{
    for (auto &[text, isExpression] : textWithEvalParts)
    {
        if (!isExpression)
        {
            continue;
        }

        int depth = 0;
        char quote = 0;
        size_t colon = std::string::npos;
        for (size_t i = 0; i < text.size(); i++)
        {
            const char c = text.at(i);
            if (quote != 0)
            {
                if (c == '\\' && i + 1 < text.size())
                {
                    i++;
                }
                else if (c == quote)
                {
                    quote = 0;
                }
                continue;
            }

            if (c == '"' || c == '\'')
            {
                quote = c;
            }
            else if (c == '(' || c == '[' || c == '{')
            {
                depth++;
            }
            else if (c == ')' || c == ']' || c == '}')
            {
                depth--;
            }
            else if (c == ':' && depth == 0)
            {
                if (i + 1 < text.size() && text.at(i + 1) == ':')
                {
                    i++;
                    continue;
                }
                colon = i;
                break;
            }
        }

        if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size())
        {
            continue;
        }

        // Build the escaped format string in a single pre-reserved buffer (no substr copy);
        // every remaining character may be escaped, so reserve the exact worst-case size upfront.
        std::string format;
        format.reserve(2 * (text.size() - colon - 1));
        for (size_t i = colon + 1; i < text.size(); i++)
        {
            const char c = text.at(i);
            if (c == '"' || c == '\\')
            {
                format.push_back('\\');
            }
            format.push_back(c);
        }

        // Build the replacement in a single pre-reserved buffer (no substr or concatenation temporaries).
        // Overhead = the '(' prefix plus the lengths of ").ToString(\"" and "\"),nq"
        // (each "sizeof - 1" drops the trailing NUL).
        static constexpr size_t toStringOverhead = (sizeof("(") - 1) + (sizeof(").ToString(\"") - 1) + (sizeof("\"),nq") - 1);
        std::string result;
        result.reserve(colon + format.size() + toStringOverhead);
        result += '(';
        result.append(text, 0, colon);
        result += ").ToString(\"";
        result += format;
        result += "\"),nq";
        text = std::move(result);
    }
}

void BuildTextWithEval(ICorDebugThread *pThread, ICorDebugValue *pForcedThisValue,
                       const std::vector<std::pair<std::string, bool>> &textWithEvalParts, std::string &output)
{
    // Buffers declared outside the loop and cleared before each use so that their capacity is
    // reused (all callees assign to these strings, so clearing is safe).
    std::string expression;
    std::string value;
    std::string errorText;

    // Build the final output text by evaluating expressions.
    for (const auto &[text, isExpression] : textWithEvalParts)
    {
        if (!isExpression)
        {
            // Literal text - append directly.
            output += text;
        }
        else
        {
            // Expression - evaluate it.
            FormatSpecifier specifier = FormatSpecifier::None;
            expression.clear();
            EvalFormatSpecifiers::ParseFormatSpecifier(text, expression, specifier);

            value.clear();
            errorText.clear();
            ToRelease<ICorDebugValue> trResultValue;
            if (SUCCEEDED(EvalStackMachine::EvaluateExpression(pThread, FrameLevel{0}, expression,
                                                               pForcedThisValue == nullptr ? specifier : specifier | FormatSpecifier::DisplaysInRawMode,
                                                               pForcedThisValue, &trResultValue, nullptr, errorText)) &&
                SUCCEEDED(PrintValue(pThread, trResultValue, specifier, value)))
            {
                output += value;
            }
            else
            {
                if (!errorText.empty())
                {
                    // Append directly - no temporaries.
                    output += '{';
                    output += errorText;
                    output += '}';
                }
                else
                {
                    output += "{unknown error}";
                }
            }
        }
    }
}

} // namespace dncdbg::EvalTextHelpers
