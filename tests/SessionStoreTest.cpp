#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include <bitshell/core/MacroStore.h>
#include <bitshell/core/YamlFile.h>

#include <bitshell/core/SessionStore.h>
#include <bitshell/core/PasswordStore.h>

#include <algorithm>

using bitshell::core::SessionStore;
using bitshell::core::PasswordStore;

class SessionStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void persistsEditableNonSecretProfiles();
    void buildsAndMaintainsNestedFolderTrees();
    void movesSessionsAndWholeFolderTrees();
    void importsMRemoteNgHierarchyWithoutSecrets();
    void searchesRankedMultiWordResultsAndRefreshes();
    void bulkDeletionIsAtomicRecursiveAndUsesStableIds();
    void deletionRejectsStaleSelectionAndExternalWrites();
    void bulkFolderMacroCleanupPreservesGroupsAndSiblings();
    void passwordProfilesKeepSecretsOutsideTheCatalog();
    void passwordChangesRollbackWhenTheCatalogConflicts();
    void deletingPasswordProfilesRemovesSecretsAndRollsBackConflicts();

private:
    QTemporaryDir m_configRoot;
};

void SessionStoreTest::searchesRankedMultiWordResultsAndRefreshes() {
    SessionStore store;
    QCOMPARE(store.addSession({{QStringLiteral("name"), QStringLiteral("Oracle primary")}, {QStringLiteral("host"), QStringLiteral("db1.example.test")},
        {QStringLiteral("group"), QStringLiteral("Production/Oracle")}, {QStringLiteral("user"), QStringLiteral("operator")},
        {QStringLiteral("tags"), QStringList{QStringLiteral("important")}}}), 0);
    QCOMPARE(store.addSession({{QStringLiteral("name"), QStringLiteral("Oracle")}, {QStringLiteral("host"), QStringLiteral("db2.example.test")}}), 1);
    const auto ranked = store.search(QStringLiteral("oracle"));
    QCOMPARE(ranked.value(QStringLiteral("total")).toInt(), 2);
    QCOMPARE(ranked.value(QStringLiteral("results")).toList()[0].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Oracle"));
    QCOMPARE(store.search(QStringLiteral("PROD important"))[QStringLiteral("total")].toInt(), 1);
    QCOMPARE(store.search(QStringLiteral("operator@db1.example.test"))[QStringLiteral("total")].toInt(), 1);
    QCOMPARE(store.search(QStringLiteral("ssh db1"))[QStringLiteral("total")].toInt(), 1);
    QCOMPARE(store.search(QString(257, u'a'))[QStringLiteral("total")].toInt(), 0);
    QCOMPARE(store.search(QStringLiteral("   "))[QStringLiteral("total")].toInt(), 0);
    QCOMPARE(store.search(QStringLiteral("oracle"), 1)[QStringLiteral("results")].toList().size(), 1);
    QVERIFY(store.updateSession(0, {{QStringLiteral("name"), QStringLiteral("Renamed")}, {QStringLiteral("group"), QStringLiteral("Moved")}}));
    QCOMPARE(store.search(QStringLiteral("renamed moved"))[QStringLiteral("total")].toInt(), 1);
    QVERIFY(store.removeSession(0));
    QCOMPARE(store.search(QStringLiteral("renamed"))[QStringLiteral("total")].toInt(), 0);
}

void SessionStoreTest::bulkDeletionIsAtomicRecursiveAndUsesStableIds() {
    SessionStore store;
    for (const auto& folder : {QStringLiteral("Prod/Child"), QStringLiteral("Production"), QStringLiteral("Keep")})
        QVERIFY(store.addSession({{QStringLiteral("name"), folder}, {QStringLiteral("host"), QStringLiteral("host.example.test")},
                                 {QStringLiteral("group"), folder}}) >= 0);
    QVERIFY(store.addFolder(QStringLiteral("Prod"), QStringLiteral("Empty")));
    QVERIFY(store.assignFolderConnection(QStringLiteral("Prod/Child"), {{QStringLiteral("mode"), QStringLiteral("direct")}}));
    const auto childId = store.get(0).value(QStringLiteral("id")).toString();
    const auto siblingId = store.get(1).value(QStringLiteral("id")).toString();
    const auto keepId = store.get(2).value(QStringLiteral("id")).toString();
    const auto preview = store.deletionPreview({childId, childId}, {QStringLiteral("Prod"), QStringLiteral("Prod/Child")}, false);
    QVERIFY(preview.value(QStringLiteral("valid")).toBool());
    QCOMPARE(preview.value(QStringLiteral("sessions")).toInt(), 1);
    QCOMPARE(preview.value(QStringLiteral("folders")).toInt(), 3);
    QSignalSpy resets(&store, &QAbstractItemModel::modelReset);
    QSignalSpy revisions(&store, &SessionStore::revisionChanged);
    QSignalSpy removedFolders(&store, &SessionStore::foldersRemoved);
    QVERIFY(store.deleteEntries({childId}, {QStringLiteral("Prod")}, false, store.revision()));
    QCOMPARE(resets.size(), 1);
    QCOMPARE(revisions.size(), 1);
    QCOMPARE(removedFolders.size(), 1);
    QCOMPARE(removedFolders[0][0].toStringList(), QStringList{QStringLiteral("Prod")});
    QCOMPARE(store.count(), 2);
    QVERIFY(store.getById(childId).isEmpty());
    QVERIFY(!store.getById(siblingId).isEmpty()); // "Prod" never matches "Production".
    QCOMPARE(store.folderConnection(QStringLiteral("Prod/Child")).value(QStringLiteral("mode")).toString(),
             QStringLiteral("inherit"));
    QCOMPARE(store.search(QStringLiteral("Prod"))[QStringLiteral("total")].toInt(), 1);
    SessionStore reloaded;
    QCOMPARE(reloaded.count(), 2);
    QVERIFY(reloaded.getById(childId).isEmpty());
    QVERIFY(store.deleteEntries({keepId}, {}, false, store.revision())); // Index changed after first delete.
    QCOMPARE(store.count(), 1);
    QCOMPARE(store.get(0).value(QStringLiteral("id")).toString(), siblingId);
    QVERIFY(store.deleteEntries({}, {}, true, store.revision()));
    QCOMPARE(store.count(), 0);
    QCOMPARE(store.folderCount(), 0);
    QVERIFY(store.treeRows({}, {}).isEmpty());
    SessionStore empty;
    QCOMPARE(empty.count(), 0);
    QCOMPARE(empty.folderCount(), 0);
    QVERIFY(!empty.deletionPreview({}, {}, true).value(QStringLiteral("valid")).toBool());
}

void SessionStoreTest::deletionRejectsStaleSelectionAndExternalWrites() {
    SessionStore store;
    QVERIFY(store.addSession({{QStringLiteral("host"), QStringLiteral("one.example.test")}}) >= 0);
    const auto id = store.get(0).value(QStringLiteral("id")).toString();
    const int reviewedRevision = store.revision();
    QVERIFY(store.addSession({{QStringLiteral("host"), QStringLiteral("two.example.test")}}) >= 0);
    QSignalSpy resets(&store, &QAbstractItemModel::modelReset);
    QVERIFY(!store.deleteEntries({id}, {}, false, reviewedRevision));
    QVERIFY(!store.deleteEntries({id, QStringLiteral("missing")}, {}, false, store.revision()));
    QVERIFY(!store.deleteEntries({}, {QStringLiteral("")}, false, store.revision()));
    QVERIFY(!store.deleteEntries({id}, {}, true, store.revision()));
    QCOMPARE(store.count(), 2);
    QCOMPARE(resets.size(), 0);
    QFile file(store.storagePath());
    QVERIFY(file.open(QIODevice::Append));
    QVERIFY(file.write("\n# external editor\n") > 0);
    file.close();
    QVERIFY(!store.deleteEntries({}, {}, true, store.revision()));
    QVERIFY(store.lastError().contains(QStringLiteral("outside")));
    QCOMPARE(store.count(), 2);
    QCOMPARE(resets.size(), 0);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().endsWith("# external editor\n"));
    file.close();
    QVERIFY(store.reload());
    QVERIFY(store.deleteEntries({}, {}, true, store.revision()));
}

