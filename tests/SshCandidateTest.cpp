#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <bitshell/core/SessionStore.h>

using bitshell::core::SessionStore;

namespace {
QVariantMap profile(const QString& id, const QString& host, const QString& user = {}, int port = 22) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), id},
            {QStringLiteral("host"), host}, {QStringLiteral("user"), user}, {QStringLiteral("port"), port}};
}

QVariantMap literalAccount(const QString& name) {
    return {{QStringLiteral("source"), QStringLiteral("literal")}, {QStringLiteral("name"), name}};
}

QVariantMap route(const QVariantMap& account) {
    return {{QStringLiteral("type"), QStringLiteral("cyberark-psmp")},
            {QStringLiteral("name"), QStringLiteral("Production gateway")},
            {QStringLiteral("host"), QStringLiteral("psmp.example.test")},
            {QStringLiteral("port"), 22}, {QStringLiteral("account"), account}};
}

QVariantList results(const SessionStore& store, const QString& host, const QString& user = {},
                     int port = 0, bool cyberArkEnabled = false, const QString& identity = {}) {
    return store.sshCandidates(host, user, port, cyberArkEnabled, identity).value(QStringLiteral("results")).toList();
}
}

class SshCandidateTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void matchesOnlyExactTargetsAndNormalizesIpv6();
    void constrainsUserAndPortWithoutChoosingAnArbitraryAccount();
    void resolvesInheritedCyberArkAccountsAndExplicitOverrides();
    void blocksMissingRoutesDisabledModeAndMissingIdentity();
    void offersExplicitRoutesAlongsideSavedHostsWithoutChangingTheCatalog();
    void remainsReadOnlyBoundedAndDoesNotOpenKeyFiles();
private:
    QTemporaryDir m_configRoot;
};

void SshCandidateTest::initTestCase() {
    QVERIFY(m_configRoot.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("ssh-candidate-test"));
}

void SshCandidateTest::init() {
    const QString root = m_configRoot.filePath(QString::fromLatin1(QTest::currentTestFunction()));
    QVERIFY(QDir().mkpath(root));
    qputenv("BIT8SHELL_PROFILE", root.toUtf8());
    qputenv("XDG_CONFIG_HOME", root.toUtf8());
}

void SshCandidateTest::matchesOnlyExactTargetsAndNormalizesIpv6() {
    SessionStore store;
    QCOMPARE(store.addSession(profile(QStringLiteral("dns"), QStringLiteral("db.example.test"))), 0);
    QCOMPARE(store.addSession(profile(QStringLiteral("not-a-host-match"), QStringLiteral("db.example.test.evil"))), 1);
    QCOMPARE(store.addSession(profile(QStringLiteral("ip"), QStringLiteral("10.20.30.4"))), 2);
    QCOMPARE(store.addSession(profile(QStringLiteral("other-ip"), QStringLiteral("10.20.30.40"))), 3);
    QCOMPARE(store.addSession(profile(QStringLiteral("ipv6"), QStringLiteral("[2001:db8::1]"))), 4);
    QCOMPARE(store.addSession(profile(QStringLiteral("alias"), QStringLiteral("unrelated.example.test"))), 5);
    QVERIFY(store.updateSession(5, {{QStringLiteral("name"), QStringLiteral("db.example.test")}}));

    const auto dns = results(store, QStringLiteral("DB.EXAMPLE.TEST."));
    QCOMPARE(dns.size(), 1);
    QCOMPARE(dns.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("dns"));
    QCOMPARE(results(store, QStringLiteral("10.20.30.4")).size(), 1);
    QVERIFY(results(store, QStringLiteral("10.20.30")).isEmpty());
    QVERIFY(results(store, QStringLiteral("db")).isEmpty());
    const auto ipv6 = results(store, QStringLiteral("2001:0DB8:0000:0000:0000:0000:0000:0001"));
    QCOMPARE(ipv6.size(), 1);
    QCOMPARE(ipv6.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("ipv6"));
    QVERIFY(results(store, QStringLiteral("2001:db8::2")).isEmpty());
    for (const auto& host : {QString{}, QStringLiteral(" db.example.test"), QStringLiteral("-oProxyCommand=bad"),
                             QStringLiteral("db.example.test;whoami"), QString(254, u'a')})
        QVERIFY(results(store, host).isEmpty());
}

