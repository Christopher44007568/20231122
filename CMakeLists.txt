cmake_minimum_required(VERSION 3.20)
project(procedural_vegetation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(VEGDIST_VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
    HINTS "${VEGDIST_VENDOR}/vulkan/include" /usr/include)
find_library(VULKAN_LIBRARY NAMES vulkan libvulkan.so.1 vulkan.so.1
    HINTS "${VEGDIST_VENDOR}/vulkan/lib" /usr/lib/x86_64-linux-gnu)
find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
    HINTS "${VEGDIST_VENDOR}/glfw/include" /usr/include)
find_library(GLFW_LIBRARY NAMES glfw glfw3 libglfw.so.3.4 glfw.so.3.4
    HINTS "${VEGDIST_VENDOR}/glfw/lib" /usr/lib/x86_64-linux-gnu)

if(NOT VULKAN_LIBRARY AND EXISTS "/usr/lib/x86_64-linux-gnu/libvulkan.so.1")
    set(VULKAN_LIBRARY "/usr/lib/x86_64-linux-gnu/libvulkan.so.1")
endif()
if(EXISTS "${VEGDIST_VENDOR}/glfw/lib/libglfw.so.3.4")
    set(GLFW_LIBRARY "${VEGDIST_VENDOR}/glfw/lib/libglfw.so.3.4")
endif()

if(NOT VULKAN_INCLUDE_DIR OR NOT VULKAN_LIBRARY)
    message(FATAL_ERROR "Vulkan headers/library not found")
endif()
if(NOT GLFW_INCLUDE_DIR OR NOT GLFW_LIBRARY)
    message(FATAL_ERROR "GLFW headers/library not found")
endif()

set(VEGDIST_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(VEGDIST_SPV "${VEGDIST_SHADER_DIR}/vegetation.comp.spv")
if(NOT EXISTS "${VEGDIST_SPV}")
    message(FATAL_ERROR "Precompiled compute shader missing: ${VEGDIST_SPV}")
endif()

add_library(vegd_core
    src/vegetation.cpp
    src/state.cpp
    src/json.cpp
    src/evidence.cpp
    src/pipeline.cpp
)
target_include_directories(vegd_core PUBLIC src "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_compile_options(vegd_core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(vegd_core PUBLIC
    VEGDIST_SHADER_DIR="${VEGDIST_SHADER_DIR}"
    VEGDIST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(vegd_vulkan src/vulkan_backend.cpp)
target_include_directories(vegd_vulkan PUBLIC
    src
    "${VULKAN_INCLUDE_DIR}"
)
target_link_libraries(vegd_vulkan PUBLIC vegd_core "${VULKAN_LIBRARY}")
target_compile_options(vegd_vulkan PRIVATE -Wall -Wextra -Wpedantic)

add_executable(vegdist
    src/main.cpp
    src/app.cpp
    src/glfw_session.cpp
)
target_include_directories(vegdist PRIVATE
    src
    "${GLFW_INCLUDE_DIR}"
)
target_link_libraries(vegdist PRIVATE
    vegd_core
    vegd_vulkan
    "${GLFW_LIBRARY}"
    dl
    pthread
)
target_compile_definitions(vegdist PRIVATE GLFW_INCLUDE_NONE)
target_compile_options(vegdist PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(vegdist PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)
add_custom_command(TARGET vegdist POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${GLFW_LIBRARY}" "$<TARGET_FILE_DIR:vegdist>/libglfw.so.3"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    add_executable(vegd_tests tests/test_core.cpp)
    target_link_libraries(vegd_tests PRIVATE vegd_core)
    target_compile_options(vegd_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME core COMMAND vegd_tests)
    add_test(
        NAME resume_flow
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/resume_flow.sh"
            "$<TARGET_FILE:vegdist>" "${CMAKE_CURRENT_BINARY_DIR}/test-artifacts"
    )
endif()
