add_library(core STATIC
    core/Version.cpp
    common/BezierPath.cpp
    common/Color.cpp
    common/EntityId.cpp
    common/Mat3.cpp
    common/Time.cpp
    common/Vec2.cpp
    animation/Animatable.cpp
    animation/Easing.cpp
    animation/Interpolator.cpp
    animation/PathResample.cpp
    model/Document.cpp
    model/EntityIndex.cpp
    model/ImageContent.cpp
    model/Layer.cpp
    model/LayerContent.cpp
    model/NullContent.cpp
    model/PrecompContent.cpp
    model/PropertyPath.cpp
    model/ShapeContent.cpp
    model/ShapeElement.cpp
    model/ShapeEllipse.cpp
    model/ShapeFill.cpp
    model/ShapeGroup.cpp
    model/ShapePath.cpp
    model/ShapeRect.cpp
    model/ShapeStroke.cpp
    model/ShapeTrimPath.cpp
    model/TextContent.cpp
    undo/AddKeyframeCommand.cpp
    undo/AddLayerCommand.cpp
    undo/CommandHelpers.cpp
    undo/CompositeCommand.cpp
    undo/MoveKeyframeCommand.cpp
    undo/MoveLayerCommand.cpp
    undo/RemoveKeyframeCommand.cpp
    undo/RemoveLayerCommand.cpp
    undo/SetEasingCommand.cpp
    undo/SetStaticValueCommand.cpp
    undo/UndoManager.cpp
    serialization/Dto.cpp
    serialization/SchemaMigrator.cpp
    serialization/Serializer.cpp
)
target_include_directories(core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_include_directories(core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# nlohmann/json 仅供 core 内部实现使用，不经公共头暴露。
target_link_libraries(core PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(core PRIVATE -Wall -Wextra)
set_target_properties(core PROPERTIES OUTPUT_NAME motionstudio_core)
