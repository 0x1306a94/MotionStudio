add_library(core STATIC
    core/Version.cpp
    common/EntityId.cpp
    animation/Easing.cpp
    animation/Interpolator.cpp
    model/Document.cpp
    model/Layer.cpp
    model/PropertyPath.cpp
    undo/Command.cpp
    undo/Commands.cpp
    undo/UndoManager.cpp
    serialization/SchemaMigrator.cpp
    serialization/Serializer.cpp
)
target_include_directories(core PUBLIC "${PROJECT_SOURCE_DIR}/include")
# nlohmann/json 仅供 core 内部实现使用，不经公共头暴露。
target_link_libraries(core PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(core PRIVATE -Wall -Wextra)
set_target_properties(core PROPERTIES OUTPUT_NAME motionstudio_core)
