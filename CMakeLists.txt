cmake_minimum_required(VERSION 3.20)
project(csm_replay_lab CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(TP ${CMAKE_SOURCE_DIR}/third_party/usr)
set(LOCAL_TOOLCHAIN /home/hj/local/usr)

# --- Shader compilation (glslc from local toolchain) ---
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SPV_DIR ${CMAKE_BINARY_DIR}/spv)
file(MAKE_DIRECTORY ${SPV_DIR})
set(SHADER_SOURCES shadow.vert scene.vert scene.frag)
set(SPV_OUTPUTS)
foreach(S ${SHADER_SOURCES})
  set(SPV ${SPV_DIR}/${S}.spv)
  add_custom_command(
    OUTPUT ${SPV}
    COMMAND ${CMAKE_COMMAND} -E env
            LD_LIBRARY_PATH=${LOCAL_TOOLCHAIN}/lib/x86_64-linux-gnu
            ${LOCAL_TOOLCHAIN}/bin/glslc -O
            ${SHADER_DIR}/${S} -o ${SPV}
    DEPENDS ${SHADER_DIR}/${S}
    COMMENT "glslc ${S}")
  list(APPEND SPV_OUTPUTS ${SPV})
endforeach()
add_custom_target(shaders ALL DEPENDS ${SPV_OUTPUTS})

add_executable(csm_lab
  src/main.cpp
  src/sha256.cpp
  src/png.cpp
  src/trace.cpp
  src/vk_context.cpp
  src/renderer.cpp
  src/experiment.cpp
  src/present.cpp
)
add_dependencies(csm_lab shaders)

target_include_directories(csm_lab PRIVATE
  ${TP}/include
  ${CMAKE_SOURCE_DIR}/src
)

target_compile_options(csm_lab PRIVATE -Wall -Wextra -O2)

target_link_libraries(csm_lab PRIVATE
  ${TP}/lib/x86_64-linux-gnu/libglfw.so
  ${LOCAL_TOOLCHAIN}/lib/x86_64-linux-gnu/libvulkan.so
  dl pthread
)

set_target_properties(csm_lab PROPERTIES
  BUILD_RPATH "${TP}/lib/x86_64-linux-gnu"
)

target_compile_definitions(csm_lab PRIVATE
  GLFW_INCLUDE_NONE
  SPV_DIR="${SPV_DIR}"
  ARTIFACTS_DIR="${CMAKE_SOURCE_DIR}/artifacts"
)
