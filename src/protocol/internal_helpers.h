// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef PROTOCOL_INTERNAL_HELPERS_H
#define PROTOCOL_INTERNAL_HELPERS_H

#include "types/protocol.h"
#include <json/json.hpp>
#include <string_view>

namespace dncdbg
{

constexpr std::string_view TWO_CRLF("\r\n\r\n");
constexpr std::string_view CONTENT_LENGTH("Content-Length: ");
constexpr std::string_view LOG_COMMAND("-> (C) ");
constexpr std::string_view LOG_RESPONSE("<- (R) ");
constexpr std::string_view LOG_EVENT("<- (E) ");

void to_json(nlohmann::json &j, const Checksum &c);
void to_json(nlohmann::json &j, const Source &s);
void to_json(nlohmann::json &j, const Breakpoint &b);
void to_json(nlohmann::json &j, const StackFrame &f);
void to_json(nlohmann::json &j, const Thread &t);
void to_json(nlohmann::json &j, const Scope &s);
void to_json(nlohmann::json &j, const Variable &v);
void to_json(nlohmann::json &j, const Module &m);
void to_json(nlohmann::json &j, const GotoTarget &g);
void to_json(nlohmann::json &j, const BreakpointLocation &b);

} // namespace dncdbg

#endif // PROTOCOL_INTERNAL_HELPERS_H