void SshCandidateTest::constrainsUserAndPortWithoutChoosingAnArbitraryAccount() {
    SessionStore store;
    const QString host = QStringLiteral("10.20.30.4");
    QCOMPARE(store.addSession(profile(QStringLiteral("z-account"), host, QStringLiteral("root"))), 0);
    QCOMPARE(store.addSession(profile(QStringLiteral("a-account"), host, QStringLiteral("deploy"))), 1);
    QCOMPARE(store.addSession(profile(QStringLiteral("b-account"), host, QStringLiteral("deploy"), 2222)), 2);
    QCOMPARE(store.addSession(profile(QStringLiteral("default-user"), host)), 3);
    const auto all = results(store, host);
    QCOMPARE(all.size(), 4);
    QCOMPARE(all.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("a-account"));
    QCOMPARE(results(store, host, QStringLiteral("deploy")).size(), 2);
    const auto constrained = results(store, host, QStringLiteral("deploy"), 2222);
    QCOMPARE(constrained.size(), 1);
    QCOMPARE(constrained.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("b-account"));
    QCOMPARE(constrained.first().toMap().value(QStringLiteral("connectionMode")).toString(), QStringLiteral("direct"));
    QCOMPARE(constrained.first().toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("session"));
    QVERIFY(constrained.first().toMap().value(QStringLiteral("valid")).toBool());
    QVERIFY(results(store, host, QStringLiteral("Deploy")).isEmpty());
    QVERIFY(results(store, host, QStringLiteral("missing")).isEmpty());
    QVERIFY(results(store, host, {}, -1).isEmpty());
    QVERIFY(results(store, host, {}, 65536).isEmpty());
    QVERIFY(results(store, host, QStringLiteral("root;whoami")).isEmpty());
    SessionStore reloaded;
    QCOMPARE(results(reloaded, host), all);
}

