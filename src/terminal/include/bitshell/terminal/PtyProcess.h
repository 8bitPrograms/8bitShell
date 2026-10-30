#pragma once

#include <QObject>
#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringList>
#include <QProcessEnvironment>

#include <bitshell/core/CredentialFlow.h>

#include <memory>

class QSocketNotifier;

namespace bitshell::terminal {

class PtyProcess final : public QObject {
    Q_OBJECT

public:
    explicit PtyProcess(QObject* parent = nullptr);
    ~PtyProcess() override;

    PtyProcess(const PtyProcess&) = delete;
    PtyProcess& operator=(const PtyProcess&) = delete;

    [[nodiscard]] bool start(const QString& program,
                             const QStringList& arguments,
                             int columns,
                             int rows,
                             QString* errorMessage = nullptr,
                             const QString& workingDirectory = {},
                             const QProcessEnvironment& environmentOverrides = {});
    void stop();
    void resize(int columns, int rows);
    void write(QByteArray data);
    // Queues a secret (a typed password). The queue keeps it in one block and
    // zeroes every byte once written or dropped. Later write()s into the same
    // queue are handled the same way until it drains; other input has no cost.
    // requireSecretRead (POSIX): queue nothing and return false unless the mode,
    // read again now, is SecretRead and no earlier input is still queued.
    // A part the PTY can't take at once is written later, unchecked.
    // Windows ignores it. False also when nothing could be queued.
    bool writeSecret(QByteArrayView data, bool requireSecretRead = false);
    [[nodiscard]] bool canWrite(qsizetype bytes) const;
    void setEraseCharacter(char value);
    [[nodiscard]] char eraseCharacter() const;
    void setInteractiveShell(bool value) { m_interactiveShell = value; }
    // A fresh token belongs to one TerminalView launch. It is never exported
    // to child-process environments; only that shell's prompt may identify it.
    void setShellIntegrationToken(const QString& value) { m_shellIntegrationToken = value; }

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] qint64 processId() const;
    // While on, each read samples the terminal's mode before dataReceived is
    // emitted (one tcgetattr per read). Windows can't tell: Unavailable.
    void setSampleMode(bool on);
    // The mode sampled at the read behind the latest dataReceived; Failed
    // while sampling is off.
    [[nodiscard]] core::TtyMode lastReadMode() const;
    // The mode now.
    [[nodiscard]] core::TtyMode readMode() const;

signals:
    void dataReceived(const QByteArray& data);
    void runningChanged(bool running);
    void finished(int exitCode);
    void errorOccurred(const QString& message);

private:
    friend class PtyProcessTestAccess;
    static void setError(QString* destination, const QString& message);
#ifdef Q_OS_WIN
    struct WindowsState;
    std::shared_ptr<WindowsState> m_windows;
    void drainWindows(const std::shared_ptr<WindowsState>& state);
    char m_eraseCharacter = '\x7f';
#else
    void handleReadyRead();
    void flushWriteBuffer();
    void finishFromMaster();
    void discardWriteBuffer();

    int m_masterFd = -1;
    int m_replicaFd = -1; // macOS only: see PtyProcess::start()
    qint64 m_childPid = -1;
    std::unique_ptr<QSocketNotifier> m_readNotifier;
    std::unique_ptr<QSocketNotifier> m_writeNotifier;
    QByteArray m_writeBuffer;
    bool m_secretQueued = false; // m_writeBuffer holds secret bytes
    char m_eraseCharacter = '\x7f';
    bool m_sampleMode = false;
    core::TtyMode m_lastReadMode = core::TtyMode::Failed;
#endif
    bool m_interactiveShell = false;
    QString m_shellIntegrationToken;
};

} // namespace bitshell::terminal
