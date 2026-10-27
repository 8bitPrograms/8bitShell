#pragma once

#include <QProcessEnvironment>
#include <QStringList>
#include <QVariantMap>
#include <expected>

namespace bitshell::core {

// All application-managed connections share this policy, including jump hosts.
// No arbitrary SSH options, shell commands, or implicit user config are accepted.
class SshSecurity {
public:
    enum class Transport { Terminal, Sftp, Monitor };
    struct Command { QString program; QStringList arguments; QString target; };
    static std::expected<Command, QString> command(const QVariantMap& profile, Transport transport);
    static std::expected<QVariantMap, QString> quickProfile(const QString& text);
    static QString validateProfile(const QVariantMap& profile);
    static QByteArray configuration();
    static QString configurationPath();
    static QProcessEnvironment environment();
    static bool safeHost(const QString& host);
    static bool safeUser(const QString& user);
    static bool safePath(const QString& path);
};

} // namespace bitshell::core
