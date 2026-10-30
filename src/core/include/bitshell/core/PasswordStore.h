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

    // Opaque secrets for credential profiles, addressed by a nonsecret
    // reference. Any bytes up to 64 KiB; stored base64 in backend-sized chunks.
    // An empty result means no secret is saved under the reference.
    [[nodiscard]] static std::expected<QByteArray, QString> readSecret(const QString& reference);
    [[nodiscard]] static std::expected<void, QString> writeSecret(const QString& reference,
                                                                const QByteArray& secret);
    [[nodiscard]] static std::expected<void, QString> removeSecret(const QString& reference);

    // Tests only: an in-process stand-in for the OS keychain, so keychain
    // paths run in CI without a Secret Service. The app never installs one.
    // Ignored on Windows, whose DPAPI backend needs no service. Pass nullptr
    // to restore the OS keychain.
    struct TestKeychain {
        virtual ~TestKeychain() = default;
        virtual std::expected<QByteArray, QString> read(const QString& slot) = 0;
        virtual std::expected<void, QString> write(const QString& slot, const QByteArray& secret) = 0;
        virtual std::expected<void, QString> remove(const QString& slot) = 0;
    };
    static void useTestKeychain(TestKeychain* keychain);
};

} // namespace bitshell::core
