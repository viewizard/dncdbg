// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef EXPRESSIONPARSER_HELPERS_H
#define EXPRESSIONPARSER_HELPERS_H

#include <cor.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dncdbg::Parser
{

HRESULT DetermineNumericTypeAndData(std::string_view text, bool realLiteral, CorElementType &elemType,
                                    std::vector<uint8_t> &data, std::string &output);

HRESULT ParsePredefinedType(std::string_view typeName, CorElementType &elemType, std::string &output);

} // namespace dncdbg::Parser

#endif // EXPRESSIONPARSER_HELPERS_H
