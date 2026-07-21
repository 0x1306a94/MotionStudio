add_library(bridge STATIC src/motionstudio_bridge.cpp)
target_include_directories(bridge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(bridge PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(bridge PUBLIC core)
target_compile_options(bridge PRIVATE -Wall -Wextra)
set_target_properties(bridge PROPERTIES OUTPUT_NAME motionstudio_bridge)

# Canvas (MTKView direct rendering) is Apple-only.
if (APPLE)
    target_sources(bridge PRIVATE src/motionstudio_bridge_canvas.mm)
    target_link_libraries(bridge PUBLIC motionstudio_tgfx_adapter)
endif()

include(GoogleTest)
add_executable(bridge_test BridgeTest.cpp)
target_link_libraries(bridge_test PRIVATE bridge GTest::gtest_main)
gtest_discover_tests(bridge_test)
