# Building from Source Code on Linux OS

## Prerequisites

1. You need to install `cmake` and `make`;
2. You need the clang C++ compiler installed (debugger can't be built with gcc).
3. You may also need to install some common developers tools not mentioned here, such as [Git](https://www.git-scm.com/downloads), etc...
4. Optional: debugger requires the **CoreCLR runtime source code**, which is typically downloaded automatically, but can also be downloaded manually from here: https://github.com/dotnet/runtime.
5. Optional: debugger requires the **.NET SDK**, which is typically downloaded automatically, but can also be downloaded manually from here: https://dotnet.microsoft.com/download.

## Compiling

Configure the build with the following commands:

```
user@dncdbg$ mkdir build
user@dncdbg$ cd build
user@build$ CC=clang CXX=clang++ cmake ..
```

In order to run tests after a successful build, you need to add the option
`-DCMAKE_INSTALL_PREFIX=$PWD/../bin`

If you have previously downloaded the .NET SDK or CoreCLR sources, then you should modify the command line by adding the following options:
`-DDOTNET_DIR=/path/to/sdk/dir -DCORECLR_DIR=/path/to/coreclr/sources`.

Add your build type (`Release` or `Debug`), for example:
`-DCMAKE_BUILD_TYPE=Release`

If cmake tries to download the .NET SDK or CoreCLR sources and fails, then please see bullet numbers 4 and 5 above. *You can download any required files manually*.

After configuration has finished, you can then build debugger:

```
cmake --build . --target install
```

As an example, all build sequence could looks like:

```
user@dncdbg$ mkdir build
user@dncdbg$ cd build
user@build$ CC=clang CXX=clang++ cmake .. -DCMAKE_INSTALL_PREFIX=$PWD/../bin -DCORECLR_DIR=/home/user/runtime/src/coreclr/ -DDOTNET_DIR=/home/user/SDK/ -DCMAKE_BUILD_TYPE=Release
user@build$ cmake --build . --target install -j12
```