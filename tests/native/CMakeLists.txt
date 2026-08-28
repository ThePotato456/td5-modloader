add_executable(
    btd5loader_native_tests
    foundation_tests.cpp
    ../../src/native/runtime/runtime_state.cpp
)
target_link_libraries(
    btd5loader_native_tests
    PRIVATE
        Catch2::Catch2WithMain
)
target_include_directories(btd5loader_native_tests PRIVATE ../../src/native/include)
btd5ml_set_project_options(btd5loader_native_tests)

add_test(NAME btd5loader_native_tests COMMAND btd5loader_native_tests)

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
