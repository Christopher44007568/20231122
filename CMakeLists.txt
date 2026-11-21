cmake_minimum_required(VERSION 3.20)

project(vfcr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(VFCR_DEPS_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${VFCR_DEPS_ROOT}/usr")
    set(Vulkan_INCLUDE_DIR "${VFCR_DEPS_ROOT}/usr/include")
    if(EXISTS "${VFCR_DEPS_ROOT}/usr/lib/x86_64-linux-gnu/libvulkan.so.1")
        set(Vulkan_LIBRARY "${VFCR_DEPS_ROOT}/usr/lib/x86_64-linux-gnu/libvulkan.so.1")
    elseif(EXISTS "/usr/lib/x86_64-linux-gnu/libvulkan.so.1")
        set(Vulkan_LIBRARY "/usr/lib/x86_64-linux-gnu/libvulkan.so.1")
    endif()
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)

add_library(vfcr_core
    src/core/json.cpp
    src/core/hash.cpp
    src/core/io.cpp
    src/core/replay.cpp
    src/core/session.cpp
    src/core/export.cpp
)
target_include_directories(vfcr_core PUBLIC src)
target_compile_options(vfcr_core PRIVATE -Wall -Wextra -Wpedantic)

add_library(vfcr_renderer
    src/app/ui.cpp
    src/app/vulkan_renderer.cpp
)
target_include_directories(vfcr_renderer PUBLIC src)
target_link_libraries(vfcr_renderer PUBLIC vfcr_core Vulkan::Vulkan glfw)
target_compile_definitions(vfcr_renderer PRIVATE
    VFCR_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets"
    GLFW_INCLUDE_NONE
)
target_compile_options(vfcr_renderer PRIVATE -Wall -Wextra -Wpedantic)

add_executable(vfcr src/app/main.cpp)
target_link_libraries(vfcr PRIVATE vfcr_renderer)
target_compile_options(vfcr PRIVATE -Wall -Wextra -Wpedantic)

add_executable(vfcr-verify src/verify/main.cpp)
target_link_libraries(vfcr-verify PRIVATE vfcr_core)
target_compile_options(vfcr-verify PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(vfcr-tests tests/test_core.cpp)
target_link_libraries(vfcr-tests PRIVATE vfcr_core)
add_test(NAME vfcr-core-tests COMMAND vfcr-tests)

install(TARGETS vfcr vfcr-verify RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/vfcr/assets)
