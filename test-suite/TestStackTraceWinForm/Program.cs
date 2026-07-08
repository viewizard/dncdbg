using System;
using System.Windows.Forms;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestStackTraceWinForm
{
    static class Program
    {
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new Form1());                             DbgTest.Label.Breakpoint("last_frame");
        }
    }

    public sealed class Form1 : Form
    {
        public Form1()
        {
            this.Load += Form1_Load;
        }

        private void Form1_Load(object? sender, EventArgs e)
        {
            DbgTest.Label.Checkpoint("init", "test_trace",
                (Object context) =>
                {
                    Context Context = (Context)context;
                    Context.Initialize(@"__FILE__:__LINE__");
                    Context.Launch(JMC: false, StepFiltering: null, RemoteConsole: false, RemoteConsolePort: 0, @"__FILE__:__LINE__");
                    Context.AddBreakpoint(@"__FILE__:__LINE__", "bp0");
                    Context.SetBreakpoints(@"__FILE__:__LINE__");
                    Context.ConfigurationDone(@"__FILE__:__LINE__");

                    Context.WasEntryPointHit(@"__FILE__:__LINE__");
                    Context.Continue(@"__FILE__:__LINE__");
                });

            ;                                                         DbgTest.Label.Breakpoint("bp0");

            DbgTest.Label.Checkpoint("test_trace", "finish",
                (Object context) =>
                {
                    Context Context = (Context)context;
                    Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp0");
                    string[] stacktrace = { "bp0" };
                    Context.TestStackTrace(@"__FILE__:__LINE__", "TestStackTraceWinForm.Form1.Form1_Load(object sender, System.EventArgs e)", stacktrace, 1);
                    Context.TestStackTraceLastFrame(@"__FILE__:__LINE__", "TestStackTraceWinForm.Program.Main()", "last_frame");
                });

            DbgTest.Label.Checkpoint("finish", "",
                (Object context) =>
                {
                    Context Context = (Context)context;
                    Context.AbortExecution(@"__FILE__:__LINE__");
                    Context.WasExit(null, @"__FILE__:__LINE__");
                    Context.DebuggerExit(@"__FILE__:__LINE__");
                });
        }
    }
}
