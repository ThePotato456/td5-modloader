add_executable(
    btd5loader_native_tests
    foundation_tests.cpp
    ../../src/native/runtime/compatibility.cpp
    ../../src/native/runtime/hook_transaction.cpp
    ../../src/native/runtime/pattern.cpp
    ../../src/native/runtime/symbol_resolver.cpp
    ../../src/native/runtime/runtime_state.cpp
)
target_link_libraries(
    btd5loader_native_tests
    PRIVATE
        Catch2::Catch2WithMain
        bcrypt
        nlohmann_json::nlohmann_json
)
target_include_directories(btd5loader_native_tests PRIVATE ../../src/native/include)
target_compile_definitions(
    btd5loader_native_tests
    PRIVATE BTD5ML_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
btd5ml_set_project_options(btd5loader_native_tests)

add_test(NAME btd5loader_native_tests COMMAND btd5loader_native_tests)

add_executable(
    btd5loader_symbol_inspector
    symbol_inspector.cpp
    ../../src/native/runtime/compatibility.cpp
    ../../src/native/runtime/pattern.cpp
    ../../src/native/runtime/symbol_resolver.cpp
)
target_link_libraries(
    btd5loader_symbol_inspector
    PRIVATE bcrypt nlohmann_json::nlohmann_json
)
btd5ml_set_project_options(btd5loader_symbol_inspector)

add_executable(btd5loader_wininet_fixture wininet_fixture.cpp)
target_link_libraries(btd5loader_wininet_fixture PRIVATE wininet)
btd5ml_set_project_options(btd5loader_wininet_fixture)

add_test(
    NAME btd5loader_proxy_fixture
    COMMAND
        ${CMAKE_COMMAND}
        -DFIXTURE=$<TARGET_FILE:btd5loader_wininet_fixture>
        -DPROXY=$<TARGET_FILE:btd5loader_bootstrap>
        -DRUNTIME=$<TARGET_FILE:btd5loader_runtime>
        -DSTAGE=${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/proxy-fixture
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_proxy_fixture.cmake
)
