# libs/ 配下のライブラリディレクトリを自動で読み込みます。
# 新しいライブラリを追加する際は、libs/<ライブラリ名>/CMakeLists.txt を作成するだけで自動認識されます。

file(GLOB SUBDIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
foreach(SUBDIR ${SUBDIRS})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}/CMakeLists.txt)
        message(STATUS "[libs] Adding library: ${SUBDIR}")
        add_subdirectory(${SUBDIR})
    endif()
endforeach()