void SessionStoreTest::bulkFolderMacroCleanupPreservesGroupsAndSiblings() {
    bitshell::core::MacroStore macros;
    const auto group = macros.saveGroup({}, QStringLiteral("Test buttons"), {});
    QVERIFY(!group.isEmpty());
    for (const auto& folder : {QStringLiteral("Prod"), QStringLiteral("Prod/Child"), QStringLiteral("Production"), QStringLiteral("Other")})
        QVERIFY(macros.assignFolder(folder, group));
    const auto oldRevision = macros.revision();
    QVERIFY(macros.removeFolders({QStringLiteral("Prod"), QStringLiteral("Other")}));
    QCOMPARE(macros.revision(), oldRevision + 1);
    QVERIFY(macros.assignedGroup(QStringLiteral("Prod/Child")).isEmpty());
    QCOMPARE(macros.assignedGroup(QStringLiteral("Production")), group);
    QVERIFY(!macros.group(group).isEmpty());
}

void SessionStoreTest::initTestCase() {
    QVERIFY(m_configRoot.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("session-store-test"));
}

void SessionStoreTest::init() {
    const QString testRoot =
        m_configRoot.filePath(QString::fromLatin1(QTest::currentTestFunction()));
    QDir directory(testRoot);
    if (directory.exists()) {
        QVERIFY(directory.removeRecursively());
    }
    QVERIFY(QDir().mkpath(testRoot));
    qputenv("XDG_CONFIG_HOME", testRoot.toUtf8());
    qputenv("BIT8SHELL_PROFILE", testRoot.toUtf8());
}

void SessionStoreTest::passwordProfilesKeepSecretsOutsideTheCatalog() {
    SessionStore store;
    QVariantMap profile{{QStringLiteral("host"), QStringLiteral("password.example.test")},
        {QStringLiteral("user"), QStringLiteral("operator")}, {QStringLiteral("authenticationMode"), QStringLiteral("password")},
        {QStringLiteral("connection"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("inherit")}}}};
    QVERIFY(store.passwordStatus(profile).value(QStringLiteral("eligible")).toBool());
    QCOMPARE(store.saveSessionWithPassword(-1, profile, {}, QStringLiteral("keep")), 0);
    QCOMPARE(store.get(0).value(QStringLiteral("authenticationMode")).toString(), QStringLiteral("password"));
    QVERIFY(!store.passwordStatus(store.get(0)).value(QStringLiteral("stored")).toBool());
#ifndef Q_OS_WIN
    QSKIP("Persistent password transactions use the isolated Windows DPAPI backend here; no desktop keyring prompt in CI.");
#endif
    const QString secret = QStringLiteral("Private-password-7! λ");
    QCOMPARE(store.saveSessionWithPassword(0, profile, secret, QStringLiteral("replace")), 0);
    auto saved = store.prepareProfile(store.get(0), {}).value(QStringLiteral("profile")).toMap();
    QVERIFY(PasswordStore::has(saved));
    QCOMPARE(PasswordStore::read(saved).value(), secret.toUtf8());
    QVERIFY(store.passwordStatus(store.get(0)).value(QStringLiteral("stored")).toBool());
    const auto candidates = store.sshCandidates(QStringLiteral("password.example.test"), {}, 0, false, {}).value(QStringLiteral("results")).toList();
    QCOMPARE(candidates.first().toMap().value(QStringLiteral("authentication")).toString(), QStringLiteral("Saved password"));
    QVERIFY(!store.get(0).contains(QStringLiteral("password")));
    QFile catalog(store.storagePath()); QVERIFY(catalog.open(QIODevice::ReadOnly));
    QVERIFY(!catalog.readAll().contains(secret.toUtf8())); catalog.close();
    const auto exported = QDir(qEnvironmentVariable("XDG_CONFIG_HOME")).filePath(QStringLiteral("export.yaml"));
    QVERIFY(store.exportYaml(exported));
    QFile exportedFile(exported); QVERIFY(exportedFile.open(QIODevice::ReadOnly));
    QVERIFY(!exportedFile.readAll().contains(secret.toUtf8())); exportedFile.close();
    QCOMPARE(store.duplicateSession(0), 1);
    QVERIFY(!store.passwordStatus(store.get(1)).value(QStringLiteral("stored")).toBool());
    QCOMPARE(store.saveSessionWithPassword(0, {{QStringLiteral("name"), QStringLiteral("Renamed")}}, {}, QStringLiteral("keep")), 0);
    QCOMPARE(PasswordStore::read(saved).value(), secret.toUtf8());
    QCOMPARE(store.saveSessionWithPassword(0, {{QStringLiteral("host"), QStringLiteral("other.example.test")}}, {}, QStringLiteral("keep")), 0);
    QVERIFY(!PasswordStore::has(saved));
    QVERIFY(!store.passwordStatus(store.get(0)).value(QStringLiteral("stored")).toBool());
    QCOMPARE(store.saveSessionWithPassword(0, {}, secret, QStringLiteral("replace")), 0);
    auto changed = store.prepareProfile(store.get(0), {}).value(QStringLiteral("profile")).toMap();
    QCOMPARE(store.saveSessionWithPassword(0, {}, {}, QStringLiteral("remove")), 0);
    QVERIFY(!PasswordStore::has(changed));
    auto routed = profile;
    routed[QStringLiteral("connection")] = QVariantMap{{QStringLiteral("mode"), QStringLiteral("route")}, {QStringLiteral("route"), QStringLiteral("missing")}};
    QVERIFY(!store.passwordStatus(routed).value(QStringLiteral("eligible")).toBool());
    QCOMPARE(store.saveSessionWithPassword(-1, routed, {}, QStringLiteral("keep")), 2); // Manual password remains valid metadata.
}

