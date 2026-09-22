// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

// Note: DEBUGGER_CONFIG_H is used here, since CONFIG_H is quite common.
#ifndef DEBUGGER_CONFIG_H
#define DEBUGGER_CONFIG_H

#include <cstdint>

namespace dncdbg::Config
{

// Just My Code (JMC) debugger option, provided by the DAP protocol ("launch" request).
bool GetJustMyCode();
void SetJustMyCode(bool state);

// Step filtering debugger option, provided by the DAP protocol ("launch" request).
bool GetStepFiltering();
void SetStepFiltering(bool state);

// Stop at entry point debugger option, provided by the DAP protocol ("launch" request).
bool GetStopAtEntry();
void SetStopAtEntry(bool state);

// Suppress JIT optimizations debugger option, provided by the DAP protocol ("launch" request).
bool GetSuppressJITOptimizations();
void SetSuppressJITOptimizations(bool state);

// Expression evaluation flags, provided by the DAP protocol ("launch" request).
constexpr uint32_t EVAL_DEFAULT       = 0x0000;
constexpr uint32_t EVAL_NOFUNCEVAL    = 0x0002;
constexpr uint32_t EVAL_NOTOSTRING    = 0x0004;
constexpr uint32_t EVAL_SHOWRAWVALUES = 0x0008;

uint32_t GetEvalFlags();
void SetEvalFlags(uint32_t evalFlags);

} // namespace dncdbg::Config

#endif // DEBUGGER_CONFIG_H
