#pragma once
#include <bitshell/core/SshSecurity.h>
#include <QRegularExpression>
#include <QVariantMap>

namespace bitshell::terminal {
// A deliberately small local-prompt grammar. Complex SSH commands stay with the
// user's shell unchanged instead of dropping an option, tail or shell operator.
class LocalSshCommand {
public:
    static QVariantMap parse(const QString& command) {
        if (command.isEmpty() || command.size() > 1024) return {};
        static const QRegularExpression allowed(QStringLiteral("\\A[A-Za-z0-9_.@:+\\[\\] -]+\\z"));
        if (!allowed.match(command).hasMatch()) return {};
        const auto words = command.trimmed().split(u' ', Qt::SkipEmptyParts);
        if (words.size() < 2 || (words.front() != QStringLiteral("ssh")
            && words.front() != QStringLiteral("ssh.exe"))) return {};
        QString host, user;
        int port = 0;
        bool options = true;
        for (qsizetype i = 1; i < words.size(); ++i) {
            const auto& word = words[i];
            if (!host.isEmpty()) return {};
            if (options && word == QStringLiteral("--")) { options = false; continue; }
            if (options && word.startsWith(u'-')) {
                const auto option = word.first(2);
                if (option != QStringLiteral("-p") && option != QStringLiteral("-l")) return {};
                QString value = word.sliced(2);
                if (value.isEmpty() && ++i < words.size()) value = words[i];
                if (value.isEmpty()) return {};
                if (option == QStringLiteral("-p")) {
                    static const QRegularExpression digits(QStringLiteral("\\A[0-9]{1,5}\\z"));
                    if (port || !digits.match(value).hasMatch()) return {};
                    port = value.toInt();
                    if (port < 1 || port > 65535) return {};
                } else {
                    if (!user.isEmpty() || !bitshell::core::SshSecurity::safeUser(value)) return {};
                    user = value;
                }
                continue;
            }
            host = word;
            const auto at = host.lastIndexOf(u'@');
            if (at >= 0) {
                if (!user.isEmpty() || at == 0) return {};
                user = host.first(at);
                host = host.sliced(at + 1);
            }
        }
        if (!bitshell::core::SshSecurity::safeHost(host)
            || !bitshell::core::SshSecurity::safeUser(user)) return {};
        return {{QStringLiteral("host"), host}, {QStringLiteral("user"), user},
                {QStringLiteral("port"), port}, {QStringLiteral("command"), command}};
    }
};
}
