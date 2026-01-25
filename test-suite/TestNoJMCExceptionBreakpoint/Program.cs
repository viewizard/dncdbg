using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;

using DbgTest;
using DbgTest.DAP;
using DbgTest.Script;

namespace TestNoJMCExceptionBreakpoint
{
class inside_user_code
{
    static public void throw_Exception()
    {
        throw new System.Exception();                                                          Label.Breakpoint("bp3");
    }

    static public void throw_NullReferenceException()
    {
        throw new System.NullReferenceException();                                             Label.Breakpoint("bp4");
    }

    static public void throw_Exception_with_catch()
    {
        try
        {
            throw new System.Exception();                                                      Label.Breakpoint("bp1");
        }
        catch (Exception e)
        {
        }
    }

    static public void throw_Exception_NullReferenceException_with_catch()
    {
        try
        {
            throw new System.Exception();
        }
        catch
        {
        }

        try
        {
            throw new System.NullReferenceException();                                         Label.Breakpoint("bp2");
        }
        catch
        {
        }
    }
}

[DebuggerNonUserCodeAttribute()]
class outside_user_code
{
    static public void throw_Exception()
    {
        throw new System.Exception();                                                          Label.Breakpoint("bp5");
    }

    static public void throw_NullReferenceException()
    {
        throw new System.NullReferenceException();                                             Label.Breakpoint("bp8");
    }

    static public void throw_Exception_with_catch()
    {
        try
        {
            throw new System.Exception();                                                      Label.Breakpoint("bp6");
        }
        catch
        {
        }
    }

    static public void throw_Exception_NullReferenceException_with_catch()
    {
        try
        {
            throw new System.Exception();
        }
        catch
        {
        }

        try
        {
            throw new System.NullReferenceException();                                         Label.Breakpoint("bp7");
        }
        catch
        {
        }
    }
}

class inside_user_code_wrapper
{
    static public void call(Action callback)
    {
        callback();
    }

    static public void call_with_catch(Action callback)
    {
        try
        {
            callback();
        }
        catch
        {
        };
    }
}

[DebuggerNonUserCodeAttribute()]
class outside_user_code_wrapper
{
    static public void call(Action callback)
    {
        callback();
    }