void SshCandidateTest::resolvesInheritedCyberArkAccountsAndExplicitOverrides() {
    SessionStore store;
    const QString host = QStringLiteral("db.example.test");
    const QVariantMap personal{{QStringLiteral("source"), QStringLiteral("identity")},
                              {QStringLiteral("suffix"), QStringLiteral("SYS")},
                              {QStringLiteral("domain"), QStringLiteral("corp.example.test")}};
    QVERIFY(store.saveRoute(QStringLiteral("production"), route(personal)));
    auto inherited = profile(QStringLiteral("inherited"), host, QStringLiteral("ignored-direct-user"));
    inherited[QStringLiteral("group")] = QStringLiteral("Production/Database");
    inherited[QStringLiteral("connection")] = QVariantMap{{QStringLiteral("mode"), QStringLiteral("inherit")}};
    QCOMPARE(store.addSession(inherited), 0);
    QVERIFY(store.assignFolderConnection(QStringLiteral("Production"),
        {{QStringLiteral("mode"), QStringLiteral("route")}, {QStringLiteral("route"), QStringLiteral("production")}}));

    const auto matches = results(store, host, QStringLiteral("employeeSYS"), 22, true, QStringLiteral("employee"));
    QCOMPARE(matches.size(), 1);
    const auto candidate = matches.first().toMap();
    QCOMPARE(candidate.value(QStringLiteral("id")).toString(), QStringLiteral("inherited"));
    QCOMPARE(candidate.value(QStringLiteral("connectionMode")).toString(), QStringLiteral("cyberark"));
    QCOMPARE(candidate.value(QStringLiteral("routeId")).toString(), QStringLiteral("production"));
    QCOMPARE(candidate.value(QStringLiteral("gateway")).toString(), QStringLiteral("psmp.example.test"));
    QCOMPARE(candidate.value(QStringLiteral("account")).toString(), QStringLiteral("employeeSYS@corp.example.test"));
    QVERIFY(candidate.value(QStringLiteral("valid")).toBool());
    QVERIFY(results(store, host, QStringLiteral("ignored-direct-user"), 0, true, QStringLiteral("employee")).isEmpty());
    QCOMPARE(results(store, host, QStringLiteral("employeeSYS@corp.example.test"), 0, true, QStringLiteral("employee")).size(), 1);

    // The nearest folder assignment overrides the route account; a session
    // override wins over both folder and route without changing canonical id.
    QVERIFY(store.assignFolderConnection(QStringLiteral("Production/Database"),
        {{QStringLiteral("mode"), QStringLiteral("route")}, {QStringLiteral("route"), QStringLiteral("production")},
         {QStringLiteral("account"), literalAccount(QStringLiteral("oracle"))}}));
    QCOMPARE(results(store, host, QStringLiteral("oracle"), 0, true, QStringLiteral("employee")).size(), 1);
    QVERIFY(results(store, host, QStringLiteral("employeeSYS"), 0, true, QStringLiteral("employee")).isEmpty());
    QVERIFY(store.updateSession(0, {{QStringLiteral("connection"), QVariantMap{
        {QStringLiteral("mode"), QStringLiteral("inherit")}, {QStringLiteral("account"), literalAccount(QStringLiteral("admin"))}}}}));
    QCOMPARE(results(store, host, QStringLiteral("admin"), 0, true, QStringLiteral("employee")).size(), 1);
    QVERIFY(results(store, host, QStringLiteral("oracle"), 0, true, QStringLiteral("employee")).isEmpty());

    QVERIFY(store.updateSession(0, {{QStringLiteral("connection"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("direct")}}}}));
    const auto direct = results(store, host, QStringLiteral("ignored-direct-user"));
    QCOMPARE(direct.size(), 1);
    QVERIFY(direct.first().toMap().value(QStringLiteral("valid")).toBool());
    QCOMPARE(direct.first().toMap().value(QStringLiteral("connectionMode")).toString(), QStringLiteral("direct"));
}

void SshCandidateTest::blocksMissingRoutesDisabledModeAndMissingIdentity() {
    SessionStore store;
    const QString host = QStringLiteral("10.20.30.4");
    QVERIFY(store.saveRoute(QStringLiteral("required"), route(literalAccount(QStringLiteral("oracle")))));
    auto saved = profile(QStringLiteral("routed"), host, QStringLiteral("direct-user"));
    saved[QStringLiteral("connection")] = QVariantMap{{QStringLiteral("mode"), QStringLiteral("route")},
                                                      {QStringLiteral("route"), QStringLiteral("required")}};
    QCOMPARE(store.addSession(saved), 0);
    auto candidate = results(store, host, {}, 0, false, QStringLiteral("employee")).first().toMap();
    QVERIFY(!candidate.value(QStringLiteral("valid")).toBool());
    QVERIFY(candidate.value(QStringLiteral("reason")).toString().contains(QStringLiteral("Enable CyberArk")));
    candidate = results(store, host, {}, 0, true).first().toMap();
    QVERIFY(!candidate.value(QStringLiteral("valid")).toBool());
    QVERIFY(candidate.value(QStringLiteral("reason")).toString().contains(QStringLiteral("employee username")));
    QVERIFY(results(store, host, {}, 0, true, QStringLiteral("employee")).first().toMap().value(QStringLiteral("valid")).toBool());
    QVERIFY(store.updateSession(0, {{QStringLiteral("port"), 2222}}));
    candidate = results(store, host, {}, 0, true, QStringLiteral("employee")).first().toMap();
    QVERIFY(!candidate.value(QStringLiteral("valid")).toBool());
    QVERIFY(candidate.value(QStringLiteral("reason")).toString().contains(QStringLiteral("port 22")));
    QVERIFY(store.removeRoute(QStringLiteral("required")));
    candidate = results(store, host, {}, 0, true, QStringLiteral("employee")).first().toMap();
    QCOMPARE(candidate.value(QStringLiteral("id")).toString(), QStringLiteral("routed"));
    QCOMPARE(candidate.value(QStringLiteral("connectionMode")).toString(), QStringLiteral("cyberark"));
    QVERIFY(!candidate.value(QStringLiteral("valid")).toBool());
    QVERIFY(candidate.value(QStringLiteral("reason")).toString().contains(QStringLiteral("Direct fallback is blocked")));
}

