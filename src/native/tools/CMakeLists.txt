add_executable(
    btd5loader_validate
    validate_package.cpp
    ../runtime/custom_tower_art.cpp
    ../runtime/custom_tower_content.cpp
    ../runtime/game_object.cpp
    ../runtime/lua_mod.cpp
    ../runtime/mod_manifest.cpp
    ../runtime/mod_package.cpp
    ../runtime/tower_registry.cpp
)
target_include_directories(btd5loader_validate PRIVATE ../include)
target_link_libraries(
    btd5loader_validate
    PRIVATE bcrypt btd5ml_lua miniz nlohmann_json::nlohmann_json
)
btd5ml_set_project_options(btd5loader_validate)
