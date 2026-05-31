// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef EXPRESSIONPARSER_HELPERS_H
#define EXPRESSIONPARSER_HELPERS_H

#include <cor.h>
#include <string>
#include <vector>

namespace dncdbg::Parser
{

HRESULT DetermineNumericTypeAndData(const std::string &text, bool realLiteral, CorElementType &type,
                                    std::vector<uint8_t> &data, std::string &output);

} // namespace dncdbg::Parser

#endif // EXPRESSIONPARSER_HELPERS_H
