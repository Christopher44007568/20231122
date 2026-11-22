cmake_minimum_required(VERSION 3.20)
project(screen_depth_recheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(DEPTH_RECHECK_BUILD_TESTS "Build core tests" ON)

if(EXISTS "${CMAKE_SOURCE_DIR}/.deps/usr/include/vulkan/vulkan.h")
  set(DEPTH_RECHECK_LOCAL_DEPS "${CMAKE_SOURCE_DIR}/.deps/usr")
endif()

find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
  HINTS "${DEPTH_RECHECK_LOCAL_DEPS}/include")
find_library(VULKAN_LIBRARY NAMES vulkan
  HINTS "${DEPTH_RECHECK_LOCAL_DEPS}/lib/x86_64-linux-gnu"
  PATHS /usr/lib/x86_64-linux-gnu)
find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
  HINTS "${DEPTH_RECHECK_LOCAL_DEPS}/include")
find_path(STB_INCLUDE_DIR stb/stb_image_write.h
  HINTS "${DEPTH_RECHECK_LOCAL_DEPS}/include")
find_library(GLFW_LIBRARY NAMES glfw glfw3
  HINTS "${DEPTH_RECHECK_LOCAL_DEPS}/lib/x86_64-linux-gnu"
  PATHS /usr/lib/x86_64-linux-gnu)
find_package(Threads REQUIRED)

if(NOT VULKAN_INCLUDE_DIR OR NOT VULKAN_LIBRARY OR
   NOT GLFW_INCLUDE_DIR OR NOT GLFW_LIBRARY OR NOT STB_INCLUDE_DIR)
  message(FATAL_ERROR "Vulkan/GLFW development files not found. "
    "Run scripts/bootstrap_deps.sh first.")
endif()

add_library(depth_core STATIC
  src/math.cpp
  src/scene.cpp
  src/params.cpp
  src/session.cpp
  src/compare.cpp
  src/report.cpp)
target_include_directories(depth_core PUBLIC src)
target_compile_options(depth_core PRIVATE -Wall -Wextra -Wpedantic)

find_program(GLSLANG_VALIDATOR NAMES glslangValidator
  HINTS "${DEPTH_RECHECK_LOCAL_DEPS}/bin")
if(NOT GLSLANG_VALIDATOR)
  message(FATAL_ERROR "glslangValidator not found. "
    "Run scripts/bootstrap_deps.sh or install glslang-tools.")
endif()
set(SHADER_NAMES scene.vert scene.frag composite.vert composite.frag ui.vert ui.frag)
set(SHADER_OUTPUTS)
foreach(shader IN LISTS SHADER_NAMES)
  set(shader_in "${CMAKE_SOURCE_DIR}/shaders/${shader}")
  set(shader_out "${CMAKE_BINARY_DIR}/bin/shaders/${shader}.spv")
  add_custom_command(
    OUTPUT "${shader_out}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/bin/shaders"
    COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
      -o "${shader_out}" "${shader_in}"
    DEPENDS "${shader_in}"
    VERBATIM)
  list(APPEND SHADER_OUTPUTS "${shader_out}")
endforeach()
add_custom_target(depth_shaders DEPENDS ${SHADER_OUTPUTS})

add_executable(depth_recheck
  src/main.cpp
  src/app.cpp
  src/app_actions.cpp
  src/app_ui.cpp
  src/renderer.cpp
  src/renderer_resources.cpp
  src/renderer_scene.cpp
  src/renderer_swapchain.cpp
  src/ui.cpp
  src/image_io.cpp)
target_link_libraries(depth_recheck PRIVATE depth_core Threads::Threads
  "${VULKAN_LIBRARY}" "${GLFW_LIBRARY}" dl)
target_include_directories(depth_recheck PRIVATE
  "${VULKAN_INCLUDE_DIR}" "${GLFW_INCLUDE_DIR}" "${STB_INCLUDE_DIR}")
target_compile_definitions(depth_recheck PRIVATE
  DEPTH_RECHECK_VERSION="0.1.0")
target_compile_options(depth_recheck PRIVATE
  -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
add_dependencies(depth_recheck depth_shaders)
if(DEPTH_RECHECK_LOCAL_DEPS)
  set_target_properties(depth_recheck PROPERTIES
    BUILD_RPATH "${DEPTH_RECHECK_LOCAL_DEPS}/lib/x86_64-linux-gnu")
endif()

if(DEPTH_RECHECK_BUILD_TESTS)
  enable_testing()
  add_executable(depth_core_tests tests/core_tests.cpp)
  target_link_libraries(depth_core_tests PRIVATE depth_core)
  add_test(NAME depth_core_tests COMMAND depth_core_tests)
endif()

