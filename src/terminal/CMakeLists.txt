qt_add_library(bitshell_terminal STATIC)

target_sources(bitshell_terminal
    PRIVATE
        src/TerminalView.cpp
        src/BroadcastController.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/bitshell/terminal/PtyProcess.h
            include/bitshell/terminal/TerminalView.h
            include/bitshell/terminal/ScrollbackLine.h
            include/bitshell/terminal/BroadcastController.h
)

target_link_libraries(bitshell_terminal
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        bitshell_core
        bitshell_vterm
)

if(WIN32)
    target_sources(bitshell_terminal PRIVATE src/PtyProcessWindows.cpp)
else()
    target_sources(bitshell_terminal PRIVATE src/PtyProcess.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(bitshell_terminal PRIVATE util)
    endif()
endif()

target_compile_definitions(bitshell_terminal
    PUBLIC
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)

target_compile_options(bitshell_terminal
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)
