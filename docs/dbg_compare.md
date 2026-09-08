# DNCDbg / NetCoreDbg Comparison Table

<div align="center">

<table>
  <thead>
    <tr>
      <th align="left"></th>
      <th align="center">DNCDbg</th>
      <th align="center"><a href="https://github.com/Samsung/netcoredbg">NetCoreDbg</a></th>
      <th align="center"><a href="https://github.com/dotnet/vscode-csharp">VsDbg</a></th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td colspan="4" align="center"><b>General</b></td>
    </tr>
    <tr>
      <td align="left"><b>License</b></td>
      <td align="center">MIT</td>
      <td align="center">MIT</td>
      <td align="center">Commercial<sup><a href="#note-1">1</a></sup></td>
    </tr>
    <tr>
      <td align="left"><b>Implementation</b></td>
      <td align="center">C++</td>
      <td align="center">C++ and C#<sup><a href="#note-2">2</a></sup></td>
      <td align="center">C#</td>
    </tr>
    <tr>
      <td align="left"><b>Physical memory footprint<sup><a href="#note-3">3</a></sup></b></td>
      <td align="center">11.7M</td>
      <td align="center">52.4M</td>
      <td align="center">146.8M</td>
    </tr>
    <tr>
      <td align="left"><b>Cross-platform</b></td>
      <td align="center">✅</td>
      <td align="center">⚠️<sup><a href="#note-4">4</a></sup></td>
      <td align="center">⚠️<sup><a href="#note-5">5</a></sup></td>
    </tr>
    <tr>
      <td colspan="4" align="center"><b>Stack Trace</b></td>
    </tr>
    <tr>
      <td align="left"><b>Display method parameters in stack traces</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Display in/ref/out parameter modifiers</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Display active CLR internal frames in stack traces</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Human-readable async stack traces</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Human-readable constructor names in stack traces</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td colspan="4" align="center"><b>Variables</b></td>
    </tr>
    <tr>
      <td align="left"><b>Display local constants (literals)</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Use <code>ToString()</code> for object variable display</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Paging for child variables</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Memory reference for variables</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td colspan="4" align="center"><b>Evaluation</b></td>
    </tr>
    <tr>
      <td align="left"><a href="evaluation_format_specifiers.md"><b>Evaluation format specifiers</b></a></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b><code>DebuggerBrowsable</code> attribute</b></td>
      <td align="center">✅</td>
      <td align="center">⚠️<sup><a href="#note-6">6</a></sup></td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b><code>DebuggerDisplay</code> attribute</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b><code>DebuggerTypeProxy</code> attribute</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Using-directive awareness in type resolution</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td colspan="4" align="center"><b>Breakpoints</b></td>
    </tr>
    <tr>
      <td align="left"><b>Logpoints</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Source breakpoints on columns</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">❓</td>
    </tr>
    <tr>
      <td align="left"><b>Instruction reference for breakpoints</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td colspan="4" align="center"><b>Debug Info & Sources</b></td>
    </tr>
    <tr>
      <td align="left"><b>Embedded PDB</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Embedded sources</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><a href="https://code.visualstudio.com/docs/csharp/debugger-settings#_source-file-map"><b>Source File Map</b></a></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Source checksums</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td colspan="4" align="center"><b>Process & I/O</b></td>
    </tr>
    <tr>
      <td align="left"><a href="inputting_text.md"><b>Standard input redirection (STDIN)</b></a></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Module unloads</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td colspan="4" align="center"><b>Execution Control</b></td>
    </tr>
    <tr>
      <td align="left"><b>Jump to Cursor (Set Next Statement)</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">✅</td>
    </tr>
    <tr>
      <td align="left"><b>Single-thread execution and stepping</b></td>
      <td align="center">✅</td>
      <td align="center">❌</td>
      <td align="center">❓</td>
    </tr>
    <tr>
      <td colspan="4" align="center"><b>Protocols & Advanced Features</b></td>
    </tr>
    <tr>
      <td align="left"><b>MI/GDB and CLI protocols</b></td>
      <td align="center">❌</td>
      <td align="center">✅</td>
      <td align="center">❌</td>
    </tr>
    <tr>
      <td align="left"><b>Mixed-mode (interop) debugging</b></td>
      <td align="center">❌</td>
      <td align="center">⚠️<sup><a href="#note-7">7</a></sup></td>
      <td align="center">❓</td>
    </tr>
    <tr>
      <td align="left"><b>Hot Reload</b></td>
      <td align="center">❌</td>
      <td align="center">⚠️<sup><a href="#note-8">8</a></sup></td>
      <td align="center">❓</td>
    </tr>
  </tbody>
</table>

</div>

## Notes

<a name="note-1"></a><small><sup>1</sup> From the VsDbg console output: "You may only use the Microsoft .NET Core Debugger (vsdbg) with Visual Studio Code, Visual Studio or Visual Studio for Mac software to help you develop and test your applications."</small><br>
<a name="note-2"></a><small><sup>2</sup> Used only for the symbol reader, the C# expression parser, and primitive type evaluation.</small><br>
<a name="note-3"></a><small><sup>3</sup> Measured on macOS 26.6.2 using the `vmmap` utility on a simple application stopped at a breakpoint, with approximately 20 local variables and 10 evaluation requests. The debug session was started inside the VS Code IDE using the DAP protocol; NetCoreDbg was built without interop and Hot Reload features.</small><br>
<a name="note-4"></a><small><sup>4</sup> Does not support `musl`-based Linux distributions.</small><br>
<a name="note-5"></a><small><sup>5</sup> Does not support `musl`-based Linux arm32 distributions.</small><br>
<a name="note-6"></a><small><sup>6</sup> Only the `Never` state for properties is supported.</small><br>
<a name="note-7"></a><small><sup>7</sup> Linux and Tizen operating systems only.</small><br>
<a name="note-8"></a><small><sup>8</sup> Available via the MI/GDB protocol only, and currently limited to the MSVS Tizen extension.</small><br>
