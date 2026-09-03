# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 IHP GmbH
#
# Build for the chiplet_format_io C++ reference reader/writer. The only
# dependency is yaml-cpp (MIT). No Qt, no KLayout, no pcbnew.

cmake_minimum_required(VERSION 3.16)
# VERSION is the reader release, kept equal to READER_RELEASE in the header and
# to chiplet_format_io.__version__ on the Python side.
project(chiplet_format_io LANGUAGES CXX VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(yaml-cpp REQUIRED)

# yaml-cpp 0.8 exports the target yaml-cpp::yaml-cpp; older packages export the
# bare `yaml-cpp` target or only the YAML_CPP_LIBRARIES/INCLUDE_DIR variables.
if(TARGET yaml-cpp::yaml-cpp)
  set(_yamlcpp_target yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
  set(_yamlcpp_target yaml-cpp)
else()
  set(_yamlcpp_target ${YAML_CPP_LIBRARIES})
endif()

add_library(chiplet_format_io STATIC src/chiplet_format_io.cpp)
target_include_directories(chiplet_format_io
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(chiplet_format_io PUBLIC ${_yamlcpp_target})
if(DEFINED YAML_CPP_INCLUDE_DIR)
  target_include_directories(chiplet_format_io PRIVATE ${YAML_CPP_INCLUDE_DIR})
endif()

# Tests are on by default; turn off with -DCHIPLET_FORMAT_IO_BUILD_TESTS=OFF.
option(CHIPLET_FORMAT_IO_BUILD_TESTS "Build chiplet_format_io tests" ON)
if(CHIPLET_FORMAT_IO_BUILD_TESTS)
  enable_testing()
  add_executable(test_chiplet_format_io tests/test_chiplet_format_io.cpp)
  target_link_libraries(test_chiplet_format_io PRIVATE chiplet_format_io)
  target_compile_definitions(test_chiplet_format_io PRIVATE
    CHIPLET_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../examples"
    CHIPLET_HEADER_FILE="${CMAKE_CURRENT_SOURCE_DIR}/include/chiplet_format_io/chiplet_format_io.hpp"
    CHIPLET_SOURCE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/chiplet_format_io.cpp")
  add_test(NAME chiplet_format_io COMMAND test_chiplet_format_io)
endif()
