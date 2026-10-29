// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "config/config.h"
#include "utils/logger.h"
#include <charconv>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>

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
constexpr std::string_view startupTimeoutName = "DNCDBG_STARTUP_TIMEOUT";
constexpr uint32_t defaultStartupTimeoutMs = 5000;
constexpr std::string_view terminationTimeoutName = "DNCDBG_TERMINATION_TIMEOUT";
constexpr uint32_t defaultTerminationTimeoutMs = 3000;
constexpr std::string_view rootHiddenWalkLimitName = "DNCDBG_ROOTHIDDEN_WALK_LIMIT";
constexpr uint32_t defaultRootHiddenWalkLimit = 32;
constexpr std::string_view innerExceptionWalkLimitName = "DNCDBG_INNER_EXCEPTION_WALK_LIMIT";
constexpr uint32_t defaultInnerExceptionWalkLimit = 32;
constexpr std::string_view pdbMmapPolicyName = "DNCDBG_PDB_MMAP_POLICY";
constexpr uint32_t defaultPdbMmapPolicy = pdbMmapFlushAfterInit;
constexpr uint32_t maxAllowedPdbMmapPolicy = pdbMmapFlushAfterInit;
constexpr std::string_view remoteConsolePortName = "DNCDBG_REMOTECONSOLE_PORT";
constexpr uint32_t defaultRemoteConsolePort = 22534;
constexpr uint32_t minAllowedRemoteConsolePort = 1;
constexpr uint32_t maxAllowedRemoteConsolePort = 65535;

// Parses a numeric environment variable value; returns std::nullopt if the value is not a valid
// number (the entire value must be consumed, so trailing garbage is rejected).
std::optional<uint32_t> ParseEnvNumber(std::string_view envVal)
{
    uint32_t value = 0;
    const auto result = std::from_chars(envVal.data(), envVal.data() + envVal.size(), value);
    if (result.ec != std::errc{} || result.ptr != envVal.data() + envVal.size())
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

    void SetFromEnv(std::string_view envVal, std::string_view envName, uint32_t defaultValue, uint32_t minAllowed, uint32_t maxAllowed)
    {
        if (const auto parsed = ParseEnvNumber(envVal); parsed.has_value())
        {
            if (*parsed > maxAllowed)
            {
                LOGE(log << "Environment variable " << envName << " exceeds the maximum allowed value, using the default value.");
                current = defaultValue;
            }
            else if (*parsed < minAllowed)
            {
                LOGE(log << "Environment variable " << envName << " is below the minimum allowed value, using the default value.");
                current = defaultValue;
            }
            else
            {
                current = *parsed;
            }
        }
        else
        {
            LOGE(log << "Failed to parse environment variable " << envName << " as a number, using the default value.");
            current = defaultValue;
        }
    }
};

