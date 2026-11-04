// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "buildinfo.h"
#include "config/config.h"
#include "debugger/manageddebugger.h"
#include "protocol/dap.h"
#include "utils/logger.h"

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#if defined(FEATURE_PAL) && !defined(__APPLE__)
#include <langinfo.h>
#endif

#ifdef DEBUG_INTERNAL_TESTS
extern void RunInternalTests();
#endif // DEBUG_INTERNAL_TESTS

namespace
{

void PrintHelp()
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

void PrintBuildinfo()
{
    std::cout << "DNCDbg version " << BuildInfo::version << "\n\n"
              << "Build info:\n"
              << "      Build type:  " << BuildInfo::buildType << "\n"
              << "      Build date:  " << BuildInfo::date << " " << BuildInfo::time <<"\n"
              << "      Target OS:   " << BuildInfo::osName << "\n"
              << "      Target arch: " << BuildInfo::cpuArch << "\n\n"

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

              << "DNCDbg VCS info:   " << BuildInfo::dncdbgVcsInfo << "\n\n"
              << "Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.\n"
              << "Copyright (c) 2026 Mikhail Kurinnoi\n"
              << "Distributed under the MIT License.\n";
}

void PrintVersion()
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
#if defined(FEATURE_PAL) && !defined(__APPLE__)
    // std::mbstowcs(), std::towupper() and std::wcstombs() (see ToUppercase()
    // in utils/utftoupper_unix.cpp) need a UTF-8-capable LC_CTYPE locale. Try
    // the locale from the environment first, but do not trust the result:
    // setlocale(LC_ALL, "") can fail on systems with no locales generated at
    // all (e.g. a minimal Ubuntu chroot), and, on glibc, it can also succeed
    // while selecting the ASCII-only "C" locale (e.g. a Docker container with
    // `LANG` unset). Therefore, verify the codeset of the applied locale and,
    // when it is not UTF-8, fall back to the "C.UTF-8" locale (built into
    // glibc and musl, so it works without the `locales` package) and, as a
    // last resort, to the plain "C" locale, which is guaranteed to succeed by
    // the ISO C standard. Only a failure to select the latter (not expected in
    // practice) is treated as fatal. Note: with the plain "C" locale,
    // ToUppercase() handles ASCII input only and returns its input unchanged
    // otherwise, so the program keeps working in either case.
    const bool utf8Locale = std::setlocale(LC_ALL, "") != nullptr &&
                            std::strcmp(nl_langinfo(CODESET), "UTF-8") == 0;
    if (!utf8Locale && std::setlocale(LC_ALL, "C.UTF-8") == nullptr)
    {
        if (std::setlocale(LC_ALL, "C") == nullptr)
        {
            std::cerr << "Failed to set locale.\n";
            return EXIT_FAILURE;
        }
    }
#endif // defined(FEATURE_PAL) && !defined(__APPLE__)

    // Force the "C" numeric locale regardless of the locale selected above, so
    // std::stof()/std::stod() (real literals) and std::stoll()/std::stoull()
    // (integer literals, see expressionparser/helpers.cpp), as well as other
    // numeric conversions, always recognize '.' as the decimal separator.
    if (std::setlocale(LC_NUMERIC, "C") == nullptr)
    {
        std::cerr << "Failed to set numeric locale to C.\n";
        return EXIT_FAILURE;
    }

    // prevent std::cout flush triggered by read operation on std::cin
    std::cin.tie(nullptr);

    std::string protocolLogFilePath;
    try
    {
#ifdef DEBUG_INTERNAL_TESTS
        RunInternalTests();
#endif // DEBUG_INTERNAL_TESTS

        // Converts all arguments, skip the program name (argv[0])
        const std::vector<std::string> args(argv + 1, argv + argc);

        std::unordered_map<std::string, std::function<void()>> entireArguments{
            {"--help", [&] {
                PrintHelp();
                exit(EXIT_SUCCESS);
            }},
            {"--buildinfo", [&] {
                PrintBuildinfo();
                exit(EXIT_SUCCESS);
            }},
            {"--version", [&] {
                PrintVersion();
                exit(EXIT_SUCCESS);
            }},
            {"--interpreter=vscode", [&] {
                // Running under the VS Code IDE, which adds this option silently
                dncdbg::Config::SetRunningViaVsDbgUI(true);
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
            const auto findEntire = entireArguments.find(arg);
            if (findEntire != entireArguments.cend())
            {
                findEntire->second();
            }
            else
            {
                const auto it = std::find_if(partialArguments.cbegin(), partialArguments.cend(),
                    [&arg](const auto &entry)
                    {
                        // Note: starts_with() is C++20, use rfind() for compatibility
                        return arg.rfind(entry.first, 0) == 0;
                    });
                if (it != partialArguments.cend())
                {
                    it->second(arg);
                }
                else
                {
                    std::cerr << "Error: Unknown option " << arg << "\n";
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    catch (...)
    {
        std::cerr << "Fatal error: unknown exception\n";
        return EXIT_FAILURE;
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
        dncdbg::DAP::SetupProtocolLogging(protocolLogFilePath);
    }

    try
    {
        dncdbg::Config::Initialize();

        dncdbg::ManagedDebugger::Initialize();

        dncdbg::DAP::CommandLoop();

        dncdbg::ManagedDebugger::Shutdown();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << "\n";
        LOGE(log << "Fatal error: " << e.what());
        return EXIT_FAILURE;
    }
    catch (...)
    {
        std::cerr << "Fatal error: unknown exception\n";
        LOGE(log << "Fatal error: unknown exception");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
