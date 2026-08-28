add_library(btd5loader_bootstrap SHARED bootstrap.cpp wininet_proxy.def)
target_include_directories(btd5loader_bootstrap PUBLIC ../include)
set_target_properties(
    btd5loader_bootstrap
    PROPERTIES
        OUTPUT_NAME wininet
        PREFIX ""
)
btd5ml_set_project_options(btd5loader_bootstrap)
