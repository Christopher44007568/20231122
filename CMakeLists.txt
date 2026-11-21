cmake_minimum_required(VERSION 3.16)
project(outline_diag CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Local dependency prefix (extracted debs live here by default).
set(LOCAL_PREFIX "$ENV{HOME}/local" CACHE PATH "Prefix with Vulkan/GLFW headers and libs")

find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
    HINTS "${LOCAL_PREFIX}/usr/include" "$ENV{VULKAN_SDK}/include")
find_library(VULKAN_LIBRARY NAMES vulkan
    HINTS "${LOCAL_PREFIX}/usr/lib/x86_64-linux-gnu" "$ENV{VULKAN_SDK}/lib")
find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
    HINTS "${LOCAL_PREFIX}/usr/include")
find_library(GLFW_LIBRARY NAMES glfw glfw3
    HINTS "${LOCAL_PREFIX}/usr/lib/x86_64-linux-gnu")
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS "${LOCAL_PREFIX}/usr/bin" "$ENV{VULKAN_SDK}/bin")

if(NOT VULKAN_INCLUDE_DIR OR NOT VULKAN_LIBRARY OR NOT GLFW_INCLUDE_DIR OR NOT GLFW_LIBRARY)
    message(FATAL_ERROR "Vulkan or GLFW not found (looked in ${LOCAL_PREFIX})")
endif()
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found")
endif()

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

set(SHADER_SOURCES
    geom.vert geom.frag fullscreen.vert edge.frag diag.frag display.frag)
set(SHADER_SPVS "")
foreach(S ${SHADER_SOURCES})
    set(SPV ${SHADER_BIN_DIR}/${S}.spv)
    add_custom_command(
        OUTPUT ${SPV}
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.2 -I${SHADER_DIR} -o ${SPV} ${SHADER_DIR}/${S}
        DEPENDS ${SHADER_DIR}/${S}
        COMMENT "Compiling ${S}")
    list(APPEND SHADER_SPVS ${SPV})
endforeach()
add_custom_target(shaders ALL DEPENDS ${SHADER_SPVS})

add_executable(outline_diag
    src/main.cpp
    src/renderer.cpp
    src/vk_util.cpp
    src/scene.cpp)
add_dependencies(outline_diag shaders)
target_include_directories(outline_diag PRIVATE
    ${VULKAN_INCLUDE_DIR} ${GLFW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(outline_diag PRIVATE ${VULKAN_LIBRARY} ${GLFW_LIBRARY} dl pthread)
target_compile_definitions(outline_diag PRIVATE
    SHADER_DIR="${SHADER_BIN_DIR}" GLFW_INCLUDE_NONE)
