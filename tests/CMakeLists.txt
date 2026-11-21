add_executable(gcdump_tests test_core.cpp)
target_link_libraries(gcdump_tests PRIVATE gcdump_core)
add_test(NAME core_tests COMMAND gcdump_tests)

if(TARGET gcdump_page)
    add_test(
        NAME page_screenshot_smoke
        COMMAND gcdump_page
                "${CMAKE_SOURCE_DIR}/samples/recovered_dump.gcdump"
                --screenshot
                "${CMAKE_BINARY_DIR}/page_smoke.ppm")
    set_tests_properties(page_screenshot_smoke PROPERTIES
        FAIL_REGULAR_EXPRESSION "fail|error")

    add_test(
        NAME page_vulkan_render_smoke
        COMMAND gcdump_page
                "${CMAKE_SOURCE_DIR}/samples/recovered_dump.gcdump"
                --frames 10)
endif()
