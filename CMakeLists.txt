cmake_minimum_required(VERSION 3.20)
project(hdr_provenance LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(VENDOR "${CMAKE_SOURCE_DIR}/third_party")
set(VULKAN_INCLUDE "${VENDOR}/vulkan/include")
set(VULKAN_LIB "${VENDOR}/vulkan/lib/libvulkan.so.1")
set(GLFW_INCLUDE "${VENDOR}/glfw/include")
set(GLFW_LIB "${VENDOR}/glfw/lib/libglfw.so.3.4")
set(IMGUI_INCLUDE "${VENDOR}/imgui/include")
set(IMGUI_LIB "${VENDOR}/imgui/lib/libimgui.a")
set(STB_INCLUDE "${VENDOR}/stb/include")
set(STB_LIB "${VENDOR}/stb/lib/libstb.a")

find_package(Threads REQUIRED)

add_library(hdrprov_core
    src/hdr_provenance.cpp
    src/sha256.cpp
)
target_include_directories(hdrprov_core PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${VENDOR}"
)
target_compile_definitions(hdrprov_core PUBLIC
    HDRPROV_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(hdrprov_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(hdr_fixtures tools/make_fixtures.cpp)
target_link_libraries(hdr_fixtures PRIVATE hdrprov_core)

add_executable(hdr_provenance
    src/main.cpp
    src/vulkan_app.cpp
    src/app_ui.cpp
)
target_include_directories(hdr_provenance PRIVATE
    "${VULKAN_INCLUDE}"
    "${GLFW_INCLUDE}"
    "${IMGUI_INCLUDE}"
    "${IMGUI_INCLUDE}/imgui"
    "${IMGUI_INCLUDE}/imgui/backends"
    "${STB_INCLUDE}"
)
target_compile_definitions(hdr_provenance PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(hdr_provenance PRIVATE
    hdrprov_core
    "${IMGUI_LIB}"
    "${STB_LIB}"
    "${GLFW_LIB}"
    "${VULKAN_LIB}"
    Threads::Threads
    dl
)
target_compile_options(hdr_provenance PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(hdr_provenance PROPERTIES
    BUILD_RPATH "\$ORIGIN;\$ORIGIN/third_party/vulkan/lib;\$ORIGIN/third_party/glfw/lib"
    INSTALL_RPATH "\$ORIGIN"
)
add_custom_command(TARGET hdr_provenance POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${GLFW_LIB}" "$<TARGET_FILE_DIR:hdr_provenance>/libglfw.so.3"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${VULKAN_LIB}" "$<TARGET_FILE_DIR:hdr_provenance>/libvulkan.so.1"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    add_executable(hdrprov_tests tests/provenance_tests.cpp)
    target_link_libraries(hdrprov_tests PRIVATE hdrprov_core)
    add_test(NAME provenance_core COMMAND hdrprov_tests)
    add_test(
        NAME gui_smoke
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_smoke.sh"
            "$<TARGET_FILE:hdr_provenance>"
            "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/screenshots/gui_smoke.png"
    )
    set_tests_properties(gui_smoke PROPERTIES SKIP_RETURN_CODE 77)
endif()
