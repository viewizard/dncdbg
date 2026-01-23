using DNCDbgTestCore;

namespace DNCDbgTest.DAP
{
class DAPLineBreakpoint : LineBreakpoint
{
    public DAPLineBreakpoint(string name, string srcName, int lineNum) : base(name, srcName, lineNum, ProtocolType.DAP)
    {
    }

    public override string ToString()
    {
        return System.String.Format("{0}:{1}", FileName, NumLine);
    }
}
}
