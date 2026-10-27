#pragma once
#include <QByteArray>
#include <QString>
#include <QVariantMap>

namespace bitshell::core {
// A versioned, atomic file with optimistic concurrency protection. A failed load
// locks writes until a successful reload, so invalid user edits cannot be erased.
class YamlFile {
public:
    YamlFile(QString path, QString kind, int version = 1);
    bool load(QVariantMap& document);
    bool save(const QVariantMap& document);
    bool importFile(const QString& source, QVariantMap& document);
    bool exportFile(const QString& destination, const QVariantMap& document);
    void reject(const QString& error);
    QString fileName() const { return m_path; }
    QString error() const { return m_error; }
    bool writable() const { return !m_readOnly; }
    static QString localPath(const QString& source);
    static QByteArray encode(const QVariantMap& document, const QString& kind, int version = 1);
    static bool decode(const QByteArray& bytes, const QString& kind,
                       QVariantMap& document, QString& error, int maximumVersion = 1);
    static bool fields(const QVariantMap& values, const QStringList& allowed, QString& error);
private:
    static bool read(const QString& path, QByteArray& bytes, QString& error);
    static bool write(const QString& path, const QByteArray& bytes, QString& error);
    QString m_path;
    QString m_kind;
    int m_version = 1;
    QString m_error;
    QString m_loadError;
    QByteArray m_digest;
    bool m_existed = false;
    bool m_readOnly = true;
};
}
