// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef PROTOCOL_DAP_COMMANDS_H
#define PROTOCOL_DAP_COMMANDS_H

#include "utils/hresult.h"
#include <json/json.hpp>
#include <string>

namespace dncdbg::DAP
{

// Note, this is the internal implementation of the protocol module.
// Do not use it outside of the protocol folder.

// Dispatches a DAP request to its handler (see the command handlers table in dap_commands.cpp)
// and fills `responseBody`. Throws nlohmann::json exceptions for malformed arguments; they are
// converted into a failed response by HandleCommandJSON() (dap.cpp).
HRESULT HandleCommand(const std::string &command, const nlohmann::json &arguments, nlohmann::json &responseBody);

} // namespace dncdbg::DAP

#endif // PROTOCOL_DAP_COMMANDS_H
