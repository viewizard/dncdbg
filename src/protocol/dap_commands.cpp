// Copyright (c) 2018-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "protocol/dap_commands.h"
#include "config/config.h"
#include "debugger/manageddebugger.h"
#include "protocol/internal_helpers.h"
#include "protocol/to_json.h" // NOLINT(misc-include-cleaner)
#include "types/protocol.h"
#include "types/types.h"
#include "utils/filesystem.h"
#include "utils/hresult.h"
#include "utils/logger.h"
#include <base64/base64.hpp>
#include <json/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// for convenience
using nlohmann::json;

namespace dncdbg::DAP
{

namespace
{

// The internal console mode (enabled by the `console` option of the `launch` request): when
// enabled, the `evaluate` command input is redirected to the debuggee stdin.
std::atomic<bool> &GetInternalConsole()
{
    static std::atomic<bool> internalConsole{false};
    return internalConsole;
}

// 0 means "no server has been initialized yet"; valid ports start at 1.
constexpr uint32_t gPortNotInitialized = 0;
std::atomic<uint32_t> &GetPreviousRemoteConsoleServerPort()
{
    static std::atomic<uint32_t> previousRemoteConsoleServerPort{gPortNotInitialized};
    return previousRemoteConsoleServerPort;
}

// Returns a reference to the string stored in the requested JSON object field, or an empty
// string when the field is absent or is not a string. Unlike json::value(), no intermediate
// string copy is made: the caller receives either the string stored inside the JSON object
// itself or a static empty string, so the reference must not outlive the inspected json object.
const std::string &GetStringField(const json &object, const char *fieldName)
{
    static const std::string emptyString;
    const auto fieldIt = object.find(fieldName);
    return (fieldIt != object.cend() && fieldIt->is_string())
               ? fieldIt->get_ref<const std::string &>() : emptyString;
}

// Same as GetStringField(), but for a required field: json::at() throws when the field is
// absent and get_ref() throws when the field is not a string (both are converted into a
// failed response by HandleCommandJSON()). No intermediate string copy is made.
const std::string &GetRequiredStringField(const json &object, const char *fieldName)
{
    return object.at(fieldName).get_ref<const std::string &>();
}

// Returns a view of the string field, or an empty view when the field is absent. Like json::value(),
// a field of a non-string type is an error (it throws), so the request is reported as failed.
// No copy is made: the view refers to the string stored inside the json object.
std::string_view GetOptionalStringView(const json &object, const char *fieldName)
{
    const auto fieldIt = object.find(fieldName);
    return fieldIt == object.cend() ? std::string_view{}
                                    : std::string_view(fieldIt->get_ref<const std::string &>());
}

// Parse a DAP Source from JSON. When the Source itself has no `sourceReference`, `fallbackSourceReference`
// is used instead (the Source Request allows this field at the top level of the arguments).
// Returns E_INVALIDARG if neither `path`, nor `name`, nor `sourceReference` is provided.
HRESULT ParseSourceJson(const json &sourceJson, int32_t fallbackSourceReference, Source &source)
{
    const std::string &sourcePath = GetStringField(sourceJson, "path");
    const std::string &sourceName = GetStringField(sourceJson, "name");
    int32_t sourceReference = std::max(sourceJson.value("sourceReference", 0), 0);

    if (sourceReference == 0)
    {
        sourceReference = std::max(fallbackSourceReference, 0);
    }
    if (sourcePath.empty() && sourceName.empty() && sourceReference == 0)
    {
        return E_INVALIDARG;
    }

    source = Source(sourcePath.empty() ? sourceName : sourcePath, sourceReference);
    const auto checksumsIt = sourceJson.find("checksums");
    if (checksumsIt != sourceJson.cend())
    {
        source.checksums.reserve(checksumsIt->size());
        std::transform(checksumsIt->cbegin(), checksumsIt->cend(),
                       std::back_inserter(source.checksums), [](const auto &c)
                       {
                           return Checksum(c.value("algorithm", std::string()),
                                           c.value("checksum", std::string()));
                       });
    }

    return S_OK;
}

// Extract the optional `env` option (the debug session environment variables) from the request arguments.
// Returns an empty map if the option is absent or malformed.
std::map<std::string, std::string> GetDebugSessionEnvironment(const json &arguments)
{
    std::map<std::string, std::string> env;
    const auto findEnv = arguments.find("env");
    if (findEnv != arguments.cend())
    {
        try
        {
            env = findEnv->get<std::map<std::string, std::string>>();
        }
        catch (const std::exception &ex)
        {
            LOGI(log << "env exception '" << ex.what() << "'");
        }
    }

    return env;
}

void ParseAndApplyDebugSessionOptions(const json &arguments, const std::map<std::string, std::string> &env)
{
    Config::Initialize(env);
    Config::SetJustMyCode(arguments.value("justMyCode", true)); // MS vsdbg has "justMyCode" enabled by default.
    Config::SetStepFiltering(arguments.value("enableStepFiltering", true)); // MS vsdbg has "enableStepFiltering" enabled by default.
    Config::SetStopAtEntry(arguments.value("stopAtEntry", false)); // MS vsdbg has "stopAtEntry" disabled by default.
    Config::SetSuppressJITOptimizations(arguments.value("suppressJITOptimizations", false)); // MS vsdbg has "suppressJITOptimizations" disabled by default.

    uint32_t evalFlags = Config::EVAL_DEFAULT;
    const auto evalOptionsIt = arguments.find("expressionEvaluationOptions");
    if (evalOptionsIt != arguments.cend())
    {
        const json &evalOptions = *evalOptionsIt;
        // https://github.com/OmniSharp/omnisharp-vscode/issues/3173
        // https://github.com/dotnet/vscode-csharp/blob/627cb33704ba2a688904313e51b460c8324a34eb/package.nls.json#L456
        const bool allowFuncEval = evalOptions.value("allowImplicitFuncEval", true);
        evalFlags |= allowFuncEval ? 0 : Config::EVAL_NOFUNCEVAL;
        // https://github.com/dotnet/vscode-csharp/blob/627cb33704ba2a688904313e51b460c8324a34eb/package.nls.json#L462
        const bool allowToString = evalOptions.value("allowToString", true);
        evalFlags |= (allowToString && allowFuncEval) ? 0 : Config::EVAL_NOTOSTRING;
        // https://github.com/dotnet/vscode-csharp/blob/627cb33704ba2a688904313e51b460c8324a34eb/package.nls.json#L469
        const bool showRawValues = evalOptions.value("showRawValues", false);
        evalFlags |= showRawValues ? Config::EVAL_SHOWRAWVALUES : 0;
    }
    Config::SetEvalFlags(evalFlags);

    const auto findSourceFileMap = arguments.find("sourceFileMap");
    std::map<std::string, std::string> map;
    if (findSourceFileMap != arguments.cend())
    {
        try
        {
            // https://code.visualstudio.com/docs/csharp/debugger-settings#_source-file-map
            map = findSourceFileMap->get<std::map<std::string, std::string>>();
        }
        catch (const std::exception &ex)
        {
            LOGI(log << "sourceFileMap exception '" << ex.what() << "'");
        }
    }
    ManagedDebugger::SetSourceFileMap(std::move(map));
}

json FormJsonForExceptionDetails(const ExceptionDetails &details)
{
    // Collect the exception chain from the outermost exception to the innermost one.
    // Typical chains are short, so the reservation avoids the first few vector reallocations.
    std::vector<const ExceptionDetails *> chain;
    chain.reserve(4);
    for (const ExceptionDetails *current = &details; current != nullptr;
         current = current->innerException.get())
    {
        chain.push_back(current);
    }

    json result;
    // Build the JSON from the innermost exception outwards, wrapping each result into the
    // `innerException` array of its parent. Note, the DAP `innerException` field is an array,
    // but in reality there is no array of inner exceptions here: the exception object holds
    // only one reference to an inner exception, in its InnerException field.
    for (auto it = chain.crbegin(); it != chain.crend(); ++it)
    {
        const ExceptionDetails *current = *it;

        json exceptionJson = json::object();
        exceptionJson.emplace("typeName", current->typeName);
        exceptionJson.emplace("fullTypeName", current->fullTypeName);
        exceptionJson.emplace("evaluateName", current->evaluateName);
        exceptionJson.emplace("stackTrace", current->stackTrace);
        exceptionJson.emplace("formattedDescription", current->formattedDescription);
        exceptionJson.emplace("source", current->source);

        if (!current->message.empty())
        {
            exceptionJson.emplace("message", current->message);
        }

        if (current->innerException)
        {
            json arr = json::array();
            arr.push_back(std::move(result));
            exceptionJson.emplace("innerException", std::move(arr));
        }

        result = std::move(exceptionJson);
    }

    return result;
}

// A launchSettings.json profile. Only the profile properties applied by the debugger are
// represented (see https://code.visualstudio.com/docs/csharp/debugger-settings#_profile-properties).
struct LaunchSettingsProfile
{
    std::string executablePath;
    std::string workingDirectory;
    std::string commandLineArgs;
    std::string applicationUrl;
    std::map<std::string, std::string> environmentVariables;
};

bool IsAbsolutePath(std::string_view path)
{
    if (path.empty())
    {
        return false;
    }

#ifdef _WIN32
    // A drive letter (e.g. "C:\...") or a UNC path (e.g. "\\server\share\...").
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path.at(0))) != 0 && path.at(1) == ':')
    {
        return true;
    }
    // Note, both forward slashes and backslashes are accepted by the Win32 API.
    return path.at(0) == '/' || path.at(0) == '\\';
#else
    return path.at(0) == '/';
#endif
}

std::string ResolveLaunchSettingsPath(const std::string &path, const std::string &baseDir)
{
    if (path.empty() || IsAbsolutePath(path) || baseDir.empty())
    {
        return path;
    }

    return baseDir + path;
}

// Splits a command-line string (the `commandLineArgs` profile property or a string `args` option
// of the launch request) into separate arguments. Arguments are separated by whitespace, unless the
// whitespace is inside double quotes. A backslash followed by a double quote escapes the quote.
// Note, this is a simplified subset of the command-line splitting rules used by `dotnet run`.
std::vector<std::string> SplitCommandLineArgs(std::string_view commandLineArgs)
{
    std::vector<std::string> args;
    std::string arg;
    arg.reserve(commandLineArgs.size());
    bool inQuotes{false};
    bool hasArg{false};

    const auto finishArg = [&]
    {
        if (hasArg)
        {
            args.push_back(std::move(arg));
            arg.clear();
            hasArg = false;
        }
    };

    for (std::size_t i = 0; i < commandLineArgs.size(); ++i)
    {
        const char ch = commandLineArgs.at(i);
        if (ch == '\\' && i + 1 < commandLineArgs.size() && commandLineArgs.at(i + 1) == '"')
        {
            arg += '"';
            hasArg = true;
            ++i;
        }
        else if (ch == '"')
        {
            inQuotes = !inQuotes;
            hasArg = true;
        }
        else if (std::isspace(static_cast<unsigned char>(ch)) != 0)
        {
            if (inQuotes)
            {
                arg += ch;
                hasArg = true;
            }
            else
            {
                finishArg();
            }
        }
        else
        {
            arg += ch;
            hasArg = true;
        }
    }
    finishArg();

    return args;
}

// Reads and parses the launchSettings.json file.
// The return value is `false` when the file could not be opened or its content could not be parsed;
// the `fileExists` out parameter distinguishes between these two cases.
bool ReadLaunchSettings(const std::string &filePath, nlohmann::ordered_json &settings, bool &fileExists)
{
    fileExists = false;

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        return false;
    }
    fileExists = true;

    // The whole file is read with a single block read and parsed from memory. Parsing directly
    // from the stream would fetch the input one character at a time.
    std::string content;
    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    if (fileSize < 0)
    {
        return false;
    }
    content.resize(static_cast<std::size_t>(fileSize));
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<std::size_t>(file.gcount()));

    try
    {
        settings = nlohmann::ordered_json::parse(content, nullptr, /*allow_exceptions*/ true,
                                                 /*ignore_comments*/ true);
    }
    catch (const std::exception &ex)
    {
        LOGE(log << "launchSettings.json parsing exception '" << ex.what() << "'");
        return false;
    }

    return true;
}

// Extracts the `executablePath`, `workingDirectory`, `commandLineArgs`, `applicationUrl` and
// `environmentVariables` profile properties. The profile properties not applied by the debugger
// (e.g. `launchBrowser`, `sslPort` and `httpPort`) are ignored.
void ParseLaunchSettingsProfile(const nlohmann::ordered_json &profileJson, LaunchSettingsProfile &profile)
{
    profile = LaunchSettingsProfile{};

    const auto getStringField = [&profileJson](const char *name, std::string &value)
    {
        const auto fieldIt = profileJson.find(name);
        if (fieldIt != profileJson.cend() && fieldIt->is_string())
        {
            value = fieldIt->get<std::string>();
        }
    };

    getStringField("executablePath", profile.executablePath);
    getStringField("workingDirectory", profile.workingDirectory);
    getStringField("commandLineArgs", profile.commandLineArgs);
    getStringField("applicationUrl", profile.applicationUrl);

    const auto environmentVariablesIt = profileJson.find("environmentVariables");
    if (environmentVariablesIt == profileJson.cend() || !environmentVariablesIt->is_object())
    {
        return;
    }

    for (const auto &entry : environmentVariablesIt->items())
    {
        if (entry.value().is_string())
        {
            profile.environmentVariables.emplace(entry.key(), entry.value().get<std::string>());
        }
        else
        {
            LOGW(log << "The '" << entry.key() << "' environment variable in the launchSettings.json profile is not a string and will be ignored");
        }
    }
}

// Return values:
// - S_OK - a profile was found and applied;
// - S_FALSE - launchSettings.json was skipped;
// - E_INVALIDARG - explicitly requested launch settings could not be applied (see `output`).
HRESULT ParseAndApplyLaunchSettings(const json &arguments, std::string &program,
                                    std::vector<std::string> &args, std::string &cwd,
                                    std::map<std::string, std::string> &env, std::string &output)
{
    // The `launchSettingsProfile` option: null or an empty string disables the launchSettings.json
    // processing; an absent option means the first profile with "commandName": "Project" is used;
    // a non-empty string selects the profile with the given name.
    std::string profileName;
    bool profileNameSet{false};
    const auto findProfile = arguments.find("launchSettingsProfile");
    if (findProfile != arguments.cend())
    {
        if (findProfile->is_null())
        {
            return S_FALSE;
        }

        if (!findProfile->is_string())
        {
            output = "The 'launchSettingsProfile' option must be a string or null.";
            return E_INVALIDARG;
        }

        profileName = findProfile->get<std::string>();
        profileNameSet = true;
        if (profileName.empty())
        {
            return S_FALSE;
        }
    }

    // The `launchSettingsFilePath` option overrides the default `{cwd}/Properties/launchSettings.json`
    // search location.
    std::string filePath;
    bool filePathSet{false};
    const auto findFilePath = arguments.find("launchSettingsFilePath");
    if (findFilePath != arguments.cend() && !findFilePath->is_null())
    {
        if (!findFilePath->is_string())
        {
            output = "The 'launchSettingsFilePath' option must be a string or null.";
            return E_INVALIDARG;
        }

        filePathSet = true;
        filePath = findFilePath->get<std::string>();
        if (!filePath.empty() && !IsAbsolutePath(filePath) && !cwd.empty())
        {
            filePath = cwd + FileSystem::PathSeparator + filePath;
        }
    }
    if (filePath.empty())
    {
        // Use the default `{cwd}/Properties/launchSettings.json` search location. When `cwd` is not
        // set, the path is relative to the current working directory of the debugger.
        filePath = cwd.empty() ? std::string{} : cwd + FileSystem::PathSeparator;
        filePath += "Properties";
        filePath += FileSystem::PathSeparator;
        filePath += "launchSettings.json";
    }

    // Problems with the launch settings explicitly selected by the user (a profile name or a file
    // path is provided) are reported as launch failures. Otherwise, the default search location
    // may simply not contain a launchSettings.json file, in which case nothing is applied.
    const bool explicitLaunchSettings = profileNameSet || filePathSet;

    nlohmann::ordered_json settings;
    bool fileExists{false};
    if (!ReadLaunchSettings(filePath, settings, fileExists))
    {
        if (explicitLaunchSettings)
        {
            output = fileExists ? "Can't parse the launchSettings.json file '" + filePath + "'."
                                : "Can't open the launchSettings.json file '" + filePath + "'.";
            return E_INVALIDARG;
        }

        return S_FALSE;
    }

    const auto profilesIt = settings.find("profiles");
    if (profilesIt == settings.cend() || !profilesIt->is_object() || profilesIt->empty())
    {
        if (explicitLaunchSettings)
        {
            output = "Can't find profiles in the launchSettings.json file '" + filePath + "'.";
            return E_INVALIDARG;
        }

        LOGI(log << "No profiles found in the launchSettings.json file '" << filePath << "'");
        return S_FALSE;
    }

    std::string selectedProfileName;
    const nlohmann::ordered_json *selectedProfile = nullptr;
    if (profileNameSet)
    {
        const auto profileIt = profilesIt->find(profileName);
        if (profileIt == profilesIt->cend() || !profileIt->is_object())
        {
            output = "Can't find the '" + profileName + "' profile in the launchSettings.json file '" + filePath + "'.";
            return E_INVALIDARG;
        }

        // The `commandName` value is compared in place (no temporary string copy is made).
        const auto commandNameIt = profileIt->find("commandName");
        if (commandNameIt == profileIt->cend() || !commandNameIt->is_string() ||
            (commandNameIt->get_ref<const std::string &>() != "Project" &&
             commandNameIt->get_ref<const std::string &>() != "Executable"))
        {
            output = "The '" + profileName + "' profile in the launchSettings.json file '" + filePath +
                      "' has an unsupported 'commandName' value: only 'Project' and 'Executable' are supported.";
            return E_INVALIDARG;
        }

        selectedProfileName = profileName;
        selectedProfile = &profileIt.value();
    }
    else
    {
        // The first profile with "commandName": "Project" (in file order) is used.
        for (const auto &entry : profilesIt->items())
        {
            if (!entry.value().is_object())
            {
                continue;
            }

            const auto commandNameIt = entry.value().find("commandName");
            if (commandNameIt != entry.value().cend() && commandNameIt->is_string() &&
                commandNameIt->get_ref<const std::string &>() == "Project")
            {
                selectedProfileName = entry.key();
                selectedProfile = &entry.value();
                break;
            }
        }

        if (selectedProfile == nullptr)
        {
            if (explicitLaunchSettings)
            {
                output = R"(Can't find a profile with "commandName": "Project" in the launchSettings.json file ')" + filePath + "'.";
                return E_INVALIDARG;
            }

            LOGI(log << "No profile with \"commandName\": \"Project\" found in the launchSettings.json file '" << filePath << "'");
            return S_FALSE;
        }
    }

    LOGI(log << "Using the '" << selectedProfileName << "' profile from the launchSettings.json file '" << filePath << "'");

    LaunchSettingsProfile profile;
    ParseLaunchSettingsProfile(*selectedProfile, profile);

    // Relative `executablePath` and `workingDirectory` profile values are resolved against the
    // directory containing the launchSettings.json file (the result of GetParentPath() ends with
    // a path separator).
    const std::string launchSettingsDir = GetParentPath(filePath);

    // The `workingDirectory` profile property is used when the launch request provides no `cwd`.
    if (cwd.empty() && !profile.workingDirectory.empty())
    {
        cwd = ResolveLaunchSettingsPath(profile.workingDirectory, launchSettingsDir);
    }
    else if (!profile.workingDirectory.empty())
    {
        LOGI(log << "The 'workingDirectory' from the launchSettings.json profile is ignored, since 'cwd' is already set by the launch request");
    }

    // The `executablePath` profile property is used when the launch request provides no `program`.
    if (program.empty() && !profile.executablePath.empty())
    {
        program = ResolveLaunchSettingsPath(profile.executablePath, launchSettingsDir);
    }
    else if (!profile.executablePath.empty())
    {
        LOGI(log << "The 'executablePath' from the launchSettings.json profile is ignored, since 'program' is already set by the launch request");
    }

    // The `environmentVariables` profile property. Variables set by the launch request take
    // precedence over the ones from launchSettings.json.
    for (auto &entry : profile.environmentVariables)
    {
        // A single lower_bound() both checks whether the variable is already set and provides
        // the insertion hint, so the map is searched only once.
        const auto envIt = env.lower_bound(entry.first);
        if (envIt != env.cend() && envIt->first == entry.first)
        {
            LOGI(log << "The '" << entry.first << "' environment variable from the launchSettings.json profile is ignored, since it is already set by the launch request");
            continue;
        }

        env.emplace_hint(envIt, entry.first, std::move(entry.second));
    }

    // The `applicationUrl` profile property. It is applied as the `ASPNETCORE_URLS` environment
    // variable, but only when `ASPNETCORE_URLS` is not already set.
    if (!profile.applicationUrl.empty())
    {
        if (env.find("ASPNETCORE_URLS") == env.cend())
        {
            env.emplace("ASPNETCORE_URLS", profile.applicationUrl);
        }
        else
        {
            LOGI(log << "The 'applicationUrl' from the launchSettings.json profile is ignored, since 'ASPNETCORE_URLS' is already set");
        }
    }

    // The `commandLineArgs` profile property. Arguments set by the launch request take precedence
    // over the ones from launchSettings.json, so `commandLineArgs` is used only when the request
    // provides no `args` (or an empty one).
    if (args.empty())
    {
        args = SplitCommandLineArgs(profile.commandLineArgs);
    }
    else if (!profile.commandLineArgs.empty())
    {
        LOGI(log << "The 'commandLineArgs' from the launchSettings.json profile is ignored, since 'args' is already set by the launch request");
    }

    return S_OK;
}

HRESULT ParseAndApplyLaunchOptions(const json &arguments, std::map<std::string, std::string> &env, std::string &output)
{
    std::string program;
    const auto findProgram = arguments.find("program");
    if (findProgram != arguments.cend() && !findProgram->is_null())
    {
        if (!findProgram->is_string())
        {
            output = "The 'program' option must be a string.";
            return E_INVALIDARG;
        }

        program = findProgram->get<std::string>();
    }

    std::vector<std::string> args;
    const auto findArgs = arguments.find("args");
    if (findArgs != arguments.cend() && !findArgs->is_null())
    {
        if (findArgs->is_string())
        {
            args = SplitCommandLineArgs(findArgs->get_ref<const std::string &>());
        }
        else if (findArgs->is_array() &&
                 std::all_of(findArgs->cbegin(), findArgs->cend(), [](const json &entry) { return entry.is_string(); }))
        {
            args = findArgs->get<std::vector<std::string>>();
        }
        else
        {
            output = "The 'args' option must be a string or an array of strings.";
            return E_INVALIDARG;
        }
    }

    std::string cwd;
    const auto cwdIt = arguments.find("cwd");
    if (cwdIt != arguments.cend() && !cwdIt->is_null())
    {
        if (!cwdIt->is_string())
        {
            output = "The 'cwd' option must be a string.";
            return E_INVALIDARG;
        }

        cwd = cwdIt->get<std::string>();
    }

    const bool noDebug = arguments.value("noDebug", false);

    // https://aka.ms/VSCode-CS-LaunchJson-Console
    // Read before the launchSettings.json profile is applied, so that the profile environment
    // variables do not change the console selection.
    std::string console;
    const auto findConsole = env.find("DNCDBG_CONSOLE");
    if (findConsole != env.cend())
    {
        console = findConsole->second;
    }
    else // Fall back to the `console` field.
    {
        const auto consoleIter = arguments.find("console");
        if (consoleIter != arguments.cend())
        {
            console = consoleIter.value();
        }
    }

    // Apply the launchSettings.json profile selected by the `launchSettingsProfile` and
    // `launchSettingsFilePath` options, mimicking the behavior of the VS Code C# debugger (vsdbg).
    // Note, the profile environment variables are merged into `env` in place. The caller passes
    // a copy of the session environment, so they affect the debuggee process environment only.
    const HRESULT launchSettingsStatus = ParseAndApplyLaunchSettings(arguments, program, args, cwd, env, output);
    if (FAILED(launchSettingsStatus))
    {
        return launchSettingsStatus;
    }

    if (program.empty())
    {
        output = "The 'program' option is required.";
        return E_INVALIDARG;
    }

    GetInternalConsole() = false;
#ifdef _WIN32
    SetEnvironmentVariableW(L"DNCDBG_CREATE_NEW_CONSOLE", L"0");
#endif
    if (console != "remoteConsole")
    {
        ManagedDebugger::CloseRemoteConsoleServer();
        GetPreviousRemoteConsoleServerPort() = gPortNotInitialized;
    }

    if (console == "internalConsole")
    {
        GetInternalConsole() = true;
    }
    else if (console == "remoteConsole")
    {
        const uint32_t remoteConsolePort = Config::GetRemoteConsolePort();

        // Reinitialize the server only when the port changes, so a restart with
        // the same settings keeps the existing remote console connection alive.
        if (GetPreviousRemoteConsoleServerPort() != remoteConsolePort)
        {
            ManagedDebugger::CloseRemoteConsoleServer();

            if (!ManagedDebugger::InitializeRemoteConsoleServer(remoteConsolePort))
            {
                // Reset the sentinel so that a subsequent request retries the server initialization.
                GetPreviousRemoteConsoleServerPort() = gPortNotInitialized;
                return INET_E_CANNOT_CONNECT;
            }

            GetPreviousRemoteConsoleServerPort() = remoteConsolePort;
        }
    }
    else if (console == "externalTerminal")
    {
#ifdef _WIN32
        if (SetEnvironmentVariableW(L"DNCDBG_CREATE_NEW_CONSOLE", L"1") == FALSE)
        {
            LOGE(log << "Failed to set the DNCDBG_CREATE_NEW_CONSOLE environment variable, error: " << GetLastError());
            return E_FAIL;
        }
#else
        LOGW(log << "externalTerminal console mode is not supported on this platform");
#endif // _WIN32
    }

    static constexpr std::string_view dllSuffix = ".dll";
    if (program.size() >= dllSuffix.size() &&
        program.compare(program.size() - dllSuffix.size(), dllSuffix.size(), dllSuffix) == 0)
    {
        args.insert(args.begin(), std::move(program));
        return ManagedDebugger::Launch("dotnet", noDebug, args, env, cwd);
    }

    // If we're not being asked to launch a DLL, assume that whatever we're given is an executable.
    return ManagedDebugger::Launch(program, noDebug, args, env, cwd);
}

bool ParseHitCondition(std::string_view requestHitCondition, HitCondition &hitCondition, std::string &output)
{
    if (requestHitCondition.empty())
    {
        return true;  // the hit condition is not set
    }

    // Spaces are removed before parsing, so "<= 10" is the same as "<=10". A condition
    // without any spaces is parsed in place, without building an intermediate string.
    std::string condition;
    std::string_view conditionView = requestHitCondition;
    if (requestHitCondition.find(' ') != std::string::npos)
    {
        condition.reserve(requestHitCondition.size());
        std::remove_copy_if(requestHitCondition.cbegin(), requestHitCondition.cend(),
                            std::back_inserter(condition),
                            [](unsigned char ch) { return ch == ' '; });
        conditionView = condition;
    }

    if (conditionView.empty())
    {
        return true;  // the hit condition contains spaces only
    }

    // Detect the operator, see HitConditionOp.
    // Note: an absent operator means `N or >=N`, see HitConditionOp::GreaterOrEqual.
    HitConditionOp op{HitConditionOp::GreaterOrEqual};
    std::size_t pos{0};
    if (conditionView.compare(pos, 2, "<=") == 0)
    {
        op = HitConditionOp::LessOrEqual;
        pos += 2;
    }
    else if (conditionView.compare(pos, 2, "==") == 0)
    {
        op = HitConditionOp::Equal;
        pos += 2;
    }
    else if (conditionView.compare(pos, 2, ">=") == 0)
    {
        op = HitConditionOp::GreaterOrEqual;
        pos += 2;
    }
    else if (conditionView.at(pos) == '<')
    {
        op = HitConditionOp::Less;
        pos += 1;
    }
    else if (conditionView.at(pos) == '>')
    {
        op = HitConditionOp::Greater;
        pos += 1;
    }
    else if (conditionView.at(pos) == '%')
    {
        op = HitConditionOp::EveryN;
        pos += 1;
    }

    uint32_t n{0};
    const char *const digitsEnd = conditionView.data() + conditionView.size();
    const auto [parseEnd, err] = std::from_chars(conditionView.data() + pos, digitsEnd, n);
    if (err != std::errc{} || parseEnd != digitsEnd)
    {
        output = "Invalid hit condition. Supported hit conditions: <N, <=N, ==N, >N, N or >=N, and %N, where N is an integer within the range 0 - 4294967295";
        return false;
    }

    if (op == HitConditionOp::EveryN && n == 0)
    {
        output = "Invalid hit condition: N must be greater than 0 for %N";
        return false;
    }

    hitCondition.op = op;
    hitCondition.n = n;
    return true;
}

// Exception filter lookup by name (binary search over gExceptionFilters, see internal_helpers.h).
const ExceptionFilterEntry *FindExceptionFilter(std::string_view name)
{
    const auto entryIt = std::lower_bound(gExceptionFilters.cbegin(), gExceptionFilters.cend(), name,
                                          [](const ExceptionFilterEntry &entry, std::string_view filterName)
                                          {
                                              return entry.name < filterName;
                                          });
    return (entryIt != gExceptionFilters.cend() && entryIt->name == name) ? &*entryIt : nullptr;
}

// Extract the `frameId` request argument. When the argument is absent, the frame identifier
// is built from the last stopped thread and the topmost frame.
FrameId GetFrameIdFromArguments(const json &arguments)
{
    const auto frameIdIter = arguments.find("frameId");
    return frameIdIter == arguments.cend()
               ? FrameId{ManagedDebugger::GetLastStoppedThreadId(), FrameLevel{0}}
               : FrameId{static_cast<int>(frameIdIter.value())};
}

// Read an optional line/column request field. For a 0-based client, a provided value shifts
// by one into the internal 1-based numbering; an omitted field stays zero (the internal
// "not provided" marker).
int32_t GetOptionalInternalField(const json &arguments, const char *fieldName, bool startsAt1)
{
    return startsAt1 ? arguments.value(fieldName, 0) : arguments.value(fieldName, -1) + 1;
}

// DAP request handler signature. The handlers run on the command executor thread (see
// CommandExecutor in dap.cpp) and are called through plain function pointers.
using CommandCallback = HRESULT (*)(const json &arguments, json &responseBody);

struct CommandEntry
{
    std::string_view name;
    CommandCallback callback;
};

// DAP request handlers. Note, the `cancel` command is handled entirely by the CommandLoop()
// pre-command action, so it has no entry here. The table is a constexpr array, so no runtime
// initialization or heap allocation is needed; the captureless lambdas are called through plain
// function pointers. It is sorted by name (ASCII) for binary-search lookup (see HandleCommand()).
constexpr std::array<CommandEntry, 32> gCommands{{
    {"attach", [](const json &arguments, json &responseBody)
        {
            if (GetProcessSetupComplete() && GetConfigurationDone())
            {
                responseBody.emplace("message", "Can't be used as part of the restart sequence. Use the 'restart' request instead.");
                return E_UNEXPECTED;
            }

            // Reject `attach` sent before `initialize` (DAP protocol violation).
            if (!GetInitialized())
            {
                responseBody.emplace("message", "The 'initialize' request must be sent first.");
                return E_UNEXPECTED;
            }

            // The pending-queue logic in CommandLoop guarantees `configurationDone` has been processed at this point.
            assert(GetConfigurationDone());

            HRESULT Status = S_OK;
            const DWORD processId = arguments.value("processId", 0);
            if (processId == 0)
            {
                return E_INVALIDARG;
            }

            IfFailRet(ManagedDebugger::Attach(processId));
            const std::map<std::string, std::string> env = GetDebugSessionEnvironment(arguments);
            ParseAndApplyDebugSessionOptions(arguments, env);

            IfFailRet(ManagedDebugger::StartDebugSession());
            GetProcessSetupComplete() = true;
            // `attach` is the final step of the initialization sequence.
            GetInitialized() = false;
            return S_OK;
        }},
    {"breakpointLocations", [](const json &arguments, json &responseBody)
        {
            HRESULT Status = S_OK;

            Source source;
            IfFailRet(ParseSourceJson(arguments.at("source"), 0, source));

            // For a 0-based client, provided values shift by one into the internal 1-based numbering;
            // an omitted optional field stays zero ("not provided").
            BreakpointLocation rangeToSearch;
            rangeToSearch.line = ToInternalLine(static_cast<int32_t>(arguments.at("line")));
            rangeToSearch.column = GetOptionalInternalField(arguments, "column", Config::GetColumnsStartAt1());
            rangeToSearch.endLine = GetOptionalInternalField(arguments, "endLine", Config::GetLinesStartAt1());
            rangeToSearch.endColumn = GetOptionalInternalField(arguments, "endColumn", Config::GetColumnsStartAt1());

            // Note, `column`, `endLine` and `endColumn` are optional; zero means the field is not provided.
            if (rangeToSearch.line <= 0 || rangeToSearch.column < 0 ||
                rangeToSearch.endLine < 0 || rangeToSearch.endColumn < 0)
            {
                return E_INVALIDARG;
            }

            std::vector<BreakpointLocation> locations;
            IfFailRet(ManagedDebugger::GetBreakpointLocations(source, rangeToSearch, locations));

            // DAP requires the 'breakpointLocations' response to be a sorted set of possible breakpoint locations.
            std::sort(locations.begin(), locations.end(), [](const BreakpointLocation &a, const BreakpointLocation &b)
            {
                return a.line < b.line || (a.line == b.line && a.column < b.column);
            });

            responseBody.emplace("breakpoints", std::move(locations));
            return S_OK;
        }},
    {"configurationDone", [](const json &/*arguments*/, json &/*responseBody*/)
        {
            assert(!GetProcessSetupComplete());
            GetConfigurationDone() = true;
            return S_OK;
        }},
    {"continue", [](const json &arguments, json &responseBody)
        {
            const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};
            const bool singleThread = arguments.value("singleThread", false);

            HRESULT Status = S_OK;
            bool allThreadsContinued = true;
            IfFailRet(ManagedDebugger::Continue(threadId, singleThread, allThreadsContinued));

            responseBody.emplace("allThreadsContinued", allThreadsContinued);
            responseBody.emplace("threadId", static_cast<int>(threadId));
            return S_OK;
        }},
    {"detach", [](const json &/*arguments*/, json &/*responseBody*/)
        {
            HRESULT Status = S_OK;
            IfFailRet(ManagedDebugger::Disconnect(ManagedDebugger::DisconnectAction::Detach));
            return S_OK;
        }},
    {"disconnect", [](const json &arguments, json &/*responseBody*/)
        {
            HRESULT Status = S_OK;
            const auto terminateArgIter = arguments.find("terminateDebuggee");
            ManagedDebugger::DisconnectAction action = ManagedDebugger::DisconnectAction::Default;
            if (terminateArgIter != arguments.cend())
            {
                action = terminateArgIter.value().get<bool>() ? ManagedDebugger::DisconnectAction::Terminate
                                                              : ManagedDebugger::DisconnectAction::Detach;
            }

            IfFailRet(ManagedDebugger::Disconnect(action));
            return S_OK;
        }},
    {"evaluate", [](const json &arguments, json &responseBody)
        {
            const std::string &expression = GetRequiredStringField(arguments, "expression");
            const FrameId frameId = GetFrameIdFromArguments(arguments);

            if (GetInternalConsole() && ManagedDebugger::IsProcessRunning())
            {
                // User pressed "Enter": the input is sent to the debuggee stdin with a trailing newline.
                std::string input;
                input.reserve(expression.size() + 1);
                input.append(expression).push_back('\n');
                ManagedDebugger::WriteStdin({input.data(), input.size()});
                responseBody.emplace("message", "Text redirected to debuggee stdin.");
                return S_OK;
            }

            HRESULT Status = S_OK;
            Variable variable;
            std::string output;
            if (FAILED(Status = ManagedDebugger::Evaluate(frameId, expression, variable, output)))
            {
                if (output.empty())
                {
                    responseBody.emplace("message", "error: " + FormatHexErrorCode(Status));
                }
                else
                {
                    responseBody.emplace("message", std::move(output));
                }
                return Status;
            }

            responseBody.emplace("result", std::move(variable.value));
            responseBody.emplace("type", std::move(variable.type));
            responseBody.emplace("variablesReference", variable.variablesReference);
            if (!variable.memoryReference.empty())
            {
                responseBody.emplace("memoryReference", std::move(variable.memoryReference));
            }
            return S_OK;
        }},
    {"exceptionInfo", [](const json &arguments, json &responseBody)
        {
            HRESULT Status = S_OK;
            const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};
            ExceptionInfo exceptionInfo;
            IfFailRet(ManagedDebugger::GetExceptionInfo(threadId, exceptionInfo));

            responseBody.emplace("exceptionId", std::move(exceptionInfo.exceptionId));
            responseBody.emplace("description", std::move(exceptionInfo.description));
            responseBody.emplace("breakMode", std::move(exceptionInfo.breakMode));
            responseBody.emplace("details", FormJsonForExceptionDetails(exceptionInfo.details));
            return S_OK;
        }},
    {"goto", [](const json &arguments, json &responseBody)
        {
            const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};
            const uint32_t targetId = arguments.at("targetId");

            HRESULT Status = S_OK;
            std::string output;
            if (FAILED(Status = ManagedDebugger::Goto(threadId, targetId, output)))
            {
                if (!output.empty())
                {
                    responseBody.emplace("message", std::move(output));
                }
                return Status;
            }
            return S_OK;
        }},
    {"gotoTargets", [](const json &arguments, json &responseBody)
        {
            HRESULT Status = S_OK;

            Source source;
            IfFailRet(ParseSourceJson(arguments.at("source"), 0, source));

            const int32_t line = ToInternalLine(static_cast<int32_t>(arguments.at("line")));
            // For a 0-based client, a provided column shifts by one into the internal 1-based
            // numbering; an omitted column stays zero ("not provided").
            const int32_t column = GetOptionalInternalField(arguments, "column", Config::GetColumnsStartAt1());

            std::vector<GotoTarget> targets;
            std::string output;
            if (FAILED(Status = ManagedDebugger::GetGotoTarget(source, line, column, targets, output)))
            {
                if (!output.empty())
                {
                    responseBody.emplace("message", std::move(output));
                }
                return Status;
            }

            responseBody.emplace("targets", std::move(targets));
            return S_OK;
        }},
    {"initialize", [](const json &arguments, json &responseBody)
        {
            // Reject a repeated `initialize` sent in the middle of the initialization sequence (DAP protocol violation).
            if (GetInitialized() && !GetConfigurationDone())
            {
                responseBody.emplace("message", "The initialization sequence is already in progress.");
                return E_UNEXPECTED;
            }
            // Reject `initialize` sent during an active debug session.
            if (ManagedDebugger::HaveProcess())
            {
                responseBody.emplace("message", "Can't start a new debug session while another debug session is active.");
                return E_UNEXPECTED;
            }

            // clientID, clientName, adapterID - not in use now

            // `linesStartAt1` and `columnsStartAt1` - whether the client uses 1-based line and
            // column numbering (both default to true when omitted).
            Config::SetLinesStartAt1(arguments.value("linesStartAt1", true));
            Config::SetColumnsStartAt1(arguments.value("columnsStartAt1", true));

            // `supportsMemoryReferences` - whether the client supports the `memoryReference` property
            // on variables (defaults to false when omitted). When false, no memory references are
            // provided and values are not pinned via GC handles.
            Config::SetSupportsMemoryReferences(arguments.value("supportsMemoryReferences", false));

            ManagedDebugger::InitializeDebugSession();
            AddCapabilitiesTo(responseBody);

            // Note: the `initialize` command starts the sequence of configuration requests.
            GetInitialized() = true;
            GetConfigurationDone() = false;
            GetProcessSetupComplete() = false;
            return S_OK;
        }},
    {"launch", [](const json &arguments, json &responseBody)
        {
            if (GetProcessSetupComplete() && GetConfigurationDone())
            {
                responseBody.emplace("message", "Can't be used as part of the restart sequence. Use the 'restart' request instead.");
                return E_UNEXPECTED;
            }

            // Reject `launch` sent before `initialize` (DAP protocol violation).
            if (!GetInitialized())
            {
                responseBody.emplace("message", "The 'initialize' request must be sent first.");
                return E_UNEXPECTED;
            }

            // The pending-queue logic in CommandLoop guarantees `configurationDone` has been processed at this point.
            assert(GetConfigurationDone());

            HRESULT Status = S_OK;
            std::map<std::string, std::string> env = GetDebugSessionEnvironment(arguments);
            // Session options (including the session environment variables) must be applied before
            // the launch options, so the remote console server setup uses the port value set for the session.
            ParseAndApplyDebugSessionOptions(arguments, env);

            std::string output;
            if (FAILED(Status = ParseAndApplyLaunchOptions(arguments, env, output)))
            {
                if (!output.empty())
                {
                    responseBody.emplace("message", std::move(output));
                }
                return Status;
            }

            IfFailRet(ManagedDebugger::StartDebugSession());
            GetProcessSetupComplete() = true;
            // `launch` is the final step of the initialization sequence.
            GetInitialized() = false;
            return S_OK;
        }},
    {"loadedSources", [](const json &/*arguments*/, json &responseBody)
        {
            std::vector<Source> sources;
            ManagedDebugger::GetLoadedSources(sources);

            responseBody.emplace("sources", std::move(sources));
            return S_OK;
        }},
    {"modules", [](const json &arguments, json &responseBody)
        {
            std::size_t totalModules = 0;
            std::vector<Module> modules;
            ManagedDebugger::GetModules(arguments.value("startModule", 0), arguments.value("moduleCount", 0),
                                        modules, totalModules);

            responseBody.emplace("modules", std::move(modules));
            responseBody.emplace("totalModules", totalModules);
            return S_OK;
        }},
    {"next", [](const json &arguments, json &/*responseBody*/)
        {
            const bool singleThread = arguments.value("singleThread", false);
            return ManagedDebugger::StepCommand(ThreadId{static_cast<int>(arguments.at("threadId"))},
                                                StepType::STEP_OVER, singleThread);
        }},
    {"pause", [](const json &arguments, json &/*responseBody*/)
        {
            const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};
            return ManagedDebugger::Pause(threadId);
        }},
    {"readMemory", [](const json &arguments, json &responseBody)
        {
            const std::string &memoryReference = GetRequiredStringField(arguments, "memoryReference");
            const int32_t offset = arguments.value("offset", 0);
            const uint32_t count = arguments.at("count");

            HRESULT Status = S_OK;
            std::string address;
            std::vector<uint8_t> data;
            IfFailRet(ManagedDebugger::ReadMemory(memoryReference, offset, count, address, data));

            try
            {
                auto base64Data = base64::encode_into<std::string>(data.cbegin(), data.cend());
                responseBody.emplace("data", std::move(base64Data));
            }
            catch (const std::exception &ex)
            {
                responseBody.emplace("message", ex.what());
                return E_FAIL;
            }

            responseBody.emplace("address", std::move(address));
            responseBody.emplace("unreadableBytes", 0);
            return S_OK;
        }},
    {"restart", [](const json &arguments, json &responseBody)
        {
            if (!GetProcessSetupComplete() || !GetConfigurationDone())
            {
                responseBody.emplace("message", "Can't be used as part of the initialization sequence. Use the 'attach' or 'launch' request instead.");
                return E_UNEXPECTED;
            }

            HRESULT Status = S_OK;

            if (ManagedDebugger::HaveProcess())
            {
                IfFailRet(ManagedDebugger::Disconnect(ManagedDebugger::DisconnectAction::Default));
            }
            else
            {
                ManagedDebugger::CleanupDebugSession();
            }

            // Note, the optional `arguments` field carries the (possibly updated) original launch or attach
            // arguments, so the restart target may differ from the one used initially.
            const auto restartArgumentsIt = arguments.find("arguments");
            if (restartArgumentsIt != arguments.cend())
            {
                const json &restartArguments = restartArgumentsIt.value();
                std::map<std::string, std::string> env = GetDebugSessionEnvironment(restartArguments);

                // Session options (including the session environment variables) must be applied before
                // the launch or attach handling, so the remote console server setup uses the port
                // value set for the session.
                ParseAndApplyDebugSessionOptions(restartArguments, env);

                const DWORD processId = restartArguments.value("processId", 0);
                if (processId != 0)
                {
                    IfFailRet(ManagedDebugger::Attach(processId));
                }
                else
                {
                    std::string output;
                    if (FAILED(Status = ParseAndApplyLaunchOptions(restartArguments, env, output)))
                    {
                        if (!output.empty())
                        {
                            responseBody.emplace("message", std::move(output));
                        }
                        return Status;
                    }
                }
            }

            return ManagedDebugger::StartDebugSession();
        }},
    {"scopes", [](const json &arguments, json &responseBody)
        {
            HRESULT Status = S_OK;
            std::vector<Scope> scopes;
            const FrameId frameId{static_cast<int>(arguments.at("frameId"))};
            IfFailRet(ManagedDebugger::GetScopes(frameId, scopes));

            responseBody.emplace("scopes", std::move(scopes));
            return S_OK;
        }},
    {"setBreakpoints", [](const json &arguments, json &responseBody)
        {
            HRESULT Status = S_OK;

            std::vector<SourceBreakpoint> sourceBreakpoints;
            const json &breakpointsJson = arguments.at("breakpoints");
            sourceBreakpoints.reserve(breakpointsJson.size());
            for (const auto &b : breakpointsJson)
            {
                const int32_t line = ToInternalLine(static_cast<int32_t>(b.at("line")));
                // For a 0-based client, a provided column shifts by one into the internal
                // 1-based numbering; an omitted column stays zero ("not provided").
                const int32_t column = GetOptionalInternalField(b, "column", Config::GetColumnsStartAt1());

                const std::string_view requestHitCondition = GetOptionalStringView(b, "hitCondition");
                HitCondition hitCondition;
                std::string output;
                if (!ParseHitCondition(requestHitCondition, hitCondition, output))
                {
                    responseBody.emplace("message", std::move(output));
                    return E_INVALIDARG;
                }

                sourceBreakpoints.emplace_back(line, column, b.value("condition", std::string()),
                                               hitCondition, b.value("logMessage", std::string()));
            }

            Source source;
            IfFailRet(ParseSourceJson(arguments.at("source"), 0, source));

            std::vector<Breakpoint> breakpoints;
            IfFailRet(ManagedDebugger::SetSourceBreakpoints(source, sourceBreakpoints, breakpoints));

            responseBody.emplace("breakpoints", std::move(breakpoints));
            return S_OK;
        }},
    {"setExceptionBreakpoints", [](const json &arguments, json &/*responseBody*/)
        {
            const auto filtersIt = arguments.find("filters");
            const json::array_t noFilters;
            const json::array_t &filters = filtersIt == arguments.cend() ?
                                                noFilters : filtersIt->get_ref<const json::array_t &>();
            // Note: the `filterOptions` entries are inspected in place (through JSON references),
            // so no intermediate std::map per entry is built. get_ref() throws a json type_error
            // for a non-array `filterOptions` value (a JSON null included), so such a request is
            // reported as failed.
            const auto filterOptionsIt = arguments.find("filterOptions");
            const json::array_t noFilterOptions;
            const json::array_t &filterOptions = filterOptionsIt == arguments.cend() ?
                                                      noFilterOptions : filterOptionsIt->get_ref<const json::array_t &>();

            // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_SetExceptionBreakpoints
            // The 'filter' and 'filterOptions' sets are additive.
            // Response to 'setExceptionBreakpoints' request:
            // ... The Breakpoint objects are in the same order as the elements of the 'filters', 'filterOptions',
            // 'exceptionOptions' arrays given as arguments.
            std::vector<ExceptionBreakpoint> exceptionBreakpoints;
            exceptionBreakpoints.reserve(filters.size() + filterOptions.size());

            for (const json &entry : filters)
            {
                const ExceptionFilterEntry *filter = FindExceptionFilter(entry.get_ref<const std::string &>());
                if (filter == nullptr)
                {
                    return E_INVALIDARG;
                }
                // in case of DAP, we can't set up categoryHint during breakpoint setup,
                // since this protocol doesn't provide such information
                exceptionBreakpoints.emplace_back(ExceptionCategory::ANY, filter->filter);
            }

            for (const json &entry : filterOptions)
            {
                const auto findId = entry.find("filterId");
                if (findId == entry.cend() || !findId->is_string() ||
                    findId->get_ref<const std::string &>().empty())
                {
                    return E_INVALIDARG;
                }

                const ExceptionFilterEntry *filter = FindExceptionFilter(findId->get_ref<const std::string &>());
                if (filter == nullptr)
                {
                    return E_INVALIDARG;
                }
                // in case of DAP, we can't set up categoryHint during breakpoint setup,
                // since this protocol doesn't provide such information
                exceptionBreakpoints.emplace_back(ExceptionCategory::ANY, filter->filter);

                const auto findCondition = entry.find("condition");
                if (findCondition == entry.cend() || !findCondition->is_string() ||
                    findCondition->get_ref<const std::string &>().empty())
                {
                    continue;
                }

                // Split the condition into tokens separated by whitespace or ','. The condition is tokenized
                // through a string_view, so it is neither copied nor rewritten.
                const std::string_view conditionText = findCondition->get_ref<const std::string &>();
                const auto isSeparator = [](char ch)
                {
                    return ch == ',' || std::isspace(static_cast<unsigned char>(ch)) != 0;
                };
                std::size_t pos = 0;
                while (true)
                {
                    while (pos < conditionText.size() && isSeparator(conditionText.at(pos)))
                    {
                        ++pos;
                    }

                    if (pos == conditionText.size())
                    {
                        break; // reached the end of the condition
                    }

                    const std::size_t tokenBegin = pos;
                    while (pos < conditionText.size() && !isSeparator(conditionText.at(pos)))
                    {
                        ++pos;
                    }

                    const std::string_view token = conditionText.substr(tokenBegin, pos - tokenBegin);

                    // Note, a lone '!' token (with no exception type after it) is ignored.
                    if (token == "!")
                    {
                        continue;
                    }

                    // Wildcards (e.g. "System.*", "*", "System.Exc*") are stored in a
                    // separate wildcardConditions container. The trailing '*' is removed:
                    // it only marks the token as a wildcard and is not needed for prefix
                    // matching. Note, a lone '*' token becomes an empty string, i.e. it
                    // matches any exception type.
                    if (token.back() == '*')
                    {
                        exceptionBreakpoints.back().wildcardConditions.emplace_back(
                            token.substr(0, token.size() - 1));
                        continue;
                    }

                    exceptionBreakpoints.back().conditions.emplace(token);
                }
            }

            HRESULT Status = S_OK;
            std::vector<Breakpoint> breakpoints;
            IfFailRet(ManagedDebugger::SetExceptionBreakpoints(exceptionBreakpoints, breakpoints));

            // TODO form responseBody with breakpoints (optional output, MS vsdbg doesn't provide it for VS Code IDE now)
            // responseBody.emplace("breakpoints", breakpoints);

            return S_OK;
        }},
    {"setExpression", [](const json &arguments, json &responseBody)
        {
            const std::string &expression = GetRequiredStringField(arguments, "expression");
            const std::string &value = GetRequiredStringField(arguments, "value");
            const FrameId frameId = GetFrameIdFromArguments(arguments);

            HRESULT Status = S_OK;
            std::string output;
            if (FAILED(Status = ManagedDebugger::SetExpression(frameId, expression, value, output)))
            {
                if (output.empty())
                {
                    responseBody.emplace("message", "error: " + FormatHexErrorCode(Status));
                }
                else
                {
                    responseBody.emplace("message", std::move(output));
                }
                return Status;
            }
            // TODO: add `memoryReference`
            responseBody.emplace("value", std::move(output));
            return S_OK;
        }},
    {"setFunctionBreakpoints", [](const json &arguments, json &responseBody)
        {
            HRESULT Status = S_OK;

            std::vector<FunctionBreakpoint> functionBreakpoints;
            const json &breakpointsJson = arguments.at("breakpoints");
            functionBreakpoints.reserve(breakpointsJson.size());
            for (const auto &b : breakpointsJson)
            {
                const std::string &fullName = GetRequiredStringField(b, "name");
                std::string name;
                std::string params;

                // The function breakpoint name may carry a parameter list, e.g.
                // "Foo(Bar)" -> name "Foo" and params "(Bar)". The parameter list must
                // end the name: a malformed name like "Foo(Bar" or "Foo(Bar)Baz" fails
                // the request.
                const std::size_t openParen = fullName.find('(');
                if (openParen == std::string::npos)
                {
                    name = fullName;
                }
                else
                {
                    const std::size_t closeParen = fullName.find(')', openParen);
                    if (openParen == 0 || closeParen == std::string::npos ||
                        closeParen + 1 != fullName.size())
                    {
                        responseBody.emplace("message",
                            "Cannot parse the function breakpoint name '" + fullName +
                            "'. The parameter list must be enclosed in parentheses at the end of the name, e.g. 'Foo(Bar)'.");
                        return E_INVALIDARG;
                    }

                    name.assign(fullName, 0, openParen);
                    params.assign(fullName, openParen, std::string::npos);
                }

                const std::string_view requestHitCondition = GetOptionalStringView(b, "hitCondition");
                HitCondition hitCondition;
                std::string output;
                if (!ParseHitCondition(requestHitCondition, hitCondition, output))
                {
                    responseBody.emplace("message", std::move(output));
                    return E_INVALIDARG;
                }

                functionBreakpoints.emplace_back(std::move(name), std::move(params), b.value("condition", std::string()),
                                                 hitCondition);
            }

            std::vector<Breakpoint> breakpoints;
            IfFailRet(ManagedDebugger::SetFunctionBreakpoints(functionBreakpoints, breakpoints));

            responseBody.emplace("breakpoints", std::move(breakpoints));
            return Status;
        }},
    {"setVariable", [](const json &arguments, json &responseBody)
        {
            const std::string &name = GetRequiredStringField(arguments, "name");
            const std::string &value = GetRequiredStringField(arguments, "value");
            const int ref = arguments.at("variablesReference");

            HRESULT Status = S_OK;
            std::string output;
            if (FAILED(Status = ManagedDebugger::SetVariable(name, value, ref, output)))
            {
                responseBody.emplace("message", std::move(output));
                return Status;
            }
            // TODO: add `type` and `memoryReference`
            responseBody.emplace("value", std::move(output));
            return S_OK;
        }},
    {"source", [](const json &arguments, json &responseBody)
        {
            HRESULT Status = S_OK;

            // Note, per the DAP spec, `source` is optional, but `sourceReference` is required.
            // The `sourceReference` at the top level of the arguments is used when
            // `source` is not provided, or provides no `sourceReference` itself.
            const int32_t sourceReference = arguments.at("sourceReference");

            Source source;
            const auto sourceIt = arguments.find("source");
            if (sourceIt != arguments.cend())
            {
                IfFailRet(ParseSourceJson(*sourceIt, sourceReference, source));
            }
            else
            {
                source = Source(std::string(), sourceReference);
            }

            std::string sourceContent;
            if (FAILED(Status = ManagedDebugger::GetSourceContent(source, sourceContent)))
            {
                if (!sourceContent.empty())
                {
                    responseBody.emplace("message", std::move(sourceContent));
                }
                return Status;
            }

            responseBody.emplace("content", std::move(sourceContent));
            return S_OK;
        }},
    {"stackTrace", [](const json &arguments, json &responseBody)
        {
            HRESULT Status = S_OK;

            const ThreadId threadId{static_cast<int>(arguments.at("threadId"))};

            std::vector<StackFrame> stackFrames;
            IfFailRet(ManagedDebugger::GetStackTrace(threadId, FrameLevel{arguments.value("startFrame", 0)},
                                                     static_cast<unsigned>(arguments.value("levels", 0)), stackFrames));

            responseBody.emplace("totalFrames", stackFrames.size()); // Read the size before stackFrames is moved from.
            responseBody.emplace("stackFrames", std::move(stackFrames));
            return S_OK;
        }},
    {"stepIn", [](const json &arguments, json &/*responseBody*/)
        {
            const bool singleThread = arguments.value("singleThread", false);
            return ManagedDebugger::StepCommand(ThreadId{static_cast<int>(arguments.at("threadId"))},
                                                StepType::STEP_IN, singleThread);
        }},
    {"stepOut", [](const json &arguments, json &/*responseBody*/)
        {
            const bool singleThread = arguments.value("singleThread", false);
            return ManagedDebugger::StepCommand(ThreadId{static_cast<int>(arguments.at("threadId"))},
                                                StepType::STEP_OUT, singleThread);
        }},
    {"terminate", [](const json &/*arguments*/, json &/*responseBody*/)
        {
            HRESULT Status = S_OK;
            IfFailRet(ManagedDebugger::Disconnect(ManagedDebugger::DisconnectAction::Terminate));
            return S_OK;
        }},
    {"threads", [](const json &/*arguments*/, json &responseBody)
        {
            HRESULT Status = S_OK;
            std::vector<Thread> threads;
            IfFailRet(ManagedDebugger::GetThreads(threads));

            responseBody.emplace("threads", std::move(threads));
            return S_OK;
        }},
    {"variables", [](const json &arguments, json &responseBody)
        {
            HRESULT Status = S_OK;
            std::vector<Variable> variables;
            IfFailRet(ManagedDebugger::GetVariables(arguments.at("variablesReference"), variables));

            responseBody.emplace("variables", std::move(variables));
            return S_OK;
        }},
    {"writeMemory", [](const json &arguments, json &responseBody)
        {
            const std::string &memoryReference = GetRequiredStringField(arguments, "memoryReference");
            const int32_t offset = arguments.value("offset", 0);

            std::vector<uint8_t> data;
            try
            {
                // Decode straight from the string stored in the JSON arguments, so the
                // (possibly large) base64 payload is not copied into an intermediate string.
                data = base64::decode_into<std::vector<uint8_t>>(arguments.at("data").get_ref<const std::string &>());
            }
            catch (const std::exception &ex)
            {
                responseBody.emplace("message", ex.what());
                return E_FAIL;
            }

            HRESULT Status = S_OK;
            IfFailRet(ManagedDebugger::WriteMemory(memoryReference, offset, data));
            return S_OK;
        }}}};

} // unnamed namespace

HRESULT HandleCommand(const std::string &command, const nlohmann::json &arguments, nlohmann::json &responseBody)
{
    const std::string_view commandView(command);
    const auto commandIt = std::lower_bound(gCommands.cbegin(), gCommands.cend(), commandView,
                                            [](const CommandEntry &entry, std::string_view name)
                                            {
                                                return entry.name < name;
                                            });
    if (commandIt == gCommands.cend() || commandIt->name != commandView)
    {
        responseBody.emplace("message", "Request '" + command + "' is not supported.");
        return E_NOTIMPL;
    }

    return commandIt->callback(arguments, responseBody);
}

} // namespace dncdbg::DAP
