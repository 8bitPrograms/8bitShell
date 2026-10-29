#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QProcessEnvironment>

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
    [[nodiscard]] bool canWrite(qsizetype bytes) const;
    void setEraseCharacter(char value);
    [[nodiscard]] char eraseCharacter() const;
    void setInteractiveShell(bool value) { m_interactiveShell = value; }
    // A fresh token belongs to one TerminalView launch. It is never exported
    // to child-process environments; only that shell's prompt may identify it.
    void setShellIntegrationToken(const QString& value) { m_shellIntegrationToken = value; }

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] qint64 processId() const;

signals:
    void dataReceived(const QByteArray& data);
    void runningChanged(bool running);
    void finished(int exitCode);
    void errorOccurred(const QString& message);

private:
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

    int m_masterFd = -1;
    qint64 m_childPid = -1;
    std::unique_ptr<QSocketNotifier> m_readNotifier;
    std::unique_ptr<QSocketNotifier> m_writeNotifier;
    QByteArray m_writeBuffer;
    char m_eraseCharacter = '\x7f';
#endif
    bool m_interactiveShell = false;
    QString m_shellIntegrationToken;
};

} // namespace bitshell::terminal
