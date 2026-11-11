// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef PROTOCOL_INTERNAL_HELPERS_H
#define PROTOCOL_INTERNAL_HELPERS_H

#include "types/protocol.h"
#include <json/json.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace dncdbg::DAP
{

inline constexpr std::string_view TWO_CRLF("\r\n\r\n");
inline constexpr std::string_view CONTENT_LENGTH("Content-Length: ");
inline constexpr std::string_view LOG_COMMAND("-> (C) ");
inline constexpr std::string_view LOG_RESPONSE("<- (R) ");
inline constexpr std::string_view LOG_EVENT("<- (E) ");

// An exception filter accepted by the `setExceptionBreakpoints` request.
struct ExceptionFilterEntry
{
    std::string_view name;
    ExceptionBreakpointFilter filter;
};

// Exception filters supported by the debugger. Sorted by name (ASCII) for binary-search
// lookup; the array order also defines the order of the `exceptionBreakpointFilters`
// array in the capabilities advertised to the client.
inline constexpr std::array<ExceptionFilterEntry, 3> gExceptionFilters{{
    {"all", ExceptionBreakpointFilter::THROW},
    {"unhandled", ExceptionBreakpointFilter::UNHANDLED},
    {"user-unhandled", ExceptionBreakpointFilter::USER_UNHANDLED}
}};

// Binary search in a name-sorted array of `std::string_view` entries (ASCII order).
template <std::size_t Size>
[[nodiscard]] bool ContainsSortedName(const std::array<std::string_view, Size> &sortedNames,
                                      std::string_view name)
{
    const auto entryIt = std::lower_bound(sortedNames.cbegin(), sortedNames.cend(), name);
    return entryIt != sortedNames.cend() && *entryIt == name;
}

void AddCapabilitiesTo(nlohmann::json &capabilities);

void SetupProtocolLoggingInternal(const std::string &path);
void EmitEvent(std::string_view name, nlohmann::json &&body);
void EmitMessageWithLog(std::string_view messagePrefix, nlohmann::json &message);
void Log(std::string_view prefix, const std::string &text);

// Rebase internal line/column numbers (the internal numbering is always 1-based) into the
// numbering base the client requested in the `initialize` request. Zero means an unknown
// location and is returned unchanged.
int32_t ToClientLine(int32_t line);
int32_t ToClientColumn(int32_t column);

// Inverse of ToClientLine(): convert a line provided by the client into the internal 1-based
// numbering. For a 0-based client the value shifts by one.
int32_t ToInternalLine(int32_t line);

} // namespace dncdbg::DAP

#endif // PROTOCOL_INTERNAL_HELPERS_H
