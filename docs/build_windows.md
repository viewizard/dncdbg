# Building from Source Code on Windows OS

## Prerequisites

1. Download and install latest **Microsoft's Visual Studio** from here: https://visualstudio.microsoft.com/downloads

   *During installation of Visual Studio you should install all of the options required
   for C# and C++ development on Windows. Plus, enable individual components: `cmake` and `git`.*
2. Optional: debugger requires the **CoreCLR runtime source code**, which is typically downloaded automatically, but can also be downloaded manually from here: https://github.com/dotnet/runtime.


## Compiling

Start VS powershell session and configure the build with the following commands given in debugger's source tree:

```
C:\Users\localuser\dncdbg> md build
C:\Users\localuser\dncdbg> cd build
C:\Users\localuser\dncdbg\build> cmake .. -G "Visual Studio 18 2026"
```

Option `-G` specifies which instance of Visual Studio should build the project.
Note, the minimum requirements for debugger's build is the **Visual Studio 2019** version.

If you want to run tests after a successful build, then you should add the following option:
`-DCMAKE_INSTALL_PREFIX="$PWD.Path\..\bin"`

If you have downloaded either the .NET SDK or .NET Core sources manually, you should add the following options:
`-DDOTNET_DIR="C:\Program Files\dotnet" -DCORECLR_DIR="path\to\coreclr"`

Add your build type (`Release` or `Debug`), for example:
`-DCMAKE_BUILD_TYPE=Release`

To compile and install, use the following command:

```
C:\Users\localuser\dncdbg\build> cmake --build . --target install --parallel $env:NUMBER_OF_PROCESSORS --config Release
```

As an example, all build sequence for VS powershell session could looks like:

```
C:\Users\localuser\dncdbg> md build
C:\Users\localuser\dncdbg> cd build
C:\Users\localuser\dncdbg\build> cmake .. -G "Visual Studio 18 2026" -DCMAKE_INSTALL_PREFIX="$PWD.Path\..\bin" -DDOTNET_DIR="C:\Program Files\dotnet" -DCORECLR_DIR="C:\Users\user\runtime\src\coreclr" -DCMAKE_BUILD_TYPE=Release
C:\Users\localuser\dncdbg\build> cmake --build . --target install --parallel $env:NUMBER_OF_PROCESSORS --config Release
```