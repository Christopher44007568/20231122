find_package(Vulkan QUIET)

# Support a rootless, project-local toolchain (apt download + dpkg-deb -x).
set(_local_prefix "${CMAKE_SOURCE_DIR}/third_party/local/usr")
if(NOT Vulkan_FOUND AND EXISTS "${_local_prefix}/include/vulkan/vulkan.h")
    add_library(gcdump_vulkan SHARED IMPORTED)
    set_target_properties(gcdump_vulkan PROPERTIES
        IMPORTED_LOCATION "${_local_prefix}/lib/x86_64-linux-gnu/libvulkan.so.1"
        INTERFACE_INCLUDE_DIRECTORIES "${_local_prefix}/include")
    set(Vulkan_LIBRARY gcdump_vulkan)
endif()

find_library(GLFW_LIBRARY NAMES glfw glfw3
    HINTS "${_local_prefix}/lib/x86_64-linux-gnu")
find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
    HINTS "${_local_prefix}/include")

if(NOT Vulkan_FOUND AND NOT TARGET gcdump_vulkan)
    message(FATAL_ERROR "Vulkan headers not found")
endif()
if(NOT GLFW_LIBRARY OR NOT GLFW_INCLUDE_DIR)
    message(FATAL_ERROR "GLFW not found")
endif()

add_executable(gcdump_page
    main.cpp
    inspection_page.cpp
    page_surface.cpp
    vulkan_renderer.cpp
)
target_include_directories(gcdump_page PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${GLFW_INCLUDE_DIR})
target_link_libraries(gcdump_page PRIVATE
    gcdump_core ${Vulkan_LIBRARY} ${GLFW_LIBRARY})
set_target_properties(gcdump_page PROPERTIES
    BUILD_RPATH "${_local_prefix}/lib/x86_64-linux-gnu")
