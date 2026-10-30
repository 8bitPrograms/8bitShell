function(bitshell_register_qtest target)
    if(WIN32)
        # Preserve diagnostics even when a Windows launcher has no console.
        add_test(NAME ${target} COMMAND ${target}
            -o "${CMAKE_CURRENT_BINARY_DIR}/${target}.txt,txt" -o "-,txt")
    else()
        add_test(NAME ${target} COMMAND ${target})
    endif()
endfunction()

find_package(Python3 COMPONENTS Interpreter QUIET)
qt_add_executable(update_client_test UpdateClientTest.cpp)
target_link_libraries(update_client_test PRIVATE bit8_updates Qt6::Test)
set_target_properties(update_client_test PROPERTIES WIN32_EXECUTABLE FALSE)
bitshell_register_qtest(update_client_test)
set_tests_properties(update_client_test PROPERTIES TIMEOUT 30)
if(Python3_Interpreter_FOUND)
    add_test(NAME release_infrastructure_test COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/ReleaseInfrastructureTest.py")
endif()

function(bitshell_add_core_test target source)
    qt_add_executable(${target} ${source})
    set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(${target} PRIVATE Qt6::Test bitshell_core)
    bitshell_register_qtest(${target})
    set_tests_properties(${target} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

bitshell_add_core_test(connection_resolver_test ConnectionResolverTest.cpp)
bitshell_add_core_test(connection_routing_test ConnectionRoutingTest.cpp)
bitshell_add_core_test(conversation_engine_test ConversationEngineTest.cpp)
bitshell_add_core_test(highlight_engine_test HighlightEngineTest.cpp)
bitshell_add_core_test(highlight_rule_store_test HighlightRuleStoreTest.cpp)
bitshell_add_core_test(session_logger_test SessionLoggerTest.cpp)
bitshell_add_core_test(session_store_test SessionStoreTest.cpp)
bitshell_add_core_test(ssh_candidate_test SshCandidateTest.cpp)
bitshell_add_core_test(native_ssh_command_test NativeSshCommandTest.cpp)
bitshell_add_core_test(app_settings_test AppSettingsTest.cpp)
if(NOT WIN32)
    bitshell_add_core_test(sftp_controller_test SftpControllerTest.cpp)
    bitshell_add_core_test(workspace_features_test WorkspaceFeaturesTest.cpp)
endif()
bitshell_add_core_test(yaml_configuration_test YamlConfigurationTest.cpp)
bitshell_add_core_test(platform_test PlatformTest.cpp)
bitshell_add_core_test(local_profile_store_test LocalProfileStoreTest.cpp)
bitshell_add_core_test(startup_directory_test StartupDirectoryTest.cpp)
target_link_libraries(startup_directory_test PRIVATE bitshell_ui bitshell_uiplugin Qt6::Gui Qt6::QuickControls2)
set_target_properties(startup_directory_test PROPERTIES WIN32_EXECUTABLE FALSE)
set_tests_properties(startup_directory_test PROPERTIES TIMEOUT 50
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
bitshell_add_core_test(ssh_key_store_test SshKeyStoreTest.cpp)
bitshell_add_core_test(password_store_test PasswordStoreTest.cpp)
bitshell_add_core_test(password_broker_test PasswordBrokerTest.cpp)
bitshell_add_core_test(password_auxiliary_test PasswordAuxiliaryTest.cpp)
add_dependencies(password_broker_test bitshell_askpass)
add_dependencies(password_auxiliary_test bitshell_askpass)
foreach(askpass_test password_broker_test password_auxiliary_test)
    add_custom_command(TARGET ${askpass_test} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:bitshell_askpass>"
                "$<TARGET_FILE_DIR:${askpass_test}>/$<TARGET_FILE_NAME:bitshell_askpass>"
        VERBATIM)
endforeach()
set_tests_properties(password_store_test password_broker_test password_auxiliary_test PROPERTIES TIMEOUT 90)
if(BIT8SHELL_PUTTYGEN_EXECUTABLE)
    if(WIN32)
        set(_bitshell_test_puttygen_name puttygen.exe)
    else()
        set(_bitshell_test_puttygen_name puttygen)
    endif()
    add_custom_command(TARGET ssh_key_store_test POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${BIT8SHELL_PUTTYGEN_EXECUTABLE}"
                "$<TARGET_FILE_DIR:ssh_key_store_test>/${_bitshell_test_puttygen_name}"
        VERBATIM
    )
endif()
qt_add_executable(vterm_safety_test VtermSafetyTest.cpp)
target_link_libraries(vterm_safety_test PRIVATE Qt6::Test bitshell_vterm)
bitshell_register_qtest(vterm_safety_test)
set_tests_properties(vterm_safety_test PROPERTIES TIMEOUT 30)
set_target_properties(vterm_safety_test PROPERTIES WIN32_EXECUTABLE FALSE)

# One ctest entry per test function so each bounds case reports and times out on its own.
function(bitshell_register_qtest_function name target test_function)
    if(WIN32)
        add_test(NAME ${name} COMMAND ${target}
            -o "${CMAKE_CURRENT_BINARY_DIR}/${name}.txt,txt" -o "-,txt" ${test_function})
    else()
        add_test(NAME ${name} COMMAND ${target} ${test_function})
    endif()
endfunction()
qt_add_executable(vterm_bounds_test VtermBoundsTest.cpp)
target_link_libraries(vterm_bounds_test PRIVATE Qt6::Test bitshell_vterm)
set_target_properties(vterm_bounds_test PROPERTIES WIN32_EXECUTABLE FALSE)
bitshell_register_qtest_function(vterm_rep_bounds_test vterm_bounds_test repWideGlyphAtRowEnd)
bitshell_register_qtest_function(vterm_rep_initial_test vterm_bounds_test repBeforeAnyGlyph)
bitshell_register_qtest_function(vterm_resize_cursor_test vterm_bounds_test resizeAfterAlignmentPattern)
bitshell_register_qtest_function(vterm_erase_bounds_test vterm_bounds_test eraseAfterZeroWidthControl)
set_tests_properties(vterm_rep_bounds_test vterm_resize_cursor_test vterm_erase_bounds_test PROPERTIES TIMEOUT 30)
set_tests_properties(vterm_rep_initial_test PROPERTIES TIMEOUT 10)

qt_add_executable(scrollback_line_test ScrollbackLineTest.cpp)
target_include_directories(scrollback_line_test PRIVATE "${PROJECT_SOURCE_DIR}/src/terminal/include")
target_link_libraries(scrollback_line_test PRIVATE Qt6::Test bitshell_vterm)
bitshell_register_qtest(scrollback_line_test)
set_target_properties(scrollback_line_test PROPERTIES WIN32_EXECUTABLE FALSE)

qt_add_executable(terminal_memory_test TerminalMemoryTest.cpp)
target_link_libraries(terminal_memory_test PRIVATE Qt6::Test bitshell_terminal)
bitshell_register_qtest(terminal_memory_test)
set_target_properties(terminal_memory_test PROPERTIES WIN32_EXECUTABLE FALSE)
set_tests_properties(terminal_memory_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
target_compile_definitions(yaml_configuration_test PRIVATE BIT8SHELL_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

qt_add_executable(win32_input_test Win32InputTest.cpp)
target_include_directories(win32_input_test PRIVATE "${PROJECT_SOURCE_DIR}/src/terminal/include")
target_link_libraries(win32_input_test PRIVATE Qt6::Test Qt6::Gui)
set_target_properties(win32_input_test PROPERTIES WIN32_EXECUTABLE FALSE)
bitshell_register_qtest(win32_input_test)
set_tests_properties(win32_input_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(WIN32)
    # POSIX subprocess/security integration tests below are not Windows evidence.
    # Exercise the native implementation explicitly instead.
    add_executable(windows_terminal_fixture WindowsTerminalFixture.cpp)
    bitshell_add_core_test(windows_security_test WindowsSecurityTest.cpp)
    qt_add_executable(windows_pty_test WindowsPtyTest.cpp)
    set_target_properties(windows_pty_test PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(windows_pty_test PRIVATE Qt6::Test bitshell_terminal)
    add_dependencies(windows_pty_test windows_terminal_fixture)
    bitshell_register_qtest(windows_pty_test)
    set_tests_properties(windows_security_test windows_pty_test PROPERTIES TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
if(NOT WIN32)
bitshell_add_core_test(security_test SecurityTest.cpp)
bitshell_add_core_test(ssh_trust_test SshTrustTest.cpp)
set_tests_properties(ssh_trust_test PROPERTIES TIMEOUT 60)
set_tests_properties(security_test PROPERTIES TIMEOUT 30)

qt_add_executable(pty_process_test PtyProcessTest.cpp)
target_link_libraries(pty_process_test PRIVATE Qt6::Test bitshell_terminal)
add_test(NAME pty_process_test COMMAND pty_process_test)
set_tests_properties(pty_process_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(terminal_view_test TerminalViewTest.cpp)
qt_add_executable(broadcast_controller_test BroadcastControllerTest.cpp)
target_link_libraries(broadcast_controller_test PRIVATE Qt6::Test bitshell_terminal)
add_test(NAME broadcast_controller_test COMMAND broadcast_controller_test)
set_tests_properties(broadcast_controller_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
target_link_libraries(terminal_view_test PRIVATE Qt6::Test bitshell_terminal)
add_test(NAME terminal_view_test COMMAND terminal_view_test)
set_tests_properties(terminal_view_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)
endif()

qt_add_executable(ui_interaction_test UiInteractionTest.cpp)
set_target_properties(ui_interaction_test PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(ui_interaction_test
    PRIVATE
        Qt6::Test
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        bitshell_ui
        bitshell_uiplugin
        bitshell_core
        bitshell_terminal
)
qt_import_qml_plugins(ui_interaction_test)
qt_add_executable(command_palette_test CommandPaletteTest.cpp)
set_target_properties(command_palette_test PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(command_palette_test PRIVATE Qt6::Test Qt6::Qml Qt6::Quick Qt6::QuickControls2
    bitshell_ui bitshell_uiplugin bitshell_core bitshell_terminal)
qt_import_qml_plugins(command_palette_test)
qt_add_executable(password_ui_test PasswordUiTest.cpp)
set_target_properties(password_ui_test PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(password_ui_test PRIVATE Qt6::Test Qt6::Qml Qt6::Quick Qt6::QuickControls2
    bitshell_ui bitshell_uiplugin bitshell_core bitshell_terminal)
qt_import_qml_plugins(password_ui_test)
bitshell_register_qtest(password_ui_test)
add_dependencies(password_ui_test bitshell_askpass)
if(WIN32)
    add_dependencies(password_ui_test windows_terminal_fixture)
endif()
set_tests_properties(password_ui_test PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
bitshell_register_qtest(command_palette_test)
set_tests_properties(command_palette_test PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
if(WIN32)
    add_dependencies(command_palette_test windows_terminal_fixture)
endif()
qt_add_executable(local_ssh_suggestions_test LocalSshSuggestionsTest.cpp)
set_target_properties(local_ssh_suggestions_test PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(local_ssh_suggestions_test PRIVATE Qt6::Test Qt6::Qml Qt6::Quick Qt6::QuickControls2
    bitshell_ui bitshell_uiplugin bitshell_core)
qt_import_qml_plugins(local_ssh_suggestions_test)
bitshell_register_qtest(local_ssh_suggestions_test)
set_tests_properties(local_ssh_suggestions_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

qt_add_executable(local_ssh_terminal_test LocalSshTerminalTest.cpp)
add_dependencies(local_ssh_terminal_test bitshell_askpass)
foreach(askpass_test password_ui_test local_ssh_terminal_test)
    add_custom_command(TARGET ${askpass_test} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:bitshell_askpass>"
                "$<TARGET_FILE_DIR:${askpass_test}>/$<TARGET_FILE_NAME:bitshell_askpass>"
        VERBATIM)
endforeach()
set_target_properties(local_ssh_terminal_test PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(local_ssh_terminal_test PRIVATE Qt6::Test bitshell_terminal)
bitshell_register_qtest(local_ssh_terminal_test)
set_tests_properties(local_ssh_terminal_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
if(WIN32)
    add_executable(local_ssh_fixture LocalSshFixture.cpp)
    add_dependencies(local_ssh_terminal_test local_ssh_fixture windows_terminal_fixture)
endif()
if(WIN32)
    add_dependencies(ui_interaction_test windows_terminal_fixture)
endif()
bitshell_register_qtest(ui_interaction_test)
set_tests_properties(ui_interaction_test PROPERTIES
    TIMEOUT 300
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)

# Opt-in measurement executable, not a timing threshold in the correctness suite.
if(NOT WIN32)
qt_add_executable(performance_benchmark PerformanceBenchmark.cpp)
target_link_libraries(performance_benchmark PRIVATE Qt6::Gui Qt6::Quick bitshell_terminal bitshell_core)
endif()

qt_add_executable(performance_regression_test PerformanceRegressionTest.cpp)
set_target_properties(performance_regression_test PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(performance_regression_test PRIVATE Qt6::Test bitshell_terminal bitshell_core)
add_test(NAME performance_regression_test COMMAND performance_regression_test)
set_tests_properties(performance_regression_test PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)
