#include <bitshell/core/Platform.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace bitshell::core {
namespace {
bool validIntegrationToken(const QString& token) {
    return token.size() >= 16 && token.size() <= 128
        && std::all_of(token.cbegin(), token.cend(), [](QChar c) {
            return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z')
                || (c >= u'0' && c <= u'9') || c == u'-' || c == u'_';
        });
}
QString shellLiteral(QString value) {
    return u'\'' + value.replace(u'\'', QStringLiteral("'\\''")) + u'\'';
}
QString bashIntegrationScript(const QString& token, const QString& kind, bool login, bool noProfile, bool noRc) {
    // The rcfile exists only as a process-substitution pipe. Run the same user
    // startup files before appending our hooks, so their prompt/history setup
    // cannot accidentally erase integration during initialization.
    QString script;
    if (login && !noProfile) {
        script = QStringLiteral("if [ -r /etc/profile ]; then . /etc/profile; fi\n"
            "for __8bit_startup in ~/.bash_profile ~/.bash_login ~/.profile; do\n"
            "  if [ -r \"$__8bit_startup\" ]; then . \"$__8bit_startup\"; break; fi\n"
            "done\nunset __8bit_startup\n");
    } else if (!login && !noRc) {
        script = QStringLiteral("if [ -r ~/.bashrc ]; then . ~/.bashrc; fi\n");
    }
    script += QStringLiteral(R"SH(
# PS0 was introduced in Bash 4.4. Without an execution marker the helper must
# stay disabled rather than interpreting input inside a running application.
if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 4) )); then return; fi
__8bit_prompt_integration() {
    local __8bit_status=$?
    history -a
    history -n
    case "$PS1" in
        *'\e]133;B;@TOKEN@\a'*) ;;
        *) PS1='\[\e]777;8bitshell;@TOKEN@;@KIND@\a\]\[\e]133;A;@TOKEN@\a\]'"$PS1"'\[\e]133;B;@TOKEN@\a\]' ;;
    esac
    case "${PS0-}" in
        *$'\e]133;C;@TOKEN@\a'*) ;;
        *) PS0=$'\e]133;C;@TOKEN@\a'"${PS0-}" ;;
    esac
    # A nested shell must not inherit the parent prompt's authentication token.
    export -n PS0 PS1 PROMPT_COMMAND
    return "$__8bit_status"
}
if [[ $(declare -p PROMPT_COMMAND 2>/dev/null) == 'declare -a '* ]]; then
    PROMPT_COMMAND+=(__8bit_prompt_integration)
else
    PROMPT_COMMAND="${PROMPT_COMMAND:+$PROMPT_COMMAND; }__8bit_prompt_integration"
