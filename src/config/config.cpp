// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "config/config.h"

namespace dncdbg::Config
{

namespace
{

bool &GetRunningViaVsDbgUIState()
{
    static bool runningViaVsDbgUIState{false};
    return runningViaVsDbgUIState;
}

bool &GetJustMyCodeState()
{
    static bool justMyCodeState{true};
    return justMyCodeState;
}

// https://docs.microsoft.com/en-us/visualstudio/debugger/navigating-through-code-with-the-debugger?view=vs-2019#BKMK_Step_into_properties_and_operators_in_managed_code
// The debugger steps over properties and operators in managed code by default. In most cases, this provides a better debugging experience.
bool &GetStepFilteringState()
{
    static bool stepFilteringState{true};
    return stepFilteringState;
}

bool &GetStopAtEntryState()
{
    static bool stopAtEntryState{false};
    return stopAtEntryState;
}

bool &GetSuppressJITOptimizationsState()
{
    static bool suppressJITOptimizationsState{false};
    return suppressJITOptimizationsState;
}

uint32_t &GetEvalFlagsState()
{
    static uint32_t evalFlags{EVAL_DEFAULT};
    return evalFlags;
}

} // unnamed namespace

bool IsRunningViaVsDbgUI()
{
    return GetRunningViaVsDbgUIState();
}

void SetRunningViaVsDbgUI(bool state)
{
    GetRunningViaVsDbgUIState() = state;
}

bool GetJustMyCode()
{
    return GetJustMyCodeState();
}

void SetJustMyCode(bool state)
{
    GetJustMyCodeState() = state;
}

bool GetStepFiltering()
{
    return GetStepFilteringState();
}

void SetStepFiltering(bool state)
{
    GetStepFilteringState() = state;
}

bool GetStopAtEntry()
{
    return GetStopAtEntryState();
}

void SetStopAtEntry(bool state)
{
    GetStopAtEntryState() = state;
}

bool GetSuppressJITOptimizations()
{
    return GetSuppressJITOptimizationsState();
}

void SetSuppressJITOptimizations(bool state)
{
    GetSuppressJITOptimizationsState() = state;
}

uint32_t GetEvalFlags()
{
    return GetEvalFlagsState();
}

void SetEvalFlags(uint32_t evalFlags)
{
    GetEvalFlagsState() = evalFlags;
}

} // namespace dncdbg::Config
