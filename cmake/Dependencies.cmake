include(FetchContent)

# All revisions are immutable and mirrored in third_party/dependencies.lock.json.
set(BTD5ML_CATCH2_REVISION "95d8a61b089317bec800c7cc4c64064cbcb3802d")
set(BTD5ML_JSON_REVISION "65ee68451d8eb2b5f3a30b410476ab83deb3289b")

function(btd5ml_enable_runtime_dependencies)
    FetchContent_Declare(
        lua
        URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
        URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(lua)

    add_library(
        btd5ml_lua
        STATIC
            ${lua_SOURCE_DIR}/src/lapi.c
            ${lua_SOURCE_DIR}/src/lauxlib.c
            ${lua_SOURCE_DIR}/src/lbaselib.c
            ${lua_SOURCE_DIR}/src/lcode.c
            ${lua_SOURCE_DIR}/src/lcorolib.c
            ${lua_SOURCE_DIR}/src/lctype.c
            ${lua_SOURCE_DIR}/src/ldblib.c
            ${lua_SOURCE_DIR}/src/ldebug.c
            ${lua_SOURCE_DIR}/src/ldo.c
            ${lua_SOURCE_DIR}/src/ldump.c
            ${lua_SOURCE_DIR}/src/lfunc.c
            ${lua_SOURCE_DIR}/src/lgc.c
            ${lua_SOURCE_DIR}/src/linit.c
            ${lua_SOURCE_DIR}/src/liolib.c
            ${lua_SOURCE_DIR}/src/llex.c
            ${lua_SOURCE_DIR}/src/lmathlib.c
            ${lua_SOURCE_DIR}/src/lmem.c
            ${lua_SOURCE_DIR}/src/loadlib.c
            ${lua_SOURCE_DIR}/src/lobject.c
            ${lua_SOURCE_DIR}/src/lopcodes.c
            ${lua_SOURCE_DIR}/src/loslib.c
            ${lua_SOURCE_DIR}/src/lparser.c
            ${lua_SOURCE_DIR}/src/lstate.c
            ${lua_SOURCE_DIR}/src/lstring.c
            ${lua_SOURCE_DIR}/src/lstrlib.c
            ${lua_SOURCE_DIR}/src/ltable.c
            ${lua_SOURCE_DIR}/src/ltablib.c
            ${lua_SOURCE_DIR}/src/ltm.c
            ${lua_SOURCE_DIR}/src/lundump.c
            ${lua_SOURCE_DIR}/src/lutf8lib.c
            ${lua_SOURCE_DIR}/src/lvm.c
            ${lua_SOURCE_DIR}/src/lzio.c
    )
    target_include_directories(btd5ml_lua PUBLIC ${lua_SOURCE_DIR}/src)
    target_compile_definitions(btd5ml_lua PRIVATE LUAI_MAXCCALLS=100)
    set_target_properties(btd5ml_lua PROPERTIES POSITION_INDEPENDENT_CODE ON)

    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        miniz
        GIT_REPOSITORY https://github.com/richgel999/miniz.git
        GIT_TAG 77d0dce8627735138c51770d1799a1ef48f2117d
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(miniz)

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