// Associates an environment variable name with its state, the default value (used when parsing
// fails or the parsed value is outside the allowed range), and the minimum and maximum allowed values.
struct EnvSetting
{
    SessionValue *state;
    uint32_t defaultValue;
    uint32_t minAllowedValue;
    uint32_t maxAllowedValue;
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

SessionValue &GetStartupTimeoutState()
{
    static SessionValue startupTimeoutState{defaultStartupTimeoutMs, defaultStartupTimeoutMs};
    return startupTimeoutState;
}

SessionValue &GetTerminationTimeoutState()
{
    static SessionValue terminationTimeoutState{defaultTerminationTimeoutMs, defaultTerminationTimeoutMs};
    return terminationTimeoutState;
}

SessionValue &GetRootHiddenWalkLimitState()
{
    static SessionValue rootHiddenWalkLimitState{defaultRootHiddenWalkLimit, defaultRootHiddenWalkLimit};
    return rootHiddenWalkLimitState;
}

SessionValue &GetInnerExceptionWalkLimitState()
{
    static SessionValue innerExceptionWalkLimitState{defaultInnerExceptionWalkLimit, defaultInnerExceptionWalkLimit};
    return innerExceptionWalkLimitState;
}

SessionValue &GetPdbMmapPolicyState()
{
    static SessionValue pdbMmapPolicyState{defaultPdbMmapPolicy, defaultPdbMmapPolicy};
    return pdbMmapPolicyState;
}

SessionValue &GetRemoteConsolePortState()
{
    static SessionValue remoteConsolePortState{defaultRemoteConsolePort, defaultRemoteConsolePort};
    return remoteConsolePortState;
}

// Maps environment variable names to their settings (see EnvSetting).
const std::unordered_map<std::string_view, EnvSetting> &GetEnvSettings()
{
    static const std::unordered_map<std::string_view, EnvSetting> envSettings{
        {stackTraceLimitName, {&GetStackTraceLimitState(), defaultStackTraceLimit, 0, std::numeric_limits<uint32_t>::max()}},
        {dapRequestTimeoutName, {&GetDapRequestTimeoutState(), defaultDapRequestTimeoutMs, 0, std::numeric_limits<uint32_t>::max()}},
        {normalEvalTimeoutName, {&GetNormalEvalTimeoutState(), defaultNormalEvalTimeoutMs, 0, std::numeric_limits<uint32_t>::max()}},
        {abortEvalTimeoutName, {&GetAbortEvalTimeoutState(), defaultAbortEvalTimeoutMs, 0, std::numeric_limits<uint32_t>::max()}},
        {httpRequestTimeoutName, {&GetHttpRequestTimeoutState(), defaultHttpRequestTimeoutMs, 0, std::numeric_limits<uint32_t>::max()}},
        {membersPerPageLimitName, {&GetMembersPerPageLimitState(), defaultMembersPerPageLimit, 0, std::numeric_limits<uint32_t>::max()}},
        {startupTimeoutName, {&GetStartupTimeoutState(), defaultStartupTimeoutMs, 0, std::numeric_limits<uint32_t>::max()}},
        {terminationTimeoutName, {&GetTerminationTimeoutState(), defaultTerminationTimeoutMs, 0, std::numeric_limits<uint32_t>::max()}},
        {rootHiddenWalkLimitName, {&GetRootHiddenWalkLimitState(), defaultRootHiddenWalkLimit, 0, std::numeric_limits<uint32_t>::max()}},
        {innerExceptionWalkLimitName, {&GetInnerExceptionWalkLimitState(), defaultInnerExceptionWalkLimit, 0, std::numeric_limits<uint32_t>::max()}},
        {pdbMmapPolicyName, {&GetPdbMmapPolicyState(), defaultPdbMmapPolicy, 0, maxAllowedPdbMmapPolicy}},
        {remoteConsolePortName, {&GetRemoteConsolePortState(), defaultRemoteConsolePort, minAllowedRemoteConsolePort, maxAllowedRemoteConsolePort}},
    };
    return envSettings;
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

bool &GetLinesStartAt1State()
{
    static bool linesStartAt1{true};
    return linesStartAt1;
}

bool &GetColumnsStartAt1State()
{
    static bool columnsStartAt1{true};
    return columnsStartAt1;
}

bool &GetSupportsMemoryReferencesState()
{
    static bool supportsMemoryReferences{false};
    return supportsMemoryReferences;
}

} // unnamed namespace

// Establishes the baseline values by reading the settings from the process environment.
void Initialize()
{
    for (const auto &[envName, envSetting] : GetEnvSettings())
    {
        if (const char *envVal = std::getenv(envName.data())) // NOLINT(bugprone-suspicious-stringview-data-usage)
        {
            envSetting.state->SetFromEnv(envVal, envName, envSetting.defaultValue,
                                         envSetting.minAllowedValue, envSetting.maxAllowedValue);
        }
        envSetting.state->initial = envSetting.state->current;
    }
}

void Initialize(const std::map<std::string, std::string> &env)
{
    const auto &envSettings = GetEnvSettings();

    // Reset to the process environment value, then apply overrides from the request options.
    // Without the reset, a value from a previous session would persist when this session does not set the variable.
    for (const auto &envSetting : envSettings)
    {
        envSetting.second.state->current = envSetting.second.state->initial;
    }

    for (const auto &[envName, envVal] : env)
    {
        const auto findSetting = envSettings.find(envName);
        if (findSetting == envSettings.cend())
        {
            continue;
        }

        findSetting->second.state->SetFromEnv(envVal, findSetting->first, findSetting->second.defaultValue,
                                              findSetting->second.minAllowedValue, findSetting->second.maxAllowedValue);
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

uint32_t GetStartupTimeoutMs()
{
    return GetStartupTimeoutState().current;
}

uint32_t GetTerminationTimeoutMs()
{
    return GetTerminationTimeoutState().current;
}

uint32_t GetRootHiddenWalkLimit()
{
    return GetRootHiddenWalkLimitState().current;
}

uint32_t GetInnerExceptionWalkLimit()
{
    return GetInnerExceptionWalkLimitState().current;
}

uint32_t GetPdbMmapPolicy()
{
    return GetPdbMmapPolicyState().current;
}

uint32_t GetRemoteConsolePort()
{
    return GetRemoteConsolePortState().current;
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

bool GetLinesStartAt1()
{
    return GetLinesStartAt1State();
}

void SetLinesStartAt1(bool state)
{
    GetLinesStartAt1State() = state;
}

bool GetColumnsStartAt1()
{
    return GetColumnsStartAt1State();
}

void SetColumnsStartAt1(bool state)
{
    GetColumnsStartAt1State() = state;
}

bool GetSupportsMemoryReferences()
{
    return GetSupportsMemoryReferencesState();
}

void SetSupportsMemoryReferences(bool state)
{
    GetSupportsMemoryReferencesState() = state;
}

} // namespace dncdbg::Config
