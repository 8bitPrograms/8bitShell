#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <expected>

#include <bitshell/core/YamlFile.h>

namespace bitshell::core {

// A small, private key library. keys.yaml contains metadata only; private key
// material is copied into a 0600/DACL-protected directory and is never exported.
class SshKeyStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList keys READ keys NOTIFY keysChanged)
    Q_PROPERTY(QString defaultKeyId READ defaultKeyId NOTIFY defaultKeyIdChanged)
    Q_PROPERTY(QString cyberArkKeyId READ cyberArkKeyId NOTIFY cyberArkKeyIdChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(bool ppkAvailable READ ppkAvailable CONSTANT)

public:
    explicit SshKeyStore(QObject* parent = nullptr);

    [[nodiscard]] QVariantList keys() const { return m_keys; }
    [[nodiscard]] QString defaultKeyId() const { return m_defaultKeyId; }
    [[nodiscard]] QString cyberArkKeyId() const { return m_cyberArkKeyId; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] QString storagePath() const { return m_yaml.fileName(); }
    [[nodiscard]] bool ppkAvailable() const;

    // Returns a stable opaque key id, or an empty string on failure.
    Q_INVOKABLE QString importKey(const QString& source, const QString& name = {},
                                  const QString& ppkPassphrase = {});
    Q_INVOKABLE bool replaceKey(const QString& id, const QString& source,
                                const QString& ppkPassphrase = {});
    Q_INVOKABLE bool removeKey(const QString& id);
    Q_INVOKABLE bool setDefaultKey(const QString& id);
    Q_INVOKABLE bool setCyberArkKey(const QString& id);
    Q_INVOKABLE QVariantMap get(const QString& id) const;
    Q_INVOKABLE bool reload();

    // Resolves a session key reference and application defaults immediately
    // before every SSH/SFTP/monitor process is constructed. A broken reference
    // is an authentication error, never a silent password/direct fallback.
    [[nodiscard]] static std::expected<QVariantMap, QString>
    applyDefaults(const QVariantMap& profile);

signals:
    void keysChanged();
    void defaultKeyIdChanged();
    void cyberArkKeyIdChanged();
    void lastErrorChanged();

private:
    struct ImportedKey {
        QByteArray bytes;
        QString format;
    };

    [[nodiscard]] static std::expected<ImportedKey, QString>
    readAndConvert(const QString& source, const QString& ppkPassphrase);
    [[nodiscard]] static std::expected<QVariantMap, QString>
    inspectManagedKey(const QString& path);
    [[nodiscard]] static bool validateDocument(const QVariantMap& document,
                                               QString& error);
    [[nodiscard]] static QString managedPath(const QString& fileName);
    [[nodiscard]] QVariantMap document() const;
    [[nodiscard]] bool save();
    [[nodiscard]] int indexOf(const QString& id) const;
    void setLastError(const QString& error);

    QVariantList m_keys;
    QString m_defaultKeyId;
    QString m_cyberArkKeyId;
    QString m_lastError;
    YamlFile m_yaml;
};

} // namespace bitshell::core
