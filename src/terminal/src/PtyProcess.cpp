#include <bitshell/terminal/PtyProcess.h>
#include <bitshell/core/SshSecurity.h>
#include <bitshell/core/Platform.h>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QTimer>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <utility>
#include <vector>
#include <thread>

#include <fcntl.h>
#ifdef Q_OS_DARWIN
#include <util.h>
#else
#include <pty.h>
#endif
#include <sys/ioctl.h>
#include <sys/wait.h>
#ifdef Q_OS_LINUX
#include <sys/syscall.h>
#endif
#include <sys/resource.h>
#include <termios.h>
#include <unistd.h>

namespace bitshell::terminal {

namespace {

QString systemError(const char* operation) {
    return QStringLiteral("%1: %2")
        .arg(QString::fromLatin1(operation), QString::fromLocal8Bit(std::strerror(errno)));
}

} // namespace

PtyProcess::PtyProcess(QObject* parent)
    : QObject(parent) {}

PtyProcess::~PtyProcess() {
    stop();
}

bool PtyProcess::start(const QString& program,
                       const QStringList& arguments,
                       int columns,
                       int rows,
                       QString* errorMessage,
                       const QString& workingDirectory,
                       const QProcessEnvironment& environmentOverrides) {
    if (!workingDirectory.isEmpty() && (workingDirectory.contains(QChar::Null)
        || !QDir::isAbsolutePath(workingDirectory) || !QFileInfo(workingDirectory).isDir())) {
        setError(errorMessage, QStringLiteral("The terminal starting directory is invalid or no longer exists.")); return false;
    }
    const QByteArray directory = QFile::encodeName(workingDirectory);
    stop();

    const auto launchArguments = m_interactiveShell
        ? core::Platform::persistentHistoryArguments(program, arguments, m_shellIntegrationToken) : arguments;

    const QByteArray executable = QFile::encodeName(program);
    if (executable.isEmpty() || !QDir::isAbsolutePath(program) || executable.contains('\0')
        || launchArguments.size() > 128 || std::any_of(launchArguments.cbegin(), launchArguments.cend(), [](const QString& arg) {
            return arg.contains(QChar::Null) || arg.size() > 1024 * 1024;
        })) {
        setError(errorMessage, QStringLiteral("Invalid terminal executable or arguments"));
        return false;
    }

    std::vector<QByteArray> argumentStorage;
    argumentStorage.reserve(static_cast<std::size_t>(launchArguments.size()) + 1U);
    argumentStorage.push_back(executable);
    for (const auto& argument : launchArguments) {
        argumentStorage.push_back(argument.toLocal8Bit());
    }

    std::vector<char*> argv;
    argv.reserve(argumentStorage.size() + 1U);
    for (auto& argument : argumentStorage) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    auto environment = core::Platform::isManagedSsh(program)
                           ? core::SshSecurity::environment() : QProcessEnvironment::systemEnvironment();
    // Only trusted application launch code supplies these per-process additions.
    // Password broker capabilities are never inserted into the GUI environment.
    environment.insert(environmentOverrides);
    if (m_interactiveShell && QFileInfo(program).fileName() == QStringLiteral("bash")) {
        // Bash normally writes history only when a shell exits. With several
        // 8bitShell tabs that makes Up show a stale snapshot and the newest
        // commands can overwrite each other. Flush and merge at every prompt,
        // while preserving any prompt hook the user already configured.
        const QString sync = QStringLiteral("history -a; history -n");
        const QString existing = environment.value(QStringLiteral("PROMPT_COMMAND"));
        if (!existing.contains(QStringLiteral("history -a")))
            environment.insert(QStringLiteral("PROMPT_COMMAND"),
                               existing.isEmpty() ? sync : existing + QStringLiteral("; ") + sync);
    }
    environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    environment.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    environment.insert(QStringLiteral("TERM_PROGRAM"), QStringLiteral("bitshell"));
    std::vector<QByteArray> environmentStorage;
    for (const auto& entry : environment.toStringList()) environmentStorage.push_back(entry.toLocal8Bit());
    std::vector<char*> envp;
    for (auto& entry : environmentStorage) envp.push_back(entry.data());
    envp.push_back(nullptr);
    const long maxFd = std::max(1024L, ::sysconf(_SC_OPEN_MAX));

    winsize windowSize{};
    windowSize.ws_col = static_cast<unsigned short>(std::clamp(columns, 2, 1000));
    windowSize.ws_row = static_cast<unsigned short>(std::clamp(rows, 2, 1000));

    int masterFd = -1;
    const pid_t childPid = ::forkpty(&masterFd, nullptr, nullptr, &windowSize);
    if (childPid < 0) {
        setError(errorMessage, systemError("forkpty"));
        return false;
    }

    if (childPid == 0) {
        // Only async-signal-safe calls between fork and exec. Never inherit
        // another session's PTY or the application's open logs/configuration.
#if defined(Q_OS_LINUX) && defined(SYS_close_range)
        if (::syscall(SYS_close_range, 3U, ~0U, 0U) < 0)
#endif
            for (long fd = 3; fd < maxFd; ++fd) ::close(static_cast<int>(fd));
        const rlimit noCore{0, 0};
        (void)::setrlimit(RLIMIT_CORE, &noCore);
        termios attributes{};
        if (::tcgetattr(STDIN_FILENO, &attributes) == 0) {
            attributes.c_cc[VERASE] = static_cast<cc_t>(m_eraseCharacter);
            (void)::tcsetattr(STDIN_FILENO, TCSANOW, &attributes);
        }
        if (!directory.isEmpty() && ::chdir(directory.constData()) != 0) {
            constexpr char message[] = "Cannot open the terminal starting directory.\r\n";
            (void)::write(STDERR_FILENO, message, sizeof(message) - 1);
            _exit(126);
        }
        ::execve(executable.constData(), argv.data(), envp.data());
        _exit(127);
    }

    const int currentFlags = ::fcntl(masterFd, F_GETFL, 0);
    if (currentFlags < 0 || ::fcntl(masterFd, F_SETFL, currentFlags | O_NONBLOCK) < 0
        || ::fcntl(masterFd, F_SETFD, FD_CLOEXEC) < 0) {
        ::close(masterFd);
        ::kill(childPid, SIGKILL);
        std::thread([childPid] { while (::waitpid(childPid, nullptr, 0) < 0 && errno == EINTR) {} }).detach();
        setError(errorMessage, QStringLiteral("Could not secure the PTY descriptor"));
        return false;
    }

    m_masterFd = masterFd;
    m_childPid = static_cast<qint64>(childPid);
#ifdef Q_OS_DARWIN
    // macOS discards PTY output that is still unread when the last replica
    // descriptor closes: ptsclose() drains for at most 60 ticks (~0.6 s) and
    // then flushes. Holding our own replica means the child's exit is never
    // that last close. The kernel then waits for us to read everything
    // (proc_exit -> ttywait, no timeout) and revokes the terminal, so the
    // master still reads EOF and finishFromMaster() runs as before.
    if (const char* replica = ::ptsname(masterFd))
        m_replicaFd = ::open(replica, O_RDWR | O_NOCTTY | O_CLOEXEC);
#endif
    m_readNotifier = std::make_unique<QSocketNotifier>(m_masterFd, QSocketNotifier::Read, this);
    m_writeNotifier =
        std::make_unique<QSocketNotifier>(m_masterFd, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);

    connect(m_readNotifier.get(),
            &QSocketNotifier::activated,
            this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { handleReadyRead(); });
    connect(m_writeNotifier.get(),
            &QSocketNotifier::activated,
            this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { flushWriteBuffer(); });

    emit runningChanged(true);
    return true;
}

void PtyProcess::stop() {
    const bool wasRunning = isRunning();
    m_readNotifier.reset();
    m_writeNotifier.reset();
    m_writeBuffer.clear();

    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
    if (m_replicaFd >= 0) {
        ::close(m_replicaFd);
        m_replicaFd = -1;
    }

    if (m_childPid > 0) {
        const auto pid = static_cast<pid_t>(m_childPid);
        ::kill(-pid, SIGHUP);
        ::kill(pid, SIGHUP);

        int status = 0;
        if (::waitpid(pid, &status, WNOHANG) == 0) {
            ::kill(-pid, SIGKILL);
            ::kill(pid, SIGKILL);
            std::thread([pid] { while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {} }).detach();
        }
        m_childPid = -1;
    }

    if (wasRunning) {
        emit runningChanged(false);
    }
}

void PtyProcess::resize(int columns, int rows) {
    if (m_masterFd < 0) {
        return;
    }

    winsize windowSize{};
    windowSize.ws_col = static_cast<unsigned short>(std::clamp(columns, 2, 1000));
    windowSize.ws_row = static_cast<unsigned short>(std::clamp(rows, 2, 1000));
    if (::ioctl(m_masterFd, TIOCSWINSZ, &windowSize) < 0 && errno != EIO) {
        emit errorOccurred(systemError("TIOCSWINSZ"));
    }
}

bool PtyProcess::canWrite(qsizetype bytes) const {
    return isRunning() && bytes >= 0 && bytes <= 1024 * 1024 - m_writeBuffer.size();
}

void PtyProcess::write(QByteArray data) {
    if (!isRunning() || data.isEmpty()) {
        return;
    }
    if (data.size() > 1024 * 1024 - m_writeBuffer.size()) {
        emit errorOccurred(QStringLiteral("Terminal input exceeded the 1 MiB queue limit; input was not sent."));
        return;
    }
    m_writeBuffer.append(std::move(data));
    flushWriteBuffer();
}

void PtyProcess::setEraseCharacter(char value) {
    const char normalized = value == '\x08' ? '\x08' : '\x7f';
    if (m_eraseCharacter == normalized) {
        return;
    }
    m_eraseCharacter = normalized;
    if (m_masterFd < 0) {
        return;
    }

    termios attributes{};
    if (::tcgetattr(m_masterFd, &attributes) == 0) {
        attributes.c_cc[VERASE] = static_cast<cc_t>(m_eraseCharacter);
        if (::tcsetattr(m_masterFd, TCSANOW, &attributes) < 0 && errno != EIO) {
            emit errorOccurred(systemError("set PTY erase character"));
        }
    }
}

char PtyProcess::eraseCharacter() const {
    return m_eraseCharacter;
}

bool PtyProcess::isRunning() const {
    return m_masterFd >= 0 && m_childPid > 0;
}

qint64 PtyProcess::processId() const {
    return m_childPid;
}

void PtyProcess::handleReadyRead() {
    if (m_masterFd < 0) {
        return;
    }

    char buffer[65536];
    // Bound work per event-loop turn, even when a peer floods the terminal.
    for (int reads = 0; reads < 4 && m_masterFd >= 0; ++reads) {
        const ssize_t count = ::read(m_masterFd, buffer, sizeof(buffer));
        if (count > 0) {
            emit dataReceived(QByteArray(buffer, static_cast<qsizetype>(count)));
            continue;
        }
        if (count == 0 || (count < 0 && errno == EIO)) {
            finishFromMaster();
            return;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        emit errorOccurred(systemError("read PTY"));
        finishFromMaster();
        return;
    }
}

void PtyProcess::flushWriteBuffer() {
    if (m_masterFd < 0 || m_writeBuffer.isEmpty()) {
        if (m_writeNotifier != nullptr) {
            m_writeNotifier->setEnabled(false);
        }
        return;
    }

    while (!m_writeBuffer.isEmpty()) {
        const ssize_t count = ::write(m_masterFd,
                                      m_writeBuffer.constData(),
                                      static_cast<std::size_t>(m_writeBuffer.size()));
        if (count > 0) {
            m_writeBuffer.remove(0, static_cast<qsizetype>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            m_writeNotifier->setEnabled(true);
            return;
        }
        emit errorOccurred(systemError("write PTY"));
        m_writeBuffer.clear();
        break;
    }

    if (m_writeNotifier != nullptr) {
        m_writeNotifier->setEnabled(!m_writeBuffer.isEmpty());
    }
}

void PtyProcess::finishFromMaster() {
    const bool wasRunning = isRunning();
    m_readNotifier.reset();
    m_writeNotifier.reset();
    m_writeBuffer.clear();

    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
    if (m_replicaFd >= 0) {
        ::close(m_replicaFd);
        m_replicaFd = -1;
    }

    int exitCode = -1;
    if (m_childPid > 0) {
        int status = 0;
        const auto result = ::waitpid(static_cast<pid_t>(m_childPid), &status, WNOHANG);
        if (result == 0 || (result < 0 && errno == EINTR)) {
            const auto pid = m_childPid;
            if (wasRunning) emit runningChanged(false);
            QTimer::singleShot(10, this, [this, pid] {
                if (m_childPid == pid && m_masterFd < 0) finishFromMaster();
            });
            return;
        }
        if (result > 0) {
            if (WIFEXITED(status)) {
                exitCode = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exitCode = 128 + WTERMSIG(status);
            }
        }
        m_childPid = -1;
    }

    if (wasRunning) {
        emit runningChanged(false);
    }
    emit finished(exitCode);
}

void PtyProcess::setError(QString* destination, const QString& message) {
    if (destination != nullptr) {
        *destination = message;
    }
}

} // namespace bitshell::terminal
