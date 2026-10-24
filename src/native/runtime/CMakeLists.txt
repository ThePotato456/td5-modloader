add_library(
    btd5loader_runtime
    SHARED
        logger.cpp
        bloon_action_hook.cpp
        cash_capture.cpp
        custom_tower_assets.cpp
        custom_tower_content.cpp
        custom_tower_art.cpp
        custom_tower_hooks.cpp
        custom_tower_saves.cpp
        lives_hook.cpp
        lives_write_hook.cpp
        match_hook.cpp
        round_hook.cpp
        native_event_hook.cpp
        active_profile.cpp
        launch_ticket.cpp
        compatibility.cpp
        frame_hook.cpp
        exit_hook.cpp
        game_object.cpp
        hook_transaction.cpp
        lua_mod.cpp
        mod_manifest.cpp
        mod_package.cpp
        pattern.cpp
        symbol_resolver.cpp
        tower_placement_hook.cpp
        tower_sale_hook.cpp
        object_lifetime_hook.cpp
        teardown_ledger.cpp
        tower_purchase_hook.cpp
        tower_registry.cpp
        tower_progress.cpp
        tower_upgrade_hook.cpp
        type_watch.cpp
        runtime.cpp
        runtime.def
        runtime_state.cpp
)
target_include_directories(btd5loader_runtime PUBLIC ../include)
target_link_libraries(
    btd5loader_runtime
    PRIVATE bcrypt btd5ml_lua minhook miniz nlohmann_json::nlohmann_json
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
