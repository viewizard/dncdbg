cmake_minimum_required(VERSION 3.15)

if (BUILD_TESTING)
    enable_testing()
endif()

project(dncdbg)

set(CORECLR_DIR "" CACHE FILEPATH "Path to CoreCLR source directory")
set(DOTNET_DIR "" CACHE FILEPATH "Path to .NET SDK directory")
set(CORECLR_BRANCH "release/10.0" CACHE STRING "CoreCLR source branch")
# After move of dbgshim from runtime to diagnostics, this sdk is used only for build of managed part.
set(DOTNET_CHANNEL "10.0" CACHE STRING ".NET SDK channel")
set(BUILD_MANAGED ON CACHE BOOL "Build managed part")

function(clr_unknown_arch)
    message(FATAL_ERROR "Only AMD64, ARM64, ARM, ARMEL, I386 and WASM are supported")
endfunction()

if (WIN32)
    # For windows it is expected that CLR_CMAKE_HOST_ARCH parameter is passed
    # to CMAKE to determine build arch.
    # If it is not passed, detect arch here.
    if ("${CLR_CMAKE_HOST_ARCH}" STREQUAL "")
        if ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
            set(CLR_CMAKE_HOST_ARCH x86)
        else()
            set(CLR_CMAKE_HOST_ARCH x64)
        endif()
    endif()
endif()

# Follow order as in root CoreCLR CMakeLists.txt
include(cmake/detectplatform.cmake) # from root
include(cmake/platformdefinitions.cmake) # from root
include(cmake/compileoptions.cmake) # disabled setting arm compiler flags in Tizen build
include(cmake/clrdefinitions.cmake)

include(cmake/fetchdeps.cmake)

set(CORECLR_SRC_DIR "${CORECLR_DIR}/src")
set(VERSION_PROPS_PATH "${CORECLR_DIR}/eng/Versions.props")
if (NOT EXISTS "${CORECLR_SRC_DIR}/pal")
    set(CORECLR_SRC_DIR "${CORECLR_DIR}")
    set(VERSION_PROPS_PATH "${CORECLR_DIR}/../../eng/Versions.props")
    set(NATIVE_SRC_DIR "${CORECLR_DIR}/../native")
endif()

if (WIN32)
    execute_process(
            COMMAND powershell -Command "Select-Xml -Path ${VERSION_PROPS_PATH} -XPath '/Project/PropertyGroup/ProductVersion' | ForEach-Object { $_.Node.InnerXML }"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE CORECLR_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_VARIABLE errout
            RESULT_VARIABLE retcode)
else()
    execute_process(
            COMMAND bash -c "grep ProductVersion ${VERSION_PROPS_PATH} | sed 's,</.*,,;s,.*>,,'"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE CORECLR_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_VARIABLE errout
            RESULT_VARIABLE retcode)
endif()

if (NOT "${retcode}" STREQUAL "0" OR "${CORECLR_VERSION}" STREQUAL "")
    # For some version of runtime Versions.props file doesn't exist
    set(CORECLR_VERSION "unknown")
endif()

message("Building with ${CORECLR_VERSION} CoreCLR")

# Basic checks for valid paths
if (NOT IS_DIRECTORY "${CORECLR_SRC_DIR}/pal")
    message(FATAL_ERROR "Invalid CoreCLR source directory")
endif()
if (NOT "${DOTNET_DIR}" STREQUAL "")
    if (NOT IS_DIRECTORY "${DOTNET_DIR}/shared/Microsoft.NETCore.App")
        message(FATAL_ERROR "Invalid .NET SDK directory")
    endif()
endif()


# Get references to VCS revision for DNCDbg itself and CoreCLR
# (this used in src/debug/dncdbg/CMakeLists.txt)
include(cmake/vcsinfo.cmake)
VCSInfo(DNCDBG_VCS_INFO)
VCSInfo(CORECLR_VCS_INFO ${CORECLR_DIR})


add_subdirectory(src)
