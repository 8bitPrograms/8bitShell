// Native console fixture. No shell/parser between the test and these arguments.
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <cstdint>
#include <string>
// Application Verifier may raise STATUS_INVALID_HANDLE for the intentional probe.
// A successful query still means inheritance and is reported as a test failure.
static bool inheritedHandle(HANDLE handle) {
    DWORD flags = 0;
#ifdef _MSC_VER
    __try {
        return GetHandleInformation(handle, &flags) != FALSE;
    } __except (GetExceptionCode() == 0xC0000008UL ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
#else
    return GetHandleInformation(handle, &flags) != FALSE;
#endif
}
int wmain(int argc, wchar_t** argv) {
    SetConsoleCP(CP_UTF8); SetConsoleOutputCP(CP_UTF8);
    const std::wstring mode = argc > 1 ? argv[1] : L"args";
    if (mode == L"args") {
        for (int n = 2; n < argc; ++n) {
            std::printf("ARG%d=", n - 2);
            for (const wchar_t* p = argv[n]; *p; ++p) std::printf("%04x", unsigned(*p));
            std::printf("\n");
        }
    } else if (mode == L"raw") {
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        if (!SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT)) return 3;
        std::printf("RAW_READY\n"); std::fflush(stdout);
        char data[16]{}; DWORD count = 0;
        if (!ReadFile(input, data, sizeof(data), &count, nullptr)) return 4;
        std::printf("INPUT=");
        for (DWORD n = 0; n < count; ++n) std::printf("%02x", unsigned(static_cast<unsigned char>(data[n])));
        std::printf("\n");
    } else if (mode == L"broadcast" || mode == L"profile") {
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        if (!SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT)) return 3;
        if (mode == L"profile") {
            wchar_t directory[32768]{};
            if (!GetCurrentDirectoryW(32768, directory)) return 7;
            std::printf("CWDHEX=");
            for (const wchar_t* p = directory; *p; ++p) std::printf("%04x", unsigned(*p));
            std::printf("\n");
        }
        std::printf("READY\n"); std::fflush(stdout);
        std::string line;
        bool previousCr = false;
        char data[512]{}; DWORD count = 0;
        while (ReadFile(input, data, sizeof(data), &count, nullptr) && count) {
            for (DWORD n = 0; n < count; ++n) {
                const char c = data[n];
                if (c == '\r' || c == '\n') {
                    if (c != '\n' || !previousCr) {
                        std::printf("ACK:%s\n", line.c_str()); std::fflush(stdout);
                        line.clear();
                    }
                } else {
                    if (line.size() >= 65536) return 6;
                    line += c;
                }
                previousCr = c == '\r';
            }
        }
    } else if (mode == L"resize") {
        std::printf("SIZE_READY\n"); std::fflush(stdout); Sleep(800);
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) return 5;
        std::printf("SIZE=%dx%d\n", int(info.srWindow.Right-info.srWindow.Left+1), int(info.srWindow.Bottom-info.srWindow.Top+1));
    } else if (mode == L"inherit") {
        const auto value = _wcstoui64(argc > 2 ? argv[2] : L"0", nullptr, 10);
        std::printf("INHERITED=%d\n", inheritedHandle(reinterpret_cast<HANDLE>(uintptr_t(value))) ? 1 : 0);
    } else if (mode == L"sleep") {
        std::printf("SLEEP_READY\n"); std::fflush(stdout); Sleep(60000);
    } else if (mode == L"flood" || mode == L"history") {
        const std::string line(160, 'x');
        for (int n = 0; n < 20000; ++n) std::printf("%s\n", line.c_str());
        std::printf("FLOOD_END\n");
        std::fflush(stdout);
        if (mode == L"history") Sleep(60000);
    } else return 2;
    std::fflush(stdout); Sleep(30); return 0;
}
