#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <expected>

#include <bitshell/core/CredentialFlow.h>
#include <bitshell/core/YamlFile.h>

namespace bitshell::core {

// Where credential secrets live. The system vault is the OS keychain through
// PasswordStore; tests substitute an in-memory fake.
class SecretVault {
public:
    virtual ~SecretVault() = default;
    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::expected<QByteArray, QString> read(const QString& reference) = 0;
    [[nodiscard]] virtual std::expected<void, QString> write(const QString& reference, const QByteArray& secret) = 0;
    [[nodiscard]] virtual std::expected<void, QString> remove(const QString& reference) = 0;
    [[nodiscard]] static SecretVault& system();
};

// Named login credentials. credentials.yaml holds only id, name, type, username
// and a keychain reference. Passwords, private keys and passphrases are only
// in the vault, and no method returns them to QML.
class CredentialStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList credentials READ credentials NOTIFY credentialsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString backendName READ backendName CONSTANT)

public:
    struct Resolved {
        QString id;
        QString name;
        QString type; // "password" or "key"
        CredentialSecrets secrets;
        QByteArray privateKey; // key type only; caller wipes it
        // Set when the keychain is locked, unavailable or lost the entry. The
        // metadata above is still valid; the secrets are empty.
        QString secretError;
    };

    explicit CredentialStore(QObject* parent = nullptr);
    CredentialStore(SecretVault& vault, const QString& path, QObject* parent = nullptr);

    [[nodiscard]] QVariantList credentials() const { return m_credentials; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] bool available() const { return m_vault.available(); }
    [[nodiscard]] QString backendName() const { return m_vault.name(); }

    // values: id (empty creates), name, type, username, password, keyFile,
    // passphrase, clearPassphrase. Empty secrets on edit keep the saved ones.
    // Returns the id, or empty on failure (see lastError).
    Q_INVOKABLE QString saveCredential(const QVariantMap& values);
    Q_INVOKABLE bool removeCredential(const QString& id);
    Q_INVOKABLE QVariantMap get(const QString& id) const;
    Q_INVOKABLE bool reload();

    [[nodiscard]] static QString defaultPath();
    // Reads metadata and secrets for one connection attempt.
    [[nodiscard]] static std::expected<Resolved, QString>
    resolve(const QString& id, SecretVault& vault, const QString& path = defaultPath());

signals:
    void credentialsChanged();
    void lastErrorChanged();

private:
    [[nodiscard]] static bool validate(const QVariantMap& document, QString& error);
    [[nodiscard]] int indexOf(const QString& id) const;
    bool fail(const QString& error);
    void setLastError(const QString& error);

    SecretVault& m_vault;
    YamlFile m_yaml;
    QVariantList m_credentials;
    QString m_lastError;
};

} // namespace bitshell::core
