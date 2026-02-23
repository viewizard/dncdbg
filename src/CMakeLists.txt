# Include diagnostics headers

if (NOT WIN32)
    include_directories(${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/pal/inc)
    include_directories(${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/pal/inc/rt)
endif()
include_directories(${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/pal/prebuilt/inc)
include_directories(${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/inc)
include_directories(${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/debug/inc)
include_directories(${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/native/)

# Build native part of the debugger

include_directories(${PROJECT_SOURCE_DIR}/third_party)
include_directories(${PROJECT_SOURCE_DIR}/src)

set(dncdbg_SRC
    debugger/breakpoints/breakpoint_break.cpp
    debugger/breakpoints/breakpoint_entry.cpp
    debugger/breakpoints/breakpoints_exception.cpp
    debugger/breakpoints/breakpoints_function.cpp
    debugger/breakpoints/breakpoints_source.cpp
    debugger/breakpoints/breakpoints.cpp
    debugger/breakpoints/breakpointutils.cpp
    debugger/steppers/stepper_async.cpp
    debugger/steppers/stepper_simple.cpp
    debugger/steppers/steppers.cpp
    debugger/callbacksqueue.cpp
    debugger/evalhelpers.cpp
    debugger/evalstackmachine.cpp
    debugger/evaluator.cpp
    debugger/evalutils.cpp
    debugger/evalwaiter.cpp
    debugger/frames.cpp
    debugger/managedcallback.cpp
    debugger/manageddebugger.cpp
    debugger/threads.cpp
    debugger/valueprint.cpp
    debugger/variables.cpp
    debuginfo/async_info.cpp
    debuginfo/debuginfo_sources.cpp
    debuginfo/debuginfo.cpp
    managed/interop.cpp
    metadata/attributes.cpp
    metadata/jmc.cpp
    metadata/typeprinter.cpp
    protocol/dap.cpp
    protocol/dapio.cpp
    types/types.cpp
    utils/dynlibs_unix.cpp
    utils/dynlibs_win32.cpp
    utils/filesystem_unix.cpp
    utils/filesystem_win32.cpp
    utils/filesystem.cpp
    utils/ioredirect.cpp
    utils/iosystem_unix.cpp
    utils/iosystem_win32.cpp
    utils/logger.cpp
    utils/platform_unix.cpp
    utils/platform_win32.cpp
    utils/streams.cpp
    utils/utf.cpp
    utils/waitpid.cpp
    buildinfo.cpp
    main.cpp
    )

# clang-tidy for dncdbg build only
if (CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY "clang-tidy")
endif()

# cppcheck for dncdbg build only
if (CPPCHECK)
    set(CMAKE_CXX_CPPCHECK cppcheck
                           --enable=all
                           --check-level=exhaustive
                           --quiet
                           --suppress=uninitMemberVar:streams.h
                           --suppress=operatorEqRetRefThis:span.h
                           --suppress=operatorEqVarError:span.h
                           --suppress=duplInheritedMember:streams.h
                           --suppress=accessMoved:streams.h
                           --suppress=noExplicitConstructor
                           --suppress=knownConditionTrueFalse
                           --suppress=useStlAlgorithm:main.cpp
                           --suppress=missingIncludeSystem
                           --suppress=unusedFunction
                           --suppress=unmatchedSuppression
                           --suppress=*:*third_party/*)
endif()

if (WIN32)
    # fix issue with std::numeric_limits<T>::max() and std::max()
    add_definitions(-DNOMINMAX)
endif()

# By default, build with case-insensitive file name collision for all OSes.
# Note, test-suite will count on this build option.
if (CASE_SENSITIVE_FILENAME_COLLISION)
    message(STATUS "Added compile option: CASE_SENSITIVE_FILENAME_COLLISION")
    add_definitions(-DCASE_SENSITIVE_FILENAME_COLLISION)
else()
    message(STATUS "Added compile option: CASE_INSENSITIVE_FILENAME_COLLISION")
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
