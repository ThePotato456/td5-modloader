include(FetchContent)

# All revisions are immutable and mirrored in third_party/dependencies.lock.json.
set(BTD5ML_CATCH2_REVISION "95d8a61b089317bec800c7cc4c64064cbcb3802d")
set(BTD5ML_JSON_REVISION "65ee68451d8eb2b5f3a30b410476ab83deb3289b")

function(btd5ml_enable_runtime_dependencies)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG ${BTD5ML_JSON_REVISION}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endfunction()

function(btd5ml_enable_test_dependencies)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
    set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG ${BTD5ML_CATCH2_REVISION}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    if(MSVC)
        target_compile_options(Catch2 PRIVATE /EHsc)
        target_compile_options(Catch2WithMain PRIVATE /EHsc)
    endif()
endfunction()
