cmake_minimum_required(VERSION 3.16)
project(voxel_slicer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

find_package(Python3 COMPONENTS Interpreter)
set(GENERATED_SPV_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_SPV_DIR})
if(Python3_Interpreter_FOUND)
  add_custom_command(
    OUTPUT ${GENERATED_SPV_DIR}/slice_vert_spv.h
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spvasm.py
            ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/slice.vert.spvasm
            ${GENERATED_SPV_DIR}/slice_vert_spv.h kSliceVertSpv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/slice.vert.spvasm
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spvasm.py
    VERBATIM)
  add_custom_command(
    OUTPUT ${GENERATED_SPV_DIR}/slice_frag_spv.h
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spvasm.py
            ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/slice.frag.spvasm
            ${GENERATED_SPV_DIR}/slice_frag_spv.h kSliceFragSpv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/slice.frag.spvasm
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/spvasm.py
    VERBATIM)
  add_custom_target(spv_headers ALL
    DEPENDS ${GENERATED_SPV_DIR}/slice_vert_spv.h
            ${GENERATED_SPV_DIR}/slice_frag_spv.h)
endif()

add_executable(voxel_slicer
  src/main.cpp
  src/evidence.cpp
  src/voxel_io.cpp
  src/vulkan_context.cpp
  src/slice_renderer.cpp
)

target_include_directories(voxel_slicer PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${GENERATED_SPV_DIR}
  ${GLFW_INCLUDE_DIRS}
)
target_link_libraries(voxel_slicer PRIVATE Vulkan::Vulkan ${GLFW_LIBRARIES})
target_link_directories(voxel_slicer PRIVATE ${GLFW_LIBRARY_DIRS})
# Vulkan structs rely on sType-only aggregate initialization with the rest
# value-initialized to zero; silence that specific noisy GCC warning while
# keeping the rest of -Wall/-Wextra/-Wpedantic active.
target_compile_options(voxel_slicer PRIVATE -Wall -Wextra -Wpedantic
                       -Wno-missing-field-initializers)

add_executable(make_fixtures tools/make_fixtures.cpp)
target_compile_options(make_fixtures PRIVATE -Wall -Wextra -Wpedantic)
