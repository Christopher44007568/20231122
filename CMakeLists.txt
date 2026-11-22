cmake_minimum_required(VERSION 3.16)
project(topo_diag CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Local sysroot extracted from Ubuntu .deb packages (see third_party/README.md).
set(SYSROOT "${CMAKE_SOURCE_DIR}/third_party/sysroot")
set(VULKAN_INCLUDE "${SYSROOT}/usr/include")
set(GLFW_INCLUDE "${SYSROOT}/usr/include")
find_library(VULKAN_LIB vulkan PATHS "${SYSROOT}/usr/lib/x86_64-linux-gnu" NO_DEFAULT_PATH)
find_library(GLFW_LIB glfw PATHS "${SYSROOT}/usr/lib/x86_64-linux-gnu" NO_DEFAULT_PATH)

if(NOT VULKAN_LIB OR NOT GLFW_LIB)
  message(FATAL_ERROR "Vulkan/GLFW not found in third_party/sysroot; see third_party/README.md")
endif()

# Compile shaders with the glslangValidator from the local sysroot.
set(GLSLANG "${SYSROOT}/usr/bin/glslangValidator")
set(SHADER_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_BIN "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_BIN}")
set(SHADER_SPVS "")
foreach(S tri.vert tri.frag)
  set(SPV "${SHADER_BIN}/${S}.spv")
  add_custom_command(
    OUTPUT "${SPV}"
    COMMAND "${GLSLANG}" -V "${SHADER_DIR}/${S}" -o "${SPV}"
    DEPENDS "${SHADER_DIR}/${S}"
    COMMENT "Compiling shader ${S}")
  list(APPEND SHADER_SPVS "${SPV}")
endforeach()
add_custom_target(shaders ALL DEPENDS ${SHADER_SPVS})

add_executable(topo_diag
  src/main.cpp
  src/vk_context.cpp
  src/renderer.cpp
  src/capture_format.cpp
  src/recorder.cpp
  src/replayer.cpp
  src/report.cpp
  src/image.cpp)
add_dependencies(topo_diag shaders)
target_include_directories(topo_diag PRIVATE "${VULKAN_INCLUDE}" "${GLFW_INCLUDE}" src)
target_link_libraries(topo_diag PRIVATE "${VULKAN_LIB}" "${GLFW_LIB}" dl pthread)
target_compile_definitions(topo_diag PRIVATE SHADER_DIR="${SHADER_BIN}")

enable_testing()
add_test(NAME pipeline COMMAND bash ${CMAKE_SOURCE_DIR}/tests/run_pipeline.sh $<TARGET_FILE:topo_diag>)
