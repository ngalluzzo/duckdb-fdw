cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME fdw_boundary_probe)
set(EXTENSION_NAME ${TARGET_NAME}_extension)

project(${TARGET_NAME})

include_directories(src/include)

set(EXTENSION_SOURCES src/fdw_boundary_probe_extension.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
