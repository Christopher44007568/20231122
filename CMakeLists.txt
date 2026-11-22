cmake_minimum_required(VERSION 3.16)
project(reflection_probe CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TP ${CMAKE_SOURCE_DIR}/third_party/prefix/usr)

add_executable(probe
  src/main.cpp
  src/vk.cpp
  src/scene.cpp
  src/trace.cpp
  src/png.cpp
  src/report.cpp
)

target_include_directories(probe PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/generated
  ${TP}/include
)

target_link_directories(probe PRIVATE ${TP}/lib/x86_64-linux-gnu)
target_link_libraries(probe PRIVATE
  ${TP}/lib/x86_64-linux-gnu/libglfw.so.3
  /usr/lib/x86_64-linux-gnu/libvulkan.so.1
  dl pthread
)

set(SPV_DIR ${CMAKE_SOURCE_DIR}/generated)
file(MAKE_DIRECTORY ${SPV_DIR})
set(GLSLANG ${CMAKE_SOURCE_DIR}/third_party/prefix/usr/bin/glslangValidator)
foreach(s scene_vert scene_frag floor_frag)
  set(src ${CMAKE_SOURCE_DIR}/shaders/${s}.glsl)
  if(s MATCHES "_vert$")
    set(stage vert)
  else()
    set(stage frag)
  endif()
  set(out ${SPV_DIR}/${s}.spv.h)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${GLSLANG} -V -S ${stage} --vn spv_${s} -o ${out} ${src}
    DEPENDS ${src}
    COMMENT "GLSL -> SPIR-V: ${s}")
  list(APPEND SPV_HEADERS ${out})
endforeach()
add_custom_target(shaders ALL DEPENDS ${SPV_HEADERS})
add_dependencies(probe shaders)
