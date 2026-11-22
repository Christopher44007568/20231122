cmake_minimum_required(VERSION 3.20)

project(pixel_history_sampler VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)

add_executable(pixel_history
    src/main.cpp
    src/artifact.cpp
    src/clock_model.cpp
    src/ordered_json.cpp
    src/pipeline.cpp
    src/renderer.cpp
    src/sha256.cpp
)

target_include_directories(pixel_history PRIVATE src)
target_link_libraries(pixel_history PRIVATE Vulkan::Vulkan glfw)

if(MSVC)
    target_compile_options(pixel_history PRIVATE /W4 /permissive-)
else()
    target_compile_options(pixel_history PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME pixel_history_self_check
        COMMAND pixel_history --mode both --out ${CMAKE_BINARY_DIR}/self-check)
    add_test(NAME pixel_history_verify
        COMMAND pixel_history --mode verify --in ${CMAKE_BINARY_DIR}/self-check/run-paused.json)
    add_test(NAME pixel_history_verify_checkpoint
        COMMAND pixel_history --mode verify --in ${CMAKE_BINARY_DIR}/self-check/checkpoint.json)
    set_tests_properties(pixel_history_verify PROPERTIES DEPENDS pixel_history_self_check)
    set_tests_properties(pixel_history_verify_checkpoint PROPERTIES DEPENDS pixel_history_self_check)
endif()

install(TARGETS pixel_history RUNTIME DESTINATION bin)
install(FILES README.md DESTINATION share/pixel_history_sampler)
