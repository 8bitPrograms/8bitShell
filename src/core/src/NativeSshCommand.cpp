#include <bitshell/core/NativeSshCommand.h>

#include <bitshell/core/ConnectionRouting.h>
#include <bitshell/core/PersonalProfile.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/SshKeyStore.h>
#include <bitshell/core/SshSecurity.h>

#include <QRegularExpression>
#include <QStringList>

namespace bitshell::core {
namespace {

QString quote(const QString& value, bool powershell) {
    QString escaped = value;
    escaped.replace(u'\'', powershell ? QStringLiteral("''") : QStringLiteral("'\\''"));
    return u'\'' + escaped + u'\'';
}

bool windowsPath(const QString& path) {
    static const QRegularExpression drive(QStringLiteral("\\A[A-Za-z]:[/\\\\]"));
    return drive.match(path).hasMatch() || path.startsWith(QStringLiteral("\\\\"))
        || path.startsWith(QStringLiteral("//"));
}

QString nativeKeyExpression(const QString& key, bool powershell) {
    if (!key.startsWith(QStringLiteral("~/"))) return quote(key, powershell);
    // Resolve ~ in the environment that will run SSH, not the GUI process.
    return powershell ? QStringLiteral("(Join-Path $HOME %1)").arg(quote(key.sliced(2), true))
                      : QStringLiteral("\"$HOME\"/%1").arg(quote(key.sliced(2), false));
}

QString unbracketed(QString host) {
    if (host.startsWith(u'[') && host.endsWith(u']')) host = host.sliced(1, host.size() - 2);
    return host;
}

} // namespace

std::expected<QString, QString> NativeSshCommand::command(const QVariantMap& profile,
                                                         const QString& shellKind,
                                                         const QProcessEnvironment& askpassEnvironment) {
    const bool powershell = shellKind == QStringLiteral("powershell");
    const bool wsl = shellKind == QStringLiteral("wsl-bash");
    if (!powershell && !wsl && shellKind != QStringLiteral("bash"))
        return std::unexpected(QStringLiteral("This local shell does not support saved SSH command assistance."));

    const auto resolved = SshKeyStore::applyDefaults(profile);
    if (!resolved) return std::unexpected(resolved.error());
    const QString validation = SshSecurity::validateProfile(*resolved);
    if (!validation.isEmpty()) return std::unexpected(validation);

    const bool cyberArk = resolved->contains(QStringLiteral("psmp"));
    const bool password = resolved->value(QStringLiteral("authenticationMode")) == QStringLiteral("password");
    if (!askpassEnvironment.isEmpty()) {
        if (!password || cyberArk || !resolved->value(QStringLiteral("proxyJump")).toString().isEmpty())
            return std::unexpected(QStringLiteral("Saved passwords are limited to direct password connections."));
        if (wsl)
            return std::unexpected(QStringLiteral("Enter the password directly in WSL; Windows saved-password delivery is unavailable in this terminal."));
        const QStringList permitted{QStringLiteral("SSH_ASKPASS"), QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("SSH_ASKPASS_PROMPT"),
            QStringLiteral("BIT8SHELL_ASKPASS_SOCKET"), QStringLiteral("BIT8SHELL_ASKPASS_TOKEN"), QStringLiteral("LC_ALL")};
        for (const auto& name : askpassEnvironment.keys()) {
            if (!permitted.contains(name) || !SshSecurity::safePath(askpassEnvironment.value(name)))
                return std::unexpected(QStringLiteral("Invalid SSH password-helper environment."));
        }
        if (askpassEnvironment.value(QStringLiteral("SSH_ASKPASS_REQUIRE")) != QStringLiteral("force")
            || askpassEnvironment.value(QStringLiteral("SSH_ASKPASS")).isEmpty()
            || askpassEnvironment.value(QStringLiteral("BIT8SHELL_ASKPASS_SOCKET")).isEmpty()
            || askpassEnvironment.value(QStringLiteral("BIT8SHELL_ASKPASS_TOKEN")).isEmpty())
            return std::unexpected(QStringLiteral("Incomplete SSH password-helper environment."));
    }
    const auto withAskpass = [&](const QString& invocation) {
        if (askpassEnvironment.isEmpty()) return invocation;
        QString setup;
        if (powershell) {
            // Restore the parent shell's environment even if client launch fails.
            // These values are expiring broker capabilities, never a password.
            for (const auto& name : askpassEnvironment.keys()) {
                const QString literal = quote(name, true);
                setup += QStringLiteral("$bitshellAskpassPrevious[%1]=[Environment]::GetEnvironmentVariable(%1,'Process'); ").arg(literal);
            }
            QString assignments;
            for (const auto& name : askpassEnvironment.keys())
                assignments += QStringLiteral("[Environment]::SetEnvironmentVariable(%1,%2,'Process'); ")
                    .arg(quote(name, true), quote(askpassEnvironment.value(name), true));
            return QStringLiteral("& { $bitshellAskpassPrevious=@{}; %1try { %2%3 } finally { "
                "foreach ($bitshellAskpassName in $bitshellAskpassPrevious.Keys) { "
                "[Environment]::SetEnvironmentVariable($bitshellAskpassName,$bitshellAskpassPrevious[$bitshellAskpassName],'Process') } } }")
                .arg(setup, assignments, invocation);
        }
        // A subshell confines both helper environment and any key preparation.
        for (const auto& name : askpassEnvironment.keys())
            setup += QStringLiteral("export %1=%2; ").arg(name, quote(askpassEnvironment.value(name), false));
        return QStringLiteral("( %1%2 )").arg(setup, invocation);
    };
    QVariantMap endpoint = *resolved;
    if (cyberArk) {
        const auto route = resolved->value(QStringLiteral("psmp")).toMap()
                               .value(QStringLiteral("route")).toMap();
        if (!PersonalProfile::isRouteApproved(ConnectionRouting::fingerprint(route)))
            return std::unexpected(QStringLiteral("Review and approve this CyberArk gateway before connecting."));
        const auto login = ConnectionRouting::login(*resolved);
        if (!login) return std::unexpected(login.error());
        endpoint[QStringLiteral("host")] = route.value(QStringLiteral("host"));
        endpoint[QStringLiteral("port")] = route.value(QStringLiteral("port"), 22);
        endpoint[QStringLiteral("user")] = *login;
    }

    const QString key = resolved->value(QStringLiteral("identityFile")).toString();
    if (!powershell && !wsl && windowsPath(key))
        return std::unexpected(QStringLiteral("A Windows SSH key path requires a PowerShell or WSL terminal."));

    // Native clients retain their own known_hosts file and interactive trust
    // prompts. Skip arbitrary per-host config commands and forwarding defaults;
    // the saved browser profile, not ~/.ssh/config, defines this connection.
    QStringList arguments;
#ifdef Q_OS_WIN
    const QString nullConfig = powershell ? QStringLiteral("NUL") : QStringLiteral("/dev/null");
    const QString executable = powershell ? QStringLiteral("ssh.exe") : QStringLiteral("ssh");
#else
    const QString nullConfig = QStringLiteral("/dev/null");
    const QString executable = QStringLiteral("ssh");
#endif
    arguments << QStringLiteral("-F") << nullConfig
              << QStringLiteral("-oStrictHostKeyChecking=ask")
              << QStringLiteral("-oHashKnownHosts=yes")
              << QStringLiteral("-oVerifyHostKeyDNS=no")
              << QStringLiteral("-oUpdateHostKeys=no")
              << QStringLiteral("-oKnownHostsCommand=none")
              << QStringLiteral("-oPermitLocalCommand=no")
              << QStringLiteral("-oForwardAgent=no")
              << QStringLiteral("-oForwardX11Trusted=no")
              << QStringLiteral("-oClearAllForwardings=yes")
              << QStringLiteral("-oControlMaster=no")
              << QStringLiteral("-oControlPath=none")
              << QStringLiteral("-oAddKeysToAgent=no")
              << QStringLiteral("-oEscapeChar=none")
              << QStringLiteral("-p") << QString::number(endpoint.value(QStringLiteral("port"), 22).toInt());
    if (password)
        arguments << QStringLiteral("-oPreferredAuthentications=password")
                  << QStringLiteral("-oPubkeyAuthentication=no")
                  << QStringLiteral("-oKbdInteractiveAuthentication=no")
                  << QStringLiteral("-oNumberOfPasswordPrompts=1");
    const QString jump = resolved->value(QStringLiteral("proxyJump")).toString();
    if (!jump.isEmpty()) arguments << QStringLiteral("-J") << jump;
    if (resolved->value(QStringLiteral("forwardX11")).toBool()) arguments << QStringLiteral("-X");
    if (resolved->value(QStringLiteral("compression")).toBool()) arguments << QStringLiteral("-C");
    const QString user = endpoint.value(QStringLiteral("user")).toString();
    if (!user.isEmpty()) arguments << QStringLiteral("-l") << user;

    QStringList quoted;
    for (const auto& argument : arguments) quoted << quote(argument, powershell);
    if (!key.isEmpty()) {
        quoted << quote(QStringLiteral("-oIdentitiesOnly=yes"), powershell)
               << quote(QStringLiteral("-i"), powershell);
        if (powershell) {
            const QString expression = nativeKeyExpression(key, true);
            quoted << QStringLiteral("$(if (Test-Path -LiteralPath %1 -PathType Leaf) { %1 } else { throw 'The saved SSH key is missing; connection cancelled.' })")
                          .arg(expression);
        } else {
            quoted << QStringLiteral("\"$bitshell_native_key\"");
        }
    }
    quoted << quote(QStringLiteral("--"), powershell)
           << quote(unbracketed(endpoint.value(QStringLiteral("host")).toString()), powershell);

    if (powershell) {
        // Application-only lookup avoids invoking a shell function called ssh.
#ifdef Q_OS_WIN
        // A fresh Windows installation may not have the OpenSSH optional
        // feature. Prefer the current shell's client; only then use the client
        // shipped with the application (or Platform's trusted system fallback).
        const QString fallback = Platform::sshExecutable();
        if (fallback.isEmpty() || !SshSecurity::safePath(fallback))
            return std::unexpected(QStringLiteral("The packaged SSH client path is unavailable."));
        return withAskpass(QStringLiteral(
            "& (& { $bitshellNativeSsh = Get-Command %1 -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1; "
            "if ($null -ne $bitshellNativeSsh) { $bitshellNativeSsh.Source } "
            "elseif (Test-Path -LiteralPath %2 -PathType Leaf) { %2 } "
            "else { throw 'OpenSSH is unavailable in this shell and the application package.' } }) %3")
            .arg(quote(executable, true), quote(fallback, true), quoted.join(u' ')));
#else
        return withAskpass(QStringLiteral("& (Get-Command %1 -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source %2")
            .arg(quote(executable, true), quoted.join(u' ')));
#endif
    }
    const QString invocation = QStringLiteral("command ssh %1").arg(quoted.join(u' '));
    if (key.isEmpty()) return withAskpass(invocation);

    // Keep variables, traps and umask confined to this single invocation. A
    // Windows key on DrvFs otherwise often appears too permissive to Linux SSH.
    // The temporary copy is never exposed in a shared directory or shell history.
    QString setup;
    if (wsl && windowsPath(key)) {
        setup = QStringLiteral(
            "umask 077; "
            "bitshell_native_key=$(command wslpath -a -u %1) || exit; "
            "[ -f \"$bitshell_native_key\" ] && [ -r \"$bitshell_native_key\" ] || { printf 'The saved SSH key is unavailable; connection cancelled.\\n' >&2; exit 1; }; "
            "bitshell_key_dir=$(command mktemp -d /tmp/8bitshell-key.XXXXXXXX) || exit; "
            "trap 'command rm -f -- \"$bitshell_key_dir/key\"; command rmdir -- \"$bitshell_key_dir\"' EXIT; "
            "command cp -- \"$bitshell_native_key\" \"$bitshell_key_dir/key\" && command chmod 600 \"$bitshell_key_dir/key\" || exit; "
            "bitshell_native_key=\"$bitshell_key_dir/key\"; ").arg(quote(key, false));
    } else {
        setup = QStringLiteral(
            "bitshell_native_key=%1; "
            "[ -f \"$bitshell_native_key\" ] && [ -r \"$bitshell_native_key\" ] || { printf 'The saved SSH key is unavailable; connection cancelled.\\n' >&2; exit 1; }; ")
                    .arg(nativeKeyExpression(key, false));
    }
    return withAskpass(QStringLiteral("( %1%2 )").arg(setup, invocation));
}

} // namespace bitshell::core
