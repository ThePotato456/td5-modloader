add_library(
    btd5loader_runtime
    SHARED
        logger.cpp
        active_profile.cpp
        compatibility.cpp
        hook_transaction.cpp
        lua_mod.cpp
        mod_manifest.cpp
        mod_package.cpp
        pattern.cpp
        symbol_resolver.cpp
        runtime.cpp
        runtime.def
        runtime_state.cpp
)
target_include_directories(btd5loader_runtime PUBLIC ../include)
target_link_libraries(
    btd5loader_runtime
    PRIVATE bcrypt btd5ml_lua miniz nlohmann_json::nlohmann_json
)
set_target_properties(btd5loader_runtime PROPERTIES PREFIX "")
add_custom_command(
    TARGET btd5loader_runtime
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/symbols
        $<TARGET_FILE_DIR:btd5loader_runtime>/symbols
)
btd5ml_set_project_options(btd5loader_runtime)
