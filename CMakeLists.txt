cmake_minimum_required(VERSION 3.16)
project(shadow_cache_analyzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW3 REQUIRED glfw3)

add_executable(shadow_cache_analyzer
  src/main.cpp
  src/vk_context.cpp
  src/spirv_gen.cpp
  src/scene.cpp
  src/shadow_cache.cpp
  src/analyzer.cpp
  src/report.cpp
  src/viewer.cpp
)

target_include_directories(shadow_cache_analyzer PRIVATE
  ${Vulkan_INCLUDE_DIRS}
  ${GLFW3_INCLUDE_DIRS}
  src
)

target_link_directories(shadow_cache_analyzer PRIVATE ${GLFW3_LIBRARY_DIRS})

target_link_libraries(shadow_cache_analyzer PRIVATE
  ${Vulkan_LIBRARIES}
  ${GLFW3_LIBRARIES}
)

target_compile_options(shadow_cache_analyzer PRIVATE -Wall -Wextra)
