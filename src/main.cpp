// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include <exception>
#include <string>
#include "utils/limits.h"
#include <stdio.h>
#include <stdlib.h>
#include <iostream>

#include "buildinfo.h"
#include "debugger/manageddebugger.h"
#include "managed/interop.h"
#include "protocol/dap.h"
#include "utils/logger.h"

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
    fprintf(stdout, "Usage: dncdbg.exe [options]\n"
#else
    fprintf(stdout, "Usage: dncdbg [options]\n"
#endif
                    "\n"
                    "Options:\n"
                    "--buildinfo                              Print build info.\n"
                    "--logProtocol=<path to log file>         Enable protocol interaction logging to file.\n"
                    "--log=<path to log file>                 Enable debugger logging to file.\n"
                    "                                         File log by default. File is created in 'current' folder.\n"
                    "--version                                Displays the current version.\n");
}

static void print_buildinfo()
{
    printf("DNCDbg version %s\n", BuildInfo::version);

    printf("\nBuild info:\n"
           "      Build type:  %s\n"
           "      Build date:  %s %s\n"
           "      Target OS:   %s\n"
           "      Target arch: %s\n\n",
           BuildInfo::build_type, BuildInfo::date, BuildInfo::time, BuildInfo::os_name, BuildInfo::cpu_arch);

    printf("DNCDbg VCS info:   %s\n\n", BuildInfo::dncdbg_vcs_info);
    printf("Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.\n");
    printf("Copyright (c) 2026 Mikhail Kurinnoi\n");
    printf("Distributed under the MIT License.\n");
    printf("See the LICENSE file in the project root for more information.\n");
}

static void print_version()
{
    printf("DNCDbg version %s\n", BuildInfo::version);
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
    fprintf(stderr, "Error: Unknown option %s\n", argv[i]);
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

    std::unordered_map<std::string, std::function<void(int &i)>> entireArguments{
        {"--help", [&](int &i) {
            print_help();
            exit(EXIT_SUCCESS);
        }},
        {"--buildinfo", [&](int &i) {
            print_buildinfo();
            exit(EXIT_SUCCESS);
        }},
        {"--version", [&](int &i) {
            print_version();
            exit(EXIT_SUCCESS);
        }},
        {"--interpreter=vscode" , [&](int &i) {
            // VSCode IDE send this option silently to debugger, just ignore it
        }}};

    std::vector<std::pair<std::string, std::function<void(int &i)>>> partialArguments{
        {"--logProtocol=", [&](int &i) {
            protocolLogFilePath = argv[i] + strlen("--logProtocol=");
        }},
        {"--log=", [&](int &i) {
            setenv("LOG_OUTPUT", argv[i] + strlen("--log="), 1);
        }}
    };

    for (int i = 1; i < argc; i++)
    {
        auto args = entireArguments.find(std::string(argv[i]));
        if (args != entireArguments.end())
        {
            args->second(i);
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

    std::shared_ptr<ManagedDebugger> debugger;
    try
    {
        debugger.reset(new ManagedDebugger(&protocol));
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "%s\n", e.what());
        exit(EXIT_FAILURE);
    }

    protocol.SetDebugger(debugger);
    protocol.CommandLoop();
    Interop::Shutdown();
    return EXIT_SUCCESS;
}
