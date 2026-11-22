cmake_minimum_required(VERSION 3.20)

project(skeleton_keyframe_lab VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KFC_ENABLE_WARNINGS "Enable strict compiler warnings" ON)

set(KFC_DEPS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/root")
if(EXISTS "${KFC_DEPS_ROOT}/usr/include/vulkan/vulkan.h")
  list(PREPEND CMAKE_PREFIX_PATH "${KFC_DEPS_ROOT}/usr")
  list(PREPEND CMAKE_INCLUDE_PATH "${KFC_DEPS_ROOT}/usr/include")
  list(PREPEND CMAKE_LIBRARY_PATH "${KFC_DEPS_ROOT}/usr/lib/x86_64-linux-gnu")
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 3.4 REQUIRED)

add_executable(skeleton-kf
  src/main.cpp
  src/json.cpp
  src/sha256.cpp
  src/scenario.cpp
  src/compressor.cpp
  src/vulkan_accelerator.cpp
  src/glfw_runtime.cpp
  src/pipeline.cpp
  src/report.cpp
)

target_include_directories(skeleton-kf PRIVATE src)
target_link_libraries(skeleton-kf PRIVATE Vulkan::Vulkan glfw)

if(KFC_ENABLE_WARNINGS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(skeleton-kf PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion)
  endif()
endif()

if(EXISTS "${KFC_DEPS_ROOT}/usr/lib/x86_64-linux-gnu/libglfw.so.3")
  set_target_properties(skeleton-kf PROPERTIES
    BUILD_RPATH "${KFC_DEPS_ROOT}/usr/lib/x86_64-linux-gnu")
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(kfc-json-test
    tests/json_test.cpp
    src/json.cpp
    src/sha256.cpp
  )
  target_include_directories(kfc-json-test PRIVATE src)
  add_test(NAME ordered_json_and_sha256 COMMAND kfc-json-test)
  add_test(NAME pipeline_smoke
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.sh $<TARGET_FILE:skeleton-kf>)
  set_tests_properties(pipeline_smoke PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    TIMEOUT 120)
endif()
