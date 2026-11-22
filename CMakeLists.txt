cmake_minimum_required(VERSION 3.16)
project(texarray_review CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEPS_ROOT)
  set(DEPS_ROOT "$ENV{HOME}/deps")
endif()
message(STATUS "DEPS_ROOT=${DEPS_ROOT}")

set(GLSLANG_VALIDATOR "${DEPS_ROOT}/bin/glslangValidator")
if(NOT EXISTS "${GLSLANG_VALIDATOR}")
  message(FATAL_ERROR "glslangValidator not found at ${GLSLANG_VALIDATOR}; run scripts/bootstrap_deps.sh")
endif()

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SPV_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SPV_DIR}")

set(SPV_OUTPUTS "")
foreach(S quad.vert quad.frag)
  set(SPV "${SPV_DIR}/${S}.spv")
  add_custom_command(
    OUTPUT "${SPV}"
    COMMAND "${GLSLANG_VALIDATOR}" -V "${SHADER_DIR}/${S}" -o "${SPV}"
    DEPENDS "${SHADER_DIR}/${S}"
    COMMENT "Compiling shader ${S}")
  list(APPEND SPV_OUTPUTS "${SPV}")
endforeach()
add_custom_target(shaders ALL DEPENDS ${SPV_OUTPUTS})

add_executable(texarray-review
  src/main.cpp
  src/util.cpp
  src/png.cpp
  src/vk_context.cpp
  src/renderer.cpp
  src/paging.cpp
  src/snapshot.cpp
  src/diff.cpp
  src/review.cpp)
add_dependencies(texarray-review shaders)

target_include_directories(texarray-review PRIVATE
  "${DEPS_ROOT}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_directories(texarray-review PRIVATE "${DEPS_ROOT}/lib")
target_link_libraries(texarray-review PRIVATE glfw3 vulkan pthread dl)
target_compile_options(texarray-review PRIVATE -Wall -Wextra
  -Wno-missing-field-initializers -O2)

# Ship shader SPIR-V next to the executable so the binary is relocatable.
add_custom_command(TARGET texarray-review POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${SPV_DIR}"
          "$<TARGET_FILE_DIR:texarray-review>/shaders")
