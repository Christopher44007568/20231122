cmake_minimum_required(VERSION 3.20)

project(material_instance_hotupdate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MI_LOCAL_DEPS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/usr")
find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
  HINTS "${MI_LOCAL_DEPS}"
  PATH_SUFFIXES include)
find_library(VULKAN_LIBRARY NAMES vulkan vulkan.so.1
  HINTS "${MI_LOCAL_DEPS}"
  PATH_SUFFIXES lib lib/x86_64-linux-gnu)
if(NOT VULKAN_LIBRARY)
  find_file(VULKAN_LIBRARY NAMES libvulkan.so.1
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu
          ${MI_LOCAL_DEPS}/lib
          ${MI_LOCAL_DEPS}/lib/x86_64-linux-gnu
          ${CMAKE_PREFIX_PATH}/lib
          ${CMAKE_PREFIX_PATH}/lib/x86_64-linux-gnu)
endif()
find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
  HINTS "${MI_LOCAL_DEPS}"
  PATH_SUFFIXES include)
find_library(GLFW_LIBRARY NAMES glfw glfw3
  HINTS "${MI_LOCAL_DEPS}"
  PATH_SUFFIXES lib lib/x86_64-linux-gnu)

if(NOT VULKAN_INCLUDE_DIR OR NOT VULKAN_LIBRARY OR
   NOT GLFW_INCLUDE_DIR OR NOT GLFW_LIBRARY)
  message(FATAL_ERROR
    "Vulkan/GLFW development files were not found.\n"
    "Ubuntu: sudo apt install libvulkan-dev libglfw3-dev\n"
    "Then reconfigure with: cmake -S . -B build "
    "-DCMAKE_PREFIX_PATH=/path/to/sysroot")
endif()

add_library(mi_core STATIC
  src/sha256.cpp
  src/json.cpp
  src/state_machine.cpp
  src/scenarios.cpp
  src/experiment.cpp
  src/report.cpp
)
target_include_directories(mi_core PUBLIC include)

add_library(mi_vulkan STATIC
  src/vulkan_backend.cpp
)
target_include_directories(mi_vulkan
  PUBLIC include
  PRIVATE ${VULKAN_INCLUDE_DIR} ${GLFW_INCLUDE_DIR}
)
target_link_libraries(mi_vulkan PUBLIC mi_core ${VULKAN_LIBRARY} ${GLFW_LIBRARY})
target_compile_definitions(mi_vulkan PRIVATE GLFW_INCLUDE_NONE)

add_executable(material_hotupdate src/main.cpp)
target_link_libraries(material_hotupdate PRIVATE mi_vulkan)

add_executable(hotupdate_tests tests/test_main.cpp)
target_link_libraries(hotupdate_tests PRIVATE mi_core)

enable_testing()
add_test(NAME hotupdate_core COMMAND hotupdate_tests)
add_test(NAME hotupdate_selfcheck
  COMMAND material_hotupdate --backend fake --output
          ${CMAKE_BINARY_DIR}/selfcheck-artifacts)
set_tests_properties(hotupdate_selfcheck PROPERTIES
  ENVIRONMENT "MI_SKIP_VISIBLE_CHECK=1")

install(TARGETS material_hotupdate RUNTIME DESTINATION bin)
