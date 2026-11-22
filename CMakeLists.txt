cmake_minimum_required(VERSION 3.20)

project(gs_inspect LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GS_LOCAL_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/.toolchain/root")
if(EXISTS "${GS_LOCAL_DEPS}/usr/include/vulkan/vulkan.h")
    list(PREPEND CMAKE_PREFIX_PATH "${GS_LOCAL_DEPS}")
    if(NOT Vulkan_INCLUDE_DIR)
        set(Vulkan_INCLUDE_DIR "${GS_LOCAL_DEPS}/usr/include" CACHE PATH
            "Vulkan headers")
    endif()
    if(NOT Vulkan_LIBRARY AND
       EXISTS "/usr/lib/x86_64-linux-gnu/libvulkan.so.1")
        set(Vulkan_LIBRARY "/usr/lib/x86_64-linux-gnu/libvulkan.so.1" CACHE
            FILEPATH "Vulkan loader")
    endif()
    if(NOT glfw3_DIR AND
       EXISTS "${GS_LOCAL_DEPS}/usr/lib/x86_64-linux-gnu/cmake/glfw3")
        set(glfw3_DIR "${GS_LOCAL_DEPS}/usr/lib/x86_64-linux-gnu/cmake/glfw3"
            CACHE PATH "GLFW package configuration")
    endif()
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang
    HINTS "${GS_LOCAL_DEPS}/usr/bin" REQUIRED)

set(GS_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(GS_SHADERS
    capture.vert
    capture.geom
    replay.vert
    output.frag
)

set(GS_SPIRV_FILES)
foreach(shader IN LISTS GS_SHADERS)
    get_filename_component(shader_name "${shader}" NAME)
    set(spirv "${GS_SHADER_OUTPUT_DIR}/${shader_name}.spv")
    add_custom_command(
        OUTPUT "${spirv}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GS_SHADER_OUTPUT_DIR}"
        COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}" -o "${spirv}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}"
        COMMENT "Compiling ${shader}"
        VERBATIM
    )
    list(APPEND GS_SPIRV_FILES "${spirv}")
endforeach()
add_custom_target(gs_shaders DEPENDS ${GS_SPIRV_FILES})

add_executable(gs_inspect
    src/main.cpp
    src/app.cpp
    src/checkpoint.cpp
    src/cpu_backend.cpp
    src/material.cpp
    src/sha256.cpp
    src/util.cpp
    src/vulkan_engine.cpp
)
add_dependencies(gs_inspect gs_shaders)

target_include_directories(gs_inspect PRIVATE src)
target_link_libraries(gs_inspect PRIVATE Vulkan::Vulkan glfw)
target_compile_definitions(gs_inspect PRIVATE
    GS_SHADER_DIR="${GS_SHADER_OUTPUT_DIR}"
    GS_DEFAULT_MATERIAL="${CMAKE_CURRENT_SOURCE_DIR}/material/geometry_input.gsi"
    GS_PROJECT_VERSION="0.1.0"
)

if(MSVC)
    target_compile_options(gs_inspect PRIVATE /W4)
else()
    target_compile_options(gs_inspect PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(
    NAME gs_inspect_acceptance
    COMMAND gs_inspect --acceptance "${CMAKE_CURRENT_BINARY_DIR}/acceptance-test"
)
set_tests_properties(gs_inspect_acceptance PROPERTIES
    ENVIRONMENT "GS_HEADLESS=1"
    TIMEOUT 120
)

add_test(
    NAME gs_inspect_compact
    COMMAND gs_inspect --acceptance
            "${CMAKE_CURRENT_BINARY_DIR}/acceptance-compact-test"
            --degrade compact
)
add_test(
    NAME gs_inspect_low_memory
    COMMAND gs_inspect --acceptance
            "${CMAKE_CURRENT_BINARY_DIR}/acceptance-low-memory-test"
            --budget-mib 1
)
add_test(
    NAME gs_inspect_resume_chain
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_resume_chain.sh"
            "$<TARGET_FILE:gs_inspect>"
            "${CMAKE_CURRENT_BINARY_DIR}/resume-chain-test"
)
set_tests_properties(gs_inspect_compact gs_inspect_low_memory
    gs_inspect_resume_chain PROPERTIES
    ENVIRONMENT "GS_HEADLESS=1"
    TIMEOUT 120
)
