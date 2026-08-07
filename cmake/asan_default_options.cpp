// Default ASAN runtime flags for instrumented test/benchmark executables.
//
// Prebuilt tgfx (Release, no ASAN) is statically linked into several tests. ASAN
// then reports libc++/glslang container-overflow false positives during vector
// reallocation. Env ASAN_OPTIONS still overrides these defaults.

extern "C" const char *__asan_default_options() {
    return "detect_container_overflow=0";
}
