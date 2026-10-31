using System;
using System.IO;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestLaunchSettings
{

class Program
{
    static void Main(string[] args)
    {
        // first checkpoint (initialization) must provide "init" as id
        Label.Checkpoint("init", "test1",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.AddArgsListEntry("launch_arg");
                Context.Launch(@"__FILE__:__LINE__", launchSettingsFilePath: Path.Combine(Context.GetProjectDir(), "launchSettings1.json"));
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        string value = Environment.GetEnvironmentVariable("MY_VARIABLE") ?? string.Empty;
        string aspURLS = Environment.GetEnvironmentVariable("ASPNETCORE_URLS") ?? string.Empty;

        ;                                                                 Label.Breakpoint("BREAK1");

        Label.Checkpoint("test1", "test2",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"my value\"", "string", "value");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"\"", "string", "aspURLS");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"launch_arg\"", "string", "args[0]");
                Context.CheckErrorAtRequest(@"__FILE__:__LINE__", frameId, "args[1]", "error");

                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExit(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("test2", "test3",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__", cwd: Context.GetProjectDir(), relativeProgramPath: false);
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"my value 1\"", "string", "value");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"https://addr1.site\"", "string", "aspURLS");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"argument10\"", "string", "args[0]");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"argument with spaces 1\"", "string", "args[1]");

                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExit(@"__FILE__:__LINE__");
            });

        Label.Checkpoint("test3", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.Initialize(@"__FILE__:__LINE__");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "BREAK1");
                Context.SetBreakpoints(@"__FILE__:__LINE__");
                Context.ConfigurationDone(@"__FILE__:__LINE__");
                Context.Launch(@"__FILE__:__LINE__", cwd: Context.GetProjectDir(), relativeProgramPath: false, launchSettingsProfile: "TestProfile2");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");

                Context.WasBreakpointHit(@"__FILE__:__LINE__", "BREAK1");
                Int64 frameId = Context.DetectFrameId(@"__FILE__:__LINE__", "BREAK1");

                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"my value 2\"", "string", "value");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"https://addrenv.site\"", "string", "aspURLS");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"argument11\"", "string", "args[0]");
                Context.GetAndCheckValue(@"__FILE__:__LINE__", frameId, "\"argument with spaces 2\"", "string", "args[1]");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // last checkpoint must provide "finish" as id or empty string ("") as next checkpoint id
        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasExit(@"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
