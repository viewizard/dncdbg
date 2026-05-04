# Building from Source Code on macOS

## Prerequisites

Before you begin, ensure you have the necessary tools installed.

#### Install Xcode Command Line Tools
macOS does not come with a compiler by default. Clang is provided via the Command Line Tools. Open your Terminal and run:
```
xcode-select --install
```
Verify by running: `clang --version`

#### Install CMake
The most efficient way to manage packages on macOS is via [Homebrew](https://brew.sh/). If you have it installed, run:
```
brew install cmake
```
Alternatively, download the binary from the [CMake official website](https://cmake.org/download/).

## Compiling

Configure the build with the following commands:

```
user@dncdbg$ mkdir build
user@dncdbg$ cd build
user@build$ cmake ..
```
The build system requires **Clang** (AppleClang). You can ensure it is used by prefixing the `cmake` command with compiler variables:
```
user@build$ CC=clang CXX=clang++ cmake ..
```
**Note:** If you have multiple versions of LLVM/Clang installed (e.g., via Homebrew), CMake will use the system-default AppleClang unless you provide a specific path.

In order to run tests after a successful build, you need to add the option
`-DCMAKE_INSTALL_PREFIX=$PWD/../bin`

If you have previously downloaded the .NET SDK, then you should modify the command line by adding the following option:
`-DDOTNET_DIR=/path/to/sdk/dir`.

Add your build type (`Release` or `Debug`), for example:
`-DCMAKE_BUILD_TYPE=Release`

Optionally, you can specify the target architecture manually using `arm64` or `x86_64`. If not provided, the build system will automatically use your host architecture. Note that universal binaries are not recommended because the debugger requires a matching .NET SDK for the specific architecture. Example: `-DCMAKE_OSX_ARCHITECTURES=arm64`

To build with Address Sanitizer, add the option
`-DASAN=1`

To build with case-sensitive file name collision, add the option
`-DCASE_SENSITIVE_FILENAME_COLLISION=1`

In case you have long hang related to VBCSCompiler process don't exit at the end of the build after all binaries are installed, disable shared compilation:
`UseSharedCompilation=false UseRearNodes=false`

After configuration has finished, you can then build debugger:

```
cmake --build . --target install --parallel $(sysctl -n hw.ncpu)
```

As an example, the complete build sequence for an Apple Silicon Mac might look like:

```
user@dncdbg$ mkdir build && cd build
user@build$ CC=clang CXX=clang++ cmake .. \
    -DCMAKE_INSTALL_PREFIX=$PWD/../bin \
    -DDOTNET_DIR=/Users/user/SDK/ \
    -DCMAKE_BUILD_TYPE=Release
user@build$ cmake --build . --target install --parallel $(sysctl -n hw.ncpu) -- UseSharedCompilation=false UseRearNodes=false
```
