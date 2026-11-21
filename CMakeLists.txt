cmake_minimum_required(VERSION 3.16)
project(aa_history_verify CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# GLFW 3.4: build only the Null platform (no X11/Wayland in this environment).
set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw-3.4)

add_executable(aa_history_verify
    src/main.cpp
    src/vulkan_probe.cpp
    src/report.cpp
)

target_include_directories(aa_history_verify PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers-1.4.335/include
)

# Link against the system Vulkan loader (only libvulkan.so.1 is present).
find_library(VULKAN_LOADER NAMES vulkan libvulkan.so.1
    PATHS /usr/lib/x86_64-linux-gnu /usr/lib)
if(NOT VULKAN_LOADER)
    message(FATAL_ERROR "Vulkan loader not found")
endif()

target_link_libraries(aa_history_verify PRIVATE glfw ${VULKAN_LOADER} ${CMAKE_DL_LIBS})