void SessionStoreTest::passwordChangesRollbackWhenTheCatalogConflicts() {
#ifndef Q_OS_WIN
    QSKIP("Isolated DPAPI transaction coverage is Windows-only.");
#endif
    SessionStore store;
    const QVariantMap profile{{QStringLiteral("host"), QStringLiteral("rollback.example.test")},
        {QStringLiteral("user"), QStringLiteral("operator")}, {QStringLiteral("authenticationMode"), QStringLiteral("password")}};
    QCOMPARE(store.saveSessionWithPassword(-1, profile, QStringLiteral("original-secret"), QStringLiteral("replace")), 0);
    const auto before = store.get(0);
    QFile file(store.storagePath()); QVERIFY(file.open(QIODevice::Append));
    QVERIFY(file.write("\n# external editor\n") > 0); file.close();
    QCOMPARE(store.saveSessionWithPassword(0, {}, QStringLiteral("replacement-secret"), QStringLiteral("replace")), -1);
    QCOMPARE(PasswordStore::read(before).value(), QByteArrayLiteral("original-secret"));
    auto changed = before; changed[QStringLiteral("host")] = QStringLiteral("new.example.test");
    QCOMPARE(store.saveSessionWithPassword(0, changed, QStringLiteral("new-target-secret"), QStringLiteral("replace")), -1);
    QVERIFY(!PasswordStore::has(changed));
    QCOMPARE(store.get(0), before);
    QVERIFY(store.reload());
    QVERIFY(store.removeSession(0));
    QVERIFY(!PasswordStore::has(before));
}

void SessionStoreTest::deletingPasswordProfilesRemovesSecretsAndRollsBackConflicts() {
#ifndef Q_OS_WIN
    QSKIP("Isolated DPAPI transaction coverage is Windows-only.");
#endif
    SessionStore store;
    const QVariantMap profile{{QStringLiteral("host"), QStringLiteral("delete.example.test")},
        {QStringLiteral("user"), QStringLiteral("operator")}, {QStringLiteral("authenticationMode"), QStringLiteral("password")}};
    QCOMPARE(store.saveSessionWithPassword(-1, profile, QStringLiteral("delete-secret"), QStringLiteral("replace")), 0);
    const auto saved = store.get(0);
    QFile file(store.storagePath()); QVERIFY(file.open(QIODevice::Append));
    QVERIFY(file.write("\n# deletion conflict\n") > 0); file.close();
    QVERIFY(!store.removeSession(0));
    QVERIFY(PasswordStore::has(saved));
    QVERIFY(!store.deleteEntries({}, {}, true, store.revision()));
    QCOMPARE(PasswordStore::read(saved).value(), QByteArrayLiteral("delete-secret"));
    QVERIFY(store.reload());
    QVERIFY(store.deleteEntries({}, {}, true, store.revision()));
    QVERIFY(!PasswordStore::has(saved));
    QCOMPARE(store.count(), 0);
}

