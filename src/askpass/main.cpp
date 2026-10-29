#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QByteArray>
#include <cstdio>
#include <cstdlib>

#ifdef Q_OS_WIN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif
#endif

namespace {
constexpr qint64 maximumPasswordBytes = 4096;

bool secureProcess() {
#ifdef Q_OS_WIN
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) return false;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (_isatty(_fileno(stdout))) return false;
    return _setmode(_fileno(stdout), _O_BINARY) != -1;
#else
    if (::geteuid() != ::getuid() || ::getegid() != ::getgid() || ::isatty(STDOUT_FILENO)) return false;
    const rlimit noCore{0, 0};
    if (::setrlimit(RLIMIT_CORE, &noCore) != 0) return false;
#ifdef Q_OS_LINUX
    if (::prctl(PR_SET_DUMPABLE, 0) != 0) return false;
#endif
    return true;
#endif
}
} // namespace

int main(int argc, char* argv[]) {
    // OpenSSH owns this stdout pipe. There is deliberately no logging, UI,
    // command parser, shell, fallback prompt or filesystem password exchange.
    if (!secureProcess() || argc != 2) return EXIT_FAILURE;
    QCoreApplication app(argc, argv);
    const auto arguments = QCoreApplication::arguments();
    if (arguments.size() != 2) return EXIT_FAILURE;
    if (!qEnvironmentVariableIsEmpty("SSH_ASKPASS_PROMPT")) return EXIT_FAILURE;
    const auto server = qEnvironmentVariable("BIT8SHELL_ASKPASS_SOCKET");
    auto token = qgetenv("BIT8SHELL_ASKPASS_TOKEN");
    static const QRegularExpression serverPattern(QStringLiteral("\\A8bitshell-password-[0-9a-f]{32}\\z"));
    static const QRegularExpression tokenPattern(QStringLiteral("\\A[0-9a-f]{64}\\z"));
    if (!serverPattern.match(server).hasMatch()
        || !tokenPattern.match(QString::fromLatin1(token)).hasMatch()
        || arguments[1].size() > 1024 || arguments[1].contains(QChar::Null)) return EXIT_FAILURE;
    qunsetenv("BIT8SHELL_ASKPASS_SOCKET");
    qunsetenv("BIT8SHELL_ASKPASS_TOKEN");

    QLocalSocket socket;
    socket.setReadBufferSize(maximumPasswordBytes + 2);
    socket.connectToServer(server, QIODevice::ReadWrite);
    if (!socket.waitForConnected(3000)) { token.fill('\0'); return EXIT_FAILURE; }
    auto request = QJsonDocument(QJsonObject{{QStringLiteral("token"), QString::fromLatin1(token)},
        {QStringLiteral("prompt"), arguments[1]}}).toJson(QJsonDocument::Compact);
    token.fill('\0');
    request.append('\n');
    const auto requestedBytes = request.size();
    const auto written = socket.write(request);
    request.fill('\0');
    if (written != requestedBytes || (!socket.waitForBytesWritten(3000) && socket.bytesToWrite() != 0)) return EXIT_FAILURE;

    QElapsedTimer elapsed;
    elapsed.start();
    while (!socket.canReadLine()) {
        const auto remaining = 5000 - elapsed.elapsed();
        if (remaining <= 0 || socket.bytesAvailable() > maximumPasswordBytes
            || !socket.waitForReadyRead(static_cast<int>(remaining))) return EXIT_FAILURE;
    }
    auto response = socket.readLine(maximumPasswordBytes + 2);
    if (response.size() < 2 || response.size() > maximumPasswordBytes + 1
        || !response.endsWith('\n') || response.contains('\0') || response.contains('\r')
        || response.first(response.size() - 1).contains('\n') || socket.bytesAvailable() != 0) {
        response.fill('\0'); return EXIT_FAILURE;
    }
    const auto length = static_cast<size_t>(response.size());
    const bool output = fwrite(response.constData(), 1, length, stdout) == length && fflush(stdout) == 0;
    response.fill('\0');
    return output ? EXIT_SUCCESS : EXIT_FAILURE;
}
