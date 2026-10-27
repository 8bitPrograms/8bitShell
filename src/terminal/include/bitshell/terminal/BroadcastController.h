#pragma once
#include <QObject>
#include <QPointer>
#include <QElapsedTimer>
#include <QVariantList>
#include <bitshell/terminal/TerminalView.h>

namespace bitshell::terminal {
// One explicit review authorizes one bounded send to these exact PTY generations.
class BroadcastController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString command READ command NOTIFY changed)
    Q_PROPERTY(QStringList targets READ targets NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
public:
    explicit BroadcastController(QObject* parent = nullptr) : QObject(parent) {}
    QString command() const { return m_command; }
    QStringList targets() const { return m_targets; }
    QString status() const { return m_status; }
    bool pending() const { return !m_entries.isEmpty(); }
    Q_INVOKABLE bool prepare(const QString& command, const QVariantList& terminals);
    Q_INVOKABLE bool commit();
    Q_INVOKABLE void cancel();
signals:
    void changed();
private:
    struct Entry { QPointer<TerminalView> terminal; quint64 generation; };
    QList<Entry> m_entries;
    QString m_command, m_status;
    QStringList m_targets;
    QElapsedTimer m_reviewAge;
};
}
