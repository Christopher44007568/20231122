cmake_minimum_required(VERSION 3.16)

project(skincheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SKINCHECK_BUILD_GUI "Build the Vulkan/GLFW status viewer" ON)
option(SKINCHECK_BUILD_TESTS "Build the self-contained test runner" ON)

add_library(skincheck_core
    src/json.cpp
    src/sha256.cpp
    src/core.cpp
    src/persist.cpp
)
target_include_directories(skincheck_core PUBLIC include)
target_compile_options(skincheck_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(skincheck src/main.cpp)
target_link_libraries(skincheck PRIVATE skincheck_core)

if(SKINCHECK_BUILD_GUI)
    find_package(Vulkan QUIET)
    find_package(glfw3 3.3 QUIET)
    if(NOT glfw3_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(GLFW3 QUIET IMPORTED_TARGET glfw3)
        endif()
    endif()

    if(Vulkan_FOUND AND (glfw3_FOUND OR GLFW3_FOUND))
        target_sources(skincheck PRIVATE src/gui.cpp)
        target_compile_definitions(skincheck PRIVATE SKINCHECK_HAVE_GUI=1)
        target_link_libraries(skincheck PRIVATE Vulkan::Vulkan)
        if(TARGET glfw)
            target_link_libraries(skincheck PRIVATE glfw)
        else()
            target_link_libraries(skincheck PRIVATE PkgConfig::GLFW3)
        endif()
        message(STATUS "skincheck: Vulkan/GLFW viewer enabled")
    else()
        message(STATUS "skincheck: Vulkan or GLFW missing; CLI-only build")
    endif()
endif()

if(SKINCHECK_BUILD_TESTS)
    enable_testing()
    add_executable(skincheck_tests tests/test_main.cpp)
    target_link_libraries(skincheck_tests PRIVATE skincheck_core)
    add_test(NAME skincheck_tests COMMAND skincheck_tests)
endif()

