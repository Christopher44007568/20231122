cmake_minimum_required(VERSION 3.20)
project(camtrak LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(CAMTRAK_BUILD_GUI "Build the Vulkan/GLFW visualization GUI" ON)
option(CAMTRAK_BUILD_TESTS "Build core unit tests" ON)

set(VENDOR "${CMAKE_SOURCE_DIR}/third_party")

add_library(camtrak_core STATIC
    src/camtrak/sha256.cpp
    src/camtrak/json.cpp
    src/camtrak/model.cpp
    src/camtrak/store.cpp
    src/camtrak/interpolate.cpp
    src/camtrak/engine.cpp
)
target_include_directories(camtrak_core PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
)
target_compile_options(camtrak_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(camtrak
    tools/camtrak_cli.cpp
    tools/cli_commands.cpp
)
target_link_libraries(camtrak PRIVATE camtrak_core)
target_compile_options(camtrak PRIVATE -Wall -Wextra -Wpedantic)

if(CAMTRAK_BUILD_GUI)
  set(GLFW_DIR "${VENDOR}/glfw")
  set(VULKAN_DIR "${VENDOR}/vulkan")
  if(NOT EXISTS "${GLFW_DIR}/include/GLFW/glfw3.h"
     OR NOT EXISTS "${VULKAN_DIR}/include/vulkan/vulkan.h")
    message(FATAL_ERROR
      "Vendored GLFW/Vulkan headers missing under third_party/. "
      "See docs/BUILD.md, or configure -DCAMTRAK_BUILD_GUI=OFF for headless-only.")
  endif()

  find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS /tmp/localtools/usr/bin ENV GLSLANG_ROOT)
  if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR
      "glslangValidator not found; required to compile shaders. See docs/BUILD.md")
  endif()

  set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
  file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")
  set(SHADER_SOURCES line.vert line.frag)
  set(COMPILED_SHADERS "")
  foreach(shader ${SHADER_SOURCES})
    set(in "${CMAKE_SOURCE_DIR}/gui/shaders/${shader}")
    set(out "${SHADER_OUT_DIR}/${shader}.spv")
    add_custom_command(
      OUTPUT "${out}"
      COMMAND "${GLSLANG_VALIDATOR}" -V "${in}" -o "${out}"
      DEPENDS "${in}"
      VERBATIM)
    list(APPEND COMPILED_SHADERS "${out}")
  endforeach()
  add_custom_target(camtrak_shaders ALL DEPENDS ${COMPILED_SHADERS})

  add_executable(camtrak_gui
      gui/main.cpp
      gui/vulkan_app.cpp
  )
  add_dependencies(camtrak_gui camtrak_shaders)
  target_include_directories(camtrak_gui PRIVATE
      "${GLFW_DIR}/include"
      "${VULKAN_DIR}/include"
      "${CMAKE_SOURCE_DIR}/src"
  )
  target_link_directories(camtrak_gui PRIVATE
      "${GLFW_DIR}/lib"
      "${VULKAN_DIR}/lib"
  )
  target_link_libraries(camtrak_gui PRIVATE
      camtrak_core
      "${GLFW_DIR}/lib/libglfw.so.3.4"
      "${VULKAN_DIR}/lib/libvulkan.so.1"
      dl pthread)
  target_compile_definitions(camtrak_gui PRIVATE
      CAMTRAK_SHADER_DIR="${SHADER_OUT_DIR}")
  target_compile_options(camtrak_gui PRIVATE -Wall -Wextra -Wpedantic)
  set_target_properties(camtrak_gui PROPERTIES
      BUILD_RPATH "${GLFW_DIR}/lib;${VULKAN_DIR}/lib")
endif()

if(CAMTRAK_BUILD_TESTS)
  enable_testing()
  add_executable(test_core
      tests/test_main.cpp
      tests/test_sha256.cpp
      tests/test_json.cpp
      tests/test_interpolate.cpp
      tests/test_store.cpp
      tests/test_engine.cpp
  )
  target_link_libraries(test_core PRIVATE camtrak_core)
  target_compile_options(test_core PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME core COMMAND test_core)
endif()
