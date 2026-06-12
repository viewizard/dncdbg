set(SOURCES
  access.c
  bytes.c
  deltas.c
  editor.c
  entry.c
  query.c
  streams.c
  tables.c
  write.c
)

set(HEADERS
  ../inc/dnmd.h
  ./internal.h
)

add_library(dnmd_pdb
  STATIC
  ${SOURCES}
  ${HEADERS}
)

target_compile_definitions(dnmd_pdb PUBLIC DNMD_PORTABLE_PDB)
target_sources(dnmd_pdb PRIVATE ../inc/dnmd_pdb.h pdb_blobs.c)
set_target_properties(dnmd_pdb PROPERTIES EXPORT_NAME pdb)

target_include_directories(dnmd_pdb PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../inc>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../inc/external>
)

set_target_properties(dnmd_pdb PROPERTIES
  PUBLIC_HEADER "../inc/dnmd.h;../inc/dnmd.hpp;../inc/dnmd_pdb.h"
  POSITION_INDEPENDENT_CODE ON)
