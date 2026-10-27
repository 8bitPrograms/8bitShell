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
bitshell_add_core_test(app_settings_test AppSettingsTest.cpp)
if(NOT WIN32)
    bitshell_add_core_test(sftp_controller_test SftpControllerTest.cpp)
    bitshell_add_core_test(workspace_features_test WorkspaceFeaturesTest.cpp)
endif()
bitshell_add_core_test(yaml_configuration_test YamlConfigurationTest.cpp)
bitshell_add_core_test(platform_test PlatformTest.cpp)
bitshell_add_core_test(local_profile_store_test LocalProfileStoreTest.cpp)
bitshell_add_core_test(ssh_key_store_test SshKeyStoreTest.cpp)
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
