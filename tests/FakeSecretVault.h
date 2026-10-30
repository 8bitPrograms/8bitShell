#pragma once

#include <QHash>
#include <bitshell/core/CredentialStore.h>

// In-memory keychain for tests. Counts reads so tests can prove when the
// keychain is touched.
class FakeSecretVault final : public bitshell::core::SecretVault {
public:
    QString name() const override { return QStringLiteral("Test keychain"); }
    bool available() const override { return usable; }
    std::expected<QByteArray, QString> read(const QString& reference) override {
        ++reads;
        if (failing) return std::unexpected(QStringLiteral("keychain locked"));
        return entries.value(reference);
    }
    std::expected<void, QString> write(const QString& reference, const QByteArray& secret) override {
        if (failing) return std::unexpected(QStringLiteral("keychain locked"));
        entries.insert(reference, secret);
        return {};
    }
    std::expected<void, QString> remove(const QString& reference) override {
        if (failing) return std::unexpected(QStringLiteral("keychain locked"));
        entries.remove(reference);
        return {};
    }
    QHash<QString, QByteArray> entries;
    int reads = 0;
    bool usable = true;
    bool failing = false;
};
