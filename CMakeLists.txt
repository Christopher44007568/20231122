cmake_minimum_required(VERSION 3.20)

project(msphere_energy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

find_library(VULKAN_LIBRARY
  NAMES vulkan vulkan.so.1 libvulkan.so.1
  HINTS /usr/lib/x86_64-linux-gnu /usr/local/lib)

if(NOT VULKAN_LIBRARY)
  message(FATAL_ERROR "Vulkan loader not found (need libvulkan.so.1)")
endif()

add_library(glfw_vendor SHARED IMPORTED)
set_target_properties(glfw_vendor PROPERTIES
  IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/lib/libglfw.so.3"
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include")

add_executable(msphere_energy
  src/main.cpp
  src/format.cpp
  src/hash.cpp
  src/image.cpp
  src/renderer.cpp
  src/report.cpp)

target_include_directories(msphere_energy PRIVATE
  src
  third_party/vulkan/include)

target_compile_definitions(msphere_energy PRIVATE
  MSPHERE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

target_compile_options(msphere_energy PRIVATE
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

target_link_libraries(msphere_energy PRIVATE
  "${VULKAN_LIBRARY}"
  glfw_vendor
  dl)

set_target_properties(msphere_energy PROPERTIES
  BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/lib")

enable_testing()
add_test(NAME evidence_self_test
  COMMAND msphere_energy --self-test
          --out "${CMAKE_CURRENT_BINARY_DIR}/evidence")
set_tests_properties(evidence_self_test PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  TIMEOUT 120)
