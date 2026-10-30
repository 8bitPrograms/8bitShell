#include <bitshell/core/CredentialStore.h>

#include <bitshell/core/PasswordStore.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/SshSecurity.h>

#include <QDir>
#include <QRegularExpression>
#include <QUuid>

namespace bitshell::core {
namespace {
const QString kind = QStringLiteral("8bitshell/credentials");
constexpr qint64 maximumKeyBytes = 48 * 1024;

class SystemVault final : public SecretVault {
public:
    QString name() const override { return PasswordStore::backendName(); }
    bool available() const override { return PasswordStore::available(); }
    std::expected<QByteArray, QString> read(const QString& reference) override {
        return PasswordStore::readSecret(reference);
    }
    std::expected<void, QString> write(const QString& reference, const QByteArray& secret) override {
        return PasswordStore::writeSecret(reference, secret);
    }
    std::expected<void, QString> remove(const QString& reference) override {
        return PasswordStore::removeSecret(reference);
    }
};

QString entry(const QString& reference, const char* part) {
    return reference + u'/' + QLatin1StringView(part);
}

void wipe(QByteArray& bytes) { bytes.fill('\0'); bytes.clear(); }

bool validId(const QString& id) {
    return !id.isEmpty() && !QUuid::fromString(id).isNull();
}

bool validName(const QString& name) {
    static const QRegularExpression pattern(QStringLiteral("\\A[A-Za-z0-9_.@+ -]{1,64}\\z"));
    return pattern.match(name).hasMatch() && name.trimmed() == name;
}

bool validSecretText(const QByteArray& text) {
    return text.size() <= 4096 && !text.contains('\0') && !text.contains('\r') && !text.contains('\n');
}

bool looksLikePrivateKey(const QByteArray& bytes) {
    return bytes.startsWith("-----BEGIN ") && bytes.contains("PRIVATE KEY-----")
        && !bytes.contains('\0');
}

QVariantMap publicView(const QVariantMap& row) {
    return {{QStringLiteral("id"), row.value(QStringLiteral("id"))},
            {QStringLiteral("name"), row.value(QStringLiteral("name"))},
            {QStringLiteral("type"), row.value(QStringLiteral("type"))},
            {QStringLiteral("username"), row.value(QStringLiteral("username"))},
            {QStringLiteral("passphrase"), row.value(QStringLiteral("passphrase"), false).toBool()}};
}
} // namespace

SecretVault& SecretVault::system() {
    static SystemVault vault;
    return vault;
}

QString CredentialStore::defaultPath() {
    return QDir(Platform::appConfigDirectory()).filePath(QStringLiteral("credentials.yaml"));
}

CredentialStore::CredentialStore(QObject* parent)
    : CredentialStore(SecretVault::system(), defaultPath(), parent) {}

CredentialStore::CredentialStore(SecretVault& vault, const QString& path, QObject* parent)
    : QObject(parent), m_vault(vault), m_yaml(path, kind, 1) {
    (void)reload();
}

bool CredentialStore::validate(const QVariantMap& document, QString& error) {
    if (!YamlFile::fields(document, {QStringLiteral("version"), QStringLiteral("kind"), QStringLiteral("credentials")}, error))
        return false;
    QStringList ids, names;
    for (const auto& value : document.value(QStringLiteral("credentials")).toList()) {
        const auto row = value.toMap();
        if (!YamlFile::fields(row, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("type"),
                QStringLiteral("username"), QStringLiteral("keychain"), QStringLiteral("passphrase")}, error))
            return false;
        const QString id = row.value(QStringLiteral("id")).toString();
        const QString name = row.value(QStringLiteral("name")).toString();
        const QString type = row.value(QStringLiteral("type")).toString();
        if (!validId(id) || ids.contains(id)) { error = QStringLiteral("Each credential needs a unique id."); return false; }
        if (!validName(name) || names.contains(name.toCaseFolded())) {
            error = QStringLiteral("Credential names must be unique, 1-64 letters, digits, spaces or _.@+-.");
            return false;
        }
        if (type != QStringLiteral("password") && type != QStringLiteral("key")) {
            error = QStringLiteral("Credential type must be password or key."); return false;
        }
        const QString user = row.value(QStringLiteral("username")).toString();
        if (user.isEmpty() || !SshSecurity::safeUser(user)) {
            error = QStringLiteral("Credential '%1' needs a valid username.").arg(name); return false;
        }
        if (row.value(QStringLiteral("keychain")).toString() != QStringLiteral("credential/") + id) {
            error = QStringLiteral("Credential '%1' has an invalid keychain reference.").arg(name); return false;
        }
        ids << id;
        names << name.toCaseFolded();
    }
    return true;
}

