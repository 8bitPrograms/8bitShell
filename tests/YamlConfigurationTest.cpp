#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QUrl>
#include <QScopeGuard>
#include <cerrno>
#include <bitshell/core/YamlFile.h>
#include <bitshell/core/AppSettings.h>
#include <bitshell/core/MacroStore.h>
#include <bitshell/core/SessionStore.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/PersonalProfile.h>
#ifdef Q_OS_WIN
#include <objbase.h>
#pragma comment(lib, "ole32.lib")
#endif

using namespace bitshell::core;

class YamlConfigurationTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    QString path(const QString& name) const {
        return QDir(qEnvironmentVariable("XDG_CONFIG_HOME")).filePath(name);
    }
    void write(const QString& name, const QByteArray& data) {
        const auto target = path(name);
        QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath()));
        QFile file(target); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(data), data.size());
    }
    QByteArray read(const QString& name) const {
        QFile file(path(name)); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
    }
    static QVariantList buttons(const QString& text) {
        return {QVariantMap{{QStringLiteral("label"), QStringLiteral("Run query")},
            {QStringLiteral("text"), text}, {QStringLiteral("enter"), false}}};
    }
private slots:
    void init() {
        QVERIFY(directory.isValid());
        qputenv("XDG_CONFIG_HOME", directory.filePath(QString::fromLatin1(QTest::currentTestFunction())).toUtf8());
        QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("yaml-config-test"));
    }
    void canonicalProfileRoundTripPreservesPrivateMaterial() {
        const QByteArray original("version: 2\nkind: 8bitshell/sessions\nsessions:\n  - id: db\n    name: Database\n    host: db.example.test\nfolders: []\n");
        write(QStringLiteral("8bitshell/sessions.yaml"), original);
        const QByteArray material("private-key-fixture");
        write(QStringLiteral("8bitshell/keys/kept-key"), material);
        QCOMPARE(Platform::appConfigDirectory(), path(QStringLiteral("8bitshell")));
        SessionStore sessions;
        QCOMPARE(sessions.count(), 1);
        QVERIFY2(sessions.updateSession(0, {{QStringLiteral("name"), QStringLiteral("Renamed")}}),
                 qPrintable(sessions.lastError()));
        QVERIFY(read(QStringLiteral("8bitshell/sessions.yaml")).contains("kind: 8bitshell/sessions"));
        QVERIFY(sessions.exportYaml(path(QStringLiteral("export.yaml"))));
        QVERIFY(read(QStringLiteral("export.yaml")).contains("kind: 8bitshell/sessions"));
        QCOMPARE(read(QStringLiteral("8bitshell/keys/kept-key")), material);
    }
    void unrelatedProfileDirectoryIsNeverAdopted() {
        write(QStringLiteral("unrelated/settings.yaml"), "version: 1\nkind: other/settings\nappearance:\n  uiTheme: nord\n");
        const QByteArray material("unrelated-private-key-fixture");
        write(QStringLiteral("unrelated/keys/key"), material);
        QCOMPARE(Platform::appConfigDirectory(), path(QStringLiteral("8bitshell")));
        AppSettings settings;
        QVERIFY2(settings.lastError().isEmpty(), qPrintable(settings.lastError()));
        QCOMPARE(settings.uiThemeId(), QStringLiteral("catppuccin-mocha"));
        QCOMPARE(read(QStringLiteral("unrelated/keys/key")), material);
        QVERIFY(!QFileInfo::exists(path(QStringLiteral("8bitshell/keys/key"))));
    }
    void canonicalProfileDoesNotMergeOtherCatalogs() {
        write(QStringLiteral("8bitshell/sessions.yaml"), "version: 2\nkind: 8bitshell/sessions\nsessions: []\nfolders: []\n");
        const QByteArray other("version: 2\nkind: other/sessions\nsessions:\n  - id: other\n    name: Other\n    host: host.example.test\nfolders: []\n");
        write(QStringLiteral("unrelated/sessions.yaml"), other);
        SessionStore sessions;
        QCOMPARE(sessions.count(), 0);
        QCOMPARE(read(QStringLiteral("unrelated/sessions.yaml")), other);
        QVERIFY2(sessions.lastError().isEmpty(), qPrintable(sessions.lastError()));
    }
    void explicitProfileUsesOnlyItsOwnSettings() {
        write(QStringLiteral("8bitshell/settings.yaml"), "version: 1\nkind: 8bitshell/settings\nappearance:\n  uiTheme: dracula\n");
        write(QStringLiteral("portable/settings.yaml"), "version: 1\nkind: 8bitshell/settings\nappearance:\n  uiTheme: nord\n");
        qputenv("BIT8SHELL_PROFILE", path(QStringLiteral("portable")).toUtf8());
        const auto restore = qScopeGuard([] { qunsetenv("BIT8SHELL_PROFILE"); });
        QCOMPARE(Platform::appConfigDirectory(), path(QStringLiteral("portable")));
        AppSettings settings;
        QCOMPARE(settings.uiThemeId(), QStringLiteral("nord"));
        QVERIFY(read(QStringLiteral("8bitshell/settings.yaml")).contains("dracula"));
    }

    void documentKindCannotBypassSchemaOrVersion() {
        QVariantMap document; QString error;
        QVERIFY(YamlFile::decode("version: 1\nkind: 8bitshell/settings\n", QStringLiteral("8bitshell/settings"), document, error));
        QVERIFY(!YamlFile::decode("version: 1\nkind: 8bitshell/sessions\n", QStringLiteral("8bitshell/settings"), document, error));
        QVERIFY(!YamlFile::decode("version: 99\nkind: 8bitshell/settings\n", QStringLiteral("8bitshell/settings"), document, error));
        QVERIFY(!YamlFile::decode("version: 1\nkind: other/settings\n", QStringLiteral("8bitshell/settings"), document, error));
    }
    void exactMultilineAndScalarRoundTrip() {
        const QStringList texts{QStringLiteral("select *\nfrom v$database;"),
            QStringLiteral("select *\nfrom v$database;\n"), QStringLiteral("line\n\n"),
            QStringLiteral("  indented\n    line"), QStringLiteral("\nleading newline"),
            QStringLiteral("true"), QStringLiteral("00123"), QStringLiteral("null"), QString{},
            QStringLiteral("yes # no : 'quoted' \"double\" \\ "), QStringLiteral("Türkçe: çalışma 🫐"),
            QStringLiteral("tab\tdata\r\nnext\n")};
        QVariantMap data{{QStringLiteral("texts"), texts}, {QStringLiteral("boolean"), true},
            {QStringLiteral("integer"), 22}, {QStringLiteral("real"), 13.5}};
        const auto bytes = YamlFile::encode(data, QStringLiteral("8bitshell/test"));
        QVERIFY(bytes.contains("|-\n"));
        QVariantMap decoded; QString error;
        QVERIFY2(YamlFile::decode(bytes, QStringLiteral("8bitshell/test"), decoded, error), qPrintable(error));
        QCOMPARE(decoded.value(QStringLiteral("texts")).toStringList(), texts);
        QCOMPARE(decoded.value(QStringLiteral("boolean")).typeId(), QMetaType::Bool);
        QCOMPARE(decoded.value(QStringLiteral("integer")).toInt(), 22);
        QCOMPARE(decoded.value(QStringLiteral("real")).toDouble(), 13.5);
    }
    void rejectsMalformedAndBoundedAliasExpansion() {
        const QByteArray header("version: 1\nkind: 8bitshell/test\n");
        const QList<QByteArray> invalid{
            "version: 2\nkind: 8bitshell/test\n", "version: '1'\nkind: 8bitshell/test\n",
            "version: 1\nkind: wrong\n", header + "a: 1\na: 2\n",
            header + "a: [broken\n", header + "a: !!python/object dangerous\n",
            header + "loop: &loop [*loop]\n", header + "? [1, 2]\n: value\n",
            header + "---\nversion: 1\nkind: 8bitshell/test\n",
            header + "a: " + QByteArray(40, '[') + "1" + QByteArray(40, ']'),
            header + "a: '" + QByteArray::fromHex("c328") + "'\n", header + "a: " + QByteArray::fromHex("e280")};
        for (const auto& data : invalid) {
            QVariantMap result; QString error;
            QVERIFY2(!YamlFile::decode(data, QStringLiteral("8bitshell/test"), result, error), data.constData());
            QVERIFY(!error.isEmpty());
        }
    }
    void legacyFilesMigrateWithoutTouchingOriginals() {
        const QByteArray sessions(R"({"version":3,"folders":["Oracle/Prod"],"sessions":[{"id":"db1","name":"Database","host":"db.example.test","group":"Oracle/Prod","port":22,"backspaceMode":"bs"}]})");
        const QByteArray macros(R"({"version":1,"groups":[{"id":"oracle","name":"Oracle","buttons":[{"label":"Query","text":"select *\nfrom dual;","enter":false}]}],"folders":{"Oracle":"oracle"}})");
        write(QStringLiteral("8bitshell/sessions.json"), sessions);
        write(QStringLiteral("8bitshell/macros.json"), macros);
        {
            QSettings ini(path(QStringLiteral("8bitshell/settings.ini")), QSettings::IniFormat);
            ini.setValue(QStringLiteral("appearance/uiTheme"), QStringLiteral("nord"));
            ini.setValue(QStringLiteral("terminal/fontSize"), 17.5);
            ini.setValue(QStringLiteral("terminal/copyOnSelect"), true);
            ini.setValue(QStringLiteral("navigation/expandedFolders"), QStringList{QStringLiteral("Oracle"), QStringLiteral("Oracle/Prod")});
            ini.sync();
        }
        const auto oldIni = read(QStringLiteral("8bitshell/settings.ini"));
        SessionStore catalog; MacroStore macrosStore; AppSettings settings;
        QVERIFY2(catalog.lastError().isEmpty(), qPrintable(catalog.lastError()));
        QVERIFY2(macrosStore.lastError().isEmpty(), qPrintable(macrosStore.lastError()));
        QVERIFY2(settings.lastError().isEmpty(), qPrintable(settings.lastError()));
        QCOMPARE(catalog.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("db1"));
        QCOMPARE(catalog.get(0).value(QStringLiteral("group")).toString(), QStringLiteral("Oracle/Prod"));
        QCOMPARE(macrosStore.assignedGroup(QStringLiteral("Oracle")), QStringLiteral("oracle"));
        QCOMPARE(settings.uiThemeId(), QStringLiteral("nord")); QCOMPARE(settings.terminalFontSize(), 17.5);
        QVERIFY(settings.copyOnSelect()); QCOMPARE(settings.expandedSessionFolders().size(), 2);
        QVERIFY(QFileInfo::exists(catalog.storagePath())); QVERIFY(QFileInfo::exists(macrosStore.storagePath()));
        QCOMPARE(read(QStringLiteral("8bitshell/sessions.json")), sessions);
        QCOMPARE(read(QStringLiteral("8bitshell/macros.json")), macros);
        QCOMPARE(read(QStringLiteral("8bitshell/settings.ini")), oldIni);
        SessionStore again; MacroStore macrosAgain; AppSettings settingsAgain;
        QCOMPARE(again.count(), 1); QCOMPARE(macrosAgain.groups().size(), 1);
        QCOMPARE(settingsAgain.terminalFontSize(), 17.5);
    }
    void legacySettingsRejectSymlinksAndOversizedFiles() {
        write(QStringLiteral("original.ini"), "[terminal]\nfontSize=18\n");
        QVERIFY(QDir().mkpath(path(QStringLiteral("8bitshell"))));
#ifdef Q_OS_WIN
        // QFile::link creates a Shell shortcut, not an NTFS symbolic link.
        // It must not be followed as a legacy settings document. Native reparse
        // point rejection is covered separately by WindowsSecurityTest.
        const auto link = path(QStringLiteral("8bitshell/settings.ini.lnk"));
#else
        const auto link = path(QStringLiteral("8bitshell/settings.ini"));
#endif
#ifdef Q_OS_WIN
        // QFile::link uses the shell link COM API. This test process is not a GUI app.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
        const bool created = QFile::link(path(QStringLiteral("original.ini")), link);
#ifdef Q_OS_WIN
        const qlonglong linkError = GetLastError();
        if (SUCCEEDED(com)) CoUninitialize();
#else
        const qlonglong linkError = errno;
#endif
        QVERIFY2(created, qPrintable(QStringLiteral("shortcut creation failed (%1)").arg(linkError)));
        {
        AppSettings linked;
#ifdef Q_OS_WIN
        QVERIFY(linked.lastError().isEmpty());
        QCOMPARE(linked.terminalFontSize(), 13.0);
        // With no legacy document, normal startup creates a default YAML file.
        QVERIFY(QFileInfo::exists(path(QStringLiteral("8bitshell/settings.yaml"))));
#else
        QVERIFY(!linked.lastError().isEmpty());
        QVERIFY(!QFileInfo::exists(path(QStringLiteral("8bitshell/settings.yaml"))));
#endif
        }
#ifdef Q_OS_WIN
        QVERIFY(QFile::remove(path(QStringLiteral("8bitshell/settings.yaml"))));
#endif
        QVERIFY(QFile::remove(link)); // Test-owned link/shortcut only.
        write(QStringLiteral("8bitshell/settings.ini"), QByteArray(8 * 1024 * 1024 + 1, ' '));
        AppSettings oversized;
        QVERIFY(!oversized.lastError().isEmpty());
        QVERIFY(!QFileInfo::exists(path(QStringLiteral("8bitshell/settings.yaml"))));
        QCOMPARE(read(QStringLiteral("original.ini")), QByteArray("[terminal]\nfontSize=18\n"));
    }
    void rejectsCatalogPathAmplificationAndOversizedMacroGroups() {
        const QString deep = QStringLiteral("x/").repeated(10000) + QStringLiteral("leaf");
        write(QStringLiteral("deep.yaml"), YamlFile::encode({{QStringLiteral("sessions"), QVariantList{}},
            {QStringLiteral("folders"), QStringList{deep}}}, QStringLiteral("8bitshell/sessions")));
        SessionStore store;
        const auto before = read(QStringLiteral("8bitshell/sessions.yaml"));
        QCOMPARE(store.importYaml(path(QStringLiteral("deep.yaml"))), -1);
        QCOMPARE(read(QStringLiteral("8bitshell/sessions.yaml")), before);
        QCOMPARE(store.count(), 0);
        QVERIFY(!store.addFolder({}, QString(1025, u'x')));
        QCOMPARE(store.addSession({{QStringLiteral("host"), QStringLiteral("host.test")},
            {QStringLiteral("group"), deep}}), -1);
        QCOMPARE(store.count(), 0);
        MacroStore macros;
        QVariantList tooMany;
        for (int i = 0; i < 257; ++i) tooMany << buttons(QStringLiteral("echo safe")).first();
        QVERIFY(macros.saveGroup({}, QStringLiteral("Too many"), tooMany).isEmpty());
        QVERIFY(macros.groups().isEmpty());
    }
    void sessionsExportMergeAndImmediateReload() {
        SessionStore store;
        QCOMPARE(store.addSession({{QStringLiteral("host"), QStringLiteral("original.test")},
            {QStringLiteral("id"), QStringLiteral("db")}, {QStringLiteral("group"), QStringLiteral("Oracle/Prod")}}), 0);
        QVERIFY(store.addFolder({}, QStringLiteral("Empty")));
        const auto target = path(QStringLiteral("catalog export.yaml"));
        QVERIFY(store.exportYaml(QUrl::fromLocalFile(target).toString()));
        QVERIFY(read(QStringLiteral("catalog export.yaml")).contains("folder:"));
        QVERIFY(store.updateSession(0, {{QStringLiteral("host"), QStringLiteral("edited.test")}}));
        QCOMPARE(store.importYaml(target), 0);
        QCOMPARE(store.get(0).value(QStringLiteral("host")).toString(), QStringLiteral("edited.test"));
        QVERIFY(!store.exportYaml(store.storagePath()));
        // An external edit must be visible without restarting, and must not be erased by a stale UI save.
        auto bytes = read(QStringLiteral("8bitshell/sessions.yaml"));
        bytes.replace("edited.test", "external.test");
        write(QStringLiteral("8bitshell/sessions.yaml"), bytes);
        QVERIFY(!store.moveSession(0, QStringLiteral("Empty")));
        QVERIFY(store.lastError().contains(QStringLiteral("Reload")));
        QCOMPARE(read(QStringLiteral("8bitshell/sessions.yaml")), bytes);
        QSignalSpy revised(&store, &SessionStore::revisionChanged);
        QVERIFY(store.reload()); QVERIFY(!revised.isEmpty());
        QCOMPARE(store.get(0).value(QStringLiteral("host")).toString(), QStringLiteral("external.test"));
        QVERIFY(store.moveSession(0, QStringLiteral("Empty")));
        SessionStore reloaded; QCOMPARE(reloaded.get(0).value(QStringLiteral("group")).toString(), QStringLiteral("Empty"));
    }
    void malformedFilesBlockWritesButRecoverAfterReload() {
        write(QStringLiteral("8bitshell/sessions.yaml"), "version: 1\nkind: 8bitshell/sessions\nsessions: [broken\n");
        write(QStringLiteral("8bitshell/macros.yaml"), "version: 999\nkind: 8bitshell/macros\n");
        write(QStringLiteral("8bitshell/settings.yaml"), "version: 1\nkind: 8bitshell/settings\nterminal:\n  fontSizTypo: 20\n");
        SessionStore sessions; MacroStore macros; AppSettings settings;
        QVERIFY(!sessions.lastError().isEmpty()); QVERIFY(!macros.lastError().isEmpty()); QVERIFY(!settings.lastError().isEmpty());
        const auto a = read(QStringLiteral("8bitshell/sessions.yaml"));
        const auto b = read(QStringLiteral("8bitshell/macros.yaml"));
        const auto c = read(QStringLiteral("8bitshell/settings.yaml"));
        QCOMPARE(sessions.addSession({{QStringLiteral("host"), QStringLiteral("test")}}), -1);
        QVERIFY(macros.saveGroup({}, QStringLiteral("No"), {}).isEmpty());
        settings.setTerminalFontSize(22); QCOMPARE(settings.terminalFontSize(), 13.0);
        QCOMPARE(read(QStringLiteral("8bitshell/sessions.yaml")), a);
        QCOMPARE(read(QStringLiteral("8bitshell/macros.yaml")), b);
        QCOMPARE(read(QStringLiteral("8bitshell/settings.yaml")), c);
        write(QStringLiteral("8bitshell/settings.yaml"), "version: 1\nkind: 8bitshell/settings\nterminal:\n  fontSize: 19\n");
        QVERIFY(settings.reload()); QCOMPARE(settings.terminalFontSize(), 19.0);
        settings.setTerminalFontSize(20);
        QVERIFY2(qFuzzyCompare(settings.terminalFontSize(), 20.0), qPrintable(settings.lastError()));
    }
    void macroExportsConflictsAndFolderAssignments() {
        MacroStore store;
        const QString first = store.saveGroup(QStringLiteral("oracle"), QStringLiteral("Oracle"), buttons(QStringLiteral("select *\nfrom dual;")));
        const QString second = store.saveGroup(QStringLiteral("linux"), QStringLiteral("Linux"), buttons(QStringLiteral("uname -a")));
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        QVERIFY(store.assignFolder(QStringLiteral("Oracle"), first));
        const auto single = path(QStringLiteral("one.yaml")), all = path(QStringLiteral("all.yaml"));
        QVERIFY(store.exportYaml(single, first)); QVERIFY(store.exportYaml(all));
        QVariantMap one; QString error;
        QVERIFY(YamlFile::decode(read(QStringLiteral("one.yaml")), QStringLiteral("8bitshell/macros"), one, error));
        QCOMPARE(one.value(QStringLiteral("groups")).toList().size(), 1);
        QVERIFY(one.value(QStringLiteral("folders")).toMap().isEmpty());
        QVERIFY(!store.exportYaml(store.storagePath(), first));
        QVERIFY(!store.saveGroup(first, QStringLiteral("Renamed"), buttons(QStringLiteral("different"))).isEmpty());
        QCOMPARE(store.importYaml(single), 1);
        QCOMPARE(store.groups().size(), 3);
        QCOMPARE(store.group(first).value(QStringLiteral("name")).toString(), QStringLiteral("Renamed"));
        QCOMPARE(store.assignedGroup(QStringLiteral("Oracle")), first);
        // A clean catalog imports complete assignments only on explicit opt-in.
        qputenv("XDG_CONFIG_HOME", directory.filePath(QStringLiteral("new-machine")).toUtf8());
        MacroStore fresh;
        QCOMPARE(fresh.importYaml(all), 2);
        QVERIFY(fresh.assignedGroup(QStringLiteral("Oracle")).isEmpty());
        QCOMPARE(fresh.importYaml(all, true), 0);
        QCOMPARE(fresh.effectiveGroup(QStringLiteral("Oracle/Prod/Cluster")).value(QStringLiteral("id")).toString(), first);
        QVERIFY(!fresh.saveGroup(first, QStringLiteral("New Oracle"), buttons(QStringLiteral("new"))).isEmpty());
        QCOMPARE(fresh.effectiveGroup(QStringLiteral("Oracle/Prod")).value(QStringLiteral("name")).toString(), QStringLiteral("New Oracle"));
    }
    void settingsImportIsAtomicAndEmitsLiveSignals() {
        AppSettings settings;
        const auto before = read(QStringLiteral("8bitshell/settings.yaml"));
        write(QStringLiteral("bad.yaml"), "version: 1\nkind: 8bitshell/settings\nappearance:\n  uiTheme: nord\nterminal:\n  fontSize: -1\n");
        QVERIFY(!settings.importYaml(path(QStringLiteral("bad.yaml"))));
        QCOMPARE(settings.uiThemeId(), QStringLiteral("catppuccin-mocha"));
        QCOMPARE(read(QStringLiteral("8bitshell/settings.yaml")), before);
        write(QStringLiteral("good.yaml"), "version: 1\nkind: 8bitshell/settings\nappearance:\n  uiTheme: nord\n  mode: light\nterminal:\n  fontSize: 16.5\nshortcuts:\n  copy: Ctrl+Insert\n  paste: Shift+Insert\n");
        QSignalSpy changed(&settings, &AppSettings::appearanceChanged);
        QVERIFY2(settings.importYaml(path(QStringLiteral("good.yaml"))), qPrintable(settings.lastError()));
        QVERIFY(!changed.isEmpty()); QVERIFY(!settings.darkAppearance());
        QCOMPARE(settings.terminalFontSize(), 16.5);
        QCOMPARE(QKeySequence(settings.keyBindings().value(QStringLiteral("copy")).toString()), QKeySequence(QStringLiteral("Ctrl+Insert")));
        const auto destination = path(QStringLiteral("settings-export.yaml"));
        QVERIFY(settings.exportYaml(destination));
        settings.restoreDefaults();
        QVERIFY(settings.importYaml(destination));
        QCOMPARE(settings.terminalFontSize(), 16.5);
        QVERIFY(!settings.exportYaml(settings.storagePath()));
        // Reject shortcut conflicts as one transaction, including default bindings.
        write(QStringLiteral("conflict.yaml"), "version: 1\nkind: 8bitshell/settings\nshortcuts:\n  paste: Ctrl+Insert\n");
        QVERIFY(!settings.importYaml(path(QStringLiteral("conflict.yaml"))));
        QCOMPARE(QKeySequence(settings.keyBindings().value(QStringLiteral("paste")).toString()), QKeySequence(QStringLiteral("Shift+Insert")));
    }
    void rejectsUnknownAndDangerousCatalogFields() {
        SessionStore store;
        write(QStringLiteral("bad.yaml"), "version: 1\nkind: 8bitshell/sessions\nsessions:\n  - id: db\n    host: example.test\n    password: do-not-store\n");
        QCOMPARE(store.importYaml(path(QStringLiteral("bad.yaml"))), -1);
        QCOMPARE(store.count(), 0);
        write(QStringLiteral("bad.yaml"), "version: 1\nkind: 8bitshell/sessions\nsessions:\n  - id: db\n    host: example.test\n    port: 22.5\n");
        QCOMPARE(store.importYaml(path(QStringLiteral("bad.yaml"))), -1);
        MacroStore macros;
        write(QStringLiteral("macros-bad.yaml"), "version: 1\nkind: 8bitshell/macros\ngroups: []\nfolders:\n  Oracle: missing\n");
        QCOMPARE(macros.importYaml(path(QStringLiteral("macros-bad.yaml")), true), -1);
        QVERIFY(macros.groups().isEmpty());
    }
    void importsLargeCatalogInOneModelUpdate() {
        QVariantList entries;
        for (int i = 0; i < 1000; ++i) entries << QVariantMap{
            {QStringLiteral("id"), QStringLiteral("db-%1").arg(i)},
            {QStringLiteral("host"), QStringLiteral("db%1.example.test").arg(i)},
            {QStringLiteral("folder"), QStringLiteral("Company/Region%1/Databases").arg(i % 20)}};
        write(QStringLiteral("large.yaml"), YamlFile::encode({{QStringLiteral("sessions"), entries}}, QStringLiteral("8bitshell/sessions")));
        SessionStore store; QSignalSpy reset(&store, &QAbstractItemModel::modelReset);
        QList<int> previousCounts;
        connect(&store, &QAbstractItemModel::modelAboutToBeReset, this, [&] { previousCounts << store.count(); });
        QCOMPARE(store.importYaml(path(QStringLiteral("large.yaml"))), 1000);
        QCOMPARE(reset.size(), 1); QCOMPARE(store.count(), 1000); QCOMPARE(store.folderCount(), 41);
        QCOMPARE(previousCounts, QList<int>{0});
        QCOMPARE(store.importYaml(path(QStringLiteral("large.yaml"))), 0);
        SessionStore loaded; QCOMPARE(loaded.count(), 1000); QCOMPARE(loaded.folderCount(), 41);
    }
    void documentedExamplesAreActuallyImportable() {
        const auto folder = QDir(QString::fromUtf8(BIT8SHELL_TEST_SOURCE_DIR)).filePath(QStringLiteral("examples/config"));
        SessionStore sessions; MacroStore macros; AppSettings settings;
        QCOMPARE(sessions.importYaml(QDir(folder).filePath(QStringLiteral("sessions.yaml"))), 2);
        QCOMPARE(sessions.importYaml(QDir(folder).filePath(QStringLiteral("cyberark-sessions.yaml"))), 3);
        QCOMPARE(macros.importYaml(QDir(folder).filePath(QStringLiteral("macros.yaml")), true), 2);
        QVERIFY2(settings.importYaml(QDir(folder).filePath(QStringLiteral("settings.yaml"))), qPrintable(settings.lastError()));
        QCOMPARE(macros.effectiveGroup(QStringLiteral("Databases/Oracle/Production")).value(QStringLiteral("id")).toString(), QStringLiteral("oracle-tools"));
        QCOMPARE(macros.effectiveGroup(QStringLiteral("Linux/Development")).value(QStringLiteral("id")).toString(), QStringLiteral("linux-tools"));
    }
};
QTEST_MAIN(YamlConfigurationTest)
#include "YamlConfigurationTest.moc"
