// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef DEBUGGER_EVALUATION_EVALUATION_H
#define DEBUGGER_EVALUATION_EVALUATION_H

namespace dncdbg::Evaluation
{

// Cleans up the Evaluation internal state. See CleanupDebugSession() in manageddebugger.cpp.
void Cleanup();

} // namespace dncdbg::Evaluation

#endif // DEBUGGER_EVALUATION_EVALUATION_H
