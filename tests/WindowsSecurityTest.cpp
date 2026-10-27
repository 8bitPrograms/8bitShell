#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <windows.h>
#include <aclapi.h>
#include <winioctl.h>
#include <cstring>
#include <vector>
#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/HostMonitor.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/SshSecurity.h>
#include <bitshell/core/YamlFile.h>
using namespace bitshell::core;
namespace {
bool privateAcl(const QString& path) {
    PACL acl = nullptr; PSID owner = nullptr; PSECURITY_DESCRIPTOR sd = nullptr;
    auto native = QDir::toNativeSeparators(path).toStdWString();
    if (GetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
                             &owner, nullptr, &acl, nullptr, &sd) != ERROR_SUCCESS) return false;
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision = 0;
    bool valid = acl && acl->AceCount == 2 && GetSecurityDescriptorControl(sd, &control, &revision) && (control & SE_DACL_PROTECTED);
    BYTE systemSid[SECURITY_MAX_SID_SIZE]{}; DWORD length = sizeof(systemSid);
    valid = valid && CreateWellKnownSid(WinLocalSystemSid, nullptr, systemSid, &length);
    for (DWORD n = 0; valid && n < acl->AceCount; ++n) {
        void* raw = nullptr;
        valid = GetAce(acl, n, &raw);
        if (!valid) break;
        const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        const auto sid = const_cast<DWORD*>(&ace->SidStart);
        valid = ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && !(ace->Header.AceFlags & INHERITED_ACE)
                && (EqualSid(owner, sid) || EqualSid(systemSid, sid));
    }
    LocalFree(sd); return valid;
}
bool junction(const QString& link, const QString& target) {
    if (!QDir().mkdir(link)) return false;
    const auto path = QDir::toNativeSeparators(link).toStdWString();
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const auto substitute = (QStringLiteral("\\??\\") + QDir::toNativeSeparators(target)).toStdWString();
    const auto display = QDir::toNativeSeparators(target).toStdWString();
    struct MountPoint { DWORD tag; WORD dataLength, reserved, subOffset, subLength, printOffset, printLength; };
    const auto subBytes = substitute.size()*sizeof(wchar_t), printBytes = display.size()*sizeof(wchar_t);
    std::vector<BYTE> data(sizeof(MountPoint) + subBytes + printBytes + 4);
    auto* reparse = reinterpret_cast<MountPoint*>(data.data());
    reparse->tag = IO_REPARSE_TAG_MOUNT_POINT; reparse->dataLength = WORD(data.size()-8);
    reparse->subLength = WORD(subBytes); reparse->printOffset = WORD(subBytes+2); reparse->printLength = WORD(printBytes);
    std::memcpy(data.data()+sizeof(MountPoint), substitute.data(), subBytes);
    std::memcpy(data.data()+sizeof(MountPoint)+subBytes+2, display.data(), printBytes);
    DWORD returned = 0;
    const bool result = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, data.data(), DWORD(data.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(handle); return result;
}
}
class WindowsSecurityTest : public QObject {
    Q_OBJECT
private slots:
    void localMonitoringUsesNativeCounters() {
        HostMonitor monitor;
        monitor.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(!monitor.metrics().isEmpty(), 5000);
        const auto metrics = monitor.metrics();
        QVERIFY(metrics.value(QStringLiteral("memoryTotalMB")).toDouble() > 0);
        const double memory = metrics.value(QStringLiteral("memoryPercent")).toDouble();
        QVERIFY(memory >= 0 && memory <= 100);
        const double cpu = metrics.value(QStringLiteral("cpuPercent")).toDouble();
        QVERIFY(cpu >= -1 && cpu <= 100);
        QVERIFY(metrics.value(QStringLiteral("uptime")).toDouble() > 0);
        QVERIFY(!metrics.value(QStringLiteral("host")).toString().isEmpty());
        QVERIFY(!metrics.value(QStringLiteral("disks")).toList().isEmpty());
        QVERIFY(monitor.status().contains(QStringLiteral("local Windows")));
        monitor.setEnabled(false);
        QVERIFY(monitor.metrics().isEmpty());
    }
    void exportsRejectCaseAliases() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto path = temp.filePath(QStringLiteral("settings.yaml"));
        QString error; QVERIFY(PrivateFile::write(path, "original", error));
        YamlFile yaml(path, QStringLiteral("8bitshell/settings"));
        QVERIFY(!yaml.exportFile(path.toUpper(), {}));
        QByteArray content; QVERIFY(PrivateFile::read(path,content,error)); QCOMPARE(content,QByteArray("original"));
    }
    void privateStorageRoundTripAndDacl() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); QString error; QByteArray data;
        const auto file = temp.filePath(QStringLiteral("Türkçe/config.yaml"));
        QVERIFY2(PrivateFile::write(file, "first", error), qPrintable(error));
        QVERIFY(privateAcl(file)); QVERIFY(privateAcl(QFileInfo(file).absolutePath()));
        QVERIFY2(PrivateFile::write(file, "second", error), qPrintable(error));
        QVERIFY(PrivateFile::read(file, data, error)); QCOMPARE(data, QByteArray("second"));
        QVERIFY(!PrivateFile::read(file, data, error, 3)); QVERIFY(data.isEmpty());
        const auto logPath = temp.filePath(QStringLiteral("log/output.log"));
        QFile log(logPath);
        QVERIFY2(PrivateFile::openLog(log, false, error), qPrintable(error));
        QCOMPARE(log.write("first\n"), 6); log.close();
        // Adopting a CRT descriptor can clear QFile's name on Windows; real
        // SessionLogger likewise sets its saved path before each reopen.
        log.setFileName(logPath);
        QVERIFY2(PrivateFile::openLog(log, true, error), qPrintable(error)); QCOMPARE(log.write("second\n"), 7); log.close();
        QVERIFY(PrivateFile::read(logPath, data, error)); QCOMPARE(data, QByteArray("first\nsecond\n"));
        QVERIFY(privateAcl(logPath));
    }
    void aliasesJunctionsAndHardlinksFailClosed() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); QString error; QByteArray data;
        const auto file = temp.filePath(QStringLiteral("source.yaml"));
        QVERIFY(PrivateFile::write(file, "untouched", error));
        for (const auto& name : {"source.yaml:secret", "CON", "NUL.txt", "trailing.", "trailing "})
            QVERIFY(!PrivateFile::write(temp.filePath(QString::fromUtf8(name)), "bad", error));
        QVERIFY(!PrivateFile::write(QStringLiteral("\\\\server\\share\\x"), "bad", error));
        const auto alias = temp.filePath(QStringLiteral("hardlink.yaml"));
        const bool linked = CreateHardLinkW(QDir::toNativeSeparators(alias).toStdWString().c_str(),
                                            QDir::toNativeSeparators(file).toStdWString().c_str(), nullptr);
        const DWORD linkError = linked ? ERROR_SUCCESS : GetLastError();
        if (!linked && (linkError == ERROR_ACCESS_DENIED || linkError == ERROR_NOT_SUPPORTED
                        || linkError == ERROR_PRIVILEGE_NOT_HELD))
            QSKIP(qPrintable(QStringLiteral("Host cannot create a hard-link fixture (Windows error %1)").arg(linkError)));
        QVERIFY(linked);
        QVERIFY(!PrivateFile::write(alias, "bad", error));
        QFile log(alias); QVERIFY(!PrivateFile::openLog(log, false, error));
        QVERIFY(PrivateFile::read(file, data, error)); QCOMPARE(data, QByteArray("untouched"));
        const auto directory = temp.filePath(QStringLiteral("real")); QVERIFY(QDir().mkdir(directory));
        const auto link = temp.filePath(QStringLiteral("junction")); QVERIFY(junction(link, directory));
        QVERIFY(!PrivateFile::write(link+QStringLiteral("/escape.yaml"), "bad", error));
        QVERIFY(!PrivateFile::secureEntry(link, error));
        QVERIFY(!QFileInfo::exists(directory+QStringLiteral("/escape.yaml")));
        // Remove the junction itself, never recursively traverse its target.
        QVERIFY(RemoveDirectoryW(QDir::toNativeSeparators(link).toStdWString().c_str()));
    }
    void publicationDoesNotOverwrite() {
        QTemporaryDir temp; QVERIFY(temp.isValid()); QString error; bool collision = false; QByteArray data;
        const auto a = temp.filePath(QStringLiteral("a")), b = temp.filePath(QStringLiteral("b"));
        QVERIFY(PrivateFile::write(a, "new", error)); QVERIFY(PrivateFile::write(b, "old", error));
        QVERIFY(!PrivateFile::publishNoReplace(a,b,collision,error)); QVERIFY(collision);
        QVERIFY(PrivateFile::read(b,data,error)); QCOMPARE(data,QByteArray("old"));
        const auto c = temp.filePath(QStringLiteral("c"));
        QVERIFY2(PrivateFile::publishNoReplace(a,c,collision,error),qPrintable(error)); QVERIFY(!collision);
        QVERIFY(!QFileInfo::exists(a)); QVERIFY(PrivateFile::read(c,data,error)); QCOMPARE(data,QByteArray("new"));
    }
    void opensshPolicyEvaluatesWithoutNetwork() {
        const auto config = SshSecurity::configurationPath(); QVERIFY(!config.isEmpty());
        QVERIFY(privateAcl(config));
        QProcess ssh; ssh.setProcessEnvironment(SshSecurity::environment());
        ssh.start(Platform::sshExecutable(), {QStringLiteral("-F"),config,QStringLiteral("-G"),QStringLiteral("example.invalid")});
        QVERIFY(ssh.waitForStarted()); QVERIFY(ssh.waitForFinished(10000));
        const auto error = ssh.readAllStandardError(); QCOMPARE(ssh.exitCode(), 0);
        const auto policy = ssh.readAllStandardOutput();
        QVERIFY2(policy.contains("stricthostkeychecking true") || policy.contains("stricthostkeychecking yes"),error.constData());
        QVERIFY(policy.contains("forwardagent no")); QVERIFY(policy.contains("permitlocalcommand no"));
        QVERIFY(policy.contains("controlmaster false") || policy.contains("controlmaster no"));
        QVERIFY(!policy.contains("diffie-hellman-group1-sha1"));
    }
};
QTEST_GUILESS_MAIN(WindowsSecurityTest)
#include "WindowsSecurityTest.moc"
