cmake_minimum_required(VERSION 3.20)
project(surface_unfold_provenance LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(VULKAN_INCLUDE "${VENDOR}/vulkan/include")
set(VULKAN_LIB "${VENDOR}/vulkan/lib/libvulkan.so.1")
set(GLFW_INCLUDE "${VENDOR}/glfw/include")
set(GLFW_LIB "${VENDOR}/glfw/lib/libglfw.so.3.4")
set(IMGUI_INCLUDE "${VENDOR}/imgui/include")
set(IMGUI_LIB "${VENDOR}/imgui/lib/libimgui.a")
set(STB_INCLUDE "${VENDOR}/stb/include")
set(STB_LIB "${VENDOR}/stb/lib/libstb.a")

find_package(Threads REQUIRED)

add_library(surfprov_core
    src/surface_provenance.cpp
    src/sha256.cpp
)
target_include_directories(surfprov_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${VENDOR}"
)
target_compile_definitions(surfprov_core PUBLIC
    SURFPROV_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_options(surfprov_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(surface_unfold_fixtures tools/make_fixtures.cpp)
target_include_directories(surface_unfold_fixtures PRIVATE "${VENDOR}")

add_executable(surface_unfold
    src/main.cpp
    src/vulkan_app.cpp
    src/app_ui.cpp
)
target_include_directories(surface_unfold PRIVATE
    "${VULKAN_INCLUDE}"
    "${GLFW_INCLUDE}"
    "${IMGUI_INCLUDE}"
    "${IMGUI_INCLUDE}/imgui"
    "${IMGUI_INCLUDE}/imgui/backends"
    "${STB_INCLUDE}"
)
target_compile_definitions(surface_unfold PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(surface_unfold PRIVATE
    surfprov_core
    "${IMGUI_LIB}"
    "${STB_LIB}"
    "${GLFW_LIB}"
    "${VULKAN_LIB}"
    Threads::Threads
    dl
)
target_compile_options(surface_unfold PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(surface_unfold PROPERTIES
    BUILD_RPATH "\$ORIGIN;\$ORIGIN/../third_party/vulkan/lib;\$ORIGIN/../third_party/glfw/lib"
    INSTALL_RPATH "\$ORIGIN"
)
add_custom_command(TARGET surface_unfold POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${GLFW_LIB}" "$<TARGET_FILE_DIR:surface_unfold>/libglfw.so.3"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${VULKAN_LIB}" "$<TARGET_FILE_DIR:surface_unfold>/libvulkan.so.1"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    add_executable(surfprov_tests tests/provenance_tests.cpp)
    target_link_libraries(surfprov_tests PRIVATE surfprov_core)
    add_test(NAME provenance_core COMMAND surfprov_tests)
    add_test(
        NAME gui_smoke
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_smoke.sh"
            "$<TARGET_FILE:surface_unfold>"
            "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/screenshots/gui_smoke.png"
    )
    set_tests_properties(gui_smoke PROPERTIES SKIP_RETURN_CODE 77)
endif()
