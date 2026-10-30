qt_add_library(bitshell_core STATIC)

target_sources(bitshell_core
    PRIVATE
        src/ConnectionResolver.cpp
        src/ConnectionRouting.cpp
        src/PersonalProfile.cpp
        src/SshKeyStore.cpp
        src/PasswordStore.cpp
        src/CredentialFlow.cpp
        src/CredentialStore.cpp
        src/PasswordBroker.cpp
        src/SshSecurity.cpp
        src/NativeSshCommand.cpp
        src/Platform.cpp
        src/AppSettings.cpp
        src/YamlFile.cpp
        src/SettingsFile.cpp
        src/MacroStore.cpp
        src/LocalProfileStore.cpp
        src/HostMonitor.cpp
        src/ConversationEngine.cpp
        src/HighlightEngine.cpp
        src/HighlightRuleStore.cpp
        src/SessionLogger.cpp
        src/SessionStore.cpp
        src/SftpController.cpp
        src/LocalFileModel.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/bitshell/core/ConnectionResolver.h
            include/bitshell/core/ConnectionRouting.h
            include/bitshell/core/PersonalProfile.h
            include/bitshell/core/SshKeyStore.h
            include/bitshell/core/PasswordStore.h
            include/bitshell/core/CredentialFlow.h
            include/bitshell/core/CredentialStore.h
            include/bitshell/core/PasswordBroker.h
            include/bitshell/core/SshSecurity.h
            include/bitshell/core/NativeSshCommand.h
            include/bitshell/core/PrivateFile.h
            include/bitshell/core/Platform.h
            include/bitshell/core/BoundedRegex.h
            include/bitshell/core/AppSettings.h
            include/bitshell/core/YamlFile.h
            include/bitshell/core/SettingsFile.h
            include/bitshell/core/MacroStore.h
            include/bitshell/core/LocalProfileStore.h
            include/bitshell/core/HostMonitor.h
            include/bitshell/core/ConnectionTypes.h
            include/bitshell/core/ConversationEngine.h
            include/bitshell/core/HighlightEngine.h
            include/bitshell/core/HighlightRuleStore.h
            include/bitshell/core/SessionLogger.h
            include/bitshell/core/SessionStore.h
            include/bitshell/core/SftpController.h
            include/bitshell/core/LocalFileModel.h
)

if(WIN32)
    target_sources(bitshell_core PRIVATE src/PrivateFileWindows.cpp)
    target_link_libraries(bitshell_core PRIVATE advapi32 iphlpapi crypt32)
else()
    target_sources(bitshell_core PRIVATE src/PrivateFile.cpp)
endif()
if(APPLE)
    target_link_libraries(bitshell_core PRIVATE "-framework Security" "-framework CoreFoundation")
endif()

target_link_libraries(bitshell_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
        Qt6::Network
    PRIVATE
        yaml-cpp::yaml-cpp
)

target_compile_definitions(bitshell_core
    PUBLIC
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)

target_compile_options(bitshell_core
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)
