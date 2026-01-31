# Fetch .NET SDK binaries if necessary
if ("${DOTNET_DIR}" STREQUAL "")
    set(DOTNET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.dotnet)

    if (WIN32)
        execute_process(
            COMMAND powershell -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 ; (new-object System.Net.WebClient).DownloadFile('https://dot.net/v1/dotnet-install.ps1', '${CMAKE_CURRENT_BINARY_DIR}/dotnet-install.ps1')"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE retcode)
        if (NOT "${retcode}" STREQUAL "0")
            message(FATAL_ERROR "Fatal error when downloading dotnet install script")
        endif()
        if (CLR_CMAKE_PLATFORM_ARCH_I386)
            set(NETSDKARCH "x86")
        else()
            set(NETSDKARCH "x64")
        endif()
        execute_process(
            COMMAND powershell -File "${CMAKE_CURRENT_BINARY_DIR}/dotnet-install.ps1" -Channel "${DOTNET_CHANNEL}" -InstallDir "${DOTNET_DIR}" -Architecture ${NETSDKARCH} -Verbose
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE retcode)
        if (NOT "${retcode}" STREQUAL "0")
            message(FATAL_ERROR "Fatal error when installing dotnet")
        endif()
    else()
        execute_process(
            COMMAND bash -c "curl -sSL \"https://dot.net/v1/dotnet-install.sh\" | bash /dev/stdin --channel \"${DOTNET_CHANNEL}\" --install-dir \"${DOTNET_DIR}\" --verbose"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE retcode)
        if (NOT "${retcode}" STREQUAL "0")
            message(FATAL_ERROR "Fatal error when installing dotnet")
        endif()
    endif()
endif()
