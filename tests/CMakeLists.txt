include(GoogleTest)

add_executable(core_tests
    core/VersionTest.cpp
    common/BezierPathTest.cpp
    common/EntityIdTest.cpp
    common/ExpectedTest.cpp
    common/MathTest.cpp
    common/TimeTest.cpp
    animation/AnimatableTest.cpp
    animation/EasingCssTest.cpp
    animation/EasingTest.cpp
    animation/InterpolatorTest.cpp
    animation/PathResampleTest.cpp
    model/DocumentTest.cpp
    model/LayerTest.cpp
    model/PropertyPathTest.cpp
    render/CommandBuilderTest.cpp
    render/PrecompTest.cpp
    render/SceneEvaluatorTest.cpp
    undo/CommandsTest.cpp
    undo/UndoManagerTest.cpp
    serialization/RandomOpsTest.cpp
    serialization/SerializerTest.cpp
)
# nlohmann/json 仅测试用来校验 JSON 产物结构，非 core 公共依赖。
target_link_libraries(core_tests PRIVATE core GTest::gtest_main nlohmann_json::nlohmann_json)
gtest_discover_tests(core_tests)
