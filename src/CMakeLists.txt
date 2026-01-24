# Build corguids static library from coreclr source files
if (NOT WIN32)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wno-extra-tokens)
    endif()
endif()
add_compile_options(-D_MIDL_USE_GUIDDEF_)
file(GLOB CORGUIDS_SOURCES "${CORECLR_SRC_DIR}/pal/prebuilt/idl/*_i.cpp")
add_library(corguids STATIC ${CORGUIDS_SOURCES})
if (NOT WIN32)
    target_compile_options(corguids PRIVATE -Wno-unused-parameter)
endif()

if (NOT WIN32)
    set(CORECLR_MINIPAL_SRC_DIR "${NATIVE_SRC_DIR}/minipal")
    if (EXISTS "${CORECLR_MINIPAL_SRC_DIR}/guid.c")
        # Build minipalguid static library from coreclr source files
        include(CheckFunctionExists)
        include(CheckIncludeFiles)
        include(CheckSymbolExists)

        check_include_files("windows.h;bcrypt.h" HAVE_BCRYPT_H)
        check_include_files("sys/auxv.h;asm/hwcap.h" HAVE_AUXV_HWCAP_H)
        check_include_files("asm/hwprobe.h" HAVE_HWPROBE_H)

        check_function_exists(sysctlbyname HAVE_SYSCTLBYNAME)
        check_function_exists(fsync HAVE_FSYNC)

        check_symbol_exists(arc4random_buf "stdlib.h" HAVE_ARC4RANDOM_BUF)
        check_symbol_exists(O_CLOEXEC fcntl.h HAVE_O_CLOEXEC)
        check_symbol_exists(CLOCK_MONOTONIC time.h HAVE_CLOCK_MONOTONIC)
        check_symbol_exists(CLOCK_MONOTONIC_COARSE time.h HAVE_CLOCK_MONOTONIC_COARSE)
        check_symbol_exists(clock_gettime_nsec_np time.h HAVE_CLOCK_GETTIME_NSEC_NP)

        if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
            set(BIGENDIAN 1)
        endif()

        configure_file(${CORECLR_MINIPAL_SRC_DIR}/minipalconfig.h.in ${CORECLR_MINIPAL_SRC_DIR}/minipalconfig.h)

        set(MINIPALGUID_SOURCES "${CORECLR_MINIPAL_SRC_DIR}/guid.c" "${CORECLR_MINIPAL_SRC_DIR}/random.c")
        add_library(minipalguid STATIC ${MINIPALGUID_SOURCES})
        target_include_directories(minipalguid PRIVATE ${CORECLR_MINIPAL_SRC_DIR}/minipalconfig.h ${CORECLR_MINIPAL_SRC_DIR}/utils.h ${CORECLR_MINIPAL_SRC_DIR}/guid.h ${CORECLR_MINIPAL_SRC_DIR}/random.h)
    endif()
endif()

# Include coreclr headers

if (NOT WIN32)
    include_directories(${CORECLR_SRC_DIR}/pal/inc)
    include_directories(${CORECLR_SRC_DIR}/pal/inc/rt)
    include_directories(${CORECLR_SRC_DIR}/pal/prebuilt/inc)
endif()
include_directories(${CORECLR_SRC_DIR}/inc)
include_directories(${CORECLR_SRC_DIR}/debug/inc)
include_directories(${CORECLR_SRC_DIR}/debug/shim)
include_directories(${CORECLR_SRC_DIR}/dlls/dbgshim)
# for CoreCLR <= 3.x
include_directories(${CORECLR_SRC_DIR}/coreclr/hosts/inc)
# for dotnet-runtime (> 3.x)
include_directories(${CORECLR_SRC_DIR}/hosts/inc)
# for dotnet-runtime (> 8.x)
if (DEFINED NATIVE_SRC_DIR)
    include_directories(${NATIVE_SRC_DIR})
endif()

# Build native part of the debugger

include_directories(${PROJECT_SOURCE_DIR}/third_party)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(dncdbg_SRC
    debugger/breakpoint_break.cpp
    debugger/breakpoint_entry.cpp
    debugger/breakpoints_exception.cpp
    debugger/breakpoints_func.cpp
    debugger/breakpoints_line.cpp
    debugger/breakpoints.cpp
    debugger/breakpointutils.cpp
    debugger/callbacksqueue.cpp
    debugger/evalhelpers.cpp
    debugger/evalstackmachine.cpp
    debugger/evaluator.cpp
    debugger/evalwaiter.cpp
    debugger/evalutils.cpp
    debugger/frames.cpp
    debugger/managedcallback.cpp
    debugger/manageddebugger.cpp
    debugger/threads.cpp
    debugger/stepper_async.cpp
    debugger/stepper_simple.cpp
    debugger/steppers.cpp
    debugger/valueprint.cpp
    debugger/variables.cpp
    debugger/waitpid.cpp
    interfaces/types.cpp
    managed/interop.cpp
    metadata/attributes.cpp
    metadata/async_info.cpp
    metadata/jmc.cpp
    metadata/modules.cpp
    metadata/modules_sources.cpp
    metadata/typeprinter.cpp
    protocols/escaped_string.cpp
    protocols/vscodeprotocol.cpp
    utils/utf.cpp
    main.cpp
    buildinfo.cpp
    utils/dynlibs_unix.cpp
    utils/dynlibs_win32.cpp
    utils/filesystem.cpp
    utils/filesystem_unix.cpp
    utils/filesystem_win32.cpp
    utils/ioredirect.cpp
    utils/iosystem_unix.cpp
    utils/iosystem_win32.cpp
    utils/interop_unix.cpp
    utils/interop_win32.cpp
    utils/logger.cpp
    utils/platform_unix.cpp
    utils/platform_win32.cpp
    utils/streams.cpp
    )

