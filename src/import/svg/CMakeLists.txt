# svg_import — SVG bytes → Core layer tree via tgfx SVGDOM (Apple only).

add_files_by_extension(SVG_IMPORT_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(svg_import STATIC ${SVG_IMPORT_SOURCES})
set_target_properties(svg_import PROPERTIES OUTPUT_NAME motionstudio_svg_import)
add_library(motionstudio_svg_import ALIAS svg_import)
add_source_group(SVG_IMPORT_SOURCES ${CMAKE_CURRENT_LIST_DIR} "Sources")

target_include_directories(svg_import
                           PUBLIC
                           "${PROJECT_SOURCE_DIR}/include"
                           PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${TGFX_INCLUDE_DIR}")

target_link_libraries(svg_import PUBLIC core)
motionstudio_add_tgfx_prebuild(svg_import)
motionstudio_link_prebuilt_tgfx(svg_import)
if(APPLE)
    target_link_libraries(svg_import PUBLIC
                          "-framework Foundation"
                          "-framework Metal"
                          "-framework MetalKit"
                          "-framework QuartzCore"
                          "-framework CoreGraphics"
                          "-framework CoreText"
                          "-framework CoreVideo"
                          "-framework CoreMedia"
                          "-framework ImageIO"
                          iconv)
    if(NOT CMAKE_GENERATOR MATCHES "Xcode")
        target_link_libraries(svg_import PUBLIC
                              "-framework ApplicationServices"
                              "-framework Cocoa")
    endif()
endif()
target_compile_options(svg_import PRIVATE -Wall -Wextra)

if(TARGET tgfx_adapter_test)
    target_link_libraries(tgfx_adapter_test PRIVATE svg_import)
endif()

include(GoogleTest)
add_files_by_extension(SVG_IMPORT_TEST_SOURCES ".h;.cpp"
    ${PROJECT_SOURCE_DIR}/tests/import/svg
)
add_executable(svg_import_tests ${SVG_IMPORT_TEST_SOURCES})
add_source_group(SVG_IMPORT_TEST_SOURCES ${PROJECT_SOURCE_DIR}/tests/import/svg "Tests")
disable_xcode_target_signing(svg_import_tests)
target_include_directories(svg_import_tests PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${TGFX_INCLUDE_DIR}")
target_link_libraries(svg_import_tests PRIVATE svg_import GTest::gtest_main)
motionstudio_link_asan_defaults(svg_import_tests)
motionstudio_add_tgfx_prebuild(svg_import_tests)
motionstudio_link_prebuilt_tgfx(svg_import_tests)
gtest_discover_tests(svg_import_tests)
