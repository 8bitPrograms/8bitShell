#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <expected>

namespace bitshell::core {

// Passwords are local to this application profile and exact direct-SSH account.
// The catalog contains neither a password nor a portable vault reference.
class PasswordStore final {
public:
    [[nodiscard]] static QString backendName();
    [[nodiscard]] static bool available();
    // Empty means ineligible: saved passwords never follow a gateway/jump host.
    [[nodiscard]] static QString binding(const QVariantMap& profile);
    // Nonsecret local presence check. read() still validates the backend value.
    [[nodiscard]] static bool has(const QVariantMap& profile);
    // An empty value means no saved password. Errors never fall back to plaintext.
    [[nodiscard]] static std::expected<QByteArray, QString> read(const QVariantMap& profile);
    [[nodiscard]] static std::expected<void, QString> write(const QVariantMap& profile,
                                                          const QByteArray& password);
    [[nodiscard]] static std::expected<void, QString> remove(const QVariantMap& profile);
};

} // namespace bitshell::core