    static public void call_with_catch(Action callback)
    {
        try
        {
            callback();
        }
        catch
        {
        };
    }
}

class Program
{
    static void Main(string[] args)
    {
        Label.Checkpoint("init", "test_all",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.PrepareStart(false, null, @"__FILE__:__LINE__");

                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_1");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_2");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_3");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_4");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_5");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_6");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_7");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_8");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_9");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_10");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_11");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_12");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_13");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_14");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_15");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_16");
                Context.AddBreakpoint(@"__FILE__:__LINE__", "bp_test_17");
                Context.SetBreakpoints(@"__FILE__:__LINE__");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAll();
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.PrepareEnd(@"__FILE__:__LINE__");
                Context.WasEntryPointHit(@"__FILE__:__LINE__");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // test filter "all"

        for (int i = 0; i < 2; ++i)
        {
            inside_user_code.throw_Exception_with_catch();                                     Label.Breakpoint("bp_test_1");
            try
            {
                outside_user_code.throw_Exception();                                           Label.Breakpoint("bp_test_2");
            }
            catch
            {
            };
            outside_user_code.throw_Exception_with_catch();                                    Label.Breakpoint("bp_test_3");
        }

        Label.Checkpoint(
            "test_all", "test_all_empty_options",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp1", "CLR", "always", "System.Exception");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_2");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp5", "CLR", "always", "System.Exception");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_3");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp6", "CLR", "always", "System.Exception");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAllWithOptions("");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // test filter "all" with empty options

        Label.Checkpoint(
            "test_all_empty_options", "test_all_concrete_exception",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp1", "CLR", "always", "System.Exception");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_2");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp5", "CLR", "always", "System.Exception");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_3");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp6", "CLR", "always", "System.Exception");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAllWithOptions("System.NullReferenceException");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // test filter "all" with options "System.NullReferenceException" ("all" for "System.NullReferenceException"
        // only)

        for (int i = 0; i < 2; ++i)
        {
            inside_user_code.throw_Exception_NullReferenceException_with_catch();              Label.Breakpoint("bp_test_4");
            outside_user_code.throw_Exception_NullReferenceException_with_catch();             Label.Breakpoint("bp_test_5");
            try
            {
                outside_user_code.throw_Exception();
            }
            catch
            {
            };
            try
            {
                outside_user_code.throw_NullReferenceException();                              Label.Breakpoint("bp_test_6");
            }
            catch
            {
            };
        }

        Label.Checkpoint("test_all_concrete_exception", "test_all_except_concrete_exception",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_4");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp2", "CLR", "always", "System.NullReferenceException");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_5");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp7", "CLR", "always", "System.NullReferenceException");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_6");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp8", "CLR", "always", "System.NullReferenceException");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAllWithOptions("!System.Exception");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // test filter "all" with options "!System.Exception" ("all" for all except "System.Exception")

        Label.Checkpoint("test_all_except_concrete_exception", "test_user_unhandled",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_4");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp2", "CLR", "always", "System.NullReferenceException");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_5");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp7", "CLR", "always", "System.NullReferenceException");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_6");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp8", "CLR", "always", "System.NullReferenceException");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterUserUnhandled();
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // test filter "user-unhandled"
        // Must emit break event only in case catch block outside of user code, but "throw" inside user code.

        for (int i = 0; i < 2; ++i)
        {
            inside_user_code.throw_Exception_with_catch();
            try
            {
                outside_user_code.throw_Exception();
            }
            catch
            {
            };
            outside_user_code.throw_Exception_with_catch();

            try
            {
                outside_user_code_wrapper.call(inside_user_code.throw_Exception);
            }
            catch
            {
            };
            try
            {
                outside_user_code_wrapper.call(outside_user_code.throw_Exception);
            }
            catch
            {
            };
            outside_user_code_wrapper.call_with_catch(inside_user_code.throw_Exception);       Label.Breakpoint("bp_test_7");
            outside_user_code_wrapper.call_with_catch(outside_user_code.throw_Exception);      Label.Breakpoint("bp_test_8");

            try
            {
                inside_user_code_wrapper.call(outside_user_code.throw_Exception);
            }
            catch
            {
            };
            try
            {
                inside_user_code_wrapper.call(inside_user_code.throw_Exception);
            }
            catch
            {
            };
            inside_user_code_wrapper.call_with_catch(outside_user_code.throw_Exception);
            inside_user_code_wrapper.call_with_catch(inside_user_code.throw_Exception);
        }

        Label.Checkpoint("test_user_unhandled", "test_user_unhandled_empty_options",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_7");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_8");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterUserUnhandledWithOptions("");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // test filter "user-unhandled" with empty options

        Label.Checkpoint("test_user_unhandled_empty_options", "test_user_unhandled_concrete_exception",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_7");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_8");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterUserUnhandledWithOptions("System.NullReferenceException");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // test filter "user-unhandled" with options "System.NullReferenceException" ("user-unhandled" for
        // "System.NullReferenceException" only)

        for (int i = 0; i < 2; ++i)
        {
            outside_user_code_wrapper.call_with_catch(inside_user_code.throw_Exception);
            outside_user_code_wrapper.call_with_catch(inside_user_code.throw_NullReferenceException);
                Console.WriteLine("end");                                                                   Label.Breakpoint("bp_test_9");
        }

        Label.Checkpoint("test_user_unhandled_concrete_exception", "test_user_unhandled_except_concrete_exception",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_9");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterUserUnhandledWithOptions("!System.Exception");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // test filter "user-unhandled" with options "!System.Exception" ("user-unhandled" for all except "System.Exception")

        Label.Checkpoint("test_user_unhandled_except_concrete_exception", "test_DAP_1",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_9");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAllWithOptions("System.NullReferenceException");
                Context.AddExceptionBreakpointFilterUserUnhandled();
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // test DAP add multiple breakpoints (filter + filter options)

        for (int i = 0; i < 3; ++i)
        {
            outside_user_code_wrapper.call_with_catch(inside_user_code.throw_Exception);                Label.Breakpoint("bp_test_10");
            outside_user_code_wrapper.call_with_catch(inside_user_code.throw_NullReferenceException);   Label.Breakpoint("bp_test_11");
            Console.WriteLine("end");                                                                   Label.Breakpoint("bp_test_12");
        }

        Label.Checkpoint("test_DAP_1", "test_DAP_2",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_10");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_11");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp4", "CLR", "always", "System.NullReferenceException");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_12");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAll();
                Context.AddExceptionBreakpointFilterUserUnhandled();
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // test DAP add multiple breakpoints (both filters)

        Label.Checkpoint(
            "test_DAP_2", "test_DAP_3",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_10");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp3", "CLR", "always", "System.Exception");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_11");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp4", "CLR", "always", "System.NullReferenceException");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_12");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAllWithOptions("!System.NullReferenceException System.ArgumentNullException System.ArgumentOutOfRangeException");
                Context.AddExceptionBreakpointFilterUserUnhandledWithOptions("System.ArgumentNullException System.Exception System.ArgumentOutOfRangeException");
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // test DAP add multiple breakpoints (both filter options)

        Label.Checkpoint("test_DAP_3", "test_DAP_inner",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_10");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp3", "CLR", "always", "System.Exception");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_11");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_12");

                Context.ResetExceptionBreakpoints();
                Context.AddExceptionBreakpointFilterAll();
                Context.SetExceptionBreakpoints(@"__FILE__:__LINE__");

                Context.Continue(@"__FILE__:__LINE__");
            });

        // test DAP inner exception (test ExceptionInfo for proper inner exception info)

        try
        {
            throw new Exception("Message1");                                                        Label.Breakpoint("bp_test_13");
        }
        catch (Exception e1)
        {
            try
            {
                throw new NullReferenceException("Message2", e1);                                   Label.Breakpoint("bp_test_14");
            }
            catch (Exception e2)
            {
                try
                {
                    throw new ArgumentOutOfRangeException("Message3", e2);                          Label.Breakpoint("bp_test_15");
                }
                catch
                {
                }
            }
        }
        Console.WriteLine("end");                                                                   Label.Breakpoint("bp_test_16");

        Label.Checkpoint("test_DAP_inner", "test_unhandled",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_13");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp_test_13", "CLR", "always", "System.Exception");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_14");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp_test_14", "CLR", "always", "System.NullReferenceException");
                Context.TestInnerException(@"__FILE__:__LINE__", 0, "System.Exception", "Message1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_15");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp_test_15", "CLR", "always", "System.ArgumentOutOfRangeException");
                Context.TestInnerException(@"__FILE__:__LINE__", 0, "System.NullReferenceException", "Message2");
                Context.TestInnerException(@"__FILE__:__LINE__", 1, "System.Exception", "Message1");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_16");
                Context.Continue(@"__FILE__:__LINE__");
            });

        // test "unhandled"

        throw new System.ArgumentOutOfRangeException();                                             Label.Breakpoint("bp_test_17");

        Label.Checkpoint("test_unhandled", "finish",
            (Object context) =>
            {
                Context Context = (Context)context;
                Context.WasBreakpointHit(@"__FILE__:__LINE__", "bp_test_17");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp_test_17", "CLR", "always", "System.ArgumentOutOfRangeException");
                Context.Continue(@"__FILE__:__LINE__");
                Context.WasExceptionBreakpointHit(@"__FILE__:__LINE__", "bp_test_17", "CLR", "unhandled", "System.ArgumentOutOfRangeException");
            });

        Label.Checkpoint("finish", "",
            (Object context) =>
            {
                Context Context = (Context)context;
                // At this point debugger stops at unhandled exception, no reason continue process, abort execution.
                Context.AbortExecution(@"__FILE__:__LINE__");
                Context.WasExit(null, @"__FILE__:__LINE__");
                Context.DebuggerExit(@"__FILE__:__LINE__");
            });
    }
}
}
