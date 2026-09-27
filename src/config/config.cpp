// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "config/config.h"
#include "utils/logger.h"
#include <charconv>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace dncdbg::Config
{

namespace
{

constexpr std::string_view stackTraceLimitName = "DNCDBG_STACKTRACE_LIMIT";
constexpr size_t defaultStackTraceLimit = 250;
constexpr std::string_view dapRequestTimeoutName = "DNCDBG_DAP_REQUEST_TIMEOUT";
constexpr size_t defaultDapRequestTimeout = 15000;

// Parses a numeric environment variable value; returns std::nullopt if the value is not a valid number.
std::optional<size_t> ParseEnvNumber(std::string_view envVal)
{
    size_t value = 0;
    const auto result = std::from_chars(envVal.data(), envVal.data() + envVal.size(), value);
    if (result.ec != std::errc{})
    {
        return std::nullopt;
    }
    return value;
}

size_t &GetStackTraceLimitState()
{
    static size_t stackTraceLimitState{defaultStackTraceLimit};
    return stackTraceLimitState;
}

void SetStackTraceLimitState(std::string_view envVal)
{
    if (const auto newLimit = ParseEnvNumber(envVal); newLimit.has_value())
    {
        GetStackTraceLimitState() = *newLimit;
    }
    else
    {
        LOGE(log << "Failed to parse environment variable " << stackTraceLimitName << " as a number, using the default value.");
        GetStackTraceLimitState() = defaultStackTraceLimit;
    }
}

size_t &GetDapRequestTimeoutState()
{
    static size_t dapRequestTimeoutState{defaultDapRequestTimeout};
    return dapRequestTimeoutState;
}

void SetDapRequestTimeoutState(std::string_view envVal)
{
    if (const auto newTimeout = ParseEnvNumber(envVal); newTimeout.has_value())
    {
        GetDapRequestTimeoutState() = *newTimeout;
    }
    else
    {
        LOGE(log << "Failed to parse environment variable " << dapRequestTimeoutName << " as a number, using the default value.");
        GetDapRequestTimeoutState() = defaultDapRequestTimeout;
    }
}

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

void Initialize()
{
    const char *envVal = std::getenv(stackTraceLimitName.data()); // NOLINT(bugprone-suspicious-stringview-data-usage)
    if (envVal != nullptr)
    {
        SetStackTraceLimitState(envVal);
    }
    envVal = std::getenv(dapRequestTimeoutName.data()); // NOLINT(bugprone-suspicious-stringview-data-usage)
    if (envVal != nullptr)
    {
        SetDapRequestTimeoutState(envVal);
    }
}

void Initialize(const std::map<std::string, std::string> &env)
{
    // Reset to the process environment value, then apply overrides from the request options.
    // Without the reset, a value from a previous session would persist when this session does not set the variable.
    GetStackTraceLimitState() = defaultStackTraceLimit;
    GetDapRequestTimeoutState() = defaultDapRequestTimeout;
    Initialize();

    for (const auto &[envName, envVal] : env)
    {
        if (envName == stackTraceLimitName)
        {
            SetStackTraceLimitState(envVal);
        }
        else if (envName == dapRequestTimeoutName)
        {
            SetDapRequestTimeoutState(envVal);
        }
    }
}

size_t GetStackTraceLimit()
{
    return GetStackTraceLimitState();
}

size_t GetDapRequestTimeout()
{
    return GetDapRequestTimeoutState();
}

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
