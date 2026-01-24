// Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.
// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include <string>
#include <exception>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils/limits.h"

#include "protocols/vscodeprotocol.h"
#include "debugger/manageddebugger.h"
#include "managed/interop.h"
#include "utils/utf.h"
#include "utils/logger.h"
#include "buildinfo.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#define PATH_MAX MAX_PATH
static void setenv(const char* var, const char* val, int) { _putenv_s(var, val); }
#define getpid() (GetCurrentProcessId())
#else
#define _isatty(fd) ::isatty(fd)
#define _fileno(file) ::fileno(file)
#endif

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <sys/types.h>
#include <unistd.h>
#endif


namespace dncdbg
{

static const uint16_t DEFAULT_SERVER_PORT = 4711;

static void print_help()
{
    fprintf(stdout,
        ".NET Core debugger\n"
        "\n"
        "Options:\n"
        "--buildinfo                           Print build info.\n"
        "--attach <process-id>                 Attach the debugger to the specified process id.\n"
        "--run                                 Run program without waiting commands\n"
        "--engineLogging[=<path to log file>]  Enable logging to VsDbg-UI or file for the engine.\n"
        "                                      Only supported by the VsCode interpreter.\n"
        "--server[=port_num]                   Start the debugger listening for requests on the\n"
        "                                      specified TCP/IP port instead of stdin/out. If port is not specified\n"
        "                                      TCP %i will be used.\n"
        "--log[=<file>]                        Enable logging. Supported logging to file only.\n"
        "                                      File log by default. File is created in 'current' folder.\n"
        "--version                             Displays the current version.\n",
        (int)DEFAULT_SERVER_PORT
    );
}

static void print_buildinfo()
{
    printf(".NET Core debugger %s\n", BuildInfo::version);

    printf(
        "\nBuild info:\n"
        "      Build type:  %s\n"
        "      Build date:  %s %s\n"
        "      Target OS:   %s\n"
        "      Target arch: %s\n\n",
            BuildInfo::build_type,
            BuildInfo::date, BuildInfo::time,
            BuildInfo::os_name,
            BuildInfo::cpu_arch
    );

    printf("DNCDbg VCS info:   %s\n", BuildInfo::dncdbg_vcs_info);
    printf("CoreCLR VCS info:  %s\n", BuildInfo::coreclr_vcs_info);
}

// protocol names for logging
template <typename ProtocolType> struct ProtocolDetails { static const char name[]; };
template <> const char ProtocolDetails<VSCodeProtocol>::name[] = "VSCodeProtocol";

// argument needed for protocol creation
using Streams = std::pair<std::istream&, std::ostream&>;

using ProtocolHolder = std::shared_ptr<IProtocol>;
using ProtocolConstructor = ProtocolHolder (*)(Streams);

// static functions which used to create protocol instance (like class fabric)
template <typename ProtocolType>
ProtocolHolder instantiate_protocol(Streams streams)
{
    LOGI("Creating protocol %s", ProtocolDetails<ProtocolType>::name);
    return ProtocolHolder{new ProtocolType(streams.first, streams.second)};
}



// function creates pair of input/output streams for debugger protocol
template <typename Holder>
Streams open_streams(Holder& holder, unsigned server_port, ProtocolConstructor constructor)
{
    if (server_port != 0)
    {
        IOSystem::FileHandle socket = IOSystem::listen_socket(server_port);
        if (! socket)
        {
            fprintf(stderr, "can't open listening socket for port %u\n", server_port);
            exit(EXIT_FAILURE);
        }

        std::iostream *stream = new IOStream(StreamBuf(socket));
        holder.push_back(typename Holder::value_type{stream});
        return {*stream, *stream};
    }

    return {std::cin, std::cout};
}

} // namespace dncdbg


using namespace dncdbg;

