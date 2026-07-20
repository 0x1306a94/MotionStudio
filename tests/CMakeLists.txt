include(GoogleTest)

add_executable(core_tests
    core/VersionTest.cpp
)
target_link_libraries(core_tests PRIVATE core GTest::gtest_main)
gtest_discover_tests(core_tests)
