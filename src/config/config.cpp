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
constexpr uint32_t defaultStackTraceLimit = 250;
constexpr std::string_view dapRequestTimeoutName = "DNCDBG_DAP_REQUEST_TIMEOUT";
constexpr uint32_t defaultDapRequestTimeoutMs = 15000;
constexpr std::string_view normalEvalTimeoutName = "DNCDBG_NORMAL_EVAL_TIMEOUT";
constexpr uint32_t defaultNormalEvalTimeoutMs = 5000;
constexpr std::string_view abortEvalTimeoutName = "DNCDBG_ABORT_EVAL_TIMEOUT";
constexpr uint32_t defaultAbortEvalTimeoutMs = 5000;
constexpr std::string_view httpRequestTimeoutName = "DNCDBG_HTTP_REQUEST_TIMEOUT";
constexpr uint32_t defaultHttpRequestTimeoutMs = 60000;
constexpr std::string_view membersPerPageLimitName = "DNCDBG_MEMBERS_PER_PAGE_LIMIT";
constexpr uint32_t defaultMembersPerPageLimit = 25;

// Parses a numeric environment variable value; returns std::nullopt if the value is not a valid number.
std::optional<uint32_t> ParseEnvNumber(std::string_view envVal)
{
    uint32_t value = 0;
    const auto result = std::from_chars(envVal.data(), envVal.data() + envVal.size(), value);
    if (result.ec != std::errc{})
    {
        return std::nullopt;
    }
    return value;
}

// Holds a configurable value along with the baseline taken from the process environment,
// so that each session can reset the value before applying request-specific overrides.
struct SessionValue
{
    uint32_t current;
    uint32_t initial;

    void SetFromEnv(std::string_view envVal, std::string_view envName, uint32_t defaultValue)
    {
        if (const auto parsed = ParseEnvNumber(envVal); parsed.has_value())
        {
            current = *parsed;
        }
        else
        {
            LOGE(log << "Failed to parse environment variable " << envName << " as a number, using the default value.");
            current = defaultValue;
        }
    }
};

SessionValue &GetStackTraceLimitState()
{
    static SessionValue stackTraceLimitState{defaultStackTraceLimit, defaultStackTraceLimit};
    return stackTraceLimitState;
}

SessionValue &GetDapRequestTimeoutState()
{
    static SessionValue dapRequestTimeoutState{defaultDapRequestTimeoutMs, defaultDapRequestTimeoutMs};
    return dapRequestTimeoutState;
}

SessionValue &GetNormalEvalTimeoutState()
{
    static SessionValue normalEvalTimeoutState{defaultNormalEvalTimeoutMs, defaultNormalEvalTimeoutMs};
    return normalEvalTimeoutState;
}

SessionValue &GetAbortEvalTimeoutState()
{
    static SessionValue abortEvalTimeoutState{defaultAbortEvalTimeoutMs, defaultAbortEvalTimeoutMs};
    return abortEvalTimeoutState;
}

SessionValue &GetHttpRequestTimeoutState()
{
    static SessionValue httpRequestTimeoutState{defaultHttpRequestTimeoutMs, defaultHttpRequestTimeoutMs};
    return httpRequestTimeoutState;
}

