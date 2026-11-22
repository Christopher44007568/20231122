cmake_minimum_required(VERSION 3.16)
project(nmts_verify LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(LOCAL_PREFIX "${CMAKE_SOURCE_DIR}/third_party/local/usr"
    CACHE PATH "Local dependency prefix (vulkan/glfw headers and libs)")

find_program(GLSLANG_VALIDATOR
  NAMES glslangValidator
  HINTS "${LOCAL_PREFIX}/bin" /usr/bin /usr/local/bin)
if(NOT GLSLANG_VALIDATOR)
  message(FATAL_ERROR "glslangValidator not found; install glslang-tools")
endif()

set(SPV "${CMAKE_BINARY_DIR}/generated/verify.comp.spv")
set(SPV_HEADER "${CMAKE_BINARY_DIR}/generated/verify_comp_spv.hpp")
add_custom_command(
  OUTPUT "${SPV}" "${SPV_HEADER}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
  COMMAND "${GLSLANG_VALIDATOR}" -V
          "${CMAKE_SOURCE_DIR}/shaders/verify.comp" -o "${SPV}"
  COMMAND ${CMAKE_COMMAND}
          -DSPV="${SPV}" -DHEADER="${SPV_HEADER}"
          -P "${CMAKE_SOURCE_DIR}/cmake/embed_spv.cmake"
  DEPENDS "${CMAKE_SOURCE_DIR}/shaders/verify.comp"
          "${CMAKE_SOURCE_DIR}/cmake/embed_spv.cmake"
  COMMENT "Compiling compute shader and embedding SPIR-V")

add_executable(nmts_verify
  src/main.cpp
  src/material.cpp
  src/sha256.cpp
  src/verify_cpu.cpp
  src/verify_vk.cpp
  src/pipeline.cpp
  src/report.cpp
  src/util.cpp)

target_include_directories(nmts_verify PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/generated"
  "${LOCAL_PREFIX}/include")

target_link_directories(nmts_verify PRIVATE
  "${LOCAL_PREFIX}/lib/x86_64-linux-gnu")

target_link_libraries(nmts_verify PRIVATE vulkan glfw dl pthread)

set_target_properties(nmts_verify PROPERTIES
  BUILD_RPATH "${LOCAL_PREFIX}/lib/x86_64-linux-gnu"
  INSTALL_RPATH "$ORIGIN/../third_party/local/usr/lib/x86_64-linux-gnu")

add_custom_target(shader_embed DEPENDS "${SPV_HEADER}")
add_dependencies(nmts_verify shader_embed)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(nmts_verify PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()
