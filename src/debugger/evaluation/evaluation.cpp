// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "debugger/evaluation/evaluation.h"
#include "debugger/evaluation/evalhelpers/evalexec.h"
#include "debugger/evaluation/evalhelpers/evalwaiter.h"
#include "debugger/evaluation/evalhelpers/systemtypes.h"
#include "debugger/evaluation/evalhelpers/typeproxy.h"
#include "debugger/evaluation/walkers/walkers.h"

namespace dncdbg::Evaluation
{

void Cleanup()
{
    EvalExec::Cleanup();
    SystemTypes::Cleanup();
    EvalWaiter::Cleanup();
    TypeProxy::Cleanup();
    Walkers::Cleanup();
}

} // namespace dncdbg::Evaluation
