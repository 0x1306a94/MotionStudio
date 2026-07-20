add_executable(scene_evaluator_benchmark SceneEvaluatorBenchmark.cpp)
target_link_libraries(scene_evaluator_benchmark PRIVATE core)

# Recorded only, never gating: run with `ctest -L benchmark`.
add_test(NAME SceneEvaluatorBenchmark COMMAND scene_evaluator_benchmark)
set_tests_properties(SceneEvaluatorBenchmark PROPERTIES LABELS "benchmark")
