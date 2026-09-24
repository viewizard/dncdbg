// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef PROTOCOL_DAP_H
#define PROTOCOL_DAP_H

#include <string>

namespace dncdbg::DAP
{

// Implements the DAP protocol command loop: reads requests from the input stream, dispatches them for execution,
// and emits responses. Returns after the "disconnect" command or when the input stream is closed.
void CommandLoop();

void SetupProtocolLogging(const std::string &path);

} // namespace dncdbg::DAP

#endif // PROTOCOL_DAP_H
