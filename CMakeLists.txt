cmake_minimum_required(VERSION 3.16)
project(instance_model_selection_experiment LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

set(VULKAN_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan/include")
set(GLFW_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include")
set(GLFW_LIBRARY
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/lib/libglfw3.a")

if(NOT EXISTS "${VULKAN_INCLUDE_DIR}/vulkan/vulkan.h")
    message(FATAL_ERROR "Vulkan headers missing under third_party/vulkan")
endif()
if(NOT EXISTS "${GLFW_LIBRARY}")
    message(FATAL_ERROR "GLFW null-platform static library missing")
endif()
foreach(shader instance.vert.spv instance.frag.spv)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}")
        message(FATAL_ERROR "precompiled shader missing: shaders/${shader}")
    endif()
endforeach()

find_library(VULKAN_LOADER NAMES vulkan vulkan.so.1)
if(NOT VULKAN_LOADER)
    set(VULKAN_LOADER "/usr/lib/x86_64-linux-gnu/libvulkan.so.1")
endif()
if(NOT EXISTS "${VULKAN_LOADER}")
    message(FATAL_ERROR "Vulkan loader library not found")
endif()

add_library(glfw_null STATIC IMPORTED)
set_target_properties(glfw_null PROPERTIES
    IMPORTED_LOCATION "${GLFW_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${GLFW_INCLUDE_DIR}")

add_library(instance_core STATIC
    src/experiment.cpp
    src/format.cpp
    src/selection.cpp
    src/journal.cpp
    src/evidence.cpp
    src/vk_context.cpp
    src/renderer.cpp)
target_include_directories(instance_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${VULKAN_INCLUDE_DIR}")
target_link_libraries(instance_core PUBLIC
    glfw_null "${VULKAN_LOADER}" Threads::Threads dl)
target_compile_options(instance_core PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)

add_executable(instance_select src/main.cpp)
target_link_libraries(instance_select PRIVATE instance_core)
target_compile_definitions(instance_select PRIVATE
    INSTANCE_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
target_compile_options(instance_select PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)

add_executable(generate_fixtures tools/generate_fixtures.cpp)
target_link_libraries(generate_fixtures PRIVATE instance_core)
target_compile_options(generate_fixtures PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)

add_executable(instance_core_tests tests/core_tests.cpp)
target_link_libraries(instance_core_tests PRIVATE instance_core)
target_compile_options(instance_core_tests PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)

enable_testing()
add_test(NAME instance_core_tests COMMAND instance_core_tests)
