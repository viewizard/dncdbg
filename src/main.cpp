// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "buildinfo.h"
#include "protocol/dap.h"
#include "protocol/dapio.h"
#include "utils/logger.h"

#include <string>
#include <cstdlib>
#include <iostream>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
static void setenv(const char *var, const char *val, int) { _putenv_s(var, val); }
#endif

namespace
{

void print_help()
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
              << "--loglevel=number/level                  Minimal logging level:\n"
              << "                                         0 or DEBUG (available for debug build only)\n"
              << "                                         1 or INFO\n"
              << "                                         2 or WARNING\n"
              << "                                         3 or ERROR\n"
              << "                                         by default, set to INFO.\n"
              << "--version                                Displays the current version.\n";
}

void print_buildinfo()
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

void print_version()
{
    std::cout << "DNCDbg version " << BuildInfo::version << "\n";
}

} // unnamed namespace

int
#if defined(_WIN32) && defined(_TARGET_X86_)
    __cdecl
#endif
    main(int argc, char *argv[])
{
    // prevent std::cout flush triggered by read operation on std::cin
    std::cin.tie(nullptr);

    std::string protocolLogFilePath;
    // Converts all arguments, skip the program name (argv[0])
    const std::vector<std::string> args(argv + 1, argv + argc);

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
            // VSCode IDE sends this option silently to debugger, just ignore it
        }}};

    const std::vector<std::pair<std::string, std::function<void(const std::string &arg)>>> partialArguments{
        {"--logProtocol=", [&](const std::string &arg) {
            protocolLogFilePath = arg.substr(strlen("--logProtocol="));
        }},
        {"--log=", [&](const std::string &arg) {
            dncdbg::Logger::OpenLogStream(arg.substr(strlen("--log=")).c_str());
        }},
        {"--loglevel=", [&](const std::string &arg) {
            dncdbg::Logger::SetLogLevel(arg.substr(strlen("--loglevel=")).c_str());
        }}
    };

    for (const std::string &arg : args)
    {
        auto findEntire = entireArguments.find(arg);
        if (findEntire != entireArguments.end())
        {
            findEntire->second();
        }
        else
        {
            bool found = false;
            for (auto const &entry : partialArguments)
            {
                // Note: starts_with() is C++20, use rfind() for compatibility
                if (arg.rfind(entry.first, 0) == 0)
                {
                    entry.second(arg);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                std::cerr << "Error: Unknown option " << arg << "\n";
                exit(EXIT_FAILURE);
            }
        }
    }

    LOGI(log << "DNCDbg started");
    // Note: there is no possibility to know which exception caused call to std::terminate
    std::set_terminate([] { LOGE(log << "DNCDbg is terminated due to call to std::terminate: see stderr..."); });

#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    if (!protocolLogFilePath.empty())
    {
        dncdbg::DAPIO::SetupProtocolLogging(protocolLogFilePath);
    }

    dncdbg::DAP protocol;

    protocol.CommandLoop();
    return EXIT_SUCCESS;
}
