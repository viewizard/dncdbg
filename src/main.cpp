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
#define PATH_MAX MAX_PATH
static void setenv(const char *var, const char *val, int) { _putenv_s(var, val); }
#define getpid() (GetCurrentProcessId())
#endif

namespace dncdbg
{

static void print_help()
{
    fprintf(stdout, ".NET Core debugger\n"
                    "\n"
                    "Options:\n"
                    "--buildinfo                              Print build info.\n"
                    "--attach <process-id>                    Attach the debugger to the specified process id.\n"
                    "--run                                    Run program without waiting commands\n"
                    "--protocolMessages[=<path to log file>]  Enable logging protocol interaction to DAP or file.\n"
                    "--log[=<file>]                           Enable logging. Supported logging to file only.\n"
                    "                                         File log by default. File is created in 'current' folder.\n"
                    "--version                                Displays the current version.\n");
}

static void print_buildinfo()
{
    printf(".NET Core debugger %s\n", BuildInfo::version);

    printf("\nBuild info:\n"
           "      Build type:  %s\n"
           "      Build date:  %s %s\n"
           "      Target OS:   %s\n"
           "      Target arch: %s\n\n",
           BuildInfo::build_type, BuildInfo::date, BuildInfo::time, BuildInfo::os_name, BuildInfo::cpu_arch);

    printf("DNCDbg VCS info:   %s\n", BuildInfo::dncdbg_vcs_info);
}

static void print_version()
{
    printf("NET Core debugger %s (%s, %s)\n\n", BuildInfo::version, BuildInfo::dncdbg_vcs_info, BuildInfo::build_type);
    printf("Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.\n");
    printf("Copyright (c) 2026 Mikhail Kurinnoi\n");
    printf("Distributed under the MIT License.\n");
    printf("See the LICENSE file in the project root for more information.\n");
}

} // namespace dncdbg

using namespace dncdbg;

static void FindAndParseArgs(char **argv, std::vector<std::pair<std::string, std::function<void(int &i)>>> &partialArguments, int i)
{
    for (auto argument : partialArguments)
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

static void CheckStartOptions(std::string &execFile, bool run)
{
    if (run && execFile.empty())
    {
        fprintf(stderr, "--run option was given, but no executable file specified!\n");
        exit(EXIT_FAILURE);
    }
}

static HRESULT AttachToExistingProcess(ManagedDebugger *pDebugger, DWORD pidDebuggee)
{
    HRESULT Status;
    IfFailRet(pDebugger->Initialize());
    IfFailRet(pDebugger->Attach(pidDebuggee));
    return pDebugger->ConfigurationDone();
}

static HRESULT LaunchNewProcess(ManagedDebugger *pDebugger, std::string &execFile, std::vector<std::string> &execArgs)
{
    HRESULT Status;
    IfFailRet(pDebugger->Initialize());

    try
    {
        IfFailRet(pDebugger->Launch(execFile, execArgs, {}, {}, false));
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "%s\n", e.what());
        exit(EXIT_FAILURE);
    }

    return pDebugger->ConfigurationDone();
}

int
#if defined(_WIN32) && defined(_TARGET_X86_)
    __cdecl
#endif
    main(int argc, char *argv[])
{
    DWORD pidDebuggee = 0;
    // prevent std::cout flush triggered by read operation on std::cin
    std::cin.tie(nullptr);

    bool protocolMessages = false;
    std::string logFilePath;

    std::string execFile;
    std::vector<std::string> execArgs;

    bool run = false;

    std::unordered_map<std::string, std::function<void(int &i)>> entireArguments{
        {"--attach", [&](int &i) {
            i++;
            if (i >= argc)
            {
                fprintf(stderr, "Error: Missing process id\n");
                exit(EXIT_FAILURE);
            }
            char *err;
            pidDebuggee = strtoul(argv[i], &err, 10);
            if (*err != 0)
            {
                fprintf(stderr, "Error: Missing process id\n");
                exit(EXIT_FAILURE);
            }
        }},
        {"--run", [&](int &i) {
            run = true;
        }},
        {"--protocolMessages", [&](int &i) {
            protocolMessages = true;
        }},
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
        {"--log", [&](int &i) {
#ifdef _WIN32
            static const char path_separator[] = "/\\";
#else
            static const char path_separator[] = "/";
#endif
            // somethat similar to basename(3)
            char *s = argv[0] + strlen(argv[0]);
            while (s > argv[0] && !strchr(path_separator, s[-1]))
                s--;

            char tmp[PATH_MAX];
            auto tempdir = GetTempDir();
            snprintf(tmp, sizeof(tmp), "%.*s/%s.%u.log", int(tempdir.size()), tempdir.data(), s, getpid());
            setenv("LOG_OUTPUT", tmp, 1);
        }},
        {"--interpreter=vscode" , [&](int &i) {
            // VSCode IDE send this option silently to debugger, just ignore it
        }},
        {"--", [&](int &i) {
            ++i;
            if (i < argc)
            {
                execFile = argv[i];
            }
            else
            {
                fprintf(stderr, "Error: Missing program argument\n");
                exit(EXIT_FAILURE);
            }
            for (++i; i < argc; ++i)
            {
                execArgs.push_back(argv[i]);
            }
        }}};

    std::vector<std::pair<std::string, std::function<void(int &i)>>> partialArguments{
        {"--protocolMessages=", [&](int &i) {
            protocolMessages = true;
            logFilePath = argv[i] + strlen("--protocolMessages=");
        }},
        {"--log=", [&](int &i) {
            setenv("LOG_OUTPUT", *argv + strlen("--log="), 1);
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

    CheckStartOptions(execFile, run);

    LOGI("DNCDbg started");
    // Note: there is no possibility to know which exception caused call to std::terminate
    std::set_terminate([] { LOGF("DNCDbg is terminated due to call to std::terminate: see stderr..."); });

#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    std::unique_ptr<DAP> protocol(new DAP(std::cin, std::cout));

    if (protocolMessages)
    {
        protocol->ProtocolMessages(logFilePath);
    }

    std::shared_ptr<ManagedDebugger> debugger;
    try
    {
        debugger.reset(new ManagedDebugger(protocol.get()));
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "%s\n", e.what());
        exit(EXIT_FAILURE);
    }

    protocol->SetDebugger(debugger);

    if (!execFile.empty())
        protocol->SetLaunchCommand(execFile, execArgs);

    LOGI("pidDebugee %d", pidDebuggee);
    HRESULT Status;
    if (pidDebuggee != 0 && FAILED(Status = AttachToExistingProcess(debugger.get(), pidDebuggee)))
    {
        fprintf(stderr, "Error: 0x%x Failed to attach to %i\n", Status, pidDebuggee);
        Interop::Shutdown();
        return EXIT_FAILURE;
    }
    else if (run && FAILED(Status = LaunchNewProcess(debugger.get(), execFile, execArgs)))
    {
        fprintf(stderr, "Error: 0x%08x failed to launch new process\n", Status);
        Interop::Shutdown();
        return EXIT_FAILURE;
    }

    protocol->CommandLoop();
    Interop::Shutdown();
    return EXIT_SUCCESS;
}