#include <bitshell/terminal/PtyProcess.h>
#include <bitshell/core/Platform.h>
#include <utility>
#include <bitshell/core/SshSecurity.h>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcessEnvironment>
#include <windows.h>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace bitshell::terminal {
namespace {
constexpr qsizetype inputQueueLimit = 1024 * 1024;
constexpr qsizetype outputQueueLimit = 256 * 1024;
constexpr qsizetype outputBatchSize = 64 * 1024;
QString windowsError(const char* operation) {
    return QStringLiteral("%1 failed (Windows error %2).").arg(QString::fromLatin1(operation)).arg(GetLastError());
}
}

struct PtyProcess::WindowsState {
    HANDLE input = nullptr, output = nullptr, process = nullptr, job = nullptr;
    HPCON pseudo = nullptr;
    DWORD pid = 0;
    std::mutex mutex, apiMutex;
    std::condition_variable changed;
    QByteArray received, pendingInput;
    qsizetype inFlightInput = 0;
    QString error;
    bool closing = false, completed = false, notified = false;
    int exitCode = -1;
    PtyProcess* owner = nullptr; // Only read/write while holding mutex.
    ~WindowsState() {
        if (pseudo) ClosePseudoConsole(pseudo);
        for (HANDLE handle : {input, output, process, job}) if (handle) CloseHandle(handle);
    }
    // Caller holds mutex. stop() clears owner under the same lock before the
    // QObject can be destroyed; queued callbacks also have the QObject context.
    void notify(const std::shared_ptr<WindowsState>& self) {
        if (!owner || notified) return;
        notified = true;
        auto* receiver = owner;
        QMetaObject::invokeMethod(receiver, [receiver, self] { receiver->drainWindows(self); }, Qt::QueuedConnection);
    }
};

PtyProcess::PtyProcess(QObject* parent) : QObject(parent) {}
PtyProcess::~PtyProcess() { stop(); }

bool PtyProcess::start(const QString& program, const QStringList& arguments, int columns, int rows, QString* errorMessage,
                       const QString& workingDirectory, const QProcessEnvironment& environmentOverrides) {
    if (!workingDirectory.isEmpty() && (workingDirectory.contains(QChar::Null)
        || !QDir::isAbsolutePath(workingDirectory) || !QFileInfo(workingDirectory).isDir())) {
        setError(errorMessage, QStringLiteral("The terminal starting directory is invalid or no longer exists.")); return false;
    }
    stop();
    // cmd.exe also parses argv[0] itself and rejects forward-slash executable paths.
    const auto launchArguments = m_interactiveShell
        ? core::Platform::persistentHistoryArguments(program, arguments, m_shellIntegrationToken) : arguments;
    const QString encoded = core::Platform::windowsCommandLine(QDir::toNativeSeparators(program), launchArguments);
    if (encoded.isEmpty() || !QDir::isAbsolutePath(program) || !QFileInfo(program).isFile()
        || !program.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        setError(errorMessage, QStringLiteral("Use an absolute Windows executable and valid literal arguments (maximum command line 32766 UTF-16 units)."));
        return false;
    }
    auto state = std::make_shared<WindowsState>();
    HANDLE childInput = nullptr, childOutput = nullptr;
    if (!CreatePipe(&childInput, &state->input, nullptr, 65536) || !CreatePipe(&state->output, &childOutput, nullptr, 65536)) {
        setError(errorMessage, windowsError("CreatePipe"));
        if (childInput) CloseHandle(childInput);
        if (childOutput) CloseHandle(childOutput);
        return false;
    }
    const COORD size{SHORT(std::clamp(columns, 2, 1000)), SHORT(std::clamp(rows, 2, 1000))};
    const HRESULT created = CreatePseudoConsole(size, childInput, childOutput, 0, &state->pseudo);
    CloseHandle(childInput); CloseHandle(childOutput);
    if (FAILED(created)) {
        setError(errorMessage, QStringLiteral("CreatePseudoConsole failed (0x%1).").arg(quint32(created), 8, 16, QLatin1Char('0'))); return false;
    }
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<unsigned char> attributes(attributeBytes);
    auto* attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeBytes)) { setError(errorMessage, windowsError("Initialize attributes")); return false; }
    const bool attributed = UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                                      state->pseudo, sizeof(state->pseudo), nullptr, nullptr);
    if (!attributed) { setError(errorMessage, windowsError("Attach ConPTY")); DeleteProcThreadAttributeList(attributeList); return false; }
    state->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!state->job || !SetInformationJobObject(state->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        setError(errorMessage, windowsError("Create session job")); DeleteProcThreadAttributeList(attributeList); return false;
    }
    auto environment = core::Platform::isManagedSsh(program) ? core::SshSecurity::environment() : QProcessEnvironment::systemEnvironment();
    environment.insert(environmentOverrides);
    environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    environment.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    environment.insert(QStringLiteral("TERM_PROGRAM"), QStringLiteral("bitshell"));
    auto entries = environment.toStringList();
    std::sort(entries.begin(), entries.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    std::wstring environmentBlock;
    for (const auto& entry : entries) { environmentBlock += entry.toStdWString(); environmentBlock.push_back(L'\0'); }
    environmentBlock.push_back(L'\0');
    auto commandLine = encoded.toStdWString();
    const auto executable = QDir::toNativeSeparators(program).toStdWString();
    const auto directory = QDir::toNativeSeparators(workingDirectory.isEmpty() ? QDir::currentPath() : workingDirectory).toStdWString();
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    // Null standard handles plus this flag request fresh ConPTY connections.
    // Without it Windows may duplicate a launcher's redirected stdout/stdin
    // even with bInheritHandles=FALSE, leaking output outside the terminal.
    // https://github.com/microsoft/terminal/discussions/15814
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.lpAttributeList = attributeList;
    PROCESS_INFORMATION process{};
    // FALSE is intentional: no application's file, log, token, or other
    // session handle is inherited. Only ConPTY is passed through its attribute.
    const bool started = CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
        environmentBlock.data(), directory.c_str(), &startup.StartupInfo, &process);
    const DWORD startError = GetLastError();
    DeleteProcThreadAttributeList(attributeList);
    if (!started) { SetLastError(startError); setError(errorMessage, windowsError("CreateProcess")); return false; }
    state->process = process.hProcess;
    state->pid = process.dwProcessId;
    if (!AssignProcessToJobObject(state->job, state->process)) {
        setError(errorMessage, windowsError("Assign session job"));
        TerminateProcess(state->process, 1); CloseHandle(process.hThread); return false;
    }
    if (ResumeThread(process.hThread) == DWORD(-1)) {
        setError(errorMessage, windowsError("Resume terminal process"));
        TerminateJobObject(state->job, 1); CloseHandle(process.hThread); return false;
    }
    CloseHandle(process.hThread);
    state->owner = this;
    m_windows = state;

    // Each synchronous pipe has a dedicated worker. Backpressure bounds memory;
    // the UI never waits for ReadFile, WriteFile, process exit, or ConPTY teardown.
    std::thread reader([state] {
        char buffer[32768]; DWORD count = 0;
        while (ReadFile(state->output, buffer, sizeof(buffer), &count, nullptr) && count) {
            std::unique_lock lock(state->mutex);
            // Natural completion can set closing before the reader has consumed
            // ConPTY's final frame. Keep delivering bounded output while the
            // owner is alive; an explicit stop clears owner and drains/discards.
            state->changed.wait(lock, [&] { return !state->owner || state->received.size() + qsizetype(count) <= outputQueueLimit; });
            if (state->owner) {
                state->received.append(buffer, qsizetype(count));
                state->notify(state);
            }
            // After stop we still drain output; ClosePseudoConsole may write a
            // final frame and would deadlock if this pipe were abandoned.
        }
    });
    std::thread writer([state] {
        for (;;) {
            QByteArray bytes;
            {
                std::unique_lock lock(state->mutex);
                state->changed.wait(lock, [&] { return state->closing || !state->pendingInput.isEmpty(); });
                if (state->closing) break;
                bytes = state->pendingInput.left(32768);
                state->pendingInput.remove(0, bytes.size());
                state->inFlightInput = bytes.size();
            }
            qsizetype offset = 0;
            while (offset < bytes.size()) {
                DWORD written = 0;
                if (!WriteFile(state->input, bytes.constData() + offset, DWORD(bytes.size() - offset), &written, nullptr) || !written) {
                    std::lock_guard lock(state->mutex);
                    if (!state->closing) { state->error = windowsError("Write terminal input"); state->notify(state); }
                    state->inFlightInput = 0;
                    return;
                }
                offset += qsizetype(written);
            }
            std::lock_guard lock(state->mutex);
            state->inFlightInput = 0;
        }
    });
    std::thread([state, reader = std::move(reader), writer = std::move(writer)]() mutable {
        WaitForSingleObject(state->process, INFINITE);
        DWORD code = 1; GetExitCodeProcess(state->process, &code);
        // The session is finished when its root exits. Stop remaining children
        // before ConPTY shutdown so they cannot keep producing output forever.
        TerminateJobObject(state->job, code);
        {
            std::lock_guard apiLock(state->apiMutex);
            ClosePseudoConsole(state->pseudo); state->pseudo = nullptr;
        }
        {
            std::lock_guard lock(state->mutex);
            state->closing = true;
        }
        state->changed.notify_all();
        CancelSynchronousIo(writer.native_handle());
        writer.join(); reader.join();
        {
            std::lock_guard lock(state->mutex);
            state->exitCode = int(code);
            state->completed = true;
            state->notify(state);
        }
    }).detach();
    emit runningChanged(true);
    return true;
}

void PtyProcess::stop() {
    auto state = std::exchange(m_windows, {});
    if (!state) return;
    {
        std::lock_guard lock(state->mutex);
        state->owner = nullptr;
        state->closing = true;
        state->received.clear(); state->pendingInput.clear();
    }
    state->changed.notify_all();
    TerminateJobObject(state->job, 1);
    emit runningChanged(false);
}
void PtyProcess::resize(int columns, int rows) {
    const auto state = m_windows;
    if (!state) return;
    std::unique_lock apiLock(state->apiMutex, std::try_to_lock);
    if (!apiLock.owns_lock() || !state->pseudo) return;
    const HRESULT result = ResizePseudoConsole(state->pseudo, {SHORT(std::clamp(columns, 2, 1000)), SHORT(std::clamp(rows, 2, 1000))});
    if (FAILED(result)) emit errorOccurred(QStringLiteral("Cannot resize the Windows pseudoconsole."));
}
void PtyProcess::write(QByteArray data) {
    const auto state = m_windows;
    if (!state || data.isEmpty()) return;
    bool rejected = false;
    {
        std::lock_guard lock(state->mutex);
        if (state->closing) return;
        rejected = data.size() > inputQueueLimit - state->pendingInput.size() - state->inFlightInput;
        if (!rejected) state->pendingInput += data;
    }
    if (rejected) emit errorOccurred(QStringLiteral("Terminal input queue limit exceeded; input was not sent."));
    else state->changed.notify_all();
}
bool PtyProcess::canWrite(qsizetype bytes) const {
    const auto state = m_windows;
    if (!state || bytes < 0) return false;
    std::lock_guard lock(state->mutex);
    return !state->closing && bytes <= inputQueueLimit - state->pendingInput.size() - state->inFlightInput;
}
bool PtyProcess::isRunning() const {
    const auto state = m_windows;
    if (!state) return false;
    std::lock_guard lock(state->mutex);
    return !state->closing && !state->completed;
}
qint64 PtyProcess::processId() const { return m_windows ? qint64(m_windows->pid) : -1; }
void PtyProcess::setEraseCharacter(char value) { m_eraseCharacter = value == '\x08' ? '\x08' : '\x7f'; }
char PtyProcess::eraseCharacter() const { return m_eraseCharacter; }
void PtyProcess::setError(QString* destination, const QString& message) { if (destination) *destination = message; }
void PtyProcess::drainWindows(const std::shared_ptr<WindowsState>& state) {
    if (m_windows != state) return;
    QByteArray bytes; QString error; bool finished = false; int exitCode = -1;
    {
        std::lock_guard lock(state->mutex);
        state->notified = false;
        bytes = state->received.left(outputBatchSize);
        state->received.remove(0, bytes.size());
        error = std::exchange(state->error, {});
        finished = state->completed && state->received.isEmpty();
        exitCode = state->exitCode;
        if (!state->received.isEmpty()) state->notify(state);
    }
    state->changed.notify_all();
    if (!bytes.isEmpty()) emit dataReceived(bytes);
    if (!error.isEmpty()) emit errorOccurred(error);
    if (finished && m_windows == state) {
        { std::lock_guard lock(state->mutex); state->owner = nullptr; }
        m_windows.reset();
        emit runningChanged(false); emit this->finished(exitCode);
    }
}
} // namespace bitshell::terminal
