using DNCDbgTestCore;

namespace DNCDbgTest
{
public enum BreakpointType
{
    None,
    Line
}

public class Breakpoint
{
    public Breakpoint(string name, BreakpointType type = BreakpointType.None)
    {
        Name = name;
        Type = type;
    }

    public string Name;
    public BreakpointType Type;
}

public class LineBreakpoint : Breakpoint
{
    public LineBreakpoint(string name, string fileName, int numLine)
        : base(name, BreakpointType.Line)
    {
        FileName = fileName;
        NumLine = numLine;
    }

    public string FileName;
    public int NumLine;
}
}
