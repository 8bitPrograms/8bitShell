#include <QtTest>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <bitshell/core/ConnectionRouting.h>
#include <bitshell/core/PersonalProfile.h>
#include <bitshell/core/SessionStore.h>
#include <bitshell/core/AppSettings.h>
#include <bitshell/core/SshSecurity.h>
#include <bitshell/core/PrivateFile.h>
using namespace bitshell::core;

class ConnectionRoutingTest : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    QString path(const QString& name) const { return QDir(qEnvironmentVariable("XDG_CONFIG_HOME")).filePath(name); }
    static QVariantMap account() { return {{QStringLiteral("source"), QStringLiteral("identity")}, {QStringLiteral("suffix"), QStringLiteral("SYS")}, {QStringLiteral("domain"), QStringLiteral("corp.example.test")}}; }
    static QVariantMap route() { return {{QStringLiteral("type"), QStringLiteral("cyberark-psmp")}, {QStringLiteral("host"), QStringLiteral("psmp.example.test")}, {QStringLiteral("port"), 22}, {QStringLiteral("account"), account()}}; }
    static QVariantMap selection(const QString& mode, const QString& id = {}) {
        QVariantMap result{{QStringLiteral("mode"), mode}};
        if (!id.isEmpty()) result[QStringLiteral("route")] = id;
        return result;
    }
    static QVariantMap session() {
        return {{QStringLiteral("host"), QStringLiteral("db.example.test")}, {QStringLiteral("user"), QStringLiteral("other")},
            {QStringLiteral("group"), QStringLiteral("Oracle/Prod")}, {QStringLiteral("connection"), selection(QStringLiteral("inherit"))}};
    }
    void write(const QString& name, const QByteArray& bytes) { QString error; QVERIFY2(PrivateFile::write(path(name), bytes, error), qPrintable(error)); }
    QByteArray read(const QString& name) const { QByteArray bytes; QString error; (void)PrivateFile::read(path(name), bytes, error); return bytes; }
