cmake_minimum_required(VERSION 3.15)

project(miniz LANGUAGES C)

set(SOURCES
  miniz_tdef.c
  miniz_tinfl.c
  miniz_zip.c
  miniz.c
)

add_library(miniz
  STATIC
  ${SOURCES}
)
