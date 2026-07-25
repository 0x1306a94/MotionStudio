
add_files_by_extension(COMMON_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

add_files_by_extension(CORE_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/core
)

add_files_by_extension(MODEL_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/model
)

add_files_by_extension(RENDER_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/render
)

add_files_by_extension(ANIMATION_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/animation
)

add_files_by_extension(UNDO_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/undo
)

add_files_by_extension(SERIALIZATION_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/serialization
)

add_files_by_extension(PUBLIC_HEADERS ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/MotionStudio
)

list(APPEND CORE_SOURCES
    ${COMMON_SOURCES}
    ${CORE_SOURCES}
    ${MODEL_SOURCES}
    ${RENDER_SOURCES}
    ${ANIMATION_SOURCES}
    ${UNDO_SOURCES}
    ${SERIALIZATION_SOURCES}
)
add_library(core STATIC ${PUBLIC_HEADERS} ${CORE_SOURCES})
set_target_properties(core PROPERTIES OUTPUT_NAME motionstudio_core)

add_library(motionstudio_core ALIAS core)
add_source_group(PUBLIC_HEADERS ${CMAKE_CURRENT_LIST_DIR}/../include/MotionStudio "Public Headers")
add_source_group(CORE_SOURCES ${CMAKE_CURRENT_LIST_DIR} "Sources")

target_include_directories(core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_include_directories(core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# nlohmann/json 仅供 core 内部实现使用，不经公共头暴露。
target_link_libraries(core PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(core PRIVATE -Wall -Wextra)
