cmake_minimum_required(VERSION 3.16)
project(rpsc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Locally extracted toolchain (no root access in this environment).
set(RPSC_DEPS_ROOT "${CMAKE_SOURCE_DIR}/.toolchain/root/usr" CACHE PATH "prefix with vulkan/glfw headers+libs")

find_program(GLSLANG_VALIDATOR glslangValidator HINTS "${RPSC_DEPS_ROOT}/bin" REQUIRED)

set(SHADER_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")

set(SHADER_SPVS "")
foreach(S tri.vert tri.frag)
  set(SPV "${SHADER_BIN_DIR}/${S}.spv")
  add_custom_command(
    OUTPUT "${SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${SHADER_DIR}/${S}" -o "${SPV}"
    DEPENDS "${SHADER_DIR}/${S}"
    COMMENT "Compiling shader ${S}")
  list(APPEND SHADER_SPVS "${SPV}")
endforeach()
add_custom_target(shaders ALL DEPENDS ${SHADER_SPVS})

add_executable(rpsc
  src/main.cpp
  src/trace.cpp
  src/recover.cpp
  src/scene.cpp
  src/image.cpp
  src/renderer.cpp
  src/report.cpp)
add_dependencies(rpsc shaders)

target_include_directories(rpsc PRIVATE "${RPSC_DEPS_ROOT}/include")
target_link_directories(rpsc PRIVATE "${RPSC_DEPS_ROOT}/lib/x86_64-linux-gnu")
target_link_libraries(rpsc PRIVATE vulkan glfw)
target_compile_definitions(rpsc PRIVATE
  GLFW_INCLUDE_NONE
  RPSC_SHADER_DIR="${SHADER_BIN_DIR}")
target_compile_options(rpsc PRIVATE -Wall -Wextra)

# Let the binary find the locally extracted libglfw at runtime.
set_target_properties(rpsc PROPERTIES
  BUILD_RPATH "${RPSC_DEPS_ROOT}/lib/x86_64-linux-gnu")
