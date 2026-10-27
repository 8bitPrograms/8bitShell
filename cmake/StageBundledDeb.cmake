# Only writes inside CPack's disposable staging directory.
if(NOT IS_ABSOLUTE "${CPACK_TEMPORARY_DIRECTORY}" OR
   NOT EXISTS "${CPACK_TEMPORARY_DIRECTORY}/opt/bitshell/bin/bitshell")
    message(FATAL_ERROR "Refusing an unexpected DEB staging directory")
endif()
set(_stage "${CPACK_TEMPORARY_DIRECTORY}")
file(MAKE_DIRECTORY "${_stage}/usr/bin" "${_stage}/usr/share/applications"
    "${_stage}/usr/share/icons/hicolor/scalable/apps" "${_stage}/usr/share/metainfo")
file(CREATE_LINK /opt/bitshell/bin/bitshell "${_stage}/usr/bin/bitshell" SYMBOLIC)
foreach(_data IN ITEMS applications/io.github.8bitshell.App.desktop
        icons/hicolor/scalable/apps/bitshell.svg metainfo/io.github.8bitshell.App.metainfo.xml)
    file(COPY_FILE "${_stage}/opt/bitshell/share/${_data}" "${_stage}/usr/share/${_data}")
endforeach()
