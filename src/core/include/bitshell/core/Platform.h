#pragma once

#include <QProcessEnvironment>
#include <QStringList>

namespace bitshell::core {
// OS choices live here; connection policy and catalogs remain platform-neutral.
class Platform final {
public:
    static QString sshExecutable();
    static QString sftpExecutable();
    static QString sshKeygenExecutable();
    // PuTTY keys are converted once at import time; OpenSSH remains the only
    // connection transport. Official packages bundle a verified converter
    // beside the application; developer builds may use /usr/bin on Linux.
    // It is never discovered through PATH.
    static QString puttygenExecutable();
    static bool isManagedSsh(const QString& program);
    static QString localShell();
    static QString windowsSystemDirectory();
    static QStringList localShellArguments();
    // PowerShell already stores Up-arrow history in the PSReadLine file shared
    // with Windows Terminal and conhost. SaveAtExit never flushes when a tab is
    // closed, so interactive launches switch that one default to incremental
    // append. Existing history lines and an explicit user setting are left alone.
    static QStringList persistentHistoryArguments(const QString& program, const QStringList& arguments);
    static QString trustedExecutablePath();
    static QString configRoot();
    // Application settings are isolated under the 8bitshell profile.
    static QString appConfigDirectory();
    static QString iniSettingsDirectory();
    static bool initializeProcess(QString& error);

    // Microsoft CRT argv encoding, NOT cmd.exe/PowerShell source. Kept portable
    // so the difficult quoting cases are tested on both build platforms.
    static QString windowsCommandLine(const QString& program, const QStringList& arguments);
    static bool windowsFileName(const QString& name);
};
} // namespace bitshell::core
