# Include diagnostics headers

if (NOT WIN32)
    include_directories(${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/pal/inc)
    include_directories(${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/pal/inc/rt)
    include_directories(${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/pal/prebuilt/inc)
endif()
include_directories(${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/inc)
include_directories(${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/debug/inc)
include_directories(${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/native/)

# Build native part of the debugger

include_directories(${PROJECT_SOURCE_DIR}/third_party)
include_directories(${PROJECT_SOURCE_DIR}/src)

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
    protocol/escaped_string.cpp
    protocol/dap.cpp
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
    utils/logger.cpp
    utils/platform_unix.cpp
    utils/platform_win32.cpp
    utils/streams.cpp
    )

# clang-tidy for dncdbg build only
if (CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY "clang-tidy")
endif()

if (WIN32)
    # fix issue with std::numeric_limits<T>::max() and std::max()
    add_definitions(-DNOMINMAX)
endif()

# By default, build with case-insensitive file name collision for all OSes.
# Note, test-suite will count on this build option.
if (CASE_SENSITIVE_FILENAME_COLLISION)
    message(STATUS "Added compiled option: CASE_SENSITIVE_FILENAME_COLLISION")
    add_definitions(-DCASE_SENSITIVE_FILENAME_COLLISION)
else()
    message(STATUS "Added compiled option: CASE_INSENSITIVE_FILENAME_COLLISION")
    add_definitions(-DCASE_INSENSITIVE_FILENAME_COLLISION)
endif()

# special threatment of buildinfo.cpp -- this file rebuilds every time,
# whatever it changed or not (because it contains version information and
# date/time of the build).
string(CONCAT buildinfo_flags
    " " -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
    " " -DDNCDBG_VCS_INFO=${DNCDBG_VCS_INFO}
    " " -DOS_NAME=${CMAKE_SYSTEM_NAME}
    " " -DCPU_ARCH=${CLR_CMAKE_TARGET_ARCH}
)
set_source_files_properties(buildinfo.cpp PROPERTIES COMPILE_FLAGS "${buildinfo_flags}")
add_custom_target(buildinfo ALL COMMAND
    ${CMAKE_COMMAND} -E touch "${CMAKE_SOURCE_DIR}/src/buildinfo.cpp")

add_executable_clr(dncdbg ${dncdbg_SRC})
# Define "DEBUG" only when the configuration is "Debug"
target_compile_definitions(dncdbg PRIVATE $<$<CONFIG:Debug>:DEBUG>)

if (WIN32)
    target_link_libraries(dncdbg corguids wsock32 ws2_32)
else()
    target_link_libraries(dncdbg corguids dl pthread)
endif()

install_clr(TARGETS dncdbg DESTINATIONS . )

# Build managed part of the debugger (ManagedPart.dll)

set(MANAGEDPART_PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/managed/ManagedPart.csproj)
set(MANAGEDPART_DLL_NAME ManagedPart.dll)
set(DOTNET_BUILD_RESULT ${CMAKE_CURRENT_BINARY_DIR}/${MANAGEDPART_DLL_NAME})

find_program(DOTNETCLI dotnet PATHS "${DOTNET_DIR}" ENV PATH NO_DEFAULT_PATH)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(MANAGEDPART_BUILD_TYPE "Debug")
else() # CMAKE_BUILD_TYPE not set or Release, RelWithDebInfo, MinSizeRel, ...
    set(MANAGEDPART_BUILD_TYPE "Release")
endif()

add_custom_command(OUTPUT ${DOTNET_BUILD_RESULT}
    COMMAND ${DOTNETCLI} publish ${MANAGEDPART_PROJECT} --self-contained -c ${MANAGEDPART_BUILD_TYPE} -o ${CMAKE_CURRENT_BINARY_DIR} /p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/obj/ /p:BaseOutputPath=${CMAKE_CURRENT_BINARY_DIR}/bin/
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/managed/*.cs" "${MANAGEDPART_PROJECT}"
    COMMENT "Compiling ${MANAGEDPART_DLL_NAME}"
    VERBATIM
)

add_custom_target(managedpart_dll ALL DEPENDS ${DOTNET_BUILD_RESULT})

# Copy dlls
set(ROSLYN_DLLS
    Microsoft.CodeAnalysis.dll
    Microsoft.CodeAnalysis.CSharp.dll)

set(DLLS_TO_DEPLOY ${DOTNET_BUILD_RESULT})
foreach(ITEM ${ROSLYN_DLLS})
    list(APPEND DLLS_TO_DEPLOY "${CMAKE_CURRENT_BINARY_DIR}/${ITEM}")
endforeach()

install(FILES ${DLLS_TO_DEPLOY} DESTINATION ${CMAKE_INSTALL_PREFIX})
