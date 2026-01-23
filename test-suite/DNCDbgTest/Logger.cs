using System;

namespace DNCDbgTest
{
class Logger
{
    public void LogLine(string line)
    {
        Console.WriteLine(line);
    }

    public void LogError(string error)
    {
        Console.Error.WriteLine(error);
    }
}
}
