# Building from Source Code on Linux OS

## Prerequisites

1. You need to install `cmake` and `make`;
2. You need the clang C++ compiler installed (the debugger cannot be built with gcc).
3. You may also need to install some common developer tools not mentioned here, such as [Git](https://www.git-scm.com/downloads), etc.
4. Optional: the debugger requires the **.NET SDK**, which is typically downloaded automatically, but can also be downloaded manually from: https://dotnet.microsoft.com/download.

***Debian/Ubuntu prerequisites installation:***
```
sudo apt install cmake clang llvm build-essential
```
***Alpine Linux prerequisites installation:***
```
apk add cmake make clang llvm build-base icu-libs bash curl
```

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

To build with Address Sanitizer, add the option
`-DASAN=1`

To build with clang-tidy (note, the `clang-tidy` package must be installed), add the option `-DCLANG_TIDY=1`

To build with cppcheck (note, the `cppcheck` package must be installed), add the option
`-DCPPCHECK=1`

To build with case-sensitive file name collision, add the option
`-DCASE_SENSITIVE_FILENAME_COLLISION=1`

If cmake tries to download the .NET SDK and fails, then please see bullet number 4 above. *You can download any required files manually*.

After configuration has finished, you can then build debugger:

```
cmake --build . --target install --parallel $(nproc --all)
```

As an example, the complete build sequence might look like:

```
user@dncdbg$ mkdir build
user@dncdbg$ cd build
user@build$ CC=clang CXX=clang++ cmake .. -DCMAKE_INSTALL_PREFIX=$PWD/../bin -DDOTNET_DIR=/home/user/SDK/ -DCMAKE_BUILD_TYPE=Release
user@build$ cmake --build . --target install --parallel $(nproc --all)
```