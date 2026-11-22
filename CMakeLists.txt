cmake_minimum_required(VERSION 3.16)

project(offscreen_consistency LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_file(ZLIB_LIBRARY NAMES libz.so.1 libz.so
    HINTS
        /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
        /lib/${CMAKE_LIBRARY_ARCHITECTURE}
        /usr/lib64
        /lib64
    PATHS /usr/lib /lib /usr/local/lib)
if(NOT ZLIB_LIBRARY)
    message(FATAL_ERROR "zlib runtime library not found")
endif()

add_library(replay_core
    src/common.cpp
    src/sha256.cpp
    src/png_writer.cpp
    src/record.cpp
    src/state.cpp
)
target_include_directories(replay_core PUBLIC src)
target_compile_options(replay_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(replay_core PUBLIC ${ZLIB_LIBRARY})

add_executable(offscreen_replay
    src/main.cpp
    src/experiment.cpp
    src/vulkan_renderer.cpp
    src/shaders_spv.cpp
)
target_link_libraries(offscreen_replay PRIVATE replay_core Vulkan::Vulkan glfw)
target_compile_options(offscreen_replay PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(state_tests tests/state_tests.cpp)
target_link_libraries(state_tests PRIVATE replay_core)
target_compile_options(state_tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(state_tests PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

add_test(NAME state_tests COMMAND state_tests)

