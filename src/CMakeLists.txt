# =============================================================================
# Third-party dependencies (SYSTEM to suppress warnings and clang-tidy checks)
# =============================================================================

if(NOT WIN32)
    include_directories(SYSTEM
        ${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/pal/inc
        ${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/pal/inc/rt
    )
endif()

include_directories(SYSTEM
    ${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/pal/prebuilt/inc
    ${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/inc
    ${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/debug/inc
    ${PROJECT_SOURCE_DIR}/third_party/diagnostics/src/shared/native
    ${PROJECT_SOURCE_DIR}/third_party/dnmd/src/inc
    ${PROJECT_SOURCE_DIR}/third_party
)

# =============================================================================
# Source files
# =============================================================================

include_directories(${PROJECT_SOURCE_DIR}/src)

set(DNCDBG_SOURCES
    debugger/breakpoints/breakpoint_break.cpp
    debugger/breakpoints/breakpoint_entry.cpp
    debugger/breakpoints/breakpoints_exception.cpp
    debugger/breakpoints/breakpoints_function.cpp
    debugger/breakpoints/breakpoints_source.cpp
    debugger/breakpoints/breakpoints.cpp
    debugger/breakpoints/breakpointutils.cpp
    debugger/evaluation/primitivetypes/binaryoperators.cpp
    debugger/evaluation/primitivetypes/cast.cpp
    debugger/evaluation/primitivetypes/types.cpp
    debugger/evaluation/primitivetypes/unaryoperators.cpp
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
    debuginfo/debuginfo.cpp
    debuginfo/debugsources.cpp
    debuginfo/pdbreader.cpp
    debuginfo/sourcefilemap.cpp
    expressionparser/helpers.cpp
    expressionparser/parser.cpp
    metadata/attributes.cpp
    metadata/jmc.cpp
    metadata/modules.cpp
    metadata/sigparse.cpp
    metadata/typeprinter.cpp
    protocol/dap.cpp
    protocol/dapio.cpp
    types/protocol.cpp
    types/types.cpp
    utils/dynlibs_unix.cpp
    utils/dynlibs_win32.cpp
    utils/filesystem_unix.cpp
    utils/filesystem_win32.cpp
    utils/filesystem.cpp
    utils/ioredirect_unix.cpp
    utils/ioredirect_win32.cpp
    utils/ioredirect.cpp
    utils/kqueue.cpp
    utils/logger.cpp
    utils/memorybuffer_unix.cpp
    utils/memorybuffer_win32.cpp
    utils/platform_unix.cpp
    utils/platform_win32.cpp
    utils/remote_console_unix.cpp
    utils/remote_console_win32.cpp
    utils/remote_console.cpp
    utils/utf.cpp
    utils/utftoupper_macos.mm
    utils/utftoupper_unix.cpp
    utils/utftoupper_win32.cpp
    utils/waitpid.cpp
    buildinfo.cpp
    internaltests.cpp
    main.cpp
)

# =============================================================================
# Static analysis tools
# =============================================================================

# clang-tidy for dncdbg build only
if(CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY "clang-tidy")
    set(CMAKE_OBJCXX_CLANG_TIDY "clang-tidy")
endif()

# cppcheck for dncdbg build only
if(CPPCHECK)
    set(CMAKE_CXX_CPPCHECK
        cppcheck
        --enable=all
        --check-level=exhaustive
        --quiet
        --suppress=preprocessorErrorDirective
        --suppress=constParameterReference
        --suppress=missingIncludeSystem
        --suppress=unusedFunction
        --suppress=unmatchedSuppression
        --suppress=*:*third_party/*
    )
endif()

# =============================================================================
# Platform-specific compile definitions
# =============================================================================

if(WIN32)
    # Fix issue with std::numeric_limits<T>::max() and std::max()
    add_compile_definitions(NOMINMAX)
endif()

if(MSVC)
    # Force MSVC to use UTF-8 execution charset, preventing C4566 warnings for non-ASCII characters.
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/execution-charset:utf-8>)
endif()

if(UNIX AND NOT APPLE)
    # Build with old ICU version
    add_compile_definitions(U_HAVE_INT64_T=1)
    add_compile_definitions(U_HAVE_UINT64_T=1)
endif()

# By default, build with case-insensitive file name collision for all OSes.
# Note: test-suite will count on this build option.
if(CASE_SENSITIVE_FILENAME_COLLISION)
    message(STATUS "Added compile option: CASE_SENSITIVE_FILENAME_COLLISION")
    add_compile_definitions(CASE_SENSITIVE_FILENAME_COLLISION)
else()
    message(STATUS "Added compile option: CASE_INSENSITIVE_FILENAME_COLLISION")
    add_compile_definitions(CASE_INSENSITIVE_FILENAME_COLLISION)
endif()

# =============================================================================
# Build info (rebuilds every time to capture version and build timestamp)
# =============================================================================

set(BUILDINFO_COMPILE_DEFINITIONS
    BUILD_TYPE=${CMAKE_BUILD_TYPE}
    DNCDBG_VCS_INFO=${DNCDBG_VCS_INFO}
    OS_NAME=${CMAKE_SYSTEM_NAME}
    CPU_ARCH=${CLR_CMAKE_TARGET_ARCH}
)

set_source_files_properties(buildinfo.cpp
    PROPERTIES COMPILE_DEFINITIONS "${BUILDINFO_COMPILE_DEFINITIONS}"
)

add_custom_target(buildinfo ALL
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_SOURCE_DIR}/src/buildinfo.cpp"
)

# =============================================================================
# Executable target
# =============================================================================

add_executable_clr(dncdbg ${DNCDBG_SOURCES})

# Define "DEBUG" only when the configuration is "Debug"
target_compile_definitions(dncdbg PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

# Define "DEBUG_INTERNAL_TESTS" only when the configuration is "Debug"
target_compile_definitions(dncdbg PRIVATE
    $<$<CONFIG:Debug>:DEBUG_INTERNAL_TESTS>
)

# =============================================================================
# Link libraries
# =============================================================================

if(WIN32)
    target_link_libraries(dncdbg PRIVATE
        corguids
        wsock32
        ws2_32
        dnmd_pdb
        miniz
        tree-sitter-csharp
    )
else()
    find_package(Iconv REQUIRED)
    if(APPLE)
        find_library(CORE_FOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
        target_link_libraries(dncdbg PRIVATE
            corguids
            dl
            pthread
            dnmd_pdb
            miniz
            Iconv::Iconv
            tree-sitter-csharp
            ${CORE_FOUNDATION_FRAMEWORK}
        )
    else()
        target_link_libraries(dncdbg PRIVATE
            corguids
            dl
            pthread
            dnmd_pdb
            miniz
            Iconv::Iconv
            tree-sitter-csharp
        )
    endif()
endif()

install_clr(TARGETS dncdbg DESTINATIONS .)
