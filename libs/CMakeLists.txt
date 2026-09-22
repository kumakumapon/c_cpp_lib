# libs/ 配下のライブラリディレクトリを自動で読み込みます。
# 新しいライブラリを追加する際は、libs/<ライブラリ名>/CMakeLists.txt を作成するだけで自動認識されます。

file(GLOB SUBDIRS CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
foreach(SUBDIR ${SUBDIRS})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SUBDIR}/CMakeLists.txt)
        message(STATUS "[libs] Adding library: ${SUBDIR}")
        add_subdirectory(${SUBDIR})
    endif()
endforeach()

include(GNUInstallDirs)
foreach(SUBDIR ${SUBDIRS})
    if(TARGET ${SUBDIR})
        add_library(c_cpp_lib::${SUBDIR} ALIAS ${SUBDIR})
        get_target_property(kind ${SUBDIR} TYPE)
        if(NOT kind STREQUAL "INTERFACE_LIBRARY")
            set_target_properties(${SUBDIR} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
            target_compile_features(${SUBDIR} PUBLIC c_std_11 cxx_std_20)
        endif()
        install(TARGETS ${SUBDIR} EXPORT c_cpp_libTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
        install(DIRECTORY ${SUBDIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()
endforeach()
if(UNIX)
    foreach(name embed_filter embed_kinematics embed_motion embed_path embed_pid)
        target_link_libraries(${name} PUBLIC m)
    endforeach()
endif()
