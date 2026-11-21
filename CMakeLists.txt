cmake_minimum_required(VERSION 3.16)
project(gpu_skinning_review LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Local, root-free toolchain layout (see third_party/).
set(THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(VULKAN_HEADERS_DIR "${THIRD_PARTY}/vulkan-headers/include")
set(GLFW_DIR "${THIRD_PARTY}/glfw/install")
set(GLSLANG_VALIDATOR
    "${THIRD_PARTY}/glslang/install/bin/glslangValidator"
    CACHE FILEPATH "Path to glslangValidator")

if(NOT EXISTS "${VULKAN_HEADERS_DIR}/vulkan/vulkan.h")
    message(FATAL_ERROR "Vulkan headers missing under ${VULKAN_HEADERS_DIR}")
endif()
if(NOT EXISTS "${GLFW_DIR}/lib/libglfw3.a")
    message(FATAL_ERROR "GLFW null-platform static lib missing under ${GLFW_DIR}")
endif()
if(NOT EXISTS "${GLSLANG_VALIDATOR}")
    message(FATAL_ERROR "glslangValidator missing: ${GLSLANG_VALIDATOR}")
endif()

# Distros without the Vulkan loader dev package only ship libvulkan.so.1;
# accept either name and fall back to the multiarch default path.
find_library(VULKAN_LIB NAMES vulkan vulkan.so.1)
if(NOT VULKAN_LIB)
    set(VULKAN_LIB "/usr/lib/x86_64-linux-gnu/libvulkan.so.1")
endif()
if(NOT EXISTS "${VULKAN_LIB}")
    message(FATAL_ERROR "Vulkan loader library not found")
endif()

add_library(glfw_null STATIC IMPORTED)
set_target_properties(glfw_null PROPERTIES
    IMPORTED_LOCATION "${GLFW_DIR}/lib/libglfw3.a"
    INTERFACE_INCLUDE_DIRECTORIES "${GLFW_DIR}/include")

add_library(skinning_core STATIC
    src/skinning_format.cpp
    src/skeleton.cpp
    src/evidence.cpp
    src/cpu_check.cpp
    src/vk_common.cpp
    src/renderer.cpp)
target_include_directories(skinning_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${VULKAN_HEADERS_DIR}")
target_link_libraries(skinning_core PUBLIC glfw_null ${VULKAN_LIB} dl pthread)
# Vulkan structs rely heavily on sType-first partial aggregate init.
target_compile_options(skinning_core PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)

add_executable(skinning_review src/main.cpp)
target_link_libraries(skinning_review PRIVATE skinning_core)

add_executable(generate_sample tools/generate_sample.cpp)
target_include_directories(generate_sample PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(generate_sample PRIVATE skinning_core)

add_executable(test_format_and_math tests/test_format_and_math.cpp)
target_include_directories(test_format_and_math PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(test_format_and_math PRIVATE skinning_core)
add_test(NAME format_and_math COMMAND test_format_and_math)
enable_testing()

# Compile GLSL to SPIR-V as part of the build; fail loudly on shader errors.
set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")
set(SPV_OUTPUTS "")
foreach(shader skinned.vert skinned.frag)
    set(in "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}")
    set(out "${SHADER_OUT_DIR}/${shader}.spv")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${GLSLANG_VALIDATOR}" -V "${in}" -o "${out}"
        DEPENDS "${in}"
        COMMENT "Compiling ${shader} to SPIR-V")
    list(APPEND SPV_OUTPUTS "${out}")
endforeach()
add_custom_target(shaders ALL DEPENDS ${SPV_OUTPUTS})
add_dependencies(skinning_review shaders)
