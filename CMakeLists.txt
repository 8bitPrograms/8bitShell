cmake_minimum_required(VERSION 3.24)
include(cmake/Version.cmake)

project(
    8bitShell
    VERSION ${BIT8SHELL_VERSION_CORE}
    DESCRIPTION "A fast, programmable remote access workspace"
    HOMEPAGE_URL "https://github.com/8bitPrograms/8bitShell"
    LANGUAGES C CXX
)

include(GNUInstallDirs)
set(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATADIR}/doc/bitshell")
option(BIT8SHELL_BUNDLE_RUNTIME "Deploy private Qt/runtime libraries (portable builds)" ON)
set(BIT8SHELL_PUTTYGEN_EXECUTABLE "" CACHE FILEPATH
    "Verified command-line PuTTYgen executable to bundle beside the application")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BIT8SHELL_BUILD_TESTS "Build the core test suite" ON)
include(cmake/Hardening.cmake)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2 Concurrent Network Svg)
if(BIT8SHELL_BUNDLE_RUNTIME AND UNIX AND NOT APPLE)
    get_target_property(_bitshell_qt_core Qt6::Core IMPORTED_LOCATION_RELEASE)
    get_filename_component(_bitshell_qt_lib "${_bitshell_qt_core}" DIRECTORY)
    # Avoid CMake's colon-padding (an empty/CWD entry) in build-tree executables.
    # Tests use the trusted SDK. The installed app has the SDK entry removed.
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_INSTALL_RPATH "${_bitshell_qt_lib}")
endif()
include(cmake/TerminalDependency.cmake)
find_package(yaml-cpp 0.9 CONFIG QUIET)
if(NOT TARGET yaml-cpp::yaml-cpp)
    include(FetchContent)
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
    set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(yaml-cpp
        URL https://codeload.github.com/jbeder/yaml-cpp/tar.gz/refs/tags/yaml-cpp-0.9.0
        URL_HASH SHA256=25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(yaml-cpp)
endif()
qt_standard_project_setup(REQUIRES 6.10)
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

add_subdirectory(src/update)
add_subdirectory(src/core)
add_subdirectory(src/terminal)

# Askpass returns one password through OpenSSH's private response pipe. It has
# no QML/UI dependency and is included in every install/portable package.
qt_add_executable(bitshell_askpass src/askpass/main.cpp)
target_link_libraries(bitshell_askpass PRIVATE Qt6::Core Qt6::Network)
set_target_properties(bitshell_askpass PROPERTIES OUTPUT_NAME "8bitShell-askpass"
    WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
if(APPLE)
    set_target_properties(bitshell_askpass PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
elseif(BIT8SHELL_BUNDLE_RUNTIME AND UNIX)
    set_target_properties(bitshell_askpass PROPERTIES INSTALL_RPATH "${_bitshell_qt_lib};$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

qt_add_executable(bitshell
    src/app/main.cpp
)
add_dependencies(bitshell bitshell_askpass)
target_link_libraries(bitshell PRIVATE bit8_updates)
configure_file(cmake/Version.h.in generated/Version.h @ONLY)
target_include_directories(bitshell PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(BIT8SHELL_BUNDLE_RUNTIME AND UNIX AND NOT APPLE)
    # Relative to this executable, never relative to the process working directory.
    set(BIT8SHELL_BUILD_APP_RPATH "${_bitshell_qt_lib}:$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    set_target_properties(bitshell PROPERTIES INSTALL_RPATH "${_bitshell_qt_lib};$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

qt_add_library(bitshell_ui STATIC)


set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(bitshell_ui
    URI BitShell
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/BitShell"
    RESOURCES assets/bitshell.svg
    SOURCES
        src/app/TerminalViewRegistration.h
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/components/AppButton.qml
        qml/components/AppDialogButtonBox.qml
        qml/components/CommandPalette.qml
        qml/components/CommandPaletteActions.qml
        qml/components/LocalLaunchButton.qml
        qml/components/LocalProfileSettings.qml
        qml/components/PanelButton.qml
        qml/components/UpwardPanel.qml
        qml/components/AccountFields.qml
        qml/components/ConnectionChoice.qml
        qml/components/RouteManager.qml
        qml/components/FolderConnectionDialog.qml
        qml/components/PersonalProfileDialog.qml
        qml/components/SshKeyManager.qml
        qml/components/AboutDialog.qml
        qml/components/ProfileAvatar.qml
        qml/components/ProfileSettings.qml
        qml/components/AppComboBox.qml
        qml/components/AppTextField.qml
        qml/components/YamlFileActions.qml
        qml/components/MacroManager.qml
        qml/components/MacroBar.qml
        qml/components/MonitorBar.qml
        qml/components/AppearanceSettings.qml
        qml/components/InputSettings.qml
        qml/components/ThemePicker.qml
        qml/components/TerminalRenderingSettings.qml
        qml/components/LockIcon.qml
        qml/components/AppMenu.qml
        qml/components/AppMenuItem.qml
        qml/components/AppMenuSeparator.qml
        qml/components/AppSwitch.qml
        qml/components/BrandMark.qml
        qml/components/HighlightPanel.qml
        qml/components/HighlightRuleDialog.qml
        qml/components/SessionRow.qml
        qml/components/SessionTreeRow.qml
        qml/components/SearchIcon.qml
        qml/components/SessionEditorDialog.qml
        qml/components/SettingsDrawer.qml
        qml/components/UpdateSettings.qml
        qml/components/SftpPanel.qml
        qml/components/FilePane.qml
        qml/components/Sidebar.qml
        qml/components/TerminalLine.qml
        qml/components/TerminalPane.qml
        qml/components/MultiSshBar.qml
        qml/components/TerminalSession.qml
        qml/components/LocalSshSuggestions.qml
        qml/components/TopBar.qml
)

target_link_libraries(bitshell_ui
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Svg
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickDialogs2
        bitshell_core
        bitshell_terminal
)

target_include_directories(bitshell_ui PRIVATE src/app)

target_link_libraries(bitshell
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Svg
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        bitshell_ui
        bitshell_uiplugin
        bitshell_core
)

set_target_properties(bitshell PROPERTIES
    OUTPUT_NAME "8bitShell"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
if(APPLE)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/assets/bitshell.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    add_custom_command(TARGET bitshell POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:bitshell_askpass>" "$<TARGET_FILE_DIR:bitshell>/8bitShell-askpass"
        VERBATIM)
    target_sources(bitshell PRIVATE "${PROJECT_SOURCE_DIR}/assets/bitshell.icns")
    set_target_properties(bitshell PROPERTIES MACOSX_BUNDLE_ICON_FILE bitshell.icns MACOSX_BUNDLE_GUI_IDENTIFIER io.github.8bitshell.App
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}" MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()
if(BIT8SHELL_PUTTYGEN_EXECUTABLE)
    if(NOT EXISTS "${BIT8SHELL_PUTTYGEN_EXECUTABLE}")
        message(FATAL_ERROR "BIT8SHELL_PUTTYGEN_EXECUTABLE does not exist")
    endif()
    if(WIN32)
        set(_bitshell_puttygen_name puttygen.exe)
    else()
        set(_bitshell_puttygen_name puttygen)
    endif()
    add_custom_command(TARGET bitshell POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${BIT8SHELL_PUTTYGEN_EXECUTABLE}"
                "$<TARGET_FILE_DIR:bitshell>/${_bitshell_puttygen_name}"
        VERBATIM
    )
    install(PROGRAMS "${BIT8SHELL_PUTTYGEN_EXECUTABLE}"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME ${_bitshell_puttygen_name}
    )
    install(FILES third_party/PUTTY-LICENSE
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/licenses")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(bitshell PRIVATE -pie)
endif()
if(WIN32 AND MSVC)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/bitshell.ico")
        message(FATAL_ERROR "The multi-resolution Windows application icon is missing")
    endif()
    set_target_properties(bitshell PROPERTIES
        QT_WINDOWS_APP_PROJECT_IDENTIFIER "io.github.8bitshell.App"
    )
    configure_file(packaging/windows-version.rc.in generated/windows-version.rc @ONLY)
    target_sources(bitshell PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/windows-version.rc")
    target_link_options(bitshell PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows.manifest")
endif()

include(CTest)
if(BUILD_TESTING AND BIT8SHELL_BUILD_TESTS)
    find_package(Qt6 6.10 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

install(TARGETS bitshell
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(APPLE)
    install(TARGETS bitshell_askpass RUNTIME DESTINATION "8bitShell.app/Contents/MacOS")
else()
    install(TARGETS bitshell_askpass RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(BIT8SHELL_BUNDLE_RUNTIME AND UNIX AND NOT APPLE)
    configure_file(cmake/InstallBundleRpath.cmake.in generated/InstallBundleRpath.cmake @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/generated/InstallBundleRpath.cmake")
endif()

if(UNIX AND NOT APPLE)
install(
    FILES packaging/io.github.8bitshell.App.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(
    FILES assets/bitshell.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME 8bitshell.svg
)
install(
    FILES packaging/io.github.8bitshell.App.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)
endif()
install(
    DIRECTORY schemas examples
    DESTINATION ${CMAKE_INSTALL_DATADIR}/bitshell
)
install(
    FILES README.md SECURITY.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
install(
    FILES third_party/yaml-cpp-LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses
)
install(
    DIRECTORY docs/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    # Developer handoff notes and VM-specific evidence are not user manuals.
    PATTERN "NEXT-SESSION.md" EXCLUDE
    PATTERN "QA-*.md" EXCLUDE
)

if(BIT8SHELL_BUNDLE_RUNTIME)
    if(Qt6_VERSION VERSION_LESS 6.10)
        message(FATAL_ERROR "Bundled runtime deployment requires Qt >= 6.10; native builds still support Qt >= 6.7")
    endif()
    install(FILES third_party/QT-NOTICES.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses)
    install(DIRECTORY third_party/qt-licenses DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses)
    get_target_property(bitshell_qt_core_dll Qt6::Core IMPORTED_LOCATION_RELEASE)
    get_filename_component(bitshell_qt_bin_dir "${bitshell_qt_core_dll}" DIRECTORY)
    file(GLOB bitshell_qt_vendor_sboms "${bitshell_qt_bin_dir}/../sbom/*.cdx.json")
    if(bitshell_qt_vendor_sboms)
        install(FILES ${bitshell_qt_vendor_sboms} DESTINATION ${CMAKE_INSTALL_DOCDIR}/vendor-sbom)
    endif()
    set(_bitshell_plugin_args INCLUDE_PLUGINS qoffscreen)
    if(UNIX AND NOT APPLE)
        set(_bitshell_plugin_args INCLUDE_PLUGIN_TYPES platforms)
    endif()
    qt_generate_deploy_qml_app_script(
        TARGET bitshell
        OUTPUT_SCRIPT bitshell_deploy_script
        # End-user runtimes must come from Microsoft's supported redistributable,
        # never copied opportunistically out of a developer toolchain directory.
        NO_COMPILER_RUNTIME
        # Keep Wayland/X11 support and the offscreen acceptance-test backend;
        # deploying only the build host's default platform is not portable.
        ${_bitshell_plugin_args}
    )
    install(SCRIPT ${bitshell_deploy_script})
endif()
include(cmake/Packaging.cmake)
