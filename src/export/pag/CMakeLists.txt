# pag_export — Document → .pag via pag_codec (Apple only; added from root if(APPLE)).

add_files_by_extension(PAG_EXPORT_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}
)
list(FILTER PAG_EXPORT_SOURCES EXCLUDE REGEX "/tests/")

add_library(pag_export STATIC ${PAG_EXPORT_SOURCES})
set_target_properties(pag_export PROPERTIES OUTPUT_NAME motionstudio_pag_export)
add_source_group(PAG_EXPORT_SOURCES ${CMAKE_CURRENT_LIST_DIR} "Sources")

target_include_directories(pag_export
                           PUBLIC
                           "${PROJECT_SOURCE_DIR}/include"
                           PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${LIBPAG_DIR}/src"
                           "${TGFX_INCLUDE_DIR}")

target_link_libraries(pag_export PUBLIC core PRIVATE pag_codec)
# Image → WebP re-encode uses tgfx ImageCodec (same prebuilt as pag_codec).
motionstudio_add_tgfx_prebuild(pag_export)
motionstudio_link_prebuilt_tgfx(pag_export)
target_compile_options(pag_export PRIVATE -Wall -Wextra)

include(GoogleTest)
add_files_by_extension(PAG_EXPORT_TEST_SOURCES ".h;.cpp"
    ${PROJECT_SOURCE_DIR}/tests/export/pag
)
add_executable(pag_export_tests ${PAG_EXPORT_TEST_SOURCES})
add_source_group(PAG_EXPORT_TEST_SOURCES ${PROJECT_SOURCE_DIR}/tests/export/pag "Tests")
disable_xcode_target_signing(pag_export_tests)
target_include_directories(pag_export_tests PRIVATE "${LIBPAG_DIR}/include")
target_link_libraries(pag_export_tests PRIVATE pag_export GTest::gtest_main)
motionstudio_link_asan_defaults(pag_export_tests)
motionstudio_add_tgfx_prebuild(pag_export_tests)
motionstudio_link_prebuilt_tgfx(pag_export_tests)
gtest_discover_tests(pag_export_tests)
