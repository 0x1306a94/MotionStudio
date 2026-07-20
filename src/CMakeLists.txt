add_library(core STATIC
    core/Version.cpp
    common/EntityId.cpp
    animation/Easing.cpp
    animation/Interpolator.cpp
    model/Document.cpp
    model/Layer.cpp
    model/PropertyPath.cpp
)
target_include_directories(core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_compile_options(core PRIVATE -Wall -Wextra)
set_target_properties(core PROPERTIES OUTPUT_NAME motionstudio_core)