void SessionStoreTest::persistsEditableNonSecretProfiles() {
    SessionStore store;
    QVERIFY(store.storagePath().startsWith(qEnvironmentVariable("XDG_CONFIG_HOME")));
    QCOMPARE(store.count(), 0);

    QVariantMap profile{
        {QStringLiteral("name"), QStringLiteral("Production DB")},
        {QStringLiteral("host"), QStringLiteral("db.example.test")},
        {QStringLiteral("user"), QStringLiteral("operator")},
        {QStringLiteral("port"), 2222},
        {QStringLiteral("group"), QStringLiteral("Production")},
        {QStringLiteral("tags"), QStringList{QStringLiteral("oracle"),
                                              QStringLiteral("primary")}},
        {QStringLiteral("identityFile"), QStringLiteral("~/.ssh/id_test")},
        {QStringLiteral("proxyJump"), QStringLiteral("bastion.example.test")},
        {QStringLiteral("forwardX11"), true},
        {QStringLiteral("agentForwarding"), true},
        {QStringLiteral("compression"), true},
        {QStringLiteral("backspaceMode"), QStringLiteral("bs")},
        {QStringLiteral("password"), QStringLiteral("must-not-be-stored")},
    };

    QCOMPARE(store.addSession(profile), 0);
    QCOMPARE(store.count(), 1);
    QCOMPARE(store.get(0).value(QStringLiteral("host")).toString(),
             QStringLiteral("db.example.test"));
    QVERIFY(!store.get(0).contains(QStringLiteral("password")));
    QCOMPARE(store.get(0).value(QStringLiteral("backspaceMode")).toString(),
             QStringLiteral("bs"));
    QCOMPARE(store.find(QStringLiteral("production db")), 0);
    QCOMPARE(store.find(QStringLiteral("operator@db.example.test")), 0);
    QCOMPARE(store.find(QStringLiteral("does-not-exist")), -1);

    QVariantMap changes{
        {QStringLiteral("host"), QStringLiteral("db-new.example.test")},
        {QStringLiteral("port"), 22},
    };
    QVERIFY(store.updateSession(0, changes));
    QCOMPARE(store.get(0).value(QStringLiteral("host")).toString(),
             QStringLiteral("db-new.example.test"));
    QCOMPARE(store.get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("Production DB"));

    QCOMPARE(store.duplicateSession(0), 1);
    QCOMPARE(store.count(), 2);
    QVERIFY(store.get(1).value(QStringLiteral("name")).toString().endsWith(
        QStringLiteral(" copy")));
    QVERIFY(store.removeSession(1));
    QCOMPARE(store.count(), 1);

    SessionStore reloaded;
    QCOMPARE(reloaded.count(), 1);
    QCOMPARE(reloaded.get(0).value(QStringLiteral("host")).toString(),
             QStringLiteral("db-new.example.test"));

    QFile catalog(store.storagePath());
    QVERIFY(catalog.open(QIODevice::ReadOnly));
    const QByteArray persisted = catalog.readAll();
    QVERIFY(persisted.contains(QByteArrayLiteral("db-new.example.test")));
    QVERIFY(!persisted.contains(QByteArrayLiteral("must-not-be-stored")));
}