fi
)SH");
    return script.replace(QStringLiteral("@TOKEN@"), token).replace(QStringLiteral("@KIND@"), kind);
}
QString bashBootstrap(const QString& token, const QString& kind, bool login, bool noProfile = false, bool noRc = false) {
    return QStringLiteral("exec \"$0\" --noprofile --rcfile <(printf '%s' %1) -i")
        .arg(shellLiteral(bashIntegrationScript(token, kind, login, noProfile, noRc)));
}
#ifdef Q_OS_WIN
QString powerShellIntegrationScript(const QString& token) {
    // PSConsoleHostReadLine is the host's actual input boundary. Wrapping it
    // leaves prompt rendering, history filters, edit modes and key bindings
    // intact, and marks execution even when history saving is disabled.
    return QStringLiteral(R"PS(
try {
    if ((Get-Module PSReadLine) -and -not (Get-Variable -Name __8bitOriginalReadLine -Scope Global -ErrorAction SilentlyContinue)) {
        $global:__8bitOriginalReadLine = (Get-Item Function:\PSConsoleHostReadLine -ErrorAction Stop).ScriptBlock
        function global:PSConsoleHostReadLine {
            [Console]::Write(([string][char]27 + ']777;8bitshell;@TOKEN@;powershell' + [char]7 + [char]27 + ']133;A;@TOKEN@' + [char]7 + [char]27 + ']133;B;@TOKEN@' + [char]7))
            try { & $global:__8bitOriginalReadLine } finally {
                [Console]::Write(([string][char]27 + ']133;C;@TOKEN@' + [char]7))
            }
        }
    }
} catch {}
)PS").replace(QStringLiteral("@TOKEN@"), token);
}
QString systemDirectory() {
    wchar_t path[MAX_PATH + 1]{};
    const UINT length = GetSystemDirectoryW(path, MAX_PATH + 1);
    return length && length <= MAX_PATH ? QDir::fromNativeSeparators(QString::fromWCharArray(path, int(length))) : QString{};
}
#endif
QString quoteWindowsArgument(const QString& text) {
    // Keep simple tokens bare, like QProcess. cmd.exe and wsl.exe parse startup
    // switches themselves and do not recognize unnecessarily quoted switches.
    // CreateProcess does not introduce a shell, so metacharacters remain argv.
    if (!text.isEmpty() && std::none_of(text.cbegin(), text.cend(), [](QChar c) { return c.isSpace() || c == u'"'; }))
        return text;
    QString result(u'"');
    qsizetype slashes = 0;
    for (const QChar ch : text) {
        if (ch == u'\\') { ++slashes; continue; }
        if (ch == u'"') {
            result += QString(slashes * 2 + 1, u'\\');
        } else {
            result += QString(slashes, u'\\');
        }
        slashes = 0;
        result += ch;
    }
    result += QString(slashes * 2, u'\\');
    result += u'"';
    return result;
}
}
QString Platform::sshExecutable() {
    const auto bundled = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
        QStringLiteral("openssh/ssh.exe"));
#else
        QStringLiteral("openssh/ssh"));
#endif
    if (QFileInfo(bundled).isFile() && QFileInfo(bundled).isExecutable()) return bundled;
#ifdef Q_OS_WIN
    const auto system = systemDirectory();
    return system.isEmpty() ? QString{} : system + QStringLiteral("/OpenSSH/ssh.exe");
#else
    return QStringLiteral("/usr/bin/ssh");
#endif
}
QString Platform::sftpExecutable() {
    const auto bundled = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
        QStringLiteral("openssh/sftp.exe"));
#else
        QStringLiteral("openssh/sftp"));
#endif
    if (QFileInfo(bundled).isFile() && QFileInfo(bundled).isExecutable()) return bundled;
#ifdef Q_OS_WIN
    const auto system = systemDirectory();
    return system.isEmpty() ? QString{} : system + QStringLiteral("/OpenSSH/sftp.exe");
#else
    return QStringLiteral("/usr/bin/sftp");
#endif
}
QString Platform::sshKeygenExecutable() {
    const auto bundled = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
        QStringLiteral("openssh/ssh-keygen.exe"));
#else
        QStringLiteral("openssh/ssh-keygen"));
#endif
    if (QFileInfo(bundled).isFile() && QFileInfo(bundled).isExecutable()) return bundled;
#ifdef Q_OS_WIN
    const auto system = systemDirectory();
    return system.isEmpty() ? QString{} : system + QStringLiteral("/OpenSSH/ssh-keygen.exe");
#else
    return QStringLiteral("/usr/bin/ssh-keygen");
#endif
}
QString Platform::puttygenExecutable() {
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
        QStringLiteral("puttygen.exe"));
    return bundled;
#else
        QStringLiteral("puttygen"));
    if (QFileInfo(bundled).isExecutable()) return bundled;
    return QStringLiteral("/usr/bin/puttygen");
#endif
}
bool Platform::isManagedSsh(const QString& program) {
#ifdef Q_OS_WIN
    return !program.isEmpty() && QDir::cleanPath(QDir::fromNativeSeparators(program)).compare(sshExecutable(), Qt::CaseInsensitive) == 0;
#else
    return program == sshExecutable();
#endif
}
QString Platform::windowsSystemDirectory() {
#ifdef Q_OS_WIN
    return systemDirectory();
#else
    return {};
#endif
}
QString Platform::localShell() {
#ifdef Q_OS_WIN
    const auto system = systemDirectory();
    return system.isEmpty() ? QString{} : system + QStringLiteral("/WindowsPowerShell/v1.0/powershell.exe");
#else
    const auto shell = qEnvironmentVariable("SHELL");
    if (QDir::isAbsolutePath(shell) && !shell.contains(QChar::Null) && QFileInfo(shell).isExecutable()) return shell;
    return QFileInfo(QStringLiteral("/bin/bash")).isExecutable() ? QStringLiteral("/bin/bash") : QStringLiteral("/bin/sh");
#endif
}
QStringList Platform::localShellArguments() {
#ifdef Q_OS_WIN
    return {QStringLiteral("-NoLogo")};
#else
    return {QStringLiteral("-l")};
#endif
}
QStringList Platform::persistentHistoryArguments(const QString& program, const QStringList& arguments,
                                                 const QString& shellIntegrationToken) {
    const bool integrate = validIntegrationToken(shellIntegrationToken);
    const auto name = QFileInfo(program).fileName().toLower();
#ifndef Q_OS_WIN
    if (integrate && name == QStringLiteral("bash")) {
        bool login = false, noProfile = false, noRc = false;
        for (const auto& arg : arguments) {
            if (arg == QStringLiteral("-l") || arg == QStringLiteral("--login")) login = true;
            else if (arg == QStringLiteral("-il") || arg == QStringLiteral("-li")) login = true;
            else if (arg == QStringLiteral("--noprofile")) noProfile = true;
            else if (arg == QStringLiteral("--norc")) noRc = true;
            else if (arg != QStringLiteral("-i")) return arguments;
        }
        return {QStringLiteral("-c"), bashBootstrap(shellIntegrationToken, QStringLiteral("bash"), login, noProfile, noRc), program};
    }
    return arguments;
#else
    if (integrate && name == QStringLiteral("wsl.exe")) {
        // The recognized shape is exactly the default LocalProfileStore launch.
        // Any extra argument may select a shell/command and remains untouched.
        if (!arguments.isEmpty() && (arguments.size() != 4
            || arguments.at(0) != QStringLiteral("--distribution")
            || arguments.at(2) != QStringLiteral("--cd"))) return arguments;
        const QString bootstrap = QStringLiteral(
            "__8bit_shell=${SHELL:-}; "
            "if command -v getent >/dev/null 2>&1; then "
            "__8bit_entry=$(getent passwd \"$(id -u)\"); __8bit_login=${__8bit_entry##*:}; "
            "if [ -x \"$__8bit_login\" ]; then __8bit_shell=$__8bit_login; fi; fi; "
            "[ -n \"$__8bit_shell\" ] || __8bit_shell=/bin/sh; "
            "case \"${__8bit_shell##*/}\" in bash) exec \"$__8bit_shell\" -c %1 \"$__8bit_shell\";; "
            "*) exec \"$__8bit_shell\" -l;; esac")
            .arg(shellLiteral(bashBootstrap(shellIntegrationToken, QStringLiteral("wsl-bash"), true)));
        auto result = arguments;
        result << QStringLiteral("--exec") << QStringLiteral("/bin/sh") << QStringLiteral("-c") << bootstrap;
        return result;
    }
    if (name != QStringLiteral("powershell.exe") && name != QStringLiteral("pwsh.exe")) return arguments;
    const auto blocksHistory = [](const QString& argument) {
        const auto token = argument.toLower();
        return (token.size() >= 2 && QStringLiteral("-command").startsWith(token))
            || token == QStringLiteral("-commandwithargs") || token == QStringLiteral("-cwa")
            || (token.size() >= 2 && QStringLiteral("-file").startsWith(token))
            || (token.size() >= 2 && QStringLiteral("-encodedcommand").startsWith(token))
            || token == QStringLiteral("-ec")
            || (token.size() >= 5 && QStringLiteral("-noninteractive").startsWith(token));
    };
    if (std::any_of(arguments.cbegin(), arguments.cend(), blocksHistory)) return arguments;
    auto result = arguments;
    result << QStringLiteral("-NoExit") << QStringLiteral("-Command")
           << QStringLiteral("try { if (-not (Get-Module PSReadLine)) { Import-Module PSReadLine -ErrorAction Stop }; "
                             "if ((Get-PSReadLineOption).HistorySaveStyle -eq 'SaveAtExit') { "
                             "Set-PSReadLineOption -HistorySaveStyle SaveIncrementally } } catch {}");
    if (integrate) result.last() += powerShellIntegrationScript(shellIntegrationToken);
    return result;
#endif
}
QString Platform::trustedExecutablePath() {
#ifdef Q_OS_WIN
    const auto system = systemDirectory();
    return system.isEmpty() ? QString{} : system + QStringLiteral("/OpenSSH;") + system;
#else
    return QStringLiteral("/usr/bin:/bin");
#endif
}
QString Platform::configRoot() {
    // Honor an explicit portable/test directory on every desktop platform.
    // Relative values are ignored, just as on XDG platforms. PrivateFile still
    // enforces local paths, ownership, ACLs and no reparse points.
    const auto override = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!override.isEmpty() && QDir::isAbsolutePath(override)) return QDir::fromNativeSeparators(override);
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}
QString Platform::appConfigDirectory() {
    const auto portable = qEnvironmentVariable("BIT8SHELL_PROFILE");
    if (!portable.isEmpty() && QDir::isAbsolutePath(portable)) return QDir::cleanPath(portable);
    // PrivateFile validates ownership, permissions and links at this exact path.
    return QDir(configRoot()).filePath(QStringLiteral("8bitshell"));
}
QString Platform::iniSettingsDirectory() {
    return QDir(appConfigDirectory()).filePath(QStringLiteral("8bitShell"));
}
bool Platform::initializeProcess(QString& error) {
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        error = QStringLiteral("Cannot inspect the Windows process token."); return false;
    }
    const bool inspected = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes);
    CloseHandle(token);
    if (!inspected || elevation.TokenIsElevated) {
        error = QStringLiteral("8bitShell must run as a normal desktop user, not an elevated administrator or SYSTEM."); return false;
    }
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        error = QStringLiteral("Cannot restrict DLL loading to the application and Windows system directories."); return false;
    }
    // No exception to Defender/EDR and no automatic crash-report uploads. This
    // suppresses error dialogs; it does NOT claim to prevent administrator dumps.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    qunsetenv("QT_PLUGIN_PATH");
    qunsetenv("QML_IMPORT_PATH");
    qunsetenv("QML2_IMPORT_PATH");
#else
    Q_UNUSED(error);
#endif
    return true;
}
QString Platform::windowsCommandLine(const QString& program, const QStringList& arguments) {
    if (program.isEmpty() || program.contains(QChar::Null) || arguments.size() > 128) return {};
    QString result = quoteWindowsArgument(program);
    for (const auto& argument : arguments) {
        if (argument.contains(QChar::Null) || argument.size() > 32766) return {};
        result += u' '; result += quoteWindowsArgument(argument);
        if (result.size() > 32766) return {};
    }
    return result.size() <= 32766 ? result : QString{};
}
bool Platform::windowsFileName(const QString& name) {
    if (name.isEmpty() || name.size() > 255 || name.endsWith(u'.') || name.endsWith(u' ')) return false;
    for (const auto ch : name) {
        if (ch.unicode() < 32 || ch.unicode() == 127 || QStringLiteral("<>:\"/\\|?*").contains(ch)) return false;
    }
    const auto stem = name.section(u'.', 0, 0).toUpper();
    if (stem == QStringLiteral("CON") || stem == QStringLiteral("PRN") || stem == QStringLiteral("AUX") || stem == QStringLiteral("NUL")
        || stem == QStringLiteral("CONIN$") || stem == QStringLiteral("CONOUT$")) return false;
    if (stem.size() == 4 && (stem.startsWith(QStringLiteral("COM")) || stem.startsWith(QStringLiteral("LPT"))))
        return !QStringLiteral("123456789\u00b9\u00b2\u00b3").contains(stem.back());
    return true;
}
} // namespace bitshell::core
