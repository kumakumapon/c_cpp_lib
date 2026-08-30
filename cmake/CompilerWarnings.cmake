# 共通のコンパイラ警告設定をターゲットに付与するヘルパー関数

function(target_set_warnings TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            /W4
            /utf-8
            /permissive-
        )
    else()
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
endfunction()
