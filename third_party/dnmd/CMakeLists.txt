cmake_minimum_required(VERSION 3.15)
project(dnmd LANGUAGES C CXX)

if (POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW) # Set timestamps in downloaded archives to the time of download.
endif()

include_directories(src/inc)
include_directories(src/inc/external) # Hiding the "external" subdirectory due to uses of <...> in cor.h.

add_subdirectory(src/)

