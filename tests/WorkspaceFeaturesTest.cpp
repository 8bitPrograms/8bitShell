#include <QtTest>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <bitshell/core/AppSettings.h>
#include <bitshell/core/MacroStore.h>
#include <bitshell/core/HostMonitor.h>
#include <bitshell/core/LocalFileModel.h>
#include <bitshell/core/SftpController.h>

using namespace bitshell::core;
class WorkspaceFeaturesTest : public QObject {
    Q_OBJECT
private slots:
    void macroInheritancePersistenceAndMoves();
    void inputSettingsPersistAndRejectConflicts();
    void monitoringParsesSamplesAndRunsLocally();
    void localBrowserCopiesNavigatesAndPreservesExisting();
    void localCopyRejectsDeepTrees();
    void realOpenSshTransfersQuotedNamesAndDirectories();
};
static QVariantList button(const QString& name) {
    return {QVariantMap{{QStringLiteral("label"), name}, {QStringLiteral("text"), QStringLiteral("echo safe")},
                        {QStringLiteral("enter"), false}}};
}
void WorkspaceFeaturesTest::macroInheritancePersistenceAndMoves() {
    MacroStore store;
    const QString base = store.saveGroup({}, QStringLiteral("Global"), button(QStringLiteral("Global")));
    const QString oracle = store.saveGroup({}, QStringLiteral("Oracle"), button(QStringLiteral("Oracle")));
    const QString child = store.saveGroup({}, QStringLiteral("Production"), button(QStringLiteral("Prod")));
    const QString empty = store.saveGroup({}, QStringLiteral("Empty"), {});
    QVERIFY(!base.isEmpty() && !oracle.isEmpty() && !child.isEmpty() && !empty.isEmpty());
    QVERIFY(store.assignFolder({}, base));
    QVERIFY(store.assignFolder(QStringLiteral("DB/Oracle"), oracle));
    QVERIFY(store.assignFolder(QStringLiteral("DB/Oracle/Prod"), child));
    QVERIFY(store.assignFolder(QStringLiteral("DB/Oracle/Dev"), empty));
    QCOMPARE(store.effectiveGroup(QStringLiteral("DB/Oracle/Prod/Cluster")).value(QStringLiteral("id")).toString(), child);
    QCOMPARE(store.effectiveGroup(QStringLiteral("DB/Oracle/Dev/Cluster")).value(QStringLiteral("id")).toString(), oracle);
    QCOMPARE(store.effectiveGroup(QStringLiteral("Other")).value(QStringLiteral("id")).toString(), base);
    QVERIFY(store.remapFolder(QStringLiteral("DB/Oracle"), QStringLiteral("DB/Database")));
    QCOMPARE(store.effectiveGroup(QStringLiteral("DB/Database/Prod")).value(QStringLiteral("id")).toString(), child);
    QCOMPARE(store.effectiveGroup(QStringLiteral("DB/Oracle/Prod")).value(QStringLiteral("id")).toString(), base);
    MacroStore loaded;
    QCOMPARE(loaded.assignedGroup(QStringLiteral("DB/Database")), oracle);
    QVERIFY(loaded.removeGroup(child));
    QCOMPARE(loaded.effectiveGroup(QStringLiteral("DB/Database/Prod")).value(QStringLiteral("id")).toString(), oracle);
    QVERIFY(loaded.removeFolder(QStringLiteral("DB/Database")));
    QCOMPARE(loaded.effectiveGroup(QStringLiteral("DB/Database/Dev")).value(QStringLiteral("id")).toString(), base);
}
void WorkspaceFeaturesTest::inputSettingsPersistAndRejectConflicts() {
    AppSettings settings;
    QVERIFY(!settings.copyOnSelect());
    QVERIFY(!settings.monitoringByDefault());
    QCOMPARE(settings.keyBindings().value(QStringLiteral("copy")).toString(), QStringLiteral("Ctrl+Shift+C"));
    QVERIFY(!settings.setKeyBinding(QStringLiteral("paste"), QStringLiteral("Ctrl+Shift+C")));
    QVERIFY(!settings.shortcutError().isEmpty());
    settings.applyKeyPreset(QStringLiteral("insert"));
    QCOMPARE(settings.keyBindings().value(QStringLiteral("copy")).toString(), QStringLiteral("Ctrl+Insert"));
    settings.setCopyOnSelect(true);
    settings.setMonitoringByDefault(true);
    settings.setSidebarVisible(false);
    settings.setFileBrowserVisible(true);
    settings.setMacroBarVisible(false);
    QVERIFY(settings.setKeyBinding(QStringLiteral("copy"), QStringLiteral("Alt+C")));
    AppSettings loaded;
    QVERIFY(loaded.copyOnSelect() && loaded.monitoringByDefault());
    QVERIFY(!loaded.sidebarVisible() && loaded.fileBrowserVisible() && !loaded.macroBarVisible());
    QCOMPARE(loaded.keyBindings().value(QStringLiteral("copy")).toString(), QStringLiteral("Alt+C"));
    settings.restoreDefaults();
    QVERIFY(!settings.copyOnSelect() && !settings.monitoringByDefault());
}
void WorkspaceFeaturesTest::monitoringParsesSamplesAndRunsLocally() {
    HostMonitor parser;
    QVERIFY(!parser.enabled());
    parser.consume("__BIT8SHELL_MONITOR_BEGIN__\ncpu 100 80\nmemory 1000 300\nuptime 10\nnet 100 200\n__BIT8SHELL_MONITOR_END__\n");
    QCOMPARE(parser.metrics().value(QStringLiteral("memoryPercent")).toDouble(), 70.0);
    parser.consume("__BIT8SHELL_MONITOR_BEGIN__\ncpu 200 130\nmemory 1000 200\nuptime 15\nnet 5220 10440\ndiskfs /home 73400320 104857600 70%\ndiskfs /var%20lib 41943040 52428800 80%\n__BIT8SHELL_MONITOR_END__\n");
    QCOMPARE(parser.metrics().value(QStringLiteral("cpuPercent")).toDouble(), 50.0);
    QCOMPARE(parser.metrics().value(QStringLiteral("rxKB")).toDouble(), 1.0);
    QCOMPARE(parser.metrics().value(QStringLiteral("txKB")).toDouble(), 2.0);
    const auto disks = parser.metrics().value(QStringLiteral("disks")).toList();
    QCOMPARE(disks.size(), 2);
    QCOMPARE(disks.at(0).toMap().value(QStringLiteral("mount")).toString(), QStringLiteral("/home"));
    QCOMPARE(disks.at(1).toMap().value(QStringLiteral("mount")).toString(), QStringLiteral("/var lib"));
    QCOMPARE(disks.at(0).toMap().value(QStringLiteral("usedGB")).toDouble(), 70.0);
    HostMonitor live;
    live.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(!live.metrics().isEmpty(), 5000);
    QVERIFY(live.metrics().value(QStringLiteral("memoryTotalMB")).toDouble() > 0);
    QVERIFY(!live.metrics().value(QStringLiteral("host")).toString().isEmpty());
    live.setEnabled(false);
    QVERIFY(live.metrics().isEmpty());
}
static void writeFixture(const QString& path, const QByteArray& data) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(data), data.size());
}
static QByteArray readFixture(const QString& path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
}
void WorkspaceFeaturesTest::localBrowserCopiesNavigatesAndPreservesExisting() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source"));
    const QString target = directory.filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkdir(source)); QVERIFY(QDir().mkdir(target));
    writeFixture(QDir(source).filePath(QStringLiteral("file.txt")), "new");
    writeFixture(QDir(target).filePath(QStringLiteral("file.txt")), "old");
    QVERIFY(QDir().mkdir(QDir(source).filePath(QStringLiteral("folder"))));
    writeFixture(QDir(source).filePath(QStringLiteral("folder/child.txt")), "child");
    LocalFileModel model;
    model.changeDirectory(target);
    QCOMPARE(model.currentPath(), target);
    model.copyFiles({QUrl::fromLocalFile(QDir(source).filePath(QStringLiteral("file.txt"))),
                     QUrl::fromLocalFile(QDir(source).filePath(QStringLiteral("folder")))});
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000);
    QCOMPARE(readFixture(QDir(target).filePath(QStringLiteral("file.txt"))), QByteArray("old"));
    QCOMPARE(readFixture(QDir(target).filePath(QStringLiteral("file.txt (1)"))), QByteArray("new"));
    QCOMPARE(readFixture(QDir(target).filePath(QStringLiteral("folder/child.txt"))), QByteArray("child"));
    QVERIFY(!(QFileInfo(QDir(target).filePath(QStringLiteral("folder"))).permissions()
              & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
    model.changeDirectory(QStringLiteral("folder"));
    QCOMPARE(model.currentPath(), QDir(target).filePath(QStringLiteral("folder")));
    model.goBack(); QCOMPARE(model.currentPath(), target);
    model.goForward(); QCOMPARE(model.currentPath(), QDir(target).filePath(QStringLiteral("folder")));
    model.goUp(); QCOMPARE(model.currentPath(), target);
    model.copyFiles({QUrl::fromLocalFile(target)});
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 3000);
    QVERIFY(model.statusText().contains(QStringLiteral("itself")));
}
void WorkspaceFeaturesTest::localCopyRejectsDeepTrees() {
    QTemporaryDir directory;
    QString nested = directory.filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkdir(nested));
    for (int i = 0; i < 70; ++i) { nested += QStringLiteral("/x"); QVERIFY(QDir().mkdir(nested)); }
    const QString target = directory.filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkdir(target));
    LocalFileModel model;
    model.changeDirectory(target);
    model.copyFiles({QUrl::fromLocalFile(directory.filePath(QStringLiteral("source")))});
    QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 3000);
    QVERIFY2(model.statusText().contains(QStringLiteral("64 directory levels")), qPrintable(model.statusText()));
}
void WorkspaceFeaturesTest::realOpenSshTransfersQuotedNamesAndDirectories() {
    QString server;
    for (const QString& path : {QStringLiteral("/usr/libexec/openssh/sftp-server"), QStringLiteral("/usr/lib/openssh/sftp-server")})
        if (QFileInfo(path).isExecutable()) server = path;
    if (server.isEmpty()) QSKIP("OpenSSH sftp-server is not installed.");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString remote = directory.filePath(QStringLiteral("remote"));
    const QString source = directory.filePath(QStringLiteral("local source"));
    const QString target = directory.filePath(QStringLiteral("local target"));
    QVERIFY(QDir().mkdir(remote)); QVERIFY(QDir().mkdir(source)); QVERIFY(QDir().mkdir(target));
    const QString name = QStringLiteral("report [1] \"quoted\" $file.txt");
    writeFixture(QDir(remote).filePath(name), "original");
    writeFixture(QDir(source).filePath(name), "upload");
    QVERIFY(QDir().mkdir(QDir(source).filePath(QStringLiteral("tree"))));
    writeFixture(QDir(source).filePath(QStringLiteral("tree/child.txt")), "nested");
    const QString wrapper = directory.filePath(QStringLiteral("direct-sftp"));
    writeFixture(wrapper, ("#!/bin/sh\ncd '" + remote.toUtf8() + "' || exit 1\nexec /usr/bin/sftp -D '" + server.toUtf8() + "' -b -\n"));
    QVERIFY(QFile::setPermissions(wrapper, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    SftpController controller;
    controller.setTransportExecutableForTesting(wrapper);
    controller.connectToProfile({{QStringLiteral("host"), QStringLiteral("local-test.invalid")}});
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(), SftpController::State::Ready, 5000);
    QCOMPARE(controller.currentPath(), remote);
    QCOMPARE(controller.count(), 1);
    controller.downloadTo(0, QUrl::fromLocalFile(target));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
    QVERIFY2(controller.connected(), qPrintable(controller.statusText()));
    QCOMPARE(readFixture(QDir(target).filePath(name)), QByteArray("original"));
    const QString sentinel = directory.filePath(QStringLiteral("sentinel"));
    writeFixture(sentinel, "must stay unchanged");
    QVERIFY(QFile::link(sentinel, QDir(target).filePath(name + QStringLiteral(" (1)"))));
    controller.downloadTo(0, QUrl::fromLocalFile(target));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
    QCOMPARE(readFixture(sentinel), QByteArray("must stay unchanged"));
    QCOMPARE(readFixture(QDir(target).filePath(name + QStringLiteral(" (2)"))), QByteArray("original"));
    controller.uploadFiles({QUrl::fromLocalFile(QDir(source).filePath(name)), QUrl::fromLocalFile(QDir(source).filePath(QStringLiteral("tree")))});
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
    QVERIFY2(controller.connected(), qPrintable(controller.statusText()));
    QCOMPARE(readFixture(QDir(remote).filePath(name)), QByteArray("original"));
    QCOMPARE(readFixture(QDir(remote).filePath(name + QStringLiteral(" (1)"))), QByteArray("upload"));
    QCOMPARE(readFixture(QDir(remote).filePath(QStringLiteral("tree/child.txt"))), QByteArray("nested"));
    QVERIFY(QFile::link(sentinel, QDir(source).filePath(QStringLiteral("tree/secret-link"))));
    controller.uploadFiles({QUrl::fromLocalFile(QDir(source).filePath(QStringLiteral("tree")))});
    QVERIFY(controller.lastTransferText().contains(QStringLiteral("Upload blocked")));
    QVERIFY(!QFileInfo::exists(QDir(remote).filePath(QStringLiteral("tree (1)"))));
    controller.openEntry(0);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
    QCOMPARE(controller.currentPath(), QDir(remote).filePath(QStringLiteral("tree")));
    controller.goBack(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000); QCOMPARE(controller.currentPath(), remote);
    controller.goForward(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000); QCOMPARE(controller.currentPath(), QDir(remote).filePath(QStringLiteral("tree")));
    controller.goUp(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000); QCOMPARE(controller.currentPath(), remote);
    controller.downloadTo(0, QUrl::fromLocalFile(target));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
    QCOMPARE(readFixture(QDir(target).filePath(QStringLiteral("tree/child.txt"))), QByteArray("nested"));
}
int main(int argc, char** argv) {
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_CACHE_HOME", config.filePath(QStringLiteral("cache")).toUtf8());
    QGuiApplication application(argc, argv);
    WorkspaceFeaturesTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "WorkspaceFeaturesTest.moc"
