// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "buildinfo.h"
#include "protocol/dap.h"
#include "utils/logger.h"

#include <string>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
static void setenv(const char *var, const char *val, int) { _putenv_s(var, val); }
#endif

namespace dncdbg
{

static void print_help()
{
#ifdef _WIN32
    std::cout << "Usage: dncdbg.exe [options]\n"
#else
    std::cout << "Usage: dncdbg [options]\n"
#endif
              << "\n"
              << "Options:\n"
              << "--buildinfo                              Print build info.\n"
              << "--logProtocol=<path to log file>         Enable protocol interaction logging to file.\n"
              << "--log=<path to log file>                 Enable debugger logging to file.\n"
              << "                                         File log by default. File is created in 'current' folder.\n"
              << "--version                                Displays the current version.\n";
}

static void print_buildinfo()
{
    std::cout << "DNCDbg version " << BuildInfo::version << "\n\n"
              << "Build info:\n"
              << "      Build type:  " << BuildInfo::build_type << "\n"
              << "      Build date:  " << BuildInfo::date << " " << BuildInfo::time <<"\n"
              << "      Target OS:   " << BuildInfo::os_name << "\n"
              << "      Target arch: " << BuildInfo::cpu_arch << "\n\n"

#if defined(CASE_INSENSITIVE_FILENAME_COLLISION) || defined(CASE_SENSITIVE_FILENAME_COLLISION)
              << "Compiled options:\n"
#ifdef CASE_INSENSITIVE_FILENAME_COLLISION
              << "      CASE_INSENSITIVE_FILENAME_COLLISION\n"
#endif
#ifdef CASE_SENSITIVE_FILENAME_COLLISION
              << "      CASE_SENSITIVE_FILENAME_COLLISION\n"
#endif
              << "\n"
#endif

              << "DNCDbg VCS info:   " << BuildInfo::dncdbg_vcs_info << "\n\n"
              << "Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.\n"
              << "Copyright (c) 2026 Mikhail Kurinnoi\n"
              << "Distributed under the MIT License.\n";
}

static void print_version()
{
    std::cout << "DNCDbg version " << BuildInfo::version << "\n";
}

} // namespace dncdbg

using namespace dncdbg;

static void FindAndParseArgs(char **argv, std::vector<std::pair<std::string, std::function<void(int &i)>>> &partialArguments, int i)
{
    for (auto const &argument : partialArguments)
    {
        if (strstr(argv[i], argument.first.c_str()) == argv[i])
        {
            argument.second(i);
            return;
        }
    }
    static_cast<void>(fprintf(stderr, "Error: Unknown option %s\n", argv[i]));
    exit(EXIT_FAILURE);
}

int
#if defined(_WIN32) && defined(_TARGET_X86_)
    __cdecl
#endif
    main(int argc, char *argv[])
{
    // prevent std::cout flush triggered by read operation on std::cin
    std::cin.tie(nullptr);

    std::string protocolLogFilePath;

    std::unordered_map<std::string, std::function<void()>> entireArguments{
        {"--help", [&]() {
            print_help();
            exit(EXIT_SUCCESS);
        }},
        {"--buildinfo", [&]() {
            print_buildinfo();
            exit(EXIT_SUCCESS);
        }},
        {"--version", [&]() {
            print_version();
            exit(EXIT_SUCCESS);
        }},
        {"--interpreter=vscode" , [&]() {
            // VSCode IDE send this option silently to debugger, just ignore it
        }}};

    std::vector<std::pair<std::string, std::function<void(int &i)>>> partialArguments{
        {"--logProtocol=", [&](int &i) {
            protocolLogFilePath = argv[i] + strlen("--logProtocol="); // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        }},
        {"--log=", [&](int &i) {
            setenv("LOG_OUTPUT", argv[i] + strlen("--log="), 1); // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        }}
    };

    for (int i = 1; i < argc; i++)
    {
        auto args = entireArguments.find(std::string(argv[i]));
        if (args != entireArguments.end())
        {
            args->second();
        }
        else
        {
            FindAndParseArgs(argv, partialArguments, i);
        }
    }

    LOGI("DNCDbg started");
    // Note: there is no possibility to know which exception caused call to std::terminate
    std::set_terminate([] { LOGF("DNCDbg is terminated due to call to std::terminate: see stderr..."); });

#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    DAP protocol(std::cin, std::cout);

    if (!protocolLogFilePath.empty())
    {
        protocol.SetupProtocolLogging(protocolLogFilePath);
    }

    protocol.CommandLoop();
    return EXIT_SUCCESS;
}
