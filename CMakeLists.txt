cmake_minimum_required(VERSION 3.16)
project(terrain_chunk_experiment CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TERRAIN_USE_SYSTEM_GLFW "Link against system GLFW instead of the headless shim" OFF)

add_executable(terrain_exp
    src/main.cpp
    src/experiment.cpp
    src/journal.cpp
    src/terrain.cpp
    src/vk_context.cpp
)

target_include_directories(terrain_exp PRIVATE src)
target_compile_options(terrain_exp PRIVATE -Wall -Wextra)
target_link_libraries(terrain_exp PRIVATE dl)

if(TERRAIN_USE_SYSTEM_GLFW)
    find_package(glfw3 REQUIRED)
    target_link_libraries(terrain_exp PRIVATE glfw)
else()
    target_sources(terrain_exp PRIVATE src/glfw_shim.cpp)
    target_include_directories(terrain_exp PRIVATE shim)
    target_compile_definitions(terrain_exp PRIVATE TERRAIN_GLFW_SHIM=1)
endif()
