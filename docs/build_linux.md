# Building from Source Code on Linux OS

## Prerequisites

1. You need to install `cmake` and `make`;
2. You need the clang C++ compiler installed (debugger can't be built with gcc).
3. You may also need to install some common developers tools not mentioned here, such as [Git](https://www.git-scm.com/downloads), etc...
4. Optional: debugger requires the **.NET SDK**, which is typically downloaded automatically, but can also be downloaded manually from here: https://dotnet.microsoft.com/download.

## Compiling

Configure the build with the following commands:

```
user@dncdbg$ mkdir build
user@dncdbg$ cd build
user@build$ CC=clang CXX=clang++ cmake ..
```

In order to run tests after a successful build, you need to add the option
`-DCMAKE_INSTALL_PREFIX=$PWD/../bin`

If you have previously downloaded the .NET SDK, then you should modify the command line by adding the following option:
`-DDOTNET_DIR=/path/to/sdk/dir`.

Add your build type (`Release` or `Debug`), for example:
`-DCMAKE_BUILD_TYPE=Release`

For build with Address Sanitizer, add the option
`-DASAN=1`

For build with clang-tidy, add the option
`-DCLANG_TIDY=1`

For build with case-sensitive file name collision, add the option
`-DCASE_SENSITIVE_FILENAME_COLLISION=1`

If cmake tries to download the .NET SDK and fails, then please see bullet numbers 4 above. *You can download any required files manually*.

After configuration has finished, you can then build debugger:

```
cmake --build . --target install --parallel $(nproc --all)
```

As an example, all build sequence could looks like:

```
user@dncdbg$ mkdir build
user@dncdbg$ cd build
user@build$ CC=clang CXX=clang++ cmake .. -DCMAKE_INSTALL_PREFIX=$PWD/../bin -DDOTNET_DIR=/home/user/SDK/ -DCMAKE_BUILD_TYPE=Release
user@build$ cmake --build . --target install --parallel $(nproc --all)
```