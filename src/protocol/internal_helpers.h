// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef PROTOCOL_INTERNAL_HELPERS_H
#define PROTOCOL_INTERNAL_HELPERS_H

#include "types/protocol.h"
#include <json/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

namespace dncdbg::DAP
{

constexpr std::string_view TWO_CRLF("\r\n\r\n");
constexpr std::string_view CONTENT_LENGTH("Content-Length: ");
constexpr std::string_view LOG_COMMAND("-> (C) ");
constexpr std::string_view LOG_RESPONSE("<- (R) ");
constexpr std::string_view LOG_EVENT("<- (E) ");

const std::unordered_map<std::string, ExceptionBreakpointFilter> &GetExceptionFilters();
void AddCapabilitiesTo(nlohmann::json &capabilities);

void SetupProtocolLoggingInternal(const std::string &path);
void EmitEvent(const std::string &name, const nlohmann::json &body);
void EmitMessageWithLog(std::string_view message_prefix, nlohmann::json &message);
void Log(std::string_view prefix, const std::string &text);

} // namespace dncdbg::DAP

#endif // PROTOCOL_INTERNAL_HELPERS_H
