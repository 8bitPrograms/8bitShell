#pragma once

#include <QHash>
#include <bitshell/core/PasswordStore.h>

// In-memory stand-in for the OS keychain behind PasswordStore, so saved
// password tests run without a Secret Service. Install it with
// PasswordStore::useTestKeychain. Windows ignores it and keeps DPAPI.
class FakeKeychain final : public bitshell::core::PasswordStore::TestKeychain {
public:
    std::expected<QByteArray, QString> read(const QString& slot) override {
        ++reads;
        if (locked) return std::unexpected(QStringLiteral("The keychain is locked."));
        return entries.value(slot);
    }
    std::expected<void, QString> write(const QString& slot, const QByteArray& secret) override {
        if (locked) return std::unexpected(QStringLiteral("The keychain is locked."));
        entries.insert(slot, secret);
        return {};
    }
    std::expected<void, QString> remove(const QString& slot) override {
        if (locked) return std::unexpected(QStringLiteral("The keychain is locked."));
        entries.remove(slot);
        return {};
    }
    QHash<QString, QByteArray> entries;
    int reads = 0;
    bool locked = false;
};