void SshCandidateTest::remainsReadOnlyBoundedAndDoesNotOpenKeyFiles() {
    SessionStore store;
    const QString host = QStringLiteral("db.example.test");
    auto keyFile = profile(QStringLiteral("key-file"), host, QStringLiteral("operator"));
    keyFile[QStringLiteral("identityFile")] = m_configRoot.filePath(QStringLiteral("nonexistent-private-key"));
    QCOMPARE(store.addSession(keyFile), 0);
    auto managed = profile(QStringLiteral("managed-key"), host, QStringLiteral("deploy"));
    managed[QStringLiteral("identityKey")] = QString(32, u'a');
    QCOMPARE(store.addSession(managed), 1);
    auto noKey = profile(QStringLiteral("no-key"), host);
    noKey[QStringLiteral("identityKey")] = QStringLiteral("none");
    QCOMPARE(store.addSession(noKey), 2);
    QFile catalog(store.storagePath());
    QVERIFY(catalog.open(QIODevice::ReadOnly));
    const auto before = catalog.readAll();
    catalog.close();
    const int revision = store.revision();
    QSignalSpy changed(&store, &SessionStore::revisionChanged);
    const auto limited = store.sshCandidates(host, {}, 0, false, {}, 1);
    QCOMPARE(limited.value(QStringLiteral("total")).toInt(), 3);
    QCOMPARE(limited.value(QStringLiteral("revision")).toInt(), revision);
    QCOMPARE(limited.value(QStringLiteral("results")).toList().size(), 1);
    const auto candidates = results(store, host);
    QCOMPARE(candidates.size(), 3);
    for (const auto& value : candidates) {
        const auto candidate = value.toMap();
        QVERIFY(candidate.value(QStringLiteral("valid")).toBool()); // No existence/key-content read during suggestions.
        QVERIFY(!candidate.contains(QStringLiteral("password")));
        QVERIFY(!candidate.contains(QStringLiteral("privateKey")));
        QVERIFY(!candidate.contains(QStringLiteral("profile"))); // Caller resolves the id afresh on activation.
    }
    QCOMPARE(candidates[0].toMap().value(QStringLiteral("authentication")).toString(), QStringLiteral("Key file: nonexistent-private-key"));
    QCOMPARE(candidates[1].toMap().value(QStringLiteral("identityKey")).toString(), QString(32, u'a'));
    QCOMPARE(candidates[2].toMap().value(QStringLiteral("authentication")).toString(), QStringLiteral("No saved SSH key"));
    QCOMPARE(store.revision(), revision);
    QCOMPARE(changed.size(), 0);
    QVERIFY(store.lastError().isEmpty());
    QVERIFY(catalog.open(QIODevice::ReadOnly));
    QCOMPARE(catalog.readAll(), before);
}

