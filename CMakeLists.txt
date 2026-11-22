cmake_minimum_required(VERSION 3.16)
project(gpu_ts_inspect CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(gpu_ts_inspect
    src/main.cpp
    src/gpu.cpp
    src/log.cpp
    src/analyze.cpp
    src/report.cpp
)

target_include_directories(gpu_ts_inspect PRIVATE
    src
    third_party/vulkan/include
    third_party/glfw/include
)

target_compile_options(gpu_ts_inspect PRIVATE -Wall -Wextra -Wpedantic)

find_library(VULKAN_LIB NAMES vulkan libvulkan.so.1
    PATHS /usr/lib/x86_64-linux-gnu /home/hj/sysroot/usr/lib/x86_64-linux-gnu)
if(NOT VULKAN_LIB)
    set(VULKAN_LIB /usr/lib/x86_64-linux-gnu/libvulkan.so.1)
endif()

target_link_directories(gpu_ts_inspect PRIVATE third_party/glfw/lib)
target_link_libraries(gpu_ts_inspect PRIVATE glfw ${VULKAN_LIB})
set_target_properties(gpu_ts_inspect PROPERTIES
    BUILD_RPATH "${CMAKE_SOURCE_DIR}/third_party/glfw/lib")
