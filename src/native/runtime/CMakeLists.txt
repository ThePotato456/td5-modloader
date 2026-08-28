add_library(
    btd5loader_runtime
    SHARED
        logger.cpp
        runtime.cpp
        runtime.def
        runtime_state.cpp
)
target_include_directories(btd5loader_runtime PUBLIC ../include)
set_target_properties(btd5loader_runtime PROPERTIES PREFIX "")
btd5ml_set_project_options(btd5loader_runtime)
