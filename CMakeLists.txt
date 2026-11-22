cmake_minimum_required(VERSION 3.16)
project(tcpv LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(TP ${CMAKE_SOURCE_DIR}/third_party)

find_program(GLSLANG_VALIDATOR glslangValidator
  PATHS ${TP}/bin NO_DEFAULT_PATH REQUIRED)

set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_BIN ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BIN})
set(SHADER_SPV "")
foreach(S quad.vert quad.frag)
  set(SPV ${SHADER_BIN}/${S}.spv)
  add_custom_command(
    OUTPUT ${SPV}
    COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER_DIR}/${S} -o ${SPV}
    DEPENDS ${SHADER_DIR}/${S}
    COMMENT "Compiling ${S}")
  list(APPEND SHADER_SPV ${SPV})
endforeach()
add_custom_target(shaders ALL DEPENDS ${SHADER_SPV})

add_executable(tcpv
  src/main.cpp
  src/util.cpp
  src/sha256.cpp
  src/json.cpp
  src/png.cpp
  src/textures.cpp
  src/codecs.cpp
  src/vk.cpp
  src/evidence.cpp
  src/report.cpp
  src/pipeline.cpp)
add_dependencies(tcpv shaders)

target_include_directories(tcpv PRIVATE
  ${TP}/vulkan-headers/include
  ${TP}/glfw/include
  ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(tcpv PRIVATE
  TCPV_SHADER_DIR="${SHADER_BIN}")
target_link_directories(tcpv PRIVATE ${TP}/glfw/lib)
find_library(VULKAN_LIB NAMES vulkan libvulkan.so.1
  PATHS /usr/lib/x86_64-linux-gnu /usr/lib REQUIRED)
target_link_libraries(tcpv PRIVATE glfw ${VULKAN_LIB} dl)
set_target_properties(tcpv PROPERTIES
  BUILD_RPATH "${TP}/glfw/lib")
