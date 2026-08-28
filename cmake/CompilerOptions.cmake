function(btd5ml_set_project_options target)
    if(MSVC)
        target_compile_options(
            ${target}
            PRIVATE
                /W4
                /WX
                /permissive-
                /EHsc
                /utf-8
                /Zc:__cplusplus
        )

        if(BTD5ML_ENABLE_ANALYSIS)
            target_compile_options(${target} PRIVATE /analyze)
        endif()
    endif()
endfunction()

