cmake_minimum_required(VERSION 3.16)
project(gpu_crash_provenance LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GCDUMP_BUILD_GUI "build the GLFW/Vulkan inspection page" ON)

add_library(gcdump_core STATIC
    src/core/types.cpp
    src/core/parser.cpp
    src/core/analyzer.cpp
    src/core/render.cpp
)
target_include_directories(gcdump_core PUBLIC src)
if(MSVC)
    target_compile_options(gcdump_core PRIVATE /W4)
else()
    target_compile_options(gcdump_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(gcdump_cli src/cli/main.cpp)
set_target_properties(gcdump_cli PROPERTIES OUTPUT_NAME gcdump)
target_link_libraries(gcdump_cli PRIVATE gcdump_core)

enable_testing()
if(GCDUMP_BUILD_GUI)
    add_subdirectory(src/gui)
endif()
add_subdirectory(tests)