private slots:
    void init() { QVERIFY(temp.isValid()); qputenv("XDG_CONFIG_HOME", temp.filePath(QString::fromLatin1(QTest::currentTestFunction())).toUtf8()); }
    void formatsAndAllTransportsFailClosed() {
        PersonalProfile personal; QVERIFY(personal.setUsername(QStringLiteral("EMP000123")));
        auto definition = route();
        const QVariantMap routes{{QStringLiteral("work"), definition}};
        const QVariantMap folders{{QStringLiteral("Oracle"), selection(QStringLiteral("route"), QStringLiteral("work"))}};
        const auto prepared = ConnectionRouting::prepare(session(), routes, folders, personal.username());
        QVERIFY2(prepared, qPrintable(prepared ? QString{} : prepared.error()));
        const QString expected = QStringLiteral("EMP000123@EMP000123SYS%corp.example.test@db.example.test");
        QCOMPARE(*ConnectionRouting::login(*prepared), expected);
        QVERIFY(!SshSecurity::command(*prepared, SshSecurity::Transport::Terminal));
        QVERIFY(personal.approveRoute(ConnectionRouting::fingerprint(definition)));
        const auto terminal = SshSecurity::command(*prepared, SshSecurity::Transport::Terminal);
        QVERIFY2(terminal, qPrintable(terminal ? QString{} : terminal.error()));
        QCOMPARE(terminal->arguments.at(terminal->arguments.indexOf(QStringLiteral("-l")) + 1), expected);
        QCOMPARE(terminal->arguments.last(), QStringLiteral("psmp.example.test"));
        QVERIFY(!SshSecurity::command(*prepared, SshSecurity::Transport::Sftp));
        QVERIFY(!SshSecurity::command(*prepared, SshSecurity::Transport::Monitor));
        // OpenSSH -G is configuration evaluation only: no DNS/auth/connection.
        QProcess ssh; auto args = terminal->arguments; args.prepend(QStringLiteral("-G"));
        ssh.setProcessEnvironment(SshSecurity::environment()); ssh.start(terminal->program, args);
        QVERIFY(ssh.waitForFinished(5000)); QCOMPARE(ssh.exitCode(), 0);
        const auto config = ssh.readAllStandardOutput().replace("\r\n", "\n");
        QVERIFY(config.contains("user " + expected.toUtf8() + '\n'));
        QVERIFY(config.contains("hostname psmp.example.test\n"));
        QVERIFY(config.contains("stricthostkeychecking true\n") || config.contains("stricthostkeychecking yes\n"));
        definition[QStringLiteral("allowSftp")] = true; definition[QStringLiteral("allowMonitoring")] = true;
        auto opc = session(); auto chosen = selection(QStringLiteral("route"), QStringLiteral("work"));
        chosen[QStringLiteral("account")] = QVariantMap{{QStringLiteral("source"), QStringLiteral("literal")}, {QStringLiteral("name"), QStringLiteral("opc")}};
        opc[QStringLiteral("connection")] = chosen;
        const auto fixed = ConnectionRouting::prepare(opc, {{QStringLiteral("work"), definition}}, {}, personal.username());
        QVERIFY(fixed); QCOMPARE(*ConnectionRouting::login(*fixed), QStringLiteral("EMP000123@opc@db.example.test"));
        QVERIFY(!SshSecurity::command(*fixed, SshSecurity::Transport::Sftp)); // changed capabilities need new approval
        QVERIFY(personal.approveRoute(ConnectionRouting::fingerprint(definition)));
        const auto sftp = SshSecurity::command(*fixed, SshSecurity::Transport::Sftp);
        const auto monitor = SshSecurity::command(*fixed, SshSecurity::Transport::Monitor);
        QVERIFY(sftp && monitor);
        QCOMPARE(sftp->arguments.last(), QStringLiteral("EMP000123@opc@db.example.test@psmp.example.test"));
        QVERIFY(sftp->arguments.contains(QStringLiteral("-oBatchMode=yes")));
        QCOMPARE(monitor->arguments.last(), QStringLiteral("psmp.example.test"));
        QVERIFY(personal.clearRouteApprovals());
        QVERIFY(!SshSecurity::command(*prepared, SshSecurity::Transport::Terminal));
        QVERIFY(!SshSecurity::command(*fixed, SshSecurity::Transport::Monitor));
    }
    void inheritedExceptionsAndMissingRequirements() {
        const QVariantMap routes{{QStringLiteral("work"), route()}};
        QVariantMap folders{{QStringLiteral("Oracle"), selection(QStringLiteral("route"), QStringLiteral("work"))}};
        QVERIFY(!ConnectionRouting::prepare(session(), routes, folders, {}));
        QVERIFY(!ConnectionRouting::prepare(session(), {}, folders, QStringLiteral("EMP1")));
        QVERIFY(!SshSecurity::command(session(), SshSecurity::Transport::Terminal)); // raw inherit is never direct
        folders[QStringLiteral("Oracle/Prod")] = selection(QStringLiteral("direct"));
        const auto direct = ConnectionRouting::prepare(session(), routes, folders, {});
        QVERIFY(direct && !direct->contains(QStringLiteral("psmp")));
        QCOMPARE(direct->value(QStringLiteral("user")).toString(), QStringLiteral("other"));
        QVERIFY(SshSecurity::command(*direct, SshSecurity::Transport::Terminal));
        auto explicitDirect = session(); explicitDirect[QStringLiteral("connection")] = selection(QStringLiteral("direct"));
        folders.remove(QStringLiteral("Oracle/Prod"));
        QVERIFY(ConnectionRouting::prepare(explicitDirect, {}, folders, {}));
        const auto first = ConnectionRouting::prepare(session(), routes, folders, QStringLiteral("EmployeeA"));
        const auto second = ConnectionRouting::prepare(session(), routes, folders, QStringLiteral("EmployeeB"));
        QVERIFY(first && second); QVERIFY(ConnectionRouting::login(*first) != ConnectionRouting::login(*second));
        QCOMPARE(*ConnectionRouting::login(*first), QStringLiteral("EmployeeA@EmployeeASYS%corp.example.test@db.example.test"));
        auto legacyUser = session(); legacyUser[QStringLiteral("user")] = QStringLiteral("old@account%domain@target");
        const auto migrated = ConnectionRouting::prepare(legacyUser, routes, folders, QStringLiteral("EMP1"));
        QVERIFY(migrated); QVERIFY(SshSecurity::validateProfile(*migrated).isEmpty());
        const auto snapshot = *ConnectionRouting::login(*first);
        folders[QStringLiteral("Oracle")] = selection(QStringLiteral("direct"));
        QCOMPARE(*ConnectionRouting::login(*first), snapshot); // Open tabs retain a frozen plan.
    }
    void rejectsDelimitersAndUnknownFields() {
        const QVariantMap routes{{QStringLiteral("work"), route()}};
        const QVariantMap folders{{QStringLiteral("Oracle"), selection(QStringLiteral("route"), QStringLiteral("work"))}};
        for (const auto& input : {QStringLiteral("EMP@x"), QStringLiteral("EMP%x"), QStringLiteral("EMP\nx"), QStringLiteral("EMP$(id)"), QStringLiteral("-oProxyCommand=id"), QString(129, u'a')})
            QVERIFY(!ConnectionRouting::prepare(session(), routes, folders, input));
        for (const auto& input : {QStringLiteral("opc@other"), QStringLiteral("opc%domain"), QStringLiteral("opc;id"), QStringLiteral("opc\r"), QStringLiteral("opc space")}) {
            auto invalid = route(); invalid[QStringLiteral("account")] = QVariantMap{{QStringLiteral("source"), QStringLiteral("literal")}, {QStringLiteral("name"), input}};
            QVERIFY(!ConnectionRouting::prepare(session(), {{QStringLiteral("work"), invalid}}, folders, QStringLiteral("EMP1")));
        }
        auto invalid = route(); invalid[QStringLiteral("command")] = QStringLiteral("id"); QString error;
        QVERIFY(!ConnectionRouting::validateRoute(invalid, error));
        auto profile = session(); profile[QStringLiteral("port")] = 2222;
        QVERIFY(!ConnectionRouting::prepare(profile, routes, folders, QStringLiteral("EMP1")));
        profile = session(); profile[QStringLiteral("proxyJump")] = QStringLiteral("jump.test");
        QVERIFY(!ConnectionRouting::prepare(profile, routes, folders, QStringLiteral("EMP1")));
        // The direct validator and key-file expansion restrictions are unchanged.
        QVERIFY(!SshSecurity::safeUser(QStringLiteral("operator%domain")));
        auto prepared = ConnectionRouting::prepare(session(), routes, folders, QStringLiteral("EMP1")); QVERIFY(prepared);
        (*prepared)[QStringLiteral("identityFile")] = QStringLiteral("/tmp/%h");
        QVERIFY(!SshSecurity::validateProfile(*prepared).isEmpty());
    }
    void catalogRoundTripMovesAndCollisionProtection() {
        SessionStore store; QVERIFY(store.addFolder({}, QStringLiteral("Oracle"))); QVERIFY(store.addFolder(QStringLiteral("Oracle"), QStringLiteral("Prod")));
        QVERIFY(store.saveRoute(QStringLiteral("work"), route()));
        QVERIFY(store.assignFolderConnection(QStringLiteral("Oracle"), selection(QStringLiteral("route"), QStringLiteral("work"))));
        QCOMPARE(store.addSession(session()), 0);
        QVERIFY(!store.prepareProfile(store.get(0), QStringLiteral("EMP1")).contains(QStringLiteral("error")));
        QVERIFY(store.renameFolder(QStringLiteral("Oracle"), QStringLiteral("Database")));
        QCOMPARE(store.folderConnection(QStringLiteral("Database")).value(QStringLiteral("route")).toString(), QStringLiteral("work"));
        QVERIFY(!store.prepareProfile(store.get(0), QStringLiteral("EMP1")).contains(QStringLiteral("error")));
        QVERIFY(store.addFolder({}, QStringLiteral("Team")));
        QVERIFY(store.moveFolder(QStringLiteral("Database"), QStringLiteral("Team")));
        QVERIFY(store.folderConnection(QStringLiteral("Team/Database")).contains(QStringLiteral("route")));
        QVERIFY(store.exportYaml(path(QStringLiteral("shared.yaml"))));
        const auto shared = read(QStringLiteral("shared.yaml"));
        QVERIFY(shared.contains("version: 2")); QVERIFY(!shared.contains("EMP1"));
        SessionStore reloaded; QCOMPARE(reloaded.count(), 1); QCOMPARE(reloaded.routes(), store.routes());
        QCOMPARE(reloaded.find(QStringLiteral("ssh db.example.test")), 0);
        QVERIFY(reloaded.moveSession(0, {}));
        QVERIFY(reloaded.removeFolder(QStringLiteral("Team/Database")));
        QCOMPARE(reloaded.folderConnection(QStringLiteral("Team/Database")).value(QStringLiteral("mode")).toString(), QStringLiteral("inherit"));
        auto different = route(); different[QStringLiteral("host")] = QStringLiteral("different.example.test");
        QVERIFY(reloaded.saveRoute(QStringLiteral("work"), different));
        const auto before = read(QStringLiteral("8bitshell/sessions.yaml"));
        QCOMPARE(reloaded.importYaml(path(QStringLiteral("shared.yaml"))), -1);
        QCOMPARE(read(QStringLiteral("8bitshell/sessions.yaml")), before);
        QVERIFY(reloaded.removeRoute(QStringLiteral("work")));
        QVERIFY(reloaded.assignFolderConnection({}, selection(QStringLiteral("route"), QStringLiteral("work"))));
        QVERIFY(reloaded.prepareProfile(reloaded.get(0), QStringLiteral("EMP1")).contains(QStringLiteral("error")));
    }
    void folderAncestorsAndVersionsAreBounded() {
        SessionStore store;
        auto importedSession = session();
        importedSession[QStringLiteral("id")] = QStringLiteral("db");
        importedSession[QStringLiteral("folder")] = importedSession.take(QStringLiteral("group"));
        QVariantMap document{{QStringLiteral("sessions"), QVariantList{importedSession}},
            {QStringLiteral("routes"), QVariantMap{{QStringLiteral("work"), route()}}},
            {QStringLiteral("folderConnections"), QVariantMap{{QStringLiteral("Oracle"), selection(QStringLiteral("route"), QStringLiteral("work"))}}}};
        write(QStringLiteral("implicit.yaml"), YamlFile::encode(document, QStringLiteral("8bitshell/sessions"), 2));
        QCOMPARE(store.importYaml(path(QStringLiteral("implicit.yaml"))), 1);
        QVERIFY(!store.prepareProfile(store.get(0), QStringLiteral("EMP1")).contains(QStringLiteral("error")));
        auto inheritedAccount = selection(QStringLiteral("inherit")); inheritedAccount[QStringLiteral("account")] = account();
        QVERIFY(!store.assignFolderConnection(QStringLiteral("Oracle"), inheritedAccount));
        // A missing folder or an integer-wraparound version must never be accepted.
        document[QStringLiteral("folderConnections")] = QVariantMap{{QStringLiteral("Unrelated"), selection(QStringLiteral("direct"))}};
        write(QStringLiteral("unknown-folder.yaml"), YamlFile::encode(document, QStringLiteral("8bitshell/sessions"), 2));
        QCOMPARE(store.importYaml(path(QStringLiteral("unknown-folder.yaml"))), -1);
        QVariantMap decoded; QString error;
        for (const auto version : {"0", "3", "4294967297", "9223372036854775807"})
            QVERIFY(!YamlFile::decode(QByteArray("version: ") + version + "\nkind: 8bitshell/sessions\nsessions: []\n", QStringLiteral("8bitshell/sessions"), decoded, error, 2));
    }
    void personalIdentityAndImageStayLocalAndBounded() {
        PersonalProfile profile;
        QVERIFY(!profile.avatarEnabled());
        QVERIFY(!profile.cyberArkEnabled());
        QVERIFY(profile.setProfile(QStringLiteral("Berry"), QStringLiteral("EMP000123"), true));
        QCOMPARE(profile.displayName(), QStringLiteral("Berry"));
        QCOMPARE(profile.employeeId(), QStringLiteral("EMP000123"));
        QCOMPARE(profile.routingIdentity(), QStringLiteral("EMP000123"));
        QCOMPARE(profile.initial(), QStringLiteral("B"));
        QImage picture(64, 40, QImage::Format_ARGB32); picture.fill(Qt::cyan); picture.setText(QStringLiteral("private-meta"), QStringLiteral("do-not-copy"));
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); QVERIFY(picture.save(&buffer, "PNG"));
        write(QStringLiteral("original.png"), png);
        QVERIFY(profile.chooseAvatar(QUrl::fromLocalFile(path(QStringLiteral("original.png")))));
        QVERIFY(!profile.avatarEnabled()); QVERIFY(!profile.avatarUrl().isEmpty());
        QCOMPARE(read(QStringLiteral("original.png")), png);
        QVERIFY(!read(QStringLiteral("8bitshell/avatar.png")).contains("do-not-copy"));
        profile.setAvatarEnabled(true); PersonalProfile again; QVERIFY(again.avatarEnabled());
        QCOMPARE(again.displayName(), profile.displayName());
        QCOMPARE(again.employeeId(), profile.employeeId());
        QVERIFY(again.cyberArkEnabled());
        QVERIFY(!profile.chooseAvatar(QUrl(QStringLiteral("https://example.test/avatar.png"))));
        write(QStringLiteral("fake.png"), "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64'/>");
        QVERIFY(!profile.chooseAvatar(QUrl::fromLocalFile(path(QStringLiteral("fake.png")))));
        QVERIFY(QFile::link(path(QStringLiteral("original.png")), path(QStringLiteral("linked.png"))));
        QVERIFY(!profile.chooseAvatar(QUrl::fromLocalFile(path(QStringLiteral("linked.png")))));
        QVERIFY(!profile.setUsername(QStringLiteral("employee@other")));
        profile.setCyberArkEnabled(false);
        QVERIFY(profile.routingIdentity().isEmpty());
        SessionStore sessions; AppSettings settings;
        QVERIFY(sessions.exportYaml(path(QStringLiteral("sessions-export.yaml"))));
        QVERIFY(settings.exportYaml(path(QStringLiteral("settings-export.yaml"))));
        QVERIFY(!read(QStringLiteral("sessions-export.yaml")).contains("EMP000123"));
        QVERIFY(!read(QStringLiteral("settings-export.yaml")).contains("EMP000123"));
        QVERIFY(!settings.exportYaml(path(QStringLiteral("8bitshell/personal.yaml"))));
        QVERIFY(profile.reload()); QVERIFY(profile.removeAvatar());
        QVERIFY(!QFileInfo::exists(path(QStringLiteral("8bitshell/avatar.png"))));
        QCOMPARE(read(QStringLiteral("original.png")), png);
    }
};
QTEST_MAIN(ConnectionRoutingTest)
#include "ConnectionRoutingTest.moc"
