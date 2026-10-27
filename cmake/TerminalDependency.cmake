# The same pinned, locally bounded parser is used on both platforms.
    include(FetchContent)
    FetchContent_Declare(bitshell_vterm_source
        URL https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz
        URL_HASH SHA256=09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(bitshell_vterm_source)
    include(${CMAKE_CURRENT_LIST_DIR}/LibvtermPatch.cmake)
    bitshell_patch_vterm("${bitshell_vterm_source_SOURCE_DIR}/src/parser.c" "${CMAKE_CURRENT_BINARY_DIR}/vterm-patched/parser.c")
    bitshell_patch_vterm_screen("${bitshell_vterm_source_SOURCE_DIR}/src/screen.c" "${CMAKE_CURRENT_BINARY_DIR}/vterm-patched/screen.c")
    add_library(bitshell_vterm STATIC "${CMAKE_CURRENT_BINARY_DIR}/vterm-patched/parser.c" "${CMAKE_CURRENT_BINARY_DIR}/vterm-patched/screen.c")
    foreach(source encoding keyboard mouse pen state unicode vterm)
        target_sources(bitshell_vterm PRIVATE "${bitshell_vterm_source_SOURCE_DIR}/src/${source}.c")
    endforeach()
    set_target_properties(bitshell_vterm PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    target_include_directories(bitshell_vterm PUBLIC "${bitshell_vterm_source_SOURCE_DIR}/include")
    target_include_directories(bitshell_vterm PRIVATE "${bitshell_vterm_source_SOURCE_DIR}/src")
    target_compile_definitions(bitshell_vterm PRIVATE _CRT_SECURE_NO_WARNINGS)
    install(FILES "${bitshell_vterm_source_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/licenses" RENAME libvterm-LICENSE)
