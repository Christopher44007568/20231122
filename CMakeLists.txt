cmake_minimum_required(VERSION 3.16)
project(ssor_reconstruct CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VULKAN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan/include")
set(VULKAN_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan/lib/libvulkan.so.1")
set(GLFW_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include")
set(GLFW_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/lib/libglfw.so.3.4")

if(NOT EXISTS "${VULKAN_INCLUDE_DIR}/vulkan/vulkan.h")
    message(FATAL_ERROR "vendored Vulkan headers are missing")
endif()
if(NOT EXISTS "${GLFW_INCLUDE_DIR}/GLFW/glfw3.h")
    message(FATAL_ERROR "vendored GLFW headers are missing")
endif()

find_package(Threads REQUIRED)

add_library(ssor_core
    src/core/util.cpp
    src/core/timestamp.cpp
    src/core/sha256.cpp
    src/core/material.cpp
    src/core/scene.cpp)
target_include_directories(ssor_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_compile_options(ssor_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ssor_make_material
    tools/make_material.cpp
    src/core/generator.cpp)
target_link_libraries(ssor_make_material PRIVATE ssor_core)

add_executable(ssor
    src/main.cpp
    src/accept.cpp
    src/runner.cpp
    src/evidence.cpp
    src/core/generator.cpp
    src/render/vk_context.cpp
    src/render/renderer.cpp)
target_include_directories(ssor PRIVATE
    "${VULKAN_INCLUDE_DIR}"
    "${GLFW_INCLUDE_DIR}")
target_compile_definitions(ssor PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(ssor PRIVATE
    ssor_core
    "${VULKAN_LIBRARY}"
    "${GLFW_LIBRARY}"
    Threads::Threads
    dl)

set_target_properties(ssor PROPERTIES
    BUILD_RPATH "$ORIGIN;${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan/lib;${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/lib")
add_custom_command(TARGET ssor POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${VULKAN_LIBRARY}" "$<TARGET_FILE_DIR:ssor>/libvulkan.so.1"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${GLFW_LIBRARY}" "$<TARGET_FILE_DIR:ssor>/libglfw.so.3"
    COMMENT "Copying runtime Vulkan/GLFW loaders next to ssor")

enable_testing()
add_executable(ssor_tests
    tests/test_core.cpp
    src/core/generator.cpp)
target_link_libraries(ssor_tests PRIVATE ssor_core)
add_test(NAME core COMMAND ssor_tests)

option(SSOR_RECOMPILE_SHADERS "Recompile GLSL with a local glslangValidator" OFF)
if(SSOR_RECOMPILE_SHADERS)
    find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang)
    if(NOT GLSLANG_VALIDATOR)
        message(FATAL_ERROR "SSOR_RECOMPILE_SHADERS=ON but glslangValidator was not found")
    endif()
    add_custom_target(ssor_shaders
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compile_shaders.sh"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_dependencies(ssor ssor_shaders)
endif()
