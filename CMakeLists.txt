cmake_minimum_required(VERSION 3.20)
project(thinline_provenance LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(AA_DEPS "${CMAKE_SOURCE_DIR}/.deps/sysroot")
set(AA_LIBDIR "${AA_DEPS}/usr/lib/x86_64-linux-gnu")
set(AA_IMGUI "${CMAKE_SOURCE_DIR}/.deps/imgui")

add_executable(thinline_provenance
    src/main.cpp
    src/ui.cpp
    ${AA_IMGUI}/imgui.cpp
    ${AA_IMGUI}/imgui_draw.cpp
    ${AA_IMGUI}/imgui_tables.cpp
    ${AA_IMGUI}/imgui_widgets.cpp
    ${AA_IMGUI}/backends/imgui_impl_glfw.cpp
    ${AA_IMGUI}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(thinline_provenance PRIVATE
    src
    ${AA_IMGUI}
    ${AA_IMGUI}/backends
    ${AA_DEPS}/usr/include
)

target_link_libraries(thinline_provenance PRIVATE
    ${AA_LIBDIR}/libglfw.so
    ${AA_LIBDIR}/libvulkan.so
    dl
    pthread
)

target_compile_definitions(thinline_provenance PRIVATE
    AA_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets"
    AA_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
    GLFW_INCLUDE_NONE
)

set_target_properties(thinline_provenance PROPERTIES
    BUILD_RPATH "${AA_LIBDIR}"
)

set(AA_GLSLANG "${AA_DEPS}/usr/bin/glslangValidator")
set(AA_VERT_SPV "${CMAKE_BINARY_DIR}/shaders/ui.vert.spv")
set(AA_FRAG_SPV "${CMAKE_BINARY_DIR}/shaders/ui.frag.spv")

add_custom_command(
    OUTPUT ${AA_VERT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${AA_GLSLANG} -V "${CMAKE_SOURCE_DIR}/shaders/ui.vert" -o ${AA_VERT_SPV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/ui.vert"
)

add_custom_command(
    OUTPUT ${AA_FRAG_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${AA_GLSLANG} -V "${CMAKE_SOURCE_DIR}/shaders/ui.frag" -o ${AA_FRAG_SPV}
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/ui.frag"
)

add_custom_target(ui_shaders DEPENDS ${AA_VERT_SPV} ${AA_FRAG_SPV})
add_dependencies(thinline_provenance ui_shaders)
