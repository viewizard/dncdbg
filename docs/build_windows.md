# Building from Source Code on Windows OS

## Prerequisites

Download and install the latest **Microsoft Visual Studio** from here: https://visualstudio.microsoft.com/downloads

*During installation of Visual Studio, you should install all of the options required for C# and C++ development on Windows. Plus, enable individual components: `cmake` and `git`.*


## Compiling

Start a VS PowerShell session and configure the build with the following commands given in the debugger's source tree:

```
C:\Users\localuser\dncdbg> md build
C:\Users\localuser\dncdbg> cd build
C:\Users\localuser\dncdbg\build> cmake .. -G "Visual Studio 18 2026"
```

Option `-G` specifies which instance of Visual Studio should build the project.
Note, the minimum requirement for the debugger's build is **Visual Studio 2019**.

If you want to run tests after a successful build, then you should add the following option:
`-DCMAKE_INSTALL_PREFIX="$PWD.Path\..\bin"`

If you have downloaded the .NET SDK manually, you should add the following option:
`-DDOTNET_DIR="C:\Program Files\dotnet"`

Add your build type (`Release` or `Debug`), for example:
`-DCMAKE_BUILD_TYPE=Release`

To build with Address Sanitizer, add the option
`-DASAN=1`

To build with case-sensitive file name collision, add the option
`-DCASE_SENSITIVE_FILENAME_COLLISION=1`

To compile and install, use the following command:

```
C:\Users\localuser\dncdbg\build> cmake --build . --target install --parallel $env:NUMBER_OF_PROCESSORS --config Release
```

As an example, the complete build sequence for a VS PowerShell session might look like:

```
C:\Users\localuser\dncdbg> md build
C:\Users\localuser\dncdbg> cd build
C:\Users\localuser\dncdbg\build> cmake .. -G "Visual Studio 18 2026" -DCMAKE_INSTALL_PREFIX="$PWD.Path\..\bin" -DDOTNET_DIR="C:\Program Files\dotnet" -DCMAKE_BUILD_TYPE=Release
C:\Users\localuser\dncdbg\build> cmake --build . --target install --parallel $env:NUMBER_OF_PROCESSORS --config Release
```