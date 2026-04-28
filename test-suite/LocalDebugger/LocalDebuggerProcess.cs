using System;
using System.IO;
using System.Diagnostics;

namespace LocalDebugger
{
public class LocalDebuggerProcess : IDisposable
{
    public LocalDebuggerProcess(string debuggerPath, string debuggerArg)
    {
        DebuggerProcess = new Process();
        DebuggerProcess.StartInfo.FileName = debuggerPath;
        DebuggerProcess.StartInfo.Arguments = debuggerArg;
        DebuggerProcess.StartInfo.UseShellExecute = false;
        DebuggerProcess.StartInfo.RedirectStandardInput = true;
        DebuggerProcess.StartInfo.RedirectStandardOutput = true;
        DebuggerProcess.EnableRaisingEvents = true;
        DebuggerProcess.Exited += DebuggerProcess_Exited;
        Input = null!;
        Output = null!;
    }

    public void Start()
    {
        DebuggerProcess.Start();
        Input = DebuggerProcess.StandardInput;
        Output = DebuggerProcess.StandardOutput;
    }

    public void Close()
    {
        CloseCalled = true;
        DebuggerProcess.Kill(true);
        DebuggerProcess.WaitForExit();
        DebuggerProcess.Dispose();
    }

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (!_disposed)
        {
            if (disposing)
            {
                if (!CloseCalled)
                {
                    Close();
                }
            }
            _disposed = true;
        }
    }

    void DebuggerProcess_Exited(object? sender, System.EventArgs e)
    {
        if (!CloseCalled && DebuggerProcess.ExitCode != 0)
        {
            // kill process of test, which is child of dncdbg
            System.Console.Error.WriteLine("Runner: dncdbg is dead with exit code {0}.", DebuggerProcess.ExitCode);
            System.Environment.Exit(DebuggerProcess.ExitCode);
        }
    }

    private bool CloseCalled = false;
    private bool _disposed = false;

    public StreamWriter? Input { get; private set; }
    public StreamReader? Output { get; private set; }
    public Process DebuggerProcess { get; }
}
}
