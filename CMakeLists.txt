cmake_minimum_required(VERSION 3.16)
project(gs_output_checker CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TP ${CMAKE_SOURCE_DIR}/third_party)

find_program(GLSLC glslc PATHS ${TP}/sysroot/usr/bin NO_DEFAULT_PATH)
if(NOT GLSLC)
    find_program(GLSLC glslc)
endif()
set(GLSLC_ENV ${CMAKE_COMMAND} -E env
    "LD_LIBRARY_PATH=${TP}/sysroot/usr/lib/x86_64-linux-gnu")

set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SPV_DIR ${CMAKE_BINARY_DIR}/spv)
file(MAKE_DIRECTORY ${SPV_DIR})

set(SHADERS "")
foreach(topo 0 1 2)
    set(out ${SPV_DIR}/capture_topo${topo}.geom.spv)
    add_custom_command(
        OUTPUT ${out}
        COMMAND ${GLSLC_ENV} ${GLSLC} -DTOPO=${topo} -fshader-stage=geom
                ${SHADER_DIR}/capture.geom -o ${out}
        DEPENDS ${SHADER_DIR}/capture.geom
        VERBATIM)
    list(APPEND SHADERS ${out})
endforeach()

set(VERT_SPV ${SPV_DIR}/passthrough.vert.spv)
add_custom_command(
    OUTPUT ${VERT_SPV}
    COMMAND ${GLSLC_ENV} ${GLSLC} -fshader-stage=vert ${SHADER_DIR}/passthrough.vert -o ${VERT_SPV}
    DEPENDS ${SHADER_DIR}/passthrough.vert
    VERBATIM)
list(APPEND SHADERS ${VERT_SPV})

add_custom_target(shaders ALL DEPENDS ${SHADERS})

add_executable(gsochecker src/main.cpp)
add_dependencies(gsochecker shaders)
target_include_directories(gsochecker PRIVATE
    ${TP}/sysroot/usr/include
    ${TP}/glfw-3.4/include)
target_compile_definitions(gsochecker PRIVATE SPV_DIR="${SPV_DIR}")
target_link_directories(gsochecker PRIVATE ${TP}/glfw-build/src)
target_link_libraries(gsochecker PRIVATE glfw3 dl pthread
    /usr/lib/x86_64-linux-gnu/libvulkan.so.1)