void SessionStoreTest::buildsAndMaintainsNestedFolderTrees() {
    SessionStore store;
    QVERIFY(store.addFolder({}, QStringLiteral("Development")));
    QVERIFY(store.addFolder(QStringLiteral("Development"), QStringLiteral("Empty")));

    const QVariantMap profile{
        {QStringLiteral("name"), QStringLiteral("db-20")},
        {QStringLiteral("host"), QStringLiteral("db20.example.test")},
        {QStringLiteral("user"), QStringLiteral("operator")},
        {QStringLiteral("group"), QStringLiteral("Production / Oracle / DC1")},
        {QStringLiteral("tags"), QStringList{QStringLiteral("primary")}},
    };
    QCOMPARE(store.addSession(profile), 0);
    QCOMPARE(store.folderCount(), 5);

    const QVariantList collapsed = store.treeRows({}, {});
    QCOMPARE(collapsed.size(), 2);
    QCOMPARE(collapsed.at(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Development"));
    QCOMPARE(collapsed.at(1).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Production"));

    const QVariantList expanded = store.treeRows(
        {},
        {QStringLiteral("Production"),
         QStringLiteral("Production/Oracle"),
         QStringLiteral("Production/Oracle/DC1")});
    const auto sessionItem = std::find_if(
        expanded.cbegin(), expanded.cend(), [](const QVariant& value) {
            return value.toMap().value(QStringLiteral("kind")).toString()
                   == QStringLiteral("session");
        });
    QVERIFY(sessionItem != expanded.cend());
    QCOMPARE(sessionItem->toMap().value(QStringLiteral("depth")).toInt(), 3);
    QCOMPARE(sessionItem->toMap().value(QStringLiteral("sourceIndex")).toInt(), 0);

    const QVariantList searched = store.treeRows(QStringLiteral("db20"), {});
    QCOMPARE(std::count_if(searched.cbegin(), searched.cend(), [](const QVariant& value) {
                 return value.toMap().value(QStringLiteral("kind")).toString()
                        == QStringLiteral("folder");
             }),
             3);
    QCOMPARE(std::count_if(searched.cbegin(), searched.cend(), [](const QVariant& value) {
                 return value.toMap().value(QStringLiteral("kind")).toString()
                        == QStringLiteral("session");
             }),
             1);

    QVERIFY(store.renameFolder(QStringLiteral("Production/Oracle"),
                               QStringLiteral("Databases")));
    QCOMPARE(store.get(0).value(QStringLiteral("group")).toString(),
             QStringLiteral("Production/Databases/DC1"));
    QVERIFY(!store.removeFolder(QStringLiteral("Production/Databases")));
    QVERIFY(store.lastError().contains(QStringLiteral("sessions")));
    QVERIFY(store.removeFolder(QStringLiteral("Development")));

    SessionStore reloaded;
    QCOMPARE(reloaded.count(), 1);
    QCOMPARE(reloaded.folderCount(), 3);
    QCOMPARE(reloaded.get(0).value(QStringLiteral("group")).toString(),
             QStringLiteral("Production/Databases/DC1"));
}

void SessionStoreTest::movesSessionsAndWholeFolderTrees() {
    SessionStore store;
    QVERIFY(store.addFolder({}, QStringLiteral("Source")));
    QVERIFY(store.addFolder(QStringLiteral("Source"), QStringLiteral("Nested")));
    QVERIFY(store.addFolder({}, QStringLiteral("Target")));
    QVERIFY(store.addFolder({}, QStringLiteral("Archive")));
    QVERIFY(store.addFolder(QStringLiteral("Archive"), QStringLiteral("Source")));

    QCOMPARE(store.addSession({
                 {QStringLiteral("name"), QStringLiteral("nested-db")},
                 {QStringLiteral("host"), QStringLiteral("nested.example.test")},
                 {QStringLiteral("group"), QStringLiteral("Source/Nested")},
             }),
             0);
    QCOMPARE(store.addSession({
                 {QStringLiteral("name"), QStringLiteral("loose-host")},
                 {QStringLiteral("host"), QStringLiteral("loose.example.test")},
             }),
             1);

    QVERIFY(!store.moveSession(1, QStringLiteral("Missing")));
    QCOMPARE(store.get(1).value(QStringLiteral("group")).toString(), QString{});
    QVERIFY(store.moveSession(1, QStringLiteral("Target")));
    QCOMPARE(store.get(1).value(QStringLiteral("group")).toString(),
             QStringLiteral("Target"));

    QVERIFY(!store.moveFolder(QStringLiteral("Source"), QStringLiteral("Source/Nested")));
    QVERIFY(store.lastError().contains(QStringLiteral("inside itself")));
    QVERIFY(!store.moveFolder(QStringLiteral("Source"), QStringLiteral("Archive")));
    QVERIFY(store.lastError().contains(QStringLiteral("already exists")));

    QVERIFY(store.moveFolder(QStringLiteral("Source"), QStringLiteral("Target")));
    QCOMPARE(store.get(0).value(QStringLiteral("group")).toString(),
             QStringLiteral("Target/Source/Nested"));
    QVERIFY(store.folderPaths().contains(QStringLiteral("Target/Source")));
    QVERIFY(store.folderPaths().contains(QStringLiteral("Target/Source/Nested")));
    QVERIFY(!store.folderPaths().contains(QStringLiteral("Source")));

    QVERIFY(store.moveFolder(QStringLiteral("Target/Source"), {}));
    QCOMPARE(store.get(0).value(QStringLiteral("group")).toString(),
             QStringLiteral("Source/Nested"));
    QVERIFY(store.moveSession(1, {}));
    QCOMPARE(store.get(1).value(QStringLiteral("group")).toString(), QString{});

    SessionStore reloaded;
    QCOMPARE(reloaded.get(0).value(QStringLiteral("group")).toString(),
             QStringLiteral("Source/Nested"));
    QCOMPARE(reloaded.get(1).value(QStringLiteral("group")).toString(), QString{});
    QVERIFY(reloaded.folderPaths().contains(QStringLiteral("Source/Nested")));
}

void SessionStoreTest::importsMRemoteNgHierarchyWithoutSecrets() {
    const QString xmlPath = m_configRoot.filePath(QStringLiteral("sample.confCons"));
    QFile xmlFile(xmlPath);
    QVERIFY(xmlFile.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray xml = QByteArrayLiteral(R"xml(<?xml version="1.0"?>
<mrng:Connections xmlns:mrng="http://mremoteng.org" Protected="encrypted-root-secret">
  <Node Name="Oracle" Type="Container">
    <Node Name="Production" Type="Container">
      <Node Name="db-02" Type="Connection" Id="22222222-2222-2222-2222-222222222222"
            Hostname="cyberark-gateway" Username="account@target" Password="secret-two"
            Protocol="SSH2" Port="2222" />
      <Node Name="db-10" Type="Connection" Id="11111111-1111-1111-1111-111111111111"
            Hostname="db10.example.test" Username="operator" Password="secret-one"
            Protocol="SSH2" Port="22" />
      <Node Name="desktop" Type="Connection" Hostname="rdp.example.test"
            Protocol="RDP" Port="3389" />
    </Node>
    <Node Name="Empty" Type="Container" />
  </Node>
</mrng:Connections>)xml");
    QCOMPARE(xmlFile.write(xml), xml.size());
    xmlFile.close();

    SessionStore store;
    QCOMPARE(store.importMRemoteNg(QUrl::fromLocalFile(xmlPath).toString()), 2);
    QCOMPARE(store.count(), 2);
    QCOMPARE(store.folderCount(), 3);
    QVERIFY(store.lastImportSummary().contains(QStringLiteral("1 unsupported")));
    QCOMPARE(store.get(0).value(QStringLiteral("group")).toString(),
             QStringLiteral("Oracle/Production"));
    QCOMPARE(store.get(0).value(QStringLiteral("user")).toString(),
             QStringLiteral("account@target"));

    QCOMPARE(store.importMRemoteNg(xmlPath), 0);
    QVERIFY(store.lastImportSummary().contains(QStringLiteral("2 duplicate")));

    QFile catalog(store.storagePath());
    QVERIFY(catalog.open(QIODevice::ReadOnly));
    const QByteArray persisted = catalog.readAll();
    QVERIFY(persisted.contains(QByteArrayLiteral("Oracle/Production")));
    QVERIFY(!persisted.contains(QByteArrayLiteral("secret-one")));
    QVERIFY(!persisted.contains(QByteArrayLiteral("secret-two")));
    QVERIFY(!persisted.contains(QByteArrayLiteral("encrypted-root-secret")));
}

QTEST_APPLESS_MAIN(SessionStoreTest)

#include "SessionStoreTest.moc"
