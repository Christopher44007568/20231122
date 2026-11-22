cmake_minimum_required(VERSION 3.20)
project(bokeh_pipeline VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)

set(BOKEH_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bokeh.comp.spv")
set(BOKEH_SHADER_COPY "${CMAKE_CURRENT_BINARY_DIR}/shaders/bokeh.comp.spv")
set(BOKEH_PREVIEW_VERT "${CMAKE_CURRENT_SOURCE_DIR}/shaders/preview.vert.spv")
set(BOKEH_PREVIEW_FRAG "${CMAKE_CURRENT_SOURCE_DIR}/shaders/preview.frag.spv")
set(BOKEH_PREVIEW_VERT_COPY "${CMAKE_CURRENT_BINARY_DIR}/shaders/preview.vert.spv")
set(BOKEH_PREVIEW_FRAG_COPY "${CMAKE_CURRENT_BINARY_DIR}/shaders/preview.frag.spv")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
configure_file("${BOKEH_SHADER}" "${BOKEH_SHADER_COPY}" COPYONLY)
configure_file("${BOKEH_PREVIEW_VERT}" "${BOKEH_PREVIEW_VERT_COPY}" COPYONLY)
configure_file("${BOKEH_PREVIEW_FRAG}" "${BOKEH_PREVIEW_FRAG_COPY}" COPYONLY)

add_executable(bokeh-pipeline
    src/main.cpp
    src/model.cpp
    src/json.cpp
    src/sha256.cpp
    src/util.cpp
    src/clock.cpp
    src/scene.cpp
    src/renderer.cpp
    src/pipeline.cpp
    src/artifact.cpp
    src/preview.cpp
)

target_include_directories(bokeh-pipeline PRIVATE src)
target_compile_options(bokeh-pipeline PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(bokeh-pipeline PRIVATE Vulkan::Vulkan PkgConfig::GLFW ${CMAKE_DL_LIBS})
target_compile_definitions(bokeh-pipeline PRIVATE
    BOKEH_SHADER_PATH="${BOKEH_SHADER_COPY}"
    BOKEH_PREVIEW_VERT_PATH="${BOKEH_PREVIEW_VERT_COPY}"
    BOKEH_PREVIEW_FRAG_PATH="${BOKEH_PREVIEW_FRAG_COPY}"
    BOKEH_VERSION="${PROJECT_VERSION}"
)

enable_testing()
add_test(NAME end_to_end
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_end_to_end.sh" "$<TARGET_FILE:bokeh-pipeline>"
)

