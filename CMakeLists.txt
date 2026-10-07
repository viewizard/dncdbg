# Copyright (c) .NET Foundation and contributors. All rights reserved.
# Licensed under the MIT license. See LICENSE file in the project root for full license information.

# Verify minimum required version
cmake_minimum_required(VERSION 3.15)

cmake_policy(SET CMP0042 NEW) # MACOSX_RPATH is enabled by default.
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW) # install() paths are normalized
endif(POLICY CMP0177)

project(dncdbg LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

# Require the Clang compiler on non-Windows platforms
if(NOT WIN32)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
        message(FATAL_ERROR "Clang compiler is required. Current C compiler: ${CMAKE_C_COMPILER_ID}")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        message(FATAL_ERROR "Clang C++ compiler is required. Current C++ compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

if(WIN32)
    # For Windows, the CLR_CMAKE_HOST_ARCH parameter is expected to be passed
    # to CMake to determine the build architecture. If it is not passed, detect
    # the architecture here.
    if(NOT CLR_CMAKE_HOST_ARCH)
        if(MSVC_CXX_ARCHITECTURE_ID)
            string(TOLOWER "${MSVC_CXX_ARCHITECTURE_ID}" LOWERCASE_ARCH_ID)
        else()
            string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" LOWERCASE_ARCH_ID)
        endif()

        if(LOWERCASE_ARCH_ID MATCHES "^arm64|^aarch64")
            set(CLR_CMAKE_HOST_ARCH arm64)
            message(STATUS "Architecture not specified: using host architecture ARM64")
        elseif(LOWERCASE_ARCH_ID MATCHES "amd64|x64" OR CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(CLR_CMAKE_HOST_ARCH x64)
            message(STATUS "Architecture not specified: using host architecture x64")
        else()
            set(CLR_CMAKE_HOST_ARCH x86)
            message(STATUS "Architecture not specified: using host architecture x86")
        endif()
    endif()
endif()

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    # For macOS, the CMAKE_OSX_ARCHITECTURES parameter is expected to be passed
    # to CMake to determine the build architecture. If it is not passed, detect
    # the architecture here.
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64")
        set(CMAKE_OSX_ARCHITECTURES "arm64")
        message(STATUS "Architecture not specified: using host architecture ARM64")
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
        set(CMAKE_OSX_ARCHITECTURES "x86_64")
        message(STATUS "Architecture not specified: using host architecture x86_64")
    endif()
endif()

# Strip debug symbols from libdbgshim and dncdbg for Release builds only
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CLR_CMAKE_KEEP_NATIVE_SYMBOLS 0)
else()
    set(CLR_CMAKE_KEEP_NATIVE_SYMBOLS 1)
endif()

# AddressSanitizer
if(ASAN)
    set(CLR_CMAKE_ENABLE_SANITIZERS "address")
endif()
# UBSanitizer
if(UBSAN)
    set(CLR_CMAKE_ENABLE_SANITIZERS "undefined")
endif()

include(cmake/configurepaths.cmake)

# diagnostics has no related code, so there is no reason to depend on `liblttng-ust-dev`
set(FEATURE_EVENT_TRACE 0)

### diagnostics CMakeLists.txt part
#
# Note: we don't use separate CMake parts, we just re-use the diagnostics CMake parts with changes:
#
# - copy the main part of `third-party/diagnostics/CMakeLists.cmake` into this file below,
#   remove the header, `project`, `cmake_minimum_required`, etc., and add `add_subdirectory(third-party/diagnostics)`
# - copy and correct the `third-party/diagnostics/eng/native/configurepaths.cmake` file into the `cmake` folder
# - correct `third-party/diagnostics/src/CMakeLists.txt`, remove the lines with `SOS` and `tests`
# - correct `third-party/diagnostics/eng/native/configurecompiler.cmake`,
#   change CMAKE_C_STANDARD and CMAKE_CXX_STANDARD to 17, change CMAKE_OSX_DEPLOYMENT_TARGET to "13.3"
# - correct `third-party/diagnostics/eng/native/functions.cmake`, change the line
#   `add_executable(${ARGV} ${VERSION_FILE_PATH})` to `add_executable(${ARGV})`
# - correct `third-party/diagnostics/src/shared/clrdefinitions.cmake`, comment out the `add_custom_target(eventing_headers)` line
#   since we disable `FEATURE_EVENT_TRACE` and diagnostics doesn't have an `eventing_headers` project in its sources
# - add the external terminal related code to `third-party/diagnostics/src/dbgshim/dbgshim.cpp`:
#   #ifdef _WIN32
#       wchar_t flagBuffer[2] = { 0 };
#       DWORD copied = GetEnvironmentVariableW(L"DNCDBG_CREATE_NEW_CONSOLE", flagBuffer, 2);
#       if (copied == 1 && flagBuffer[0] == L'1')
#       {
#           dwCreationFlags |= CREATE_NEW_CONSOLE;
#       }
#   #endif // _WIN32
# - correct `third-party/diagnostics/src/shared/pal/inc/unixasmmacros.inc`, fix for https://github.com/dotnet/diagnostics/issues/5788
#   add the following to the end of the file:
#     // Mark the stack as non-executable for ELF targets.
#     // https://github.com/dotnet/runtime/issues/125469
#     #if !defined(__APPLE__)
#     .pushsection .note.GNU-stack,"",%progbits
#     .popsection
#     #endif
# - correct `third-party/diagnostics/eng/native/configurecompiler.cmake`, disable the UBSanitizer options
#   in CLR_CMAKE_BUILD_SANITIZERS: vptr, object-size
# - correct `third-party/diagnostics/src/shared/pal/src/configure.cmake`, add:
#       else()
#         # On macOS and other systems, pthreads may be built into the system library
#         set(PTHREAD_LIBRARY c)
#       endif()
# - correct `third-party/diagnostics/src/shared/pal/src/configure.cmake`, change the `check_function_exists(pipe2 HAVE_PIPE2)` line to:
#      if(APPLE)
#          set(HAVE_PIPE2 FALSE CACHE INTERNAL "pipe2 is missing on macOS")
#      else()
#          check_function_exists(pipe2 HAVE_PIPE2)
#      endif()
# - correct `third-party/diagnostics/src/shared/pal/src/include/pal/palinternal.h`, change the `#include <CoreServices/CoreServices.h>` line to:
#        #pragma clang diagnostic push
#        #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#        #include <CoreServices/CoreServices.h>
#        #pragma clang diagnostic pop
###

# include shared compiler configs
include(${CLR_ENG_NATIVE_DIR}/configurecompiler.cmake)

if(CLR_CMAKE_HOST_UNIX)
  # -fms-extensions enables things like __if_exists, __declspec(uuid()), etc.
  add_compile_options(-fms-extensions)
  #-fms-compatibility      Enable full Microsoft Visual C++ compatibility
  #-fms-extensions         Accept some non-standard constructs supported by the Microsoft compiler
  add_compile_options(-fvisibility=default)
endif(CLR_CMAKE_HOST_UNIX)

if (CMAKE_VERSION VERSION_LESS "3.16")
  # Provide a no-op polyfill for precompiled headers on old CMake versions
  function(target_precompile_headers)
  endfunction()
endif()

# Register the default component
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME diagnostics)
add_component(diagnostics)

if(CLR_CMAKE_HOST_WIN32)
  message(STATUS "VS_PLATFORM_TOOLSET is ${CMAKE_VS_PLATFORM_TOOLSET}")
  message(STATUS "VS_PLATFORM_NAME is ${CMAKE_VS_PLATFORM_NAME}")
endif(CLR_CMAKE_HOST_WIN32)

set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

if(CLR_CMAKE_HOST_UNIX)
  # Drop the static scope for SOS so that it is available outside the
  # compilation unit for external linkage; see the extern declaration
  # in the SOS sources.
  file(READ "${VERSION_FILE_PATH}" VERSION_FILE_CONTENTS)
  string(REPLACE "static char" "char" VERSION_LINE_WITHOUT_STATIC "${VERSION_FILE_CONTENTS}")
  file(WRITE "${VERSION_FILE_PATH}" "${VERSION_LINE_WITHOUT_STATIC}")
endif(CLR_CMAKE_HOST_UNIX)

# Add the directory where _version.h for Windows is generated to the include path
if(CLR_CMAKE_HOST_WIN32)
  include_directories(${CLR_ARTIFACTS_OBJ_DIR})
endif(CLR_CMAKE_HOST_WIN32)

set(CORECLR_SET_RPATH ON)
if(CORECLR_SET_RPATH)
  # Enable @rpath support for shared libraries.
  set(MACOSX_RPATH ON)
endif(CORECLR_SET_RPATH)

OPTION(CLR_CMAKE_ENABLE_CODE_COVERAGE "Enable code coverage" OFF)
OPTION(CLR_CMAKE_WARNINGS_ARE_ERRORS "Warnings are errors" ON)

#----------------------------------------------------
# Cross target Component build specific configuration
#----------------------------------------------------
if(CLR_CROSS_COMPONENTS_BUILD)
  add_definitions(-DCROSS_COMPILE)

  if(CLR_CMAKE_HOST_ARCH_AMD64 AND (CLR_CMAKE_TARGET_ARCH_ARM OR CLR_CMAKE_TARGET_ARCH_I386))
    set(FEATURE_CROSSBITNESS 1)
  endif(CLR_CMAKE_HOST_ARCH_AMD64 AND (CLR_CMAKE_TARGET_ARCH_ARM OR CLR_CMAKE_TARGET_ARCH_I386))
endif(CLR_CROSS_COMPONENTS_BUILD)

#------------------------------------
# Definitions (for platform)
#-----------------------------------

if (CLR_CMAKE_HOST_ARCH_AMD64)
  add_definitions(-D_AMD64_)
  add_definitions(-D_WIN64)
  add_definitions(-DAMD64)
  add_definitions(-DBIT64=1)
elseif (CLR_CMAKE_HOST_ARCH_I386)
  add_definitions(-D_X86_)
elseif (CLR_CMAKE_HOST_ARCH_ARM)
  add_definitions(-D_ARM_)
  add_definitions(-DARM)
elseif (CLR_CMAKE_HOST_ARCH_ARM64)
  add_definitions(-D_ARM64_)
  add_definitions(-DARM64)
  add_definitions(-D_WIN64)
  add_definitions(-DBIT64=1)
elseif (CLR_CMAKE_HOST_ARCH_MIPS64)
  add_definitions(-D_MIPS64_)
  add_definitions(-DMIPS64)
  add_definitions(-D_WIN64)
  add_definitions(-DBIT64=1)
elseif (CLR_CMAKE_HOST_ARCH_RISCV64)
  add_definitions(-D_RISCV64_)
  add_definitions(-DRISCV64)
  add_definitions(-D_WIN64)
  add_definitions(-DBIT64=1)
elseif (CLR_CMAKE_HOST_ARCH_LOONGARCH64)
  add_definitions(-D_LOONGARCH64_)
  add_definitions(-DLOONGARCH64)
  add_definitions(-D_WIN64)
  add_definitions(-DBIT64=1)
else ()
  clr_unknown_arch()
endif ()

if (CLR_CMAKE_TARGET_ARCH_AMD64)
  if (CLR_CMAKE_HOST_UNIX)
    add_definitions(-DDBG_TARGET_AMD64_UNIX)
  endif()
  add_definitions(-D_TARGET_64BIT_=1)
  add_definitions(-D_TARGET_AMD64_=1)
  add_definitions(-DDBG_TARGET_64BIT=1)
  add_definitions(-DDBG_TARGET_AMD64=1)
  add_definitions(-DDBG_TARGET_WIN64=1)
elseif (CLR_CMAKE_TARGET_ARCH_ARM64)
  if (CLR_CMAKE_HOST_UNIX)
    add_definitions(-DDBG_TARGET_ARM64_UNIX)
  endif()
  add_definitions(-D_TARGET_ARM64_=1)
  add_definitions(-D_TARGET_64BIT_=1)
  add_definitions(-DDBG_TARGET_64BIT=1)
  add_definitions(-DDBG_TARGET_ARM64=1)
  add_definitions(-DDBG_TARGET_WIN64=1)
  add_definitions(-DFEATURE_MULTIREG_RETURN)
elseif (CLR_CMAKE_TARGET_ARCH_ARM)
  if (CLR_CMAKE_HOST_UNIX)
    add_definitions(-DDBG_TARGET_ARM_UNIX)
  elseif (WIN32 AND NOT DEFINED CLR_CROSS_COMPONENTS_BUILD)
    # Set this to ensure we can use the Arm SDK for Desktop binary linkage when doing a native (Arm32) build
    add_definitions(-D_ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE=1)
    add_definitions(-D_ARM_WORKAROUND_)
  endif (CLR_CMAKE_HOST_UNIX)
  add_definitions(-D_TARGET_ARM_=1)
  add_definitions(-DDBG_TARGET_32BIT=1)
  add_definitions(-DDBG_TARGET_ARM=1)
elseif (CLR_CMAKE_TARGET_ARCH_I386)
  add_definitions(-D_TARGET_X86_=1)
  add_definitions(-DDBG_TARGET_32BIT=1)
  add_definitions(-DDBG_TARGET_X86=1)
elseif (CLR_CMAKE_TARGET_ARCH_MIPS64)
  add_definitions(-DDBG_TARGET_MIPS64_UNIX)
  add_definitions(-D_TARGET_MIPS64_=1)
  add_definitions(-D_TARGET_64BIT_=1)
  add_definitions(-DDBG_TARGET_MIPS64=1)
  add_definitions(-DDBG_TARGET_64BIT=1)
  add_definitions(-DDBG_TARGET_WIN64=1)
  add_definitions(-DFEATURE_MULTIREG_RETURN)
elseif (CLR_CMAKE_TARGET_ARCH_RISCV64)
  add_definitions(-DDBG_TARGET_RISCV64_UNIX)
  add_definitions(-D_TARGET_RISCV64_=1)
  add_definitions(-D_TARGET_64BIT_=1)
  add_definitions(-DDBG_TARGET_RISCV64=1)
  add_definitions(-DDBG_TARGET_64BIT=1)
  add_definitions(-DDBG_TARGET_WIN64=1)
  add_definitions(-DFEATURE_MULTIREG_RETURN)
elseif (CLR_CMAKE_TARGET_ARCH_LOONGARCH64)
  add_definitions(-DDBG_TARGET_LOONGARCH64_UNIX)
  add_definitions(-D_TARGET_LOONGARCH64_=1)
  add_definitions(-D_TARGET_64BIT_=1)
  add_definitions(-DDBG_TARGET_LOONGARCH64=1)
  add_definitions(-DDBG_TARGET_64BIT=1)
  add_definitions(-DDBG_TARGET_WIN64=1)
  add_definitions(-DFEATURE_MULTIREG_RETURN)
else ()
  clr_unknown_arch()
endif (CLR_CMAKE_TARGET_ARCH_AMD64)

if(CLR_CMAKE_HOST_WIN32)
  add_definitions(-DWIN32)
  add_definitions(-D_WIN32)
  add_definitions(-DWINVER=0x0602)
  add_definitions(-D_WIN32_WINNT=0x0602)
  add_definitions(-DWIN32_LEAN_AND_MEAN=1)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif(CLR_CMAKE_HOST_WIN32)

#--------------------------------------
# FEATURE Defines
#--------------------------------------

if(CLR_CMAKE_HOST_UNIX)
    add_definitions(-DFEATURE_PAL)
endif(CLR_CMAKE_HOST_UNIX)

#-----------------------------------------
# Native Projects
#-----------------------------------------
add_subdirectory(third-party/diagnostics)
add_subdirectory(third-party/dnmd)
add_subdirectory(third-party/miniz)
add_subdirectory(third-party/tree-sitter)
add_subdirectory(third-party/tree-sitter-c-sharp)
add_subdirectory(src)
