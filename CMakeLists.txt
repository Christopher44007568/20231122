cmake_minimum_required(VERSION 3.20)
project(curved_trim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CLIPFIX_VENDOR "${CMAKE_SOURCE_DIR}/third_party")
set(CLIPFIX_VULKAN_LIB "${CLIPFIX_VENDOR}/vulkan/lib/libvulkan.so")
set(CLIPFIX_GLFW_LIB "${CLIPFIX_VENDOR}/glfw/lib/libglfw.so")
set(CLIPFIX_IMGUI_LIB "${CLIPFIX_VENDOR}/imgui/lib/libimgui.a")
set(CLIPFIX_STB_LIB "${CLIPFIX_VENDOR}/stb/lib/libstb.a")

add_library(clipfix_core
    src/core/sha256.cpp
    src/core/time_util.cpp
    src/core/obj_io.cpp
    src/core/clip_surface.cpp
    src/core/raw_material.cpp
    src/core/workspace.cpp
    src/core/workspace_runs.cpp
    src/core/acceptance.cpp
)
target_include_directories(clipfix_core PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CLIPFIX_VENDOR}"
)
target_compile_definitions(clipfix_core PUBLIC
    CLIPFIX_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(clipfix_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(curved_trim
    src/main.cpp
    src/cli.cpp
    src/app/vulkan_app.cpp
    src/app/scene_builder.cpp
    src/app/ui.cpp
)
target_include_directories(curved_trim PRIVATE
    "${CLIPFIX_VENDOR}/vulkan/include"
    "${CLIPFIX_VENDOR}/glfw/include"
    "${CLIPFIX_VENDOR}/imgui/include"
    "${CLIPFIX_VENDOR}/imgui/include/imgui"
    "${CLIPFIX_VENDOR}/imgui/include/imgui/backends"
    "${CLIPFIX_VENDOR}/imgui/include/backends"
    "${CLIPFIX_VENDOR}/stb/include"
)
target_link_libraries(curved_trim PRIVATE
    clipfix_core
    "${CLIPFIX_IMGUI_LIB}"
    "${CLIPFIX_STB_LIB}"
    "${CLIPFIX_GLFW_LIB}"
    "${CLIPFIX_VULKAN_LIB}"
    dl
    pthread
)
target_compile_options(curved_trim PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(curved_trim PROPERTIES
    BUILD_RPATH "\$ORIGIN"
    INSTALL_RPATH "\$ORIGIN"
)

add_custom_command(TARGET curved_trim POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CLIPFIX_GLFW_LIB}" "$<TARGET_FILE_DIR:curved_trim>/libglfw.so.3"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CLIPFIX_VULKAN_LIB}" "$<TARGET_FILE_DIR:curved_trim>/libvulkan.so.1"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    add_executable(clipfix_unit_tests tests/unit_tests.cpp)
    target_link_libraries(clipfix_unit_tests PRIVATE clipfix_core)
    add_test(NAME unit COMMAND clipfix_unit_tests)
    add_test(
        NAME acceptance_flow
        COMMAND bash "${CMAKE_SOURCE_DIR}/tests/acceptance_flow.sh" "$<TARGET_FILE:curved_trim>"
    )
    add_test(
        NAME gui_smoke
        COMMAND bash "${CMAKE_SOURCE_DIR}/tests/gui_smoke.sh" "$<TARGET_FILE:curved_trim>"
    )
    set_tests_properties(gui_smoke PROPERTIES SKIP_RETURN_CODE 77)
endif()