SessionValue &GetMembersPerPageLimitState()
{
    static SessionValue membersPerPageLimitState{defaultMembersPerPageLimit, defaultMembersPerPageLimit};
    return membersPerPageLimitState;
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
    if (const char *envVal = std::getenv(stackTraceLimitName.data())) // NOLINT(bugprone-suspicious-stringview-data-usage)
    {
        GetStackTraceLimitState().SetFromEnv(envVal, stackTraceLimitName, defaultStackTraceLimit);
    }
    GetStackTraceLimitState().initial = GetStackTraceLimitState().current;

    if (const char *envVal = std::getenv(dapRequestTimeoutName.data())) // NOLINT(bugprone-suspicious-stringview-data-usage)
    {
        GetDapRequestTimeoutState().SetFromEnv(envVal, dapRequestTimeoutName, defaultDapRequestTimeoutMs);
    }
    GetDapRequestTimeoutState().initial = GetDapRequestTimeoutState().current;

    if (const char *envVal = std::getenv(normalEvalTimeoutName.data())) // NOLINT(bugprone-suspicious-stringview-data-usage)
    {
        GetNormalEvalTimeoutState().SetFromEnv(envVal, normalEvalTimeoutName, defaultNormalEvalTimeoutMs);
    }
    GetNormalEvalTimeoutState().initial = GetNormalEvalTimeoutState().current;

    if (const char *envVal = std::getenv(abortEvalTimeoutName.data())) // NOLINT(bugprone-suspicious-stringview-data-usage)
    {
        GetAbortEvalTimeoutState().SetFromEnv(envVal, abortEvalTimeoutName, defaultAbortEvalTimeoutMs);
    }
    GetAbortEvalTimeoutState().initial = GetAbortEvalTimeoutState().current;

    if (const char *envVal = std::getenv(httpRequestTimeoutName.data())) // NOLINT(bugprone-suspicious-stringview-data-usage)
    {
        GetHttpRequestTimeoutState().SetFromEnv(envVal, httpRequestTimeoutName, defaultHttpRequestTimeoutMs);
    }
    GetHttpRequestTimeoutState().initial = GetHttpRequestTimeoutState().current;

    if (const char *envVal = std::getenv(membersPerPageLimitName.data())) // NOLINT(bugprone-suspicious-stringview-data-usage)
    {
        GetMembersPerPageLimitState().SetFromEnv(envVal, membersPerPageLimitName, defaultMembersPerPageLimit);
    }
    GetMembersPerPageLimitState().initial = GetMembersPerPageLimitState().current;
}

void Initialize(const std::map<std::string, std::string> &env)
{
    // Reset to the process environment value, then apply overrides from the request options.
    // Without the reset, a value from a previous session would persist when this session does not set the variable.
    GetStackTraceLimitState().current = GetStackTraceLimitState().initial;
    GetDapRequestTimeoutState().current = GetDapRequestTimeoutState().initial;
    GetNormalEvalTimeoutState().current = GetNormalEvalTimeoutState().initial;
    GetAbortEvalTimeoutState().current = GetAbortEvalTimeoutState().initial;
    GetHttpRequestTimeoutState().current = GetHttpRequestTimeoutState().initial;
    GetMembersPerPageLimitState().current = GetMembersPerPageLimitState().initial;

    for (const auto &[envName, envVal] : env)
    {
        if (envName == stackTraceLimitName)
        {
            GetStackTraceLimitState().SetFromEnv(envVal, stackTraceLimitName, defaultStackTraceLimit);
        }
        else if (envName == dapRequestTimeoutName)
        {
            GetDapRequestTimeoutState().SetFromEnv(envVal, dapRequestTimeoutName, defaultDapRequestTimeoutMs);
        }
        else if (envName == normalEvalTimeoutName)
        {
            GetNormalEvalTimeoutState().SetFromEnv(envVal, normalEvalTimeoutName, defaultNormalEvalTimeoutMs);
        }
        else if (envName == abortEvalTimeoutName)
        {
            GetAbortEvalTimeoutState().SetFromEnv(envVal, abortEvalTimeoutName, defaultAbortEvalTimeoutMs);
        }
        else if (envName == httpRequestTimeoutName)
        {
            GetHttpRequestTimeoutState().SetFromEnv(envVal, httpRequestTimeoutName, defaultHttpRequestTimeoutMs);
        }
        else if (envName == membersPerPageLimitName)
        {
            GetMembersPerPageLimitState().SetFromEnv(envVal, membersPerPageLimitName, defaultMembersPerPageLimit);
        }
    }
}

uint32_t GetStackTraceLimit()
{
    return GetStackTraceLimitState().current;
}

uint32_t GetDapRequestTimeoutMs()
{
    return GetDapRequestTimeoutState().current;
}

uint32_t GetNormalEvalTimeoutMs()
{
    return GetNormalEvalTimeoutState().current;
}

uint32_t GetAbortEvalTimeoutMs()
{
    return GetAbortEvalTimeoutState().current;
}

uint32_t GetHttpRequestTimeoutMs()
{
    return GetHttpRequestTimeoutState().current;
}

uint32_t GetMembersPerPageLimit()
{
    return GetMembersPerPageLimitState().current;
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
