# apps/ 配下のアプリケーションディレクトリを自動で読み込みます。
# 新しいアプリを追加する際は、apps/<アプリ名>/CMakeLists.txt を作成するだけで自動認識されます。

file(GLOB SUBDIRS CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
foreach(SUBDIR ${SUBDIRS})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}/CMakeLists.txt)
        message(STATUS "[apps] Adding application: ${SUBDIR}")
        add_subdirectory(${SUBDIR})
    endif()
endforeach()
