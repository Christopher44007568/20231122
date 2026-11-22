cmake_minimum_required(VERSION 3.20)

project(gbuffer_inspector VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GBI_ENABLE_WARNINGS "Enable strict compiler warnings" ON)
option(GBI_BUILD_TESTS "Build smoke tests" ON)
option(GBI_ENABLE_E2E_TESTS "Run the Vulkan end-to-end test in CTest" OFF)

find_package(Threads REQUIRED)
find_package(Vulkan QUIET)

if(NOT Vulkan_FOUND)
  find_library(VULKAN_LOADER NAMES vulkan vulkan-1 libvulkan.so.1 REQUIRED)
  add_library(Vulkan::Vulkan SHARED IMPORTED)
  set_target_properties(Vulkan::Vulkan PROPERTIES
    IMPORTED_LOCATION "${VULKAN_LOADER}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/vulkan/include"
  )
endif()

find_package(glfw3 3.3 CONFIG QUIET)
if(NOT TARGET glfw)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(GLFW3 QUIET IMPORTED_TARGET glfw3)
  endif()
endif()
if(TARGET PkgConfig::GLFW3)
  add_library(glfw ALIAS PkgConfig::GLFW3)
endif()
if(NOT TARGET glfw)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  if(UNIX AND NOT APPLE)
    if(EXISTS "/usr/include/X11/Xlib.h")
      set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    else()
      set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
    endif()
    if(EXISTS "/usr/include/wayland-client.h")
      set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    else()
      set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    endif()
  endif()
  add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/glfw" EXCLUDE_FROM_ALL)
endif()

find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslang)
find_program(GLSLC_EXECUTABLE NAMES glslc)
if(NOT GLSLC_EXECUTABLE AND NOT GLSLANG_VALIDATOR)
  message(WARNING "No shader compiler found; using checked-in SPIR-V files")
endif()

set(GBI_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${GBI_SHADER_OUTPUT_DIR}")

set(GBI_SHADER_SOURCES
  gbuffer.vert
  gbuffer.frag
  fullscreen.vert
  deferred.frag
)
set(GBI_SPIRV_FILES)

foreach(shader IN LISTS GBI_SHADER_SOURCES)
  get_filename_component(shader_ext "${shader}" EXT)
  if(shader_ext STREQUAL ".vert")
    set(shader_stage vert)
  elseif(shader_ext STREQUAL ".frag")
    set(shader_stage frag)
  else()
    message(FATAL_ERROR "Unsupported shader stage: ${shader}")
  endif()
  set(output "${GBI_SHADER_OUTPUT_DIR}/${shader}.spv")
  if(GLSLC_EXECUTABLE)
    add_custom_command(
      OUTPUT "${output}"
      COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.1 -o "${output}" "${CMAKE_SOURCE_DIR}/shaders/${shader}"
      DEPENDS "${CMAKE_SOURCE_DIR}/shaders/${shader}"
      COMMENT "Compiling ${shader}"
      VERBATIM
    )
  elseif(GLSLANG_VALIDATOR)
    add_custom_command(
      OUTPUT "${output}"
      COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.1 -S "${shader_stage}" -o "${output}" "${CMAKE_SOURCE_DIR}/shaders/${shader}"
      DEPENDS "${CMAKE_SOURCE_DIR}/shaders/${shader}"
      COMMENT "Compiling ${shader}"
      VERBATIM
    )
  else()
    add_custom_command(
      OUTPUT "${output}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/shaders/prebuilt/${shader}.spv" "${output}"
      DEPENDS "${CMAKE_SOURCE_DIR}/shaders/prebuilt/${shader}.spv"
      COMMENT "Using checked-in SPIR-V for ${shader}"
      VERBATIM
    )
  endif()
  list(APPEND GBI_SPIRV_FILES "${output}")
endforeach()

add_custom_target(gbi_shaders ALL DEPENDS ${GBI_SPIRV_FILES})

add_executable(gbuffer-inspector
  src/main.cpp
  src/json.cpp
  src/platform.cpp
  src/evidence.cpp
  src/workspace.cpp
  src/scene.cpp
  src/renderer.cpp
  src/compare.cpp
  src/commands.cpp
  src/gui.cpp
)

add_dependencies(gbuffer-inspector gbi_shaders)

target_include_directories(gbuffer-inspector PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(gbuffer-inspector PRIVATE
  Vulkan::Vulkan
  glfw
  Threads::Threads
)

target_compile_definitions(gbuffer-inspector PRIVATE
  GBI_VERSION="${PROJECT_VERSION}"
  GBI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  GBI_SHADER_DIR="${GBI_SHADER_OUTPUT_DIR}"
)

if(GBI_ENABLE_WARNINGS)
  if(MSVC)
    target_compile_options(gbuffer-inspector PRIVATE /W4 /permissive-)
  else()
    target_compile_options(gbuffer-inspector PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow
      -Wno-unused-parameter
    )
  endif()
endif()

add_custom_command(TARGET gbuffer-inspector POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:gbuffer-inspector>/assets"
  COMMENT "Copying original materials"
)

install(TARGETS gbuffer-inspector RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/gbuffer-inspector/assets)
install(DIRECTORY "${GBI_SHADER_OUTPUT_DIR}/" DESTINATION share/gbuffer-inspector/shaders)

if(GBI_BUILD_TESTS)
  enable_testing()
  add_test(NAME gbi_help
    COMMAND gbuffer-inspector --help)
  add_test(NAME gbi_verify_original
    COMMAND "${CMAKE_SOURCE_DIR}/tests/verify_original.sh"
            "${CMAKE_SOURCE_DIR}")
  if(GBI_ENABLE_E2E_TESTS)
    add_test(NAME gbi_e2e
      COMMAND "${CMAKE_SOURCE_DIR}/tests/e2e.sh"
              "$<TARGET_FILE:gbuffer-inspector>")
    set_tests_properties(gbi_e2e PROPERTIES
      ENVIRONMENT "GBI_TEST_WORKSPACE_ROOT=${CMAKE_BINARY_DIR}")
  endif()
endif()
