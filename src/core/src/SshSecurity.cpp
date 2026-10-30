#include <bitshell/core/SshSecurity.h>
#include <bitshell/core/ConnectionRouting.h>
#include <bitshell/core/PersonalProfile.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/SshKeyStore.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <algorithm>

namespace bitshell::core {
namespace {
QString unbracketed(QString host) {
    if (host.startsWith(u'[') && host.endsWith(u']')) host = host.sliced(1, host.size() - 2);
    return host;
}
bool safeJump(const QString& value) {
    if (value.isEmpty()) return true;
    const auto jumps = value.split(u',');
    if (jumps.size() > 8 || value.size() > 2048) return false;
    for (QString jump : jumps) {
        const auto at = jump.lastIndexOf(u'@');
        if (at >= 0) {
            if (at == 0 || !SshSecurity::safeUser(jump.first(at))) return false;
            jump = jump.sliced(at + 1);
        }
        QString host = jump, port;
        if (jump.startsWith(u'[')) {
            const auto end = jump.indexOf(u']');
            if (end < 0) return false;
            host = jump.first(end + 1);
            if (end + 1 < jump.size()) {
                if (jump[end + 1] != u':') return false;
                port = jump.sliced(end + 2);
                if (port.isEmpty()) return false;
            }
        } else if (jump.contains(u':')) {
            if (jump.count(u':') != 1) return false; // IPv6 jumps require brackets.
            host = jump.section(u':', 0, 0);
            port = jump.section(u':', 1);
            if (port.isEmpty()) return false;
        }
        if (!SshSecurity::safeHost(host)) return false;
        if (!port.isEmpty()) {
            bool ok = false;
            const int number = port.toInt(&ok);
            if (!ok || number < 1 || number > 65535
                || std::any_of(port.cbegin(), port.cend(), [](QChar c) { return c < u'0' || c > u'9'; })) return false;
        }
    }
    return true;
}
int openSshVersion() {
    // Ubuntu 22.04 ships OpenSSH 8.9. Newer keywords are omitted there because
    // OpenSSH rejects the whole policy file on the first unknown option.
    static const int version = [] {
        QProcess process;
        process.start(Platform::sshExecutable(), {QStringLiteral("-V")});
        if (!process.waitForFinished(3000)) return 0;
        const auto banner = QString::fromLatin1(process.readAllStandardError() + process.readAllStandardOutput());
        const int at = banner.indexOf(QStringLiteral("OpenSSH_"));
        if (at < 0) return 0;
        const auto match = QRegularExpression(QStringLiteral(R"((\d+)\.(\d+))")).match(banner.mid(at));
        if (!match.hasMatch()) return 0;
        return match.captured(1).toInt() * 100 + match.captured(2).toInt();
    }();
    return version;
}
}

bool SshSecurity::safeHost(const QString& host) {
    static const QRegularExpression pattern(QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9._:-]*\\z"));
    const QString value = unbracketed(host);
    if (value.contains(u':')) {
        QHostAddress address;
        return value.size() <= 45 && address.setAddress(value) && address.protocol() == QAbstractSocket::IPv6Protocol;
    }
    return !value.isEmpty() && value.size() <= 253 && pattern.match(value).hasMatch();
}
bool SshSecurity::safeUser(const QString& user) {
    static const QRegularExpression pattern(QStringLiteral("\\A[A-Za-z0-9_][A-Za-z0-9_.@+-]*\\z"));
    return user.isEmpty() || (user.size() <= 255 && pattern.match(user).hasMatch());
}
bool SshSecurity::safePath(const QString& path) {
    return path.size() <= 4096 && std::none_of(path.cbegin(), path.cend(), [](QChar c) {
        return c.unicode() < 32 || c.unicode() == 127;
    });
}
QString SshSecurity::validateProfile(const QVariantMap& profile) {
    const QString authenticationMode = profile.value(QStringLiteral("authenticationMode"), QStringLiteral("auto")).toString();
    if (authenticationMode != QStringLiteral("auto") && authenticationMode != QStringLiteral("password"))
        return QStringLiteral("Choose automatic or password SSH authentication.");
    if (profile.contains(QStringLiteral("connection"))) {
        const auto selection = profile.value(QStringLiteral("connection")).toMap();
        QString error;
        if (!ConnectionRouting::validateSelection(selection, error)
            || selection.value(QStringLiteral("mode")) != QStringLiteral("direct"))
            return QStringLiteral("Resolve the session's required connection route before connecting. Direct fallback is blocked.");
    }
    if (profile.contains(QStringLiteral("psmp"))) {
        const auto login = ConnectionRouting::login(profile);
        if (!login) return login.error();
    }
    if (!safeHost(profile.value(QStringLiteral("host")).toString()))
        return QStringLiteral("Use a DNS name or IP address, without SSH options, whitespace or shell characters.");
    if (!safeUser(profile.value(QStringLiteral("user")).toString()))
        return QStringLiteral("SSH user contains unsupported characters. Allowed: letters, numbers, _, ., @, + and -.");
    bool valid = false;
    const int port = profile.value(QStringLiteral("port"), 22).toInt(&valid);
    if (!valid || port < 1 || port > 65535) return QStringLiteral("SSH port must be between 1 and 65535.");
    if (!safeJump(profile.value(QStringLiteral("proxyJump")).toString()))
        return QStringLiteral("Invalid jump host. Use [user@]host[:port], separated by commas; bracket IPv6 addresses.");
    const QString key = profile.value(QStringLiteral("identityFile")).toString();
    if (!key.isEmpty() && (!safePath(key) || key.contains(u'%') || key.contains(u'$')
        || (!QDir::isAbsolutePath(key) && !key.startsWith(QStringLiteral("~/")))))
        return QStringLiteral("Identity file must be an absolute path (or ~/), without control characters or SSH expansion tokens.");
    if (profile.value(QStringLiteral("agentForwarding")).toBool())
        return QStringLiteral("Agent forwarding is disabled by the security policy. Use a jump host without forwarding your agent.");
    if (profile.value(QStringLiteral("trustedX11")).toBool())
        return QStringLiteral("Trusted X11 forwarding is disabled by the security policy.");
    return {};
}

QByteArray SshSecurity::configuration() {
    // A private, generated file is passed with -F. OpenSSH propagates -F to
    // ProxyJump subprocesses; command-line -o alone would not protect each hop.
    // Keep modern upstream crypto defaults (including new PQ KEX), removing
    // legacy families without replacing the complete, evolving algorithm list.
    QByteArray policy = QByteArrayLiteral(
        "Host *\n"
        "  StrictHostKeyChecking yes\n"
        "  HashKnownHosts yes\n"
        "  VerifyHostKeyDNS no\n"
        "  UpdateHostKeys no\n"
        "  NoHostAuthenticationForLocalhost no\n"
        "  KnownHostsCommand none\n"
        "  PermitLocalCommand no\n"
        "  ForwardAgent no\n"
        "  ForwardX11 no\n"
        "  ForwardX11Trusted no\n"
        "  ForwardX11Timeout 120\n"
        "  Tunnel no\n"
        "  ClearAllForwardings yes\n"
        "  ControlMaster no\n"
        "  ControlPath none\n"
        "  ControlPersist no\n");
    if (openSshVersion() >= 905) policy += "  EnableEscapeCommandline no\n";
    policy += QByteArrayLiteral(
        "  EscapeChar none\n"
        "  AddKeysToAgent no\n"
        "  GSSAPIAuthentication no\n"
        "  HostbasedAuthentication no\n"
        "  ConnectionAttempts 1\n"
        "  ConnectTimeout 10\n"
        "  ServerAliveInterval 15\n"
        "  ServerAliveCountMax 3\n"
        "  NumberOfPasswordPrompts 3\n");
    if (openSshVersion() >= 901) policy += "  RequiredRSASize 3072\n";
    policy += QByteArrayLiteral(
        "  Ciphers -*cbc,3des*,arcfour*\n"
        "  MACs -*md5*,*sha1*,*96,umac-64*,hmac-sha2-256,hmac-sha2-512,umac-128@openssh.com\n"
        "  KexAlgorithms -*sha1*\n"
        "  HostKeyAlgorithms -ssh-rsa,ssh-rsa-cert-v01@openssh.com,ssh-dss*\n"
        "  PubkeyAcceptedAlgorithms -ssh-rsa,ssh-rsa-cert-v01@openssh.com,ssh-dss*\n");
    return policy;
}
QString SshSecurity::configurationPath() {
    struct PrivateConfig {
        QTemporaryDir directory{QDir::tempPath() + QStringLiteral("/bitshell-transport-XXXXXX")};
        QString path;
        PrivateConfig() {
            if (!directory.isValid()) return;
            QString error;
            if (!PrivateFile::secureEntry(directory.path(), error)) return;
            const auto candidate = directory.filePath(QStringLiteral("ssh_config"));
            if (PrivateFile::write(candidate, configuration(), error)) path = candidate;
        }
    };
    static const PrivateConfig config;
    return config.path;
}
std::expected<SshSecurity::Command, QString> SshSecurity::command(const QVariantMap& profile, Transport transport) {
    const auto withKeys = SshKeyStore::applyDefaults(profile);
    if (!withKeys) return std::unexpected(withKeys.error());
    const QVariantMap resolvedProfile = *withKeys;
    const auto error = validateProfile(resolvedProfile);
    if (!error.isEmpty()) return std::unexpected(error);
    QVariantMap endpoint = resolvedProfile;
    const bool psmp = resolvedProfile.contains(QStringLiteral("psmp"));
    if (psmp) {
        const auto plan = resolvedProfile.value(QStringLiteral("psmp")).toMap();
        const auto route = plan.value(QStringLiteral("route")).toMap();
        if (!PersonalProfile::isRouteApproved(ConnectionRouting::fingerprint(route)))
            return std::unexpected(QStringLiteral("Review and approve this CyberArk gateway in the application before connecting."));
        if ((transport == Transport::Sftp && !route.value(QStringLiteral("allowSftp")).toBool())
            || (transport == Transport::Monitor && !route.value(QStringLiteral("allowMonitoring")).toBool()))
            return std::unexpected(QStringLiteral("This CyberArk route does not permit this auxiliary protocol. No direct connection was attempted."));
        endpoint[QStringLiteral("host")] = route.value(QStringLiteral("host"));
        endpoint[QStringLiteral("port")] = route.value(QStringLiteral("port"), 22);
        endpoint[QStringLiteral("user")] = *ConnectionRouting::login(resolvedProfile);
    }
    const auto config = configurationPath();
    if (config.isEmpty()) return std::unexpected(QStringLiteral("Cannot create the private SSH policy file. Connection blocked."));
    Command result;
    result.program = transport == Transport::Sftp ? Platform::sftpExecutable() : Platform::sshExecutable();
    if (!QFileInfo(result.program).isExecutable())
        return std::unexpected(QStringLiteral("The system OpenSSH client is not installed at %1.").arg(result.program));
    result.arguments << QStringLiteral("-F") << config;
    const bool password = resolvedProfile.value(QStringLiteral("authenticationMode")) == QStringLiteral("password");
    if (transport != Transport::Terminal)
        result.arguments << (password ? QStringLiteral("-oBatchMode=no") : QStringLiteral("-oBatchMode=yes"));
    // A terminal login can also be keyboard-interactive (password, then a 2FA
    // code the user types). The saved password is typed into the matched
    // password prompt by TerminalView, once. Auxiliary transports have no
    // terminal and answer through the askpass helper, which accepts only
    // OpenSSH's own password prompt, so they stay password-only.
    if (password && transport == Transport::Terminal)
        result.arguments << QStringLiteral("-oPreferredAuthentications=keyboard-interactive,password")
                         << QStringLiteral("-oPubkeyAuthentication=no")
                         << QStringLiteral("-oKbdInteractiveAuthentication=yes")
                         << QStringLiteral("-oNumberOfPasswordPrompts=1");
    else if (password)
        result.arguments << QStringLiteral("-oPreferredAuthentications=password")
                         << QStringLiteral("-oPubkeyAuthentication=no")
                         << QStringLiteral("-oKbdInteractiveAuthentication=no")
                         << QStringLiteral("-oNumberOfPasswordPrompts=1");
    if (transport == Transport::Sftp)
        // Batch mode implies quiet mode unless -N follows it. Keep host-key
        // and authentication failures visible instead of just "Connection closed".
        result.arguments << QStringLiteral("-b") << QStringLiteral("-") << QStringLiteral("-N")
                         << QStringLiteral("-S") << Platform::sshExecutable();
    if (transport == Transport::Monitor) result.arguments << QStringLiteral("-T");
    result.arguments << (transport == Transport::Sftp ? QStringLiteral("-P") : QStringLiteral("-p"))
                     << QString::number(endpoint.value(QStringLiteral("port"), 22).toInt());
    QString key = resolvedProfile.value(QStringLiteral("identityFile")).toString();
    if (key.startsWith(QStringLiteral("~/"))) key = QDir::home().filePath(key.sliced(2));
    if (!key.isEmpty())
        result.arguments << QStringLiteral("-oIdentitiesOnly=yes")
                         << QStringLiteral("-i") << key;
    const QString jump = resolvedProfile.value(QStringLiteral("proxyJump")).toString();
    if (!jump.isEmpty()) result.arguments << QStringLiteral("-J") << jump;
    if (transport == Transport::Terminal && resolvedProfile.value(QStringLiteral("forwardX11")).toBool())
        result.arguments << QStringLiteral("-X");
    if (resolvedProfile.value(QStringLiteral("compression")).toBool()) result.arguments << QStringLiteral("-C");
    const QString user = endpoint.value(QStringLiteral("user")).toString();
    QString host = unbracketed(endpoint.value(QStringLiteral("host")).toString());
    if (transport == Transport::Sftp && host.contains(u':')) host = u'[' + host + u']';
    result.target = (user.isEmpty() ? QString{} : user + u'@') + host;
    if (psmp && transport != Transport::Sftp) {
        result.arguments << QStringLiteral("-l") << user << QStringLiteral("--") << host;
    } else {
        // sftp's destination grammar splits at the last @. Pass one argv element;
        // no shell parsing, URI decoding or percent interpolation is involved.
        result.arguments << QStringLiteral("--") << result.target;
    }
    if (psmp) result.target = resolvedProfile.value(QStringLiteral("host")).toString() + QStringLiteral(" via ") + endpoint.value(QStringLiteral("host")).toString();
    return result;
}

std::expected<QVariantMap, QString> SshSecurity::quickProfile(const QString& text) {
    if (text.size() > 4096 || !safePath(text)) return std::unexpected(QStringLiteral("Invalid or oversized SSH target."));
    auto tokens = QProcess::splitCommand(text.trimmed());
    if (!tokens.isEmpty() && (tokens.first() == QStringLiteral("ssh") || tokens.first() == QStringLiteral("/usr/bin/ssh"))) tokens.removeFirst();
    QVariantMap profile;
    QString target;
    bool options = true;
    for (qsizetype index = 0; index < tokens.size(); ++index) {
        const QString token = tokens[index];
        if (options && token == QStringLiteral("--")) { options = false; continue; }
        if (options && token.startsWith(u'-')) {
            if (token == QStringLiteral("-X")) { profile[QStringLiteral("forwardX11")] = true; continue; }
            if (token == QStringLiteral("-C")) { profile[QStringLiteral("compression")] = true; continue; }
            const QString option = token.first(2);
            const QMap<QString, QString> allowed{{QStringLiteral("-p"), QStringLiteral("port")},
                {QStringLiteral("-l"), QStringLiteral("user")}, {QStringLiteral("-i"), QStringLiteral("identityFile")},
                {QStringLiteral("-J"), QStringLiteral("proxyJump")}};
            if (!allowed.contains(option))
                return std::unexpected(QStringLiteral("Security policy: only -p, -l, -i, -J, -X and -C are allowed. Arbitrary SSH options and remote commands are not accepted."));
            QString value = token.sliced(2);
            if (value.isEmpty() && ++index < tokens.size()) value = tokens[index];
            if (value.isEmpty() || profile.contains(allowed[option])) return std::unexpected(QStringLiteral("Missing or repeated SSH option."));
            profile[allowed[option]] = value;
        } else {
            if (!target.isEmpty()) return std::unexpected(QStringLiteral("Enter one SSH target; remote commands are not accepted."));
            target = token;
        }
    }
    const auto at = target.lastIndexOf(u'@');
    if (at >= 0) {
        if (at == 0 || profile.contains(QStringLiteral("user"))) return std::unexpected(QStringLiteral("Specify the SSH user once."));
        profile[QStringLiteral("user")] = target.first(at);
        target = target.sliced(at + 1);
    }
    profile[QStringLiteral("host")] = target;
    const auto error = validateProfile(profile);
    if (!error.isEmpty()) return std::unexpected(error);
    return profile;
}

QProcessEnvironment SshSecurity::environment() {
    auto env = QProcessEnvironment::systemEnvironment();
    for (const auto* name : {"SSH_ASKPASS", "SSH_ASKPASS_REQUIRE", "SSH_ASKPASS_PROMPT",
                            "BIT8SHELL_ASKPASS_SOCKET", "BIT8SHELL_ASKPASS_TOKEN",
                            "SSH_SK_PROVIDER", "LD_PRELOAD", "LD_LIBRARY_PATH"})
        env.remove(QString::fromLatin1(name));
    env.insert(QStringLiteral("PATH"), Platform::trustedExecutablePath());
    return env;
}
} // namespace bitshell::core
