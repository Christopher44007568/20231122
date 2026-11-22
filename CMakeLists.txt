cmake_minimum_required(VERSION 3.16)

project(rtsync LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RTSYNC_DEPS_ROOT
    "${CMAKE_SOURCE_DIR}/.toolchain/root/usr"
    CACHE PATH "Local Vulkan/GLFW development prefix")
find_program(GLSLANG_VALIDATOR glslangValidator
             HINTS "${RTSYNC_DEPS_ROOT}/bin" REQUIRED)

set(SHADER_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")

set(RTSYNC_SPVS "")
foreach(SHADER sync.vert sync.frag)
  set(SPV "${SHADER_BIN_DIR}/${SHADER}.spv")
  add_custom_command(
    OUTPUT "${SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${SHADER_DIR}/${SHADER}" -o "${SPV}"
    DEPENDS "${SHADER_DIR}/${SHADER}"
    COMMENT "Compiling ${SHADER}")
  list(APPEND RTSYNC_SPVS "${SPV}")
endforeach()
add_custom_target(rtsync_shaders ALL DEPENDS ${RTSYNC_SPVS})

add_library(rtsync_core
  src/json_util.cpp
  src/sha256.cpp
  src/clock.cpp
  src/image.cpp
  src/trace.cpp
  src/analyzer.cpp
  src/report.cpp
  src/artifact.cpp
  src/scenario.cpp)
target_include_directories(rtsync_core
  PUBLIC include
  PRIVATE third_party)
target_compile_options(rtsync_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)

add_executable(rtsync
  src/main.cpp
  src/renderer.cpp)
add_dependencies(rtsync rtsync_shaders)
target_include_directories(rtsync PRIVATE
  "${RTSYNC_DEPS_ROOT}/include"
  third_party)
target_link_directories(rtsync PRIVATE
  "${RTSYNC_DEPS_ROOT}/lib/x86_64-linux-gnu")
target_link_libraries(rtsync PRIVATE rtsync_core vulkan glfw)
target_compile_definitions(rtsync PRIVATE
  GLFW_INCLUDE_NONE
  RTSYNC_SHADER_DIR="${SHADER_BIN_DIR}")
target_compile_options(rtsync PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic
  -Wno-missing-field-initializers>)
set_target_properties(rtsync PROPERTIES
  BUILD_RPATH "${RTSYNC_DEPS_ROOT}/lib/x86_64-linux-gnu")

enable_testing()
add_executable(rtsync_tests tests/test_main.cpp)
target_link_libraries(rtsync_tests PRIVATE rtsync_core)
target_include_directories(rtsync_tests PRIVATE third_party)
add_test(NAME rtsync_unit COMMAND rtsync_tests)
add_test(NAME rtsync_empty_overlarge
  COMMAND bash "${CMAKE_SOURCE_DIR}/tests/edge_cases.sh" "$<TARGET_FILE:rtsync>")

