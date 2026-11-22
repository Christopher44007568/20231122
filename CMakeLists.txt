cmake_minimum_required(VERSION 3.20)

project(raster_regression
    VERSION 1.0.0
    DESCRIPTION "Vulkan rasterization-rule regression evidence runner"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RR_BUILD_TESTS "Build the raster-regression self tests" ON)
option(RR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(RR_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)

find_program(RR_GLSLANG_VALIDATOR
    NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{GLSLANG_ROOT}/bin")
if(NOT RR_GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator is required to build the embedded shader stages")
endif()

set(RR_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(RR_VERT_SPV "${RR_SHADER_DIR}/triangle.vert.spv")
set(RR_FRAG_SPV "${RR_SHADER_DIR}/triangle.frag.spv")

file(MAKE_DIRECTORY "${RR_SHADER_DIR}")
add_custom_command(
    OUTPUT "${RR_VERT_SPV}"
    COMMAND "${RR_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert" -o "${RR_VERT_SPV}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert"
    VERBATIM)
add_custom_command(
    OUTPUT "${RR_FRAG_SPV}"
    COMMAND "${RR_GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag" -o "${RR_FRAG_SPV}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag"
    VERBATIM)
add_custom_target(rr_shaders DEPENDS "${RR_VERT_SPV}" "${RR_FRAG_SPV}")

add_library(rr_core STATIC
    src/json.cpp
    src/model.cpp
    src/image.cpp
    src/evidence.cpp
    src/checksum.cpp
    src/sample_cases.cpp)
target_include_directories(rr_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(rr_core PUBLIC
    RR_VERSION="${PROJECT_VERSION}"
    RR_BUILD_TYPE="$<CONFIG>"
    RR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_library(rr_gpu STATIC
    src/runner.cpp
    src/vulkan_renderer.cpp
    src/platform.cpp)
target_include_directories(rr_gpu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(rr_gpu PUBLIC rr_core Vulkan::Vulkan glfw)
target_compile_definitions(rr_gpu PRIVATE
    RR_SHADER_DIR="${RR_SHADER_DIR}")
add_dependencies(rr_gpu rr_shaders)

add_executable(raster-regression src/main.cpp)
target_link_libraries(raster-regression PRIVATE rr_core rr_gpu)

if(RR_BUILD_TESTS)
    enable_testing()
    add_executable(rr_tests tests/test_core.cpp)
    target_link_libraries(rr_tests PRIVATE rr_core)
    add_test(NAME rr_core COMMAND rr_tests)
endif()

install(TARGETS raster-regression RUNTIME DESTINATION bin)