void SshCandidateTest::offersExplicitRoutesAlongsideSavedHostsWithoutChangingTheCatalog() {
    SessionStore store;
    const QString host = QStringLiteral("10.20.30.4");
    QCOMPARE(store.addSession(profile(QStringLiteral("saved-direct"), host, QStringLiteral("deploy"))), 0);
    QVERIFY(store.saveRoute(QStringLiteral("oracle"), route(literalAccount(QStringLiteral("oracle")))));
    QVERIFY(store.saveRoute(QStringLiteral("personal"), route({{QStringLiteral("source"), QStringLiteral("identity")},
                                                              {QStringLiteral("suffix"), QStringLiteral("SYS")}})));
    const int revision = store.revision();
    const auto routes = store.sshRouteCandidates(host, {}, 0, true, QStringLiteral("employee"));
    QCOMPARE(routes.value(QStringLiteral("total")).toInt(), 2);
    QCOMPARE(routes.value(QStringLiteral("revision")).toInt(), revision);
    QCOMPARE(results(store, host).size(), 1); // Saved profiles and explicit route choices stay separate.
    for (const auto& value : routes.value(QStringLiteral("results")).toList()) {
        const auto candidate = value.toMap();
        QCOMPARE(candidate.value(QStringLiteral("kind")).toString(), QStringLiteral("route"));
        QVERIFY(candidate.value(QStringLiteral("id")).toString().isEmpty());
        QVERIFY(!candidate.value(QStringLiteral("routeId")).toString().isEmpty());
        QCOMPARE(candidate.value(QStringLiteral("port")).toInt(), 22);
        QVERIFY(candidate.value(QStringLiteral("valid")).toBool());
    }
    QCOMPARE(store.sshRouteCandidates(host, QStringLiteral("oracle"), 0, true, QStringLiteral("employee"))
                 .value(QStringLiteral("total")).toInt(), 1);
    QCOMPARE(store.sshRouteCandidates(host, QStringLiteral("employeeSYS"), 0, true, QStringLiteral("employee"))
                 .value(QStringLiteral("total")).toInt(), 1);
    QCOMPARE(store.sshRouteCandidates(host, QStringLiteral("deploy"), 0, true, QStringLiteral("employee"))
                 .value(QStringLiteral("total")).toInt(), 0); // Typed users never invent a route account override.
    QCOMPARE(store.sshRouteCandidates(QStringLiteral("new-host.example.test"), {}, 0, true, QStringLiteral("employee"))
                 .value(QStringLiteral("total")).toInt(), 2);
    QCOMPARE(store.sshRouteCandidates(host, {}, 0, false, QStringLiteral("employee"))
                 .value(QStringLiteral("total")).toInt(), 0);
    for (const auto& identity : {QString{}, QStringLiteral("bad identity")}) {
        const auto disabled = store.sshRouteCandidates(host, {}, 0, true, identity).value(QStringLiteral("results")).toList();
        QCOMPARE(disabled.size(), 2);
        for (const auto& value : disabled) {
            QVERIFY(!value.toMap().value(QStringLiteral("valid")).toBool());
            QVERIFY(!value.toMap().value(QStringLiteral("reason")).toString().isEmpty());
        }
    }
    for (const auto& value : store.sshRouteCandidates(host, {}, 2222, true, QStringLiteral("employee"))
                                 .value(QStringLiteral("results")).toList()) {
        QVERIFY(!value.toMap().value(QStringLiteral("valid")).toBool());
        QVERIFY(value.toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("port 22")));
    }
    QCOMPARE(store.sshRouteCandidates(QStringLiteral("ssh 10.20.30.4"), {}, 0, true, QStringLiteral("employee"))
                 .value(QStringLiteral("total")).toInt(), 0);
    const auto limited = store.sshRouteCandidates(host, {}, 0, true, QStringLiteral("employee"), 1);
    QCOMPARE(limited.value(QStringLiteral("total")).toInt(), 2);
    QCOMPARE(limited.value(QStringLiteral("results")).toList().size(), 1);
    QCOMPARE(store.count(), 1);
    QCOMPARE(store.revision(), revision);
}

QTEST_APPLESS_MAIN(SshCandidateTest)
#include "SshCandidateTest.moc"
