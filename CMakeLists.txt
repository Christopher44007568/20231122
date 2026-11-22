cmake_minimum_required(VERSION 3.16)
project(texture_page_debugger LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW3 REQUIRED IMPORTED_TARGET glfw3)

add_library(paging_core
    src/PagingModel.cpp
    src/Scenarios.cpp
)
target_include_directories(paging_core PUBLIC src)
target_compile_options(paging_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(texture_page_debugger
    src/main.cpp
    src/VulkanApp.cpp
    src/DebugUI.cpp
)
target_link_libraries(texture_page_debugger PRIVATE
    paging_core
    Vulkan::Vulkan
    PkgConfig::GLFW3
)
target_compile_definitions(texture_page_debugger PRIVATE
    TPD_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders"
)
target_compile_options(texture_page_debugger PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(paging_model_tests tests/paging_model_tests.cpp)
target_link_libraries(paging_model_tests PRIVATE paging_core)
target_compile_options(paging_model_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME paging_model_tests COMMAND paging_model_tests)
