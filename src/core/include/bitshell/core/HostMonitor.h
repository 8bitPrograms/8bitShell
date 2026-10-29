#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <memory>

namespace bitshell::core {
class PasswordBroker;
class HostMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QVariantMap profile READ profile WRITE setProfile NOTIFY changed)
    Q_PROPERTY(QVariantMap metrics READ metrics NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit HostMonitor(QObject* parent = nullptr);
    ~HostMonitor() override;
    bool enabled() const { return m_enabled; }
    void setEnabled(bool value);
    QVariantMap profile() const { return m_profile; }
    void setProfile(const QVariantMap& value);
    QVariantMap metrics() const { return m_metrics; }
    QString status() const { return m_status; }
    static QByteArray sampleScript();
    void consume(const QByteArray& bytes);
    // Native tests only; never exposed to QML, settings or the command line.
    void setTransportExecutableForTesting(const QString& path) {
        if (m_process.state() == QProcess::NotRunning) m_testTransport = path;
    }
signals:
    void changed();
private:
    void start();
    void stop();
    void finishSample();
#ifdef Q_OS_WIN
    void sampleLocalWindows();
#endif
    QProcess m_process;
    std::unique_ptr<PasswordBroker> m_passwordBroker;
    QTimer m_timeout;
#ifdef Q_OS_WIN
    QTimer m_localTimer;
    bool m_lastNetworkAvailable = false;
#endif
    bool m_enabled = false;
    bool m_stopping = false;
    QVariantMap m_profile, m_metrics;
    QString m_status;
    QString m_testTransport;
    QByteArray m_buffer, m_frame, m_error;
    double m_lastTotal = -1, m_lastIdle = 0, m_lastRx = 0, m_lastTx = 0, m_lastUptime = 0;
};
}
