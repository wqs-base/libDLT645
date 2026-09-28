cmake_minimum_required(VERSION 3.10)

project(libDLT645_2007
    VERSION 0.1.0
    DESCRIPTION "DL/T 645-2007 multi-function watt-hour meter protocol library"
    LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(DLT645_BUILD_TESTS    "Build unit tests"          ON)
option(DLT645_BUILD_EXAMPLES "Build examples"            ON)
option(DLT645_BUILD_SERIAL   "Build the serial adapter"  ON)

# ---------------------------------------------------------------------
# Core library (no OS dependency, no dynamic memory)
# ---------------------------------------------------------------------
add_library(dlt645 STATIC
    src/dlt645_codec.c
    src/dlt645_frame.c
    src/dlt645_master.c
    src/dlt645_slave.c
    src/dlt645_di.c)

add_library(dlt645::dlt645 ALIAS dlt645)

target_include_directories(dlt645 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/dlt645>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/dlt645>)

if(MSVC)
    target_compile_options(dlt645 PRIVATE /W4 /utf-8)
else()
    target_compile_options(dlt645 PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------
# Optional serial port adapter
# ---------------------------------------------------------------------
if(DLT645_BUILD_SERIAL)
    add_library(dlt645_serial STATIC port/dlt645_serial.c)
    add_library(dlt645::dlt645_serial ALIAS dlt645_serial)
    target_link_libraries(dlt645_serial PUBLIC dlt645)
    if(NOT MSVC)
        target_compile_options(dlt645_serial PRIVATE -Wall -Wextra)
    endif()
endif()

# ---------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------
if(DLT645_BUILD_TESTS)
    enable_testing()
    set(DLT645_TESTS test_frame test_link test_di)
    foreach(t ${DLT645_TESTS})
        add_executable(${t} tests/${t}.c)
        target_link_libraries(${t} PRIVATE dlt645)
        target_include_directories(${t} PRIVATE tests)
        add_test(NAME ${t} COMMAND ${t})
    endforeach()
endif()

# ---------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------
if(DLT645_BUILD_EXAMPLES)
    add_executable(master_demo examples/master_demo.c)
    target_link_libraries(master_demo PRIVATE dlt645)
    if(DLT645_BUILD_SERIAL)
        target_link_libraries(master_demo PRIVATE dlt645_serial)
        target_compile_definitions(master_demo PRIVATE DLT645_HAVE_SERIAL)
    endif()

    add_executable(slave_demo examples/slave_demo.c)
    target_link_libraries(slave_demo PRIVATE dlt645)
    if(DLT645_BUILD_SERIAL)
        target_link_libraries(slave_demo PRIVATE dlt645_serial)
        target_compile_definitions(slave_demo PRIVATE DLT645_HAVE_SERIAL)
    endif()
endif()

# ---------------------------------------------------------------------
# Install + package config (find_package / pkg-config)
# ---------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(DLT645_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/dlt645"
    CACHE STRING "Installation directory for the CMake package files")

set(DLT645_PUBLIC_HEADERS
    include/dlt645)

# Targets that make up the exported package.
set(DLT645_EXPORT_TARGETS dlt645)
if(DLT645_BUILD_SERIAL)
    list(APPEND DLT645_EXPORT_TARGETS dlt645_serial)
endif()

install(TARGETS ${DLT645_EXPORT_TARGETS}
    EXPORT dlt645Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY ${DLT645_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT dlt645Targets
    FILE dlt645Targets.cmake
    NAMESPACE dlt645::
    DESTINATION ${DLT645_INSTALL_CMAKEDIR})

configure_package_config_file(
    cmake/dlt645Config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/dlt645Config.cmake"
    INSTALL_DESTINATION ${DLT645_INSTALL_CMAKEDIR})

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/dlt645ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/dlt645Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/dlt645ConfigVersion.cmake"
    DESTINATION ${DLT645_INSTALL_CMAKEDIR})

# pkg-config metadata
configure_file(cmake/dlt645.pc.in
    "${CMAKE_CURRENT_BINARY_DIR}/dlt645.pc" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/dlt645.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
