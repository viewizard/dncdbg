using System;
using System.IO;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestExitCode
{
class Program
{
    [DllImport("libc")]
    static extern void exit(int status);

    [DllImport("libc")]
    static extern void _exit(int status);

    [DllImport("libc")]
    static extern int kill(int pid, int sig);

    [DllImport("kernel32.dll")]
    static extern void ExitProcess(uint uExitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return:MarshalAs(UnmanagedType.Bool)]
    static extern bool TerminateProcess(IntPtr hProcess, uint uExitCode);

    static void Main(string[] args)
    {
        Label.Checkpoint("init", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(null, null, @"__FILE__:__LINE__");
                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // TODO as soon, as dncdbg will be able restart debuggee process, implement all tests

        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            // Console.WriteLine("Test TerminateProcess()");
            // ExitProcess(3);

            Console.WriteLine("Test TerminateProcess()");
            TerminateProcess(Process.GetCurrentProcess().Handle, 3);
        }
        else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
        {
            // Console.WriteLine("Test exit()");
            // exit(3);

            Console.WriteLine("Test _exit()");
            _exit(3);

            // int PID = Process.GetCurrentProcess().Id;
            // Console.WriteLine("Test SIGABRT, process Id = " + PID);
            // kill(PID, 6); // SIGABRT
        }
        else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
        {
            Console.WriteLine("Test _exit()");
            _exit(3);
        }
        // Console.WriteLine("Test return 3");
        // return 3;

        // Console.WriteLine("Test throw new System.Exception()");
        // throw new System.Exception();

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(3, @"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
