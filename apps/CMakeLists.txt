# apps/ 配下のアプリケーションディレクトリを自動で読み込みます。
# 新しいアプリを追加する際は、apps/<アプリ名>/CMakeLists.txt を作成するだけで自動認識されます。
#
# 各デモは引数なし・入力なしで実行すると、自分の結果を検査して 0 / 非 0 で終了します。
# BUILD_TESTING が有効なら、全デモを CTest のスモークテスト (demo_<アプリ名>) として登録し、
# サンプルコードが壊れたまま放置されないようにします。

file(GLOB SUBDIRS CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
foreach(SUBDIR ${SUBDIRS})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}/CMakeLists.txt)
        message(STATUS "[apps] Adding application: ${SUBDIR}")
        add_subdirectory(${SUBDIR})
        if(BUILD_TESTING AND TARGET ${SUBDIR})
            add_test(NAME demo_${SUBDIR} COMMAND ${SUBDIR})
            set_tests_properties(demo_${SUBDIR} PROPERTIES TIMEOUT 60 LABELS demo)
        endif()
    endif()
endforeach()
