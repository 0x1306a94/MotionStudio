include(GoogleTest)

add_executable(core_tests
    core/VersionTest.cpp
    common/BezierPathTest.cpp
    common/EntityIdTest.cpp
    common/MathTest.cpp
    common/TimeTest.cpp
    animation/AnimatableTest.cpp
    animation/EasingTest.cpp
    animation/InterpolatorTest.cpp
    model/DocumentTest.cpp
    model/LayerTest.cpp
    model/PropertyPathTest.cpp
    undo/CommandsTest.cpp
    undo/UndoManagerTest.cpp
)
target_link_libraries(core_tests PRIVATE core GTest::gtest_main)
gtest_discover_tests(core_tests)
