#pragma once

#include <QObject>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>

namespace bitshell::core {
// Each launch is a separate process, with independent terminals and the same
// profile. YamlFile's short write locks/conflict checks protect shared settings.
class WindowLauncher : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    using QObject::QObject;
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE bool newWindow() {
        QProcess process;
        process.setProgram(QCoreApplication::applicationFilePath());
        process.setArguments({QStringLiteral("--working-directory"), QDir::currentPath()});
        process.setWorkingDirectory(QDir::currentPath());
        const bool started = process.startDetached();
        m_lastError = started ? QString{} : process.errorString();
        emit lastErrorChanged();
        return started;
    }
signals:
    void lastErrorChanged();
private:
    QString m_lastError;
};
} // namespace bitshell::core
