cmake_minimum_required(VERSION 3.16)
project(pcdclip CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Local sysroot with Vulkan/GLFW headers and libs (see third_party/).
set(PCDCLIP_SYSROOT "${CMAKE_SOURCE_DIR}/third_party/sysroot/usr"
    CACHE PATH "Prefix containing Vulkan/GLFW dev files")

find_program(GLSLANG_VALIDATOR
  NAMES glslangValidator
  HINTS "${PCDCLIP_SYSROOT}/bin")
if(NOT GLSLANG_VALIDATOR)
  message(FATAL_ERROR "glslangValidator not found")
endif()

add_library(pcdclip_deps INTERFACE)
target_include_directories(pcdclip_deps INTERFACE "${PCDCLIP_SYSROOT}/include")
target_link_directories(pcdclip_deps INTERFACE "${PCDCLIP_SYSROOT}/lib/x86_64-linux-gnu")
target_link_libraries(pcdclip_deps INTERFACE vulkan glfw dl pthread)

# Compile GLSL shaders to SPIR-V.
set(SHADER_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")
set(SHADER_SPV "")
foreach(S density.comp points.vert points.frag)
  set(SRC "${SHADER_DIR}/${S}")
  set(SPV "${SHADER_BIN_DIR}/${S}.spv")
  add_custom_command(
    OUTPUT "${SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${SRC}" -o "${SPV}"
    DEPENDS "${SRC}"
    COMMENT "GLSL -> SPIR-V: ${S}")
  list(APPEND SHADER_SPV "${SPV}")
endforeach()
add_custom_target(shaders ALL DEPENDS ${SHADER_SPV})

add_executable(pcdclip
  src/main.cpp
  src/cloud.cpp
  src/sha256.cpp
  src/workspace.cpp
  src/params.cpp
  src/vk.cpp
  src/runner.cpp
  src/compare.cpp
  src/report.cpp
  src/render.cpp
  src/viewer.cpp
  src/util.cpp
)
add_dependencies(pcdclip shaders)
target_link_libraries(pcdclip PRIVATE pcdclip_deps)
target_compile_definitions(pcdclip PRIVATE
  PCDCLIP_SHADER_DIR="${SHADER_BIN_DIR}")
target_compile_options(pcdclip PRIVATE -Wall -Wextra -O2)
