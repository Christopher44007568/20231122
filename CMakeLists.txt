cmake_minimum_required(VERSION 3.16)

project(fog_stability LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)

add_executable(fog_stability
    src/main.cpp
    src/config.cpp
    src/checkpoint.cpp
    src/evidence.cpp
    src/renderer.cpp
)

target_include_directories(fog_stability PRIVATE src)
target_link_libraries(fog_stability PRIVATE Vulkan::Vulkan glfw)
target_compile_definitions(fog_stability PRIVATE
    FOG_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(fog_stability PRIVATE /W4)
else()
    target_compile_options(fog_stability PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(SHADER_FILES
    shaders/fog.comp.spv
    shaders/display.vert.spv
    shaders/display.frag.spv
)

foreach(shader IN LISTS SHADER_FILES)
    add_custom_command(TARGET fog_stability POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
            "$<TARGET_FILE_DIR:fog_stability>/${shader}"
    )
endforeach()
