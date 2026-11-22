cmake_minimum_required(VERSION 3.20)

project(bbox_replay_experiment LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan QUIET)
if(NOT TARGET Vulkan::Vulkan)
  find_path(BBOX_VULKAN_INCLUDE_DIR
    NAMES vulkan/vulkan.h
    PATH_SUFFIXES include)
  set(BBOX_VULKAN_HINTS)
  foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
    list(APPEND BBOX_VULKAN_HINTS
      "${_prefix}/lib"
      "${_prefix}/lib/x86_64-linux-gnu"
      "${_prefix}/lib/aarch64-linux-gnu")
  endforeach()
  find_library(BBOX_VULKAN_LIBRARY
    NAMES vulkan
    HINTS ${BBOX_VULKAN_HINTS}
    PATH_SUFFIXES lib lib/x86_64-linux-gnu lib/aarch64-linux-gnu)
  if(NOT BBOX_VULKAN_INCLUDE_DIR OR NOT BBOX_VULKAN_LIBRARY)
    message(FATAL_ERROR
      "Vulkan development files not found. Install libvulkan-dev.")
  endif()
  add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
  set_target_properties(Vulkan::Vulkan PROPERTIES
    IMPORTED_LOCATION "${BBOX_VULKAN_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${BBOX_VULKAN_INCLUDE_DIR}")
endif()
find_package(glfw3 3.3 CONFIG REQUIRED)

find_program(GLSLC_EXECUTABLE glslc)
set(BBOX_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${BBOX_SHADER_OUTPUT_DIR}")

function(compile_shader source_name output_name)
  set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source_name}")
  set(output "${BBOX_SHADER_OUTPUT_DIR}/${output_name}")
  if(GLSLC_EXECUTABLE)
    add_custom_command(
      OUTPUT "${output}"
      COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.0
              "${source}" -o "${output}"
      DEPENDS "${source}"
      COMMENT "Compiling ${source_name}"
      VERBATIM)
  else()
    set(precompiled "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${output_name}")
    if(NOT EXISTS "${precompiled}")
      message(FATAL_ERROR
        "glslc is unavailable and no precompiled shader exists: ${precompiled}")
    endif()
    add_custom_command(
      OUTPUT "${output}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${precompiled}" "${output}"
      DEPENDS "${precompiled}"
      VERBATIM)
  endif()
endfunction()

compile_shader(bbox.vert bbox.vert.spv)
compile_shader(bbox.frag bbox.frag.spv)
add_custom_target(bbox_shaders DEPENDS
  "${BBOX_SHADER_OUTPUT_DIR}/bbox.vert.spv"
  "${BBOX_SHADER_OUTPUT_DIR}/bbox.frag.spv")

add_library(bbox_core
  src/model.cpp
  src/replay.cpp
  src/sha256.cpp)
target_include_directories(bbox_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/picojson")
target_compile_features(bbox_core PUBLIC cxx_std_17)

add_executable(bbox_replay
  src/main.cpp
  src/evidence.cpp
  src/renderer.cpp)
target_link_libraries(bbox_replay PRIVATE bbox_core Vulkan::Vulkan glfw)
target_compile_definitions(bbox_replay PRIVATE
  BBOX_SHADER_DIR="${BBOX_SHADER_OUTPUT_DIR}")
add_dependencies(bbox_replay bbox_shaders)

if(MSVC)
  target_compile_options(bbox_core PRIVATE /W4)
  target_compile_options(bbox_replay PRIVATE /W4)
else()
  target_compile_options(bbox_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(bbox_replay PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(
  NAME replay_experiment
  COMMAND bbox_replay
          --data-dir "${CMAKE_CURRENT_SOURCE_DIR}/data"
          --output-dir "${CMAKE_CURRENT_BINARY_DIR}/test-artifacts"
          --render none)

