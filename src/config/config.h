// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Note: DEBUGGER_CONFIG_H is used here, since CONFIG_H is quite common.
#ifndef DEBUGGER_CONFIG_H
#define DEBUGGER_CONFIG_H

namespace dncdbg::Config
{

// Just My Code (JMC) debugger option, provided by the DAP protocol ("initialize" request).
bool GetJustMyCode();
void SetJustMyCode(bool state);

} // namespace dncdbg::Config

#endif // DEBUGGER_CONFIG_H
