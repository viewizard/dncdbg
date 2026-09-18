# Compiler configurations

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckCCompilerFlag)

include(CheckCXXCompilerFlag)

#
# Configure CMake for platforms
#
if(WIN32)
  add_compile_definitions(BUILD_WINDOWS=1)
elseif(APPLE)
  add_compile_definitions(BUILD_MACOS=1)
else()
  add_compile_definitions(BUILD_UNIX=1)
endif()

#
# Configure compilers
#
if(MSVC)
  add_compile_options(/Zc:wchar_t-) # wchar_t is a built-in type.
  add_compile_options(/W4 /WX) # warning level 4 and warnings are errors.
  add_compile_options(/Zi) # enable debugging information.

  add_link_options(/DEBUG) # enable debugging information.
else()
  add_compile_options(-Wall -Werror) # All warnings and are errors.
  add_compile_options(-g) # enable debugging information.

  check_c_compiler_flag(-Wno-pragma-pack C_HAS_NO_PRAGMA_PACK)
  if (C_HAS_NO_PRAGMA_PACK)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-pragma-pack>) # cor.h controls pack pragmas via headers.
  endif()
  check_cxx_compiler_flag(-Wno-pragma-pack CXX_HAS_NO_PRAGMA_PACK)
  if (CXX_HAS_NO_PRAGMA_PACK)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-pragma-pack>) # cor.h controls pack pragmas via headers.
  endif()
endif()
