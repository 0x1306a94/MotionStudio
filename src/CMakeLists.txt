add_library(core STATIC
    core/Version.cpp
    common/EntityId.cpp
)
target_include_directories(core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_options(core PRIVATE -Wall -Wextra)
set_target_properties(core PROPERTIES OUTPUT_NAME motionstudio_core)