set(CMAKE_INCLUDE_CURRENT_DIR OFF)

if (NOT WIN32)
    add_definitions(-DPAL_STDCPP_COMPAT)
endif()

if (WIN32)
    # fix issue with std::numeric_limits<T>::max() and std::max()
    add_definitions(-DNOMINMAX)
endif()

# special threatment of buildinfo.cpp -- this file rebuilds every time,
# whatever it changed or not (because it contains version information and
# date/time of the build).
string(CONCAT buildinfo_flags
    " " -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
    " " -DDNCDBG_VCS_INFO=${DNCDBG_VCS_INFO}
    " " -DCORECLR_VCS_INFO=${CORECLR_VCS_INFO}
    " " -DOS_NAME=${CMAKE_SYSTEM_NAME}
    " " -DCPU_ARCH=${CLR_CMAKE_TARGET_ARCH}
)
set_source_files_properties(buildinfo.cpp PROPERTIES COMPILE_FLAGS "${buildinfo_flags}")
add_custom_target(buildinfo ALL COMMAND
    ${CMAKE_COMMAND} -E touch "${CMAKE_SOURCE_DIR}/src/buildinfo.cpp")


add_executable(dncdbg ${dncdbg_SRC})

if (WIN32)
    target_link_libraries(dncdbg corguids wsock32 ws2_32)
else()
    target_link_libraries(dncdbg corguids dl pthread)
    if (EXISTS "${CORECLR_MINIPAL_SRC_DIR}/guid.c")
        target_link_libraries(dncdbg minipalguid)
    endif()
endif()

install(TARGETS dncdbg DESTINATION ${CMAKE_INSTALL_PREFIX})

# Build managed part of the debugger (ManagedPart.dll)

if (BUILD_MANAGED)
    set(MANAGEDPART_PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/managed/ManagedPart.csproj)
    set(MANAGEDPART_DLL_NAME ManagedPart.dll)
    set(DOTNET_BUILD_RESULT ${CMAKE_CURRENT_BINARY_DIR}/${MANAGEDPART_DLL_NAME})

    find_program(DOTNETCLI dotnet PATHS "${DOTNET_DIR}" ENV PATH NO_DEFAULT_PATH)

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(MANAGEDPART_BUILD_TYPE "Debug")
    else() # CMAKE_BUILD_TYPE not set or Release, RelWithDebInfo, MinSizeRel, ...
        set(MANAGEDPART_BUILD_TYPE "Release")
    endif()

    if (NOT RID_NAME)
        if (CLR_CMAKE_PLATFORM_UNIX)
            if (CLR_CMAKE_PLATFORM_DARWIN)
                set(RID_NAME "osx")
            elseif (EXISTS "/etc/alpine-release") # Alpine use musl only.
                set(RID_NAME "linux-musl")
            else()
                set(RID_NAME "linux")
            endif()
        elseif (WIN32)
            set(RID_NAME "win")
        else()
            message(FATAL_ERROR "Unsupported platform")
        endif()
    endif() # NOT RID_NAME

    add_custom_command(OUTPUT ${DOTNET_BUILD_RESULT}
      COMMAND ${DOTNETCLI} publish ${MANAGEDPART_PROJECT} -r ${RID_NAME}-${CLR_CMAKE_TARGET_ARCH} --self-contained -c ${MANAGEDPART_BUILD_TYPE} -o ${CMAKE_CURRENT_BINARY_DIR} /p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/obj/ /p:BaseOutputPath=${CMAKE_CURRENT_BINARY_DIR}/bin/
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/managed/*.cs" "${MANAGEDPART_PROJECT}"
      COMMENT "Compiling ${MANAGEDPART_DLL_NAME}"
      VERBATIM
    )

    add_custom_target(managedpart_dll ALL DEPENDS ${DOTNET_BUILD_RESULT})

    # Copy dlls
    set(ROSLYN_DLLS
        Microsoft.CodeAnalysis.dll
        Microsoft.CodeAnalysis.CSharp.dll
        Microsoft.CodeAnalysis.Scripting.dll
        Microsoft.CodeAnalysis.CSharp.Scripting.dll)

    set(DLLS_TO_DEPLOY ${DOTNET_BUILD_RESULT})
    foreach(ITEM ${ROSLYN_DLLS})
        list(APPEND DLLS_TO_DEPLOY "${CMAKE_CURRENT_BINARY_DIR}/${ITEM}")
    endforeach()

    install(FILES ${DLLS_TO_DEPLOY} DESTINATION ${CMAKE_INSTALL_PREFIX})

    if (CLR_CMAKE_PLATFORM_UNIX)
        if (CLR_CMAKE_PLATFORM_DARWIN)
            set(DBGSHIM_LIB_NAME "libdbgshim.dylib")
        else()
            set(DBGSHIM_LIB_NAME "libdbgshim.so")
        endif()
    elseif (WIN32)
        set(DBGSHIM_LIB_NAME "dbgshim.dll")
    else()
        message(FATAL_ERROR "Unsupported platform")
    endif()

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${DBGSHIM_LIB_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX})
endif()
