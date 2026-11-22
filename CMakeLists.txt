cmake_minimum_required(VERSION 3.16)
project(occlusion_audit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

add_executable(occlusion_audit
  src/main.cpp
  src/evidence.cpp
  src/vulkan_runner.cpp
  src/report.cpp
)

target_include_directories(occlusion_audit PRIVATE
  src
  ${GLFW_INCLUDE_DIRS}
)

target_link_libraries(occlusion_audit PRIVATE
  Vulkan::Vulkan
  ${GLFW_LIBRARIES}
)

target_compile_options(occlusion_audit PRIVATE -Wall -Wextra -Wpedantic)
