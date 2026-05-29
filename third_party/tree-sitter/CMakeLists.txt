cmake_minimum_required(VERSION 3.15)
project(tree-sitter-runtime LANGUAGES C)

# Define runtime source file (lib.c includes all other necessary .c files)
set(TREE_SITTER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/lib/src/lib.c)

# Create static library for tree-sitter core functionality
add_library(tree-sitter STATIC ${TREE_SITTER_SRC})

# Expose public include directory for dependent targets
target_include_directories(tree-sitter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/include)

# Configure private include directory for internal compilation
target_include_directories(tree-sitter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/src)

# Enable position independent code for the library
set_target_properties(tree-sitter PROPERTIES POSITION_INDEPENDENT_CODE ON)
