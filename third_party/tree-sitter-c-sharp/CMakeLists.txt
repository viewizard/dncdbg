cmake_minimum_required(VERSION 3.14)
project(tree-sitter-csharp-parser LANGUAGES C)

# Define core parser source file
set(TS_CSHARP_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/parser.c")

# Conditionally add scanner.c if it exists in this version of the parser
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/scanner.c")
    list(APPEND TS_CSHARP_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/scanner.c")
endif()

# Create static library for the C# parser
add_library(tree-sitter-csharp STATIC ${TS_CSHARP_SRCS})

# Expose parser src directory so main project can find parser.h if needed
target_include_directories(tree-sitter-csharp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# CHANGE HERE: Use PUBLIC instead of PRIVATE so targets linking to tree-sitter-csharp 
# will automatically inherit tree-sitter core include directories.
target_link_libraries(tree-sitter-csharp PUBLIC tree-sitter)

# Enable position independent code for the parser library
set_target_properties(tree-sitter-csharp PROPERTIES POSITION_INDEPENDENT_CODE ON)
