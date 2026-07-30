# textlayout — boxed text wrap / align / font-size shrink (no tgfx dependency).

add_library(textlayout STATIC
    src/TextLayout.cpp
)

target_include_directories(textlayout
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(textlayout PUBLIC cxx_std_17)

enable_testing()
include(GoogleTest)

add_executable(textlayout_tests
    tests/TextLayoutTest.cpp
)
target_link_libraries(textlayout_tests PRIVATE textlayout GTest::gtest_main)
disable_xcode_target_signing(textlayout_tests)
gtest_discover_tests(textlayout_tests)