static void FindAndParseArgs(char **argv, std::vector<std::pair<std::string, std::function<void(int& i)>>> &partialArguments, int i)
{
    for(auto argument:partialArguments)
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

static void CheckStartOptions(ProtocolConstructor &protocol_constructor, char* argv[], std::string &execFile, bool run, uint16_t serverPort)
{
    if (run && execFile.empty())
    {
        fprintf(stderr, "--run option was given, but no executable file specified!\n");
        exit(EXIT_FAILURE);
    }
}

static HRESULT AttachToExistingProcess(IDebugger *pDebugger, DWORD pidDebuggee)
{
    HRESULT Status;
    IfFailRet(pDebugger->Initialize());
    IfFailRet(pDebugger->Attach(pidDebuggee));
    return pDebugger->ConfigurationDone();
}

static HRESULT LaunchNewProcess(IDebugger *pDebugger, std::string &execFile, std::vector<std::string> &execArgs)
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
#if defined(WIN32) && defined(_TARGET_X86_)
    __cdecl
#endif
            main(int argc, char* argv[])
{

    DWORD pidDebuggee = 0;
    // prevent std::cout flush triggered by read operation on std::cin
    std::cin.tie(nullptr);

    ProtocolConstructor protocol_constructor = &instantiate_protocol<VSCodeProtocol>;

    bool engineLogging = false;
    std::string logFilePath;

    std::vector<std::string> initTexts;

    uint16_t serverPort = 0;

    std::string execFile;
    std::vector<std::string> execArgs;

    bool run = false;

    std::unordered_map<std::string, std::function<void(int& i)>> entireArguments
    {
        {"--attach", [&](int& i){

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

        } },
        { "--run", [&](int& i){

            run = true;

        } },
        { "--engineLogging", [&](int& i){

            engineLogging = true;

        } },
        { "--help", [&](int& i){

            print_help();
            exit(EXIT_SUCCESS);

        } },
        { "--buildinfo", [&](int& i){

            print_buildinfo();
            exit(EXIT_SUCCESS);

        } },
        { "--version", [&](int& i){

            fprintf(stdout, "NET Core debugger %s (%s, %s)\n\n",
                BuildInfo::version, BuildInfo::dncdbg_vcs_info, BuildInfo::build_type);
            fprintf(stdout, "Copyright (c) 2017-2025 Samsung Electronics Co., Ltd.\n");
            fprintf(stdout, "Copyright (c) 2026 Mikhail Kurinnoi\n");
            fprintf(stdout, "Distributed under the MIT License.\n");
            fprintf(stdout, "See the LICENSE file in the project root for more information.\n");
            exit(EXIT_SUCCESS);

        } },
        { "--log", [&](int& i){

            #ifdef _WIN32
            static const char path_separator[] = "/\\";
            #else
            static const char path_separator[] = "/";
            #endif

            // somethat similar to basename(3)
            char *s = argv[0] + strlen(argv[0]);
            while (s > argv[0] && !strchr(path_separator, s[-1])) s--;

            char tmp[PATH_MAX];
            auto tempdir = GetTempDir();
            snprintf(tmp, sizeof(tmp), "%.*s/%s.%u.log", int(tempdir.size()), tempdir.data(), s, getpid());
            setenv("LOG_OUTPUT", tmp, 1);

        } },
        { "--server", [&](int& i){

            serverPort = DEFAULT_SERVER_PORT;

        } },
        { "--", [&](int& i){

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
        } }
    };

    std::vector<std::pair<std::string, std::function<void(int& i)>>> partialArguments
    {
        { "--engineLogging=", [&](int& i){

            engineLogging = true;
            logFilePath = argv[i] + strlen("--engineLogging=");

        } },
        { "--log=", [&](int& i){

            setenv("LOG_OUTPUT", *argv + strlen("--log="), 1);

        } },
        { "--server=", [&](int& i){

            char *err;
            serverPort = static_cast<uint16_t>(strtoul(argv[i] + strlen("--server="), &err, 10));
            if (*err != 0)
            {
                fprintf(stderr, "Error: Missing process id\n");
                exit(EXIT_FAILURE);
            }

        } },
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

    CheckStartOptions(protocol_constructor, argv, execFile, run, serverPort);

    LOGI("DNCDbg started");
    // Note: there is no possibility to know which exception caused call to std::terminate
    std::set_terminate([]{ LOGF("DNCDbg is terminated due to call to std::terminate: see stderr..."); });

    std::vector<std::unique_ptr<std::ios_base> > streams;
    std::shared_ptr<IProtocol> protocol = protocol_constructor(open_streams(streams, serverPort, protocol_constructor));

    if (engineLogging)
    {
        auto p = dynamic_cast<VSCodeProtocol*>(protocol.get());
        if (!p)
        {
            fprintf(stderr, "Error: Engine logging is only supported in VsCode interpreter mode.\n");
            LOGE("Engine logging is only supported in VsCode interpreter mode.");
            exit(EXIT_FAILURE);
        }

        p->EngineLogging(logFilePath);
    }

    std::shared_ptr<IDebugger> debugger;
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
        fprintf(stderr, "Error: 0x%08x\n", Status);
        Interop::Shutdown();
        return EXIT_FAILURE;
    }

    protocol->CommandLoop();
    Interop::Shutdown();
    return EXIT_SUCCESS;
}