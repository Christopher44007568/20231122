cmake_minimum_required(VERSION 3.16)
project(gpu_lifecycle CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_library(VULKAN_LIBRARY NAMES vulkan libvulkan.so.1 REQUIRED)

add_executable(gpu_lifecycle src/main.cpp)
target_include_directories(gpu_lifecycle PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/vulkan-headers/include
    ${CMAKE_SOURCE_DIR}/third_party/glfw/include)
target_link_directories(gpu_lifecycle PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/glfw-build/src)
target_link_libraries(gpu_lifecycle PRIVATE glfw3 ${VULKAN_LIBRARY} dl pthread)