bool CredentialStore::reload() {
    QVariantMap document;
    QString error;
    if (!m_yaml.load(document)) return fail(m_yaml.error());
    if (!validate(document, error)) { m_yaml.reject(error); return fail(error); }
    QVariantList rows;
    for (const auto& value : document.value(QStringLiteral("credentials")).toList())
        rows << publicView(value.toMap());
    m_credentials = rows;
    emit credentialsChanged();
    setLastError({});
    return true;
}

int CredentialStore::indexOf(const QString& id) const {
    for (int index = 0; index < m_credentials.size(); ++index)
        if (m_credentials[index].toMap().value(QStringLiteral("id")).toString() == id) return index;
    return -1;
}

QVariantMap CredentialStore::get(const QString& id) const {
    const int index = indexOf(id);
    return index < 0 ? QVariantMap{} : m_credentials[index].toMap();
}

QString CredentialStore::saveCredential(const QVariantMap& values) {
    QString id = values.value(QStringLiteral("id")).toString();
    const bool creating = id.isEmpty();
    const int index = creating ? -1 : indexOf(id);
    if (!creating && index < 0) { fail(QStringLiteral("That credential no longer exists.")); return {}; }
    const QVariantMap previous = creating ? QVariantMap{} : m_credentials[index].toMap();
    const QString name = values.value(QStringLiteral("name")).toString().trimmed();
    const QString type = creating ? values.value(QStringLiteral("type")).toString()
                                  : previous.value(QStringLiteral("type")).toString();
    const QString username = values.value(QStringLiteral("username")).toString().trimmed();
    if (!creating && values.contains(QStringLiteral("type"))
        && values.value(QStringLiteral("type")).toString() != type) {
        fail(QStringLiteral("Create a new credential to change its type.")); return {};
    }
    if (!m_vault.available()) { fail(QStringLiteral("%1 is not available, so secrets cannot be saved.").arg(m_vault.name())); return {}; }

    QByteArray password = values.value(QStringLiteral("password")).toString().toUtf8();
    QByteArray passphrase = values.value(QStringLiteral("passphrase")).toString().toUtf8();
    QByteArray key;
    const auto cleanup = [&] { wipe(password); wipe(passphrase); wipe(key); };
    if (!validSecretText(password) || !validSecretText(passphrase)) {
        cleanup(); fail(QStringLiteral("Secrets can be up to 4096 bytes, without line breaks.")); return {};
    }
    if (type == QStringLiteral("password") && creating && password.isEmpty()) {
        cleanup(); fail(QStringLiteral("Enter the password to save.")); return {};
    }
    const QString keyFile = YamlFile::localPath(values.value(QStringLiteral("keyFile")).toString());
    if (type == QStringLiteral("key")) {
        if (!keyFile.isEmpty()) {
            QString error;
            if (!PrivateFile::read(keyFile, key, error, maximumKeyBytes)) {
                cleanup(); fail(QStringLiteral("Cannot read the private key: %1").arg(error)); return {};
            }
            if (!looksLikePrivateKey(key)) {
                cleanup(); fail(QStringLiteral("Choose an OpenSSH or PEM private key. Convert PuTTY keys in the SSH key library first."));
                return {};
            }
            if (!key.endsWith('\n')) key.append('\n');
        } else if (creating) {
            cleanup(); fail(QStringLiteral("Choose the private key file.")); return {};
        }
    }

    if (creating) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString reference = QStringLiteral("credential/") + id;
    const bool clearPassphrase = values.value(QStringLiteral("clearPassphrase")).toBool();
    bool hasPassphrase = previous.value(QStringLiteral("passphrase"), false).toBool();
    if (type == QStringLiteral("key")) hasPassphrase = !passphrase.isEmpty() || (hasPassphrase && !clearPassphrase && keyFile.isEmpty());
    QVariantMap row{{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
                    {QStringLiteral("type"), type}, {QStringLiteral("username"), username},
                    {QStringLiteral("keychain"), reference}};
    if (type == QStringLiteral("key")) row[QStringLiteral("passphrase")] = hasPassphrase;

    QVariantMap document;
    QVariantList rows;
    {
        QVariantMap current;
        if (!m_yaml.load(current)) { cleanup(); fail(m_yaml.error()); return {}; }
        rows = current.value(QStringLiteral("credentials")).toList();
    }
    bool replaced = false;
    for (auto& value : rows) {
        if (value.toMap().value(QStringLiteral("id")).toString() == id) { value = row; replaced = true; }
    }
    if (!replaced) rows << row;
    document[QStringLiteral("credentials")] = rows;
    QString error;
    if (!validate(document, error)) { cleanup(); fail(error); return {}; }

    // Secrets first: a failed keychain write must not leave a usable-looking entry.
    std::expected<void, QString> stored;
    if (!password.isEmpty()) stored = m_vault.write(entry(reference, "password"), password);
    if (stored && !key.isEmpty()) stored = m_vault.write(entry(reference, "key"), key);
    if (stored && !passphrase.isEmpty()) stored = m_vault.write(entry(reference, "passphrase"), passphrase);
    else if (stored && type == QStringLiteral("key") && !hasPassphrase) stored = m_vault.remove(entry(reference, "passphrase"));
    cleanup();
    if (!stored) {
        if (creating) for (const char* part : {"password", "key", "passphrase"}) (void)m_vault.remove(entry(reference, part));
        fail(stored.error());
        return {};
    }
    if (!m_yaml.save(document)) {
        if (creating) for (const char* part : {"password", "key", "passphrase"}) (void)m_vault.remove(entry(reference, part));
        fail(m_yaml.error());
        return {};
    }
    QVariantList visible;
    for (const auto& value : rows) visible << publicView(value.toMap());
    m_credentials = visible;
    emit credentialsChanged();
    setLastError({});
    return id;
}

bool CredentialStore::removeCredential(const QString& id) {
    if (indexOf(id) < 0) return fail(QStringLiteral("That credential no longer exists."));
    QVariantMap current;
    if (!m_yaml.load(current)) return fail(m_yaml.error());
    QVariantList rows = current.value(QStringLiteral("credentials")).toList();
    rows.removeIf([&](const QVariant& value) { return value.toMap().value(QStringLiteral("id")).toString() == id; });
    current[QStringLiteral("credentials")] = rows;
    const QString reference = QStringLiteral("credential/") + id;
    for (const char* part : {"password", "key", "passphrase"}) {
        const auto removed = m_vault.remove(entry(reference, part));
        if (!removed) return fail(removed.error());
    }
    if (!m_yaml.save(current)) return fail(m_yaml.error());
    QVariantList visible;
    for (const auto& value : rows) visible << publicView(value.toMap());
    m_credentials = visible;
    emit credentialsChanged();
    setLastError({});
    return true;
}

std::expected<CredentialStore::Resolved, QString>
CredentialStore::resolve(const QString& id, SecretVault& vault, const QString& path) {
    if (!validId(id)) return std::unexpected(QStringLiteral("The session refers to an invalid credential."));
    QByteArray bytes;
    QString error;
    QVariantMap document;
    if (!PrivateFile::read(path, bytes, error) || !YamlFile::decode(bytes, kind, document, error, 1)
        || !validate(document, error))
        return std::unexpected(QStringLiteral("Credentials are unavailable: %1").arg(error));
    QVariantMap row;
    for (const auto& value : document.value(QStringLiteral("credentials")).toList())
        if (value.toMap().value(QStringLiteral("id")).toString() == id) row = value.toMap();
    if (row.isEmpty()) return std::unexpected(QStringLiteral("The credential for this connection was deleted."));
    Resolved result;
    result.id = id;
    result.name = row.value(QStringLiteral("name")).toString();
    result.type = row.value(QStringLiteral("type")).toString();
    result.secrets.username = row.value(QStringLiteral("username")).toString().toUtf8();
    const QString reference = row.value(QStringLiteral("keychain")).toString();
    const auto read = [&](const char* part, QByteArray& target, bool required) -> std::expected<void, QString> {
        auto value = vault.read(entry(reference, part));
        if (!value) return std::unexpected(value.error());
        if (required && value->isEmpty())
            return std::unexpected(QStringLiteral("The secret for credential '%1' is missing from %2.").arg(result.name, vault.name()));
        target = std::move(*value);
        return {};
    };
    std::expected<void, QString> loaded;
    if (result.type == QStringLiteral("password")) {
        loaded = read("password", result.secrets.password, true);
    } else {
        loaded = read("key", result.privateKey, true);
        if (loaded && row.value(QStringLiteral("passphrase")).toBool())
            loaded = read("passphrase", result.secrets.passphrase, true);
    }
    if (!loaded) {
        result.secrets.wipe();
        wipe(result.privateKey);
        result.secretError = loaded.error();
        // Keep the username: a fallback login still uses the right account.
        result.secrets.username = row.value(QStringLiteral("username")).toString().toUtf8();
    }
    return result;
}

bool CredentialStore::fail(const QString& error) {
    setLastError(error.isEmpty() ? QStringLiteral("The credential could not be saved.") : error);
    return false;
}

void CredentialStore::setLastError(const QString& error) {
    if (m_lastError == error) return;
    m_lastError = error;
    emit lastErrorChanged();
}

} // namespace bitshell::core
