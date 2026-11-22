cmake_minimum_required(VERSION 3.16)
project(grid_boundary_marker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 允许通过 -DLOCAL_DEPS_DIR 指向项目内解包的开发包（无 root 安装场景）
if(NOT LOCAL_DEPS_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/.localdeps/usr/include")
    set(LOCAL_DEPS_DIR "${CMAKE_SOURCE_DIR}/.localdeps/usr")
endif()
if(LOCAL_DEPS_DIR)
    list(PREPEND CMAKE_PREFIX_PATH "${LOCAL_DEPS_DIR}")
    include_directories(SYSTEM "${LOCAL_DEPS_DIR}/include")
    link_directories("${LOCAL_DEPS_DIR}/lib/x86_64-linux-gnu")
    # 系统仅有 loader 运行库(libvulkan.so.1)时，显式指向解包目录中的开发符号链接
    if(EXISTS "${LOCAL_DEPS_DIR}/lib/x86_64-linux-gnu/libvulkan.so")
        set(Vulkan_LIBRARY "${LOCAL_DEPS_DIR}/lib/x86_64-linux-gnu/libvulkan.so" CACHE FILEPATH "")
    endif()
endif()

find_package(Vulkan REQUIRED)

# GLFW：优先 config/模块查找，找不到时回退到本地解包目录
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h PATHS "${LOCAL_DEPS_DIR}/include" REQUIRED)
    find_library(GLFW_LIBRARY NAMES glfw PATHS "${LOCAL_DEPS_DIR}/lib/x86_64-linux-gnu" REQUIRED)
    add_library(glfw INTERFACE)
    target_include_directories(glfw INTERFACE "${GLFW_INCLUDE_DIR}")
    target_link_libraries(glfw INTERFACE "${GLFW_LIBRARY}")
endif()

# 着色器编译：优先 PATH，其次本地解包目录
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS "${LOCAL_DEPS_DIR}/bin" ENV GLSLANG_VALIDATOR)
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator 未找到，请安装 glslang-tools 或运行 scripts/setup_deps.sh")
endif()

set(SPV_OUTPUTS "")
file(GLOB SHADER_SOURCES "${CMAKE_SOURCE_DIR}/shaders/*.vert" "${CMAKE_SOURCE_DIR}/shaders/*.frag")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME "${SHADER}" NAME)
    set(SPV "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT "${SPV}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND "${GLSLANG_VALIDATOR}" -V "${SHADER}" -o "${SPV}"
        DEPENDS "${SHADER}"
        COMMENT "编译着色器 ${SHADER_NAME}"
        VERBATIM)
    list(APPEND SPV_OUTPUTS "${SPV}")
endforeach()
add_custom_target(shaders ALL DEPENDS ${SPV_OUTPUTS})

add_executable(grid_marker
    src/main.cpp
    src/config.cpp
    src/renderer.cpp
    src/evidence.cpp
    src/sha256.cpp
    src/json.cpp)
add_dependencies(grid_marker shaders)
target_include_directories(grid_marker PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(grid_marker Vulkan::Vulkan glfw dl pthread)
