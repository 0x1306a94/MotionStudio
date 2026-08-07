add_executable(scene_evaluator_benchmark SceneEvaluatorBenchmark.cpp)
target_link_libraries(scene_evaluator_benchmark PRIVATE core)
motionstudio_link_asan_defaults(scene_evaluator_benchmark)

# Recorded only, never gating: run with `ctest -L benchmark`.
add_test(NAME SceneEvaluatorBenchmark COMMAND scene_evaluator_benchmark)
set_tests_properties(SceneEvaluatorBenchmark PROPERTIES LABELS "benchmark")

disable_xcode_target_signing(scene_evaluator_benchmark)
