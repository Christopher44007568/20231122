cmake_minimum_required(VERSION 3.16)

project(lshc_review LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

set(DEPS_PREFIX "$ENV{HOME}/deps" CACHE PATH "Fallback prefix for Vulkan/GLFW headers")

find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
  HINTS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan/include"
    "${DEPS_PREFIX}/include"
)
find_file(VULKAN_LIBRARY
  NAMES libvulkan.so.1 libvulkan.so
  HINTS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib
)
find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
  HINTS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include"
    "${DEPS_PREFIX}/include"
)
set(GLFW_LIBRARY "")
set(VENDORED_GLFW "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/lib/libglfw.so.3")
if(EXISTS "${VENDORED_GLFW}")
  set(GLFW_LIBRARY "${VENDORED_GLFW}")
else()
  find_library(GLFW_LIBRARY
    NAMES glfw glfw3
    HINTS
      "${DEPS_PREFIX}/lib"
  )
endif()
find_program(GLSLANG_VALIDATOR
  NAMES glslangValidator glslang
  HINTS "${DEPS_PREFIX}/bin" "$ENV{HOME}/local/usr/bin" "$ENV{VULKAN_SDK}/bin"
)

if(NOT VULKAN_INCLUDE_DIR OR NOT VULKAN_LIBRARY)
  message(FATAL_ERROR "Vulkan headers/loader not found")
endif()
if(NOT GLFW_INCLUDE_DIR)
  message(FATAL_ERROR "GLFW headers not found")
endif()

if(NOT GLFW_LIBRARY)
  message(FATAL_ERROR "GLFW library not found")
endif()

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")

set(SHADER_FILES shadow.vert shadow.frag)
set(SHADER_SPVS "")
foreach(shader IN LISTS SHADER_FILES)
  set(spv "${SHADER_BIN_DIR}/${shader}.spv")
  if(GLSLANG_VALIDATOR)
    add_custom_command(
      OUTPUT "${spv}"
      COMMAND "${GLSLANG_VALIDATOR}" -V --target-env vulkan1.1
              -o "${spv}" "${SHADER_DIR}/${shader}"
      DEPENDS "${SHADER_DIR}/${shader}"
      COMMENT "Compiling ${shader}"
      VERBATIM
    )
  else()
    add_custom_command(
      OUTPUT "${spv}"
      COMMAND "${CMAKE_COMMAND}" -E copy
              "${SHADER_DIR}/spv/${shader}.spv" "${spv}"
      DEPENDS "${SHADER_DIR}/spv/${shader}.spv"
      COMMENT "Using precompiled ${shader}"
      VERBATIM
    )
  endif()
  list(APPEND SHADER_SPVS "${spv}")
endforeach()
add_custom_target(lshc_shaders ALL DEPENDS ${SHADER_SPVS})

add_executable(lshc_review
  src/main.cpp
  src/cache_format.cpp
  src/demo.cpp
  src/hash.cpp
  src/image.cpp
  src/platform.cpp
  src/report.cpp
  src/vulkan_review.cpp
)
add_dependencies(lshc_review lshc_shaders)

target_include_directories(lshc_review PRIVATE
  src
  "${VULKAN_INCLUDE_DIR}"
  "${GLFW_INCLUDE_DIR}"
)
target_compile_definitions(lshc_review PRIVATE
  SHADER_DIR="${SHADER_BIN_DIR}"
  GLFW_INCLUDE_NONE
  LSHC_VERSION="1.0.0"
)
target_compile_options(lshc_review PRIVATE
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow
)
target_link_libraries(lshc_review PRIVATE
  "${VULKAN_LIBRARY}"
  "${GLFW_LIBRARY}"
  dl
  pthread
)
set_target_properties(lshc_review PROPERTIES
  BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/lib"
)

enable_testing()
add_test(NAME lshc_self_test
  COMMAND lshc_review --self-test --out "${CMAKE_CURRENT_BINARY_DIR}/self-test"
)
set_tests_properties(lshc_self_test PROPERTIES
  ENVIRONMENT "VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json"
)

