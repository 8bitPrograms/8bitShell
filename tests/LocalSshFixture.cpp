#include <windows.h>
#include <cstdio>

// A native executable copied to ssh.exe in an isolated test PATH. It never
// opens sockets or reads keys; the real local shell must invoke this process.
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::printf("LOCAL_SSH_STUB_EXECUTED\n");
    for (int i = 1; i < argc; ++i) {
        std::printf("SSH_ARG%d=", i);
        for (const wchar_t* c = argv[i]; *c; ++c) std::printf("%04x", unsigned(*c));
        std::printf("\n");
    }
    std::fflush(stdout);
    return 0;
}
