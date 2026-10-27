#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <bitshell/core/SshSecurity.h>
#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/YamlFile.h>
#include <bitshell/core/SessionLogger.h>
#include <bitshell/core/SessionStore.h>
#include <bitshell/core/BoundedRegex.h>
#include <random>
#include <sys/stat.h>
#include <unistd.h>

using namespace bitshell::core;

class SecurityTest : public QObject {
    Q_OBJECT
private slots:
    void rejectsSshInjection_data();
    void rejectsSshInjection();
    void validatesStructuredProfiles();
    void effectiveOpenSshPolicy();
    void policyAppliesToAllTransports();
    void privateFilesRejectLinksAndSpecialFiles();
    void logRotationIsPrivateAndRejectsSymlinks();
    void yamlBudgetsBeforeObjectConstruction();
    void deterministicParserMutationCorpus();
    void xmlRejectsDtdAndExcessiveNesting();
    void regularExpressionsHaveFiniteWork();
};

void SecurityTest::rejectsSshInjection_data() {
    QTest::addColumn<QString>("input");
    const QStringList inputs{
        QStringLiteral("ssh -oProxyCommand=touch /tmp/bitshell-never-execute host"),
        QStringLiteral("ssh -oStrictHostKeyChecking=no host"), QStringLiteral("ssh -F /tmp/config host"),
        QStringLiteral("ssh -S /tmp/socket host"), QStringLiteral("ssh -D 1080 host"),
        QStringLiteral("ssh -L 1111:localhost:22 host"), QStringLiteral("ssh -R 2222:localhost:22 host"),
        QStringLiteral("ssh -Y host"), QStringLiteral("ssh -A host"), QStringLiteral("ssh host id"),
        QStringLiteral("ssh -i ../key host"), QStringLiteral("ssh -i /tmp/%h host"),
        QStringLiteral("ssh -J user@host;id target"), QStringLiteral("ssh -J $(id) target"),
        QStringLiteral("ssh -J \"user@host -oProxyCommand=id\" target"),
        QStringLiteral("ssh -J user@host:0 target"), QStringLiteral("ssh -J user@host:65536 target"),
        QStringLiteral("ssh -p -22 host"), QStringLiteral("ssh -p 65536 host"),
        QStringLiteral("ssh -- -oProxyCommand=id"), QStringLiteral("-oProxyCommand=id"),
        QStringLiteral("ssh user`id`@host"), QStringLiteral("ssh user$(id)@host"),
        QStringLiteral("ssh host\n-oProxyCommand=id"), QStringLiteral("ssh user@host") + QChar::Null,
        QStringLiteral("ssh -p 22 -p 23 host"), QStringLiteral("ssh -l a b@host"),
        QStringLiteral("ssh -J user@host, target"), QStringLiteral("ssh -J [::1]: target"),
        QStringLiteral("ssh -J host:%p target"), QString(5000, u'a')
    };
    for (int index = 0; index < inputs.size(); ++index)
        QTest::newRow(qPrintable(QString::number(index))) << inputs[index];
}
void SecurityTest::rejectsSshInjection() {
    QFETCH(QString, input);
    const auto result = SshSecurity::quickProfile(input);
    QVERIFY2(!result, qPrintable(input));
}
void SecurityTest::validatesStructuredProfiles() {
    for (const auto& target : {QStringLiteral("db.example.test"), QStringLiteral("user@10.0.0.1"),
        QStringLiteral("ssh -p2222 -J operator@bastion:22,[::1]:2222 db.example.test"),
        QStringLiteral("ssh -l operator@account@target gateway.example.test"),
        QStringLiteral("ssh -i \"/tmp/a key\" user@[::1]")}) {
        auto result = SshSecurity::quickProfile(target);
        QVERIFY2(result.has_value(), qPrintable(result ? target : result.error()));
    }
    QVariantMap profile{{QStringLiteral("host"), QStringLiteral("example.test")}};
    for (const auto& flag : {QStringLiteral("agentForwarding"), QStringLiteral("trustedX11")}) {
        profile[flag] = true;
        QVERIFY(!SshSecurity::validateProfile(profile).isEmpty());
        profile.remove(flag);
    }
    profile[QStringLiteral("host")] = QStringLiteral("host;touch /tmp/never");
    for (auto transport : {SshSecurity::Transport::Terminal, SshSecurity::Transport::Sftp, SshSecurity::Transport::Monitor})
        QVERIFY(!SshSecurity::command(profile, transport));
}
void SecurityTest::effectiveOpenSshPolicy() {
    const auto command = SshSecurity::command({{QStringLiteral("host"), QStringLiteral("policy.invalid")}}, SshSecurity::Transport::Terminal);
    QVERIFY(command);
    auto arguments = command->arguments;
    arguments.prepend(QStringLiteral("-G")); // Dumps effective configuration; never connects.
    QProcess process;
    process.setProcessEnvironment(SshSecurity::environment());
    process.start(command->program, arguments);
    QVERIFY(process.waitForFinished(5000));
    const auto error = process.readAllStandardError();
    QCOMPARE(process.exitCode(), 0);
    const auto effective = process.readAllStandardOutput();
    for (const auto& line : {QByteArray("stricthostkeychecking true"), QByteArray("forwardagent no"),
        QByteArray("forwardx11 no"), QByteArray("forwardx11trusted no"), QByteArray("permitlocalcommand no"),
        QByteArray("clearallforwardings yes"), QByteArray("controlmaster false"), QByteArray("requiredrsasize 3072")})
        QVERIFY2(effective.contains(line), (line + "\n" + effective + error).constData());
    for (const auto& line : effective.split('\n')) {
        if (line.startsWith("ciphers ")) QVERIFY(!line.contains("cbc"));
        if (line.startsWith("macs ") || line.startsWith("kexalgorithms ")) QVERIFY(!line.contains("sha1"));
        if (line.startsWith("hostkeyalgorithms ")) {
            QVERIFY(!line.contains("ssh-rsa")); QVERIFY(!line.contains("ssh-dss"));
        }
    }
    const QFileInfo config(SshSecurity::configurationPath());
    QVERIFY(!(config.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther)));
    QVERIFY(!(QFileInfo(config.absolutePath()).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
}
void SecurityTest::policyAppliesToAllTransports() {
    const QVariantMap profile{{QStringLiteral("host"), QStringLiteral("target.invalid")},
        {QStringLiteral("proxyJump"), QStringLiteral("user@bastion.invalid:2222")}};
    for (auto transport : {SshSecurity::Transport::Terminal, SshSecurity::Transport::Sftp, SshSecurity::Transport::Monitor}) {
        const auto command = SshSecurity::command(profile, transport);
        QVERIFY(command);
        QCOMPARE(command->arguments[0], QStringLiteral("-F"));
        QCOMPARE(command->arguments[1], SshSecurity::configurationPath());
        QCOMPARE(command->arguments.last(), QStringLiteral("target.invalid"));
        QCOMPARE(command->arguments[command->arguments.size() - 2], QStringLiteral("--"));
        QVERIFY(command->arguments.contains(QStringLiteral("user@bastion.invalid:2222")));
        if (transport != SshSecurity::Transport::Terminal) QVERIFY(command->arguments.contains(QStringLiteral("-oBatchMode=yes")));
    }
    qputenv("SSH_ASKPASS", "/tmp/must-not-run");
    qputenv("BIT8SHELL_SFTP_EXECUTABLE", "/tmp/must-not-run");
    QVERIFY(!SshSecurity::environment().contains(QStringLiteral("SSH_ASKPASS")));
    const auto command = SshSecurity::command(profile, SshSecurity::Transport::Sftp);
    QCOMPARE(command->program, QStringLiteral("/usr/bin/sftp"));
    QVERIFY(command->arguments.contains(QStringLiteral("-N")));
    QVERIFY(!command->arguments.contains(QStringLiteral("-q")));
    const auto explicitKey = SshSecurity::command(
        {{QStringLiteral("host"), QStringLiteral("target.invalid")},
         {QStringLiteral("identityFile"), QStringLiteral("/tmp/explicit-key")}},
        SshSecurity::Transport::Terminal);
    QVERIFY(explicitKey);
    QVERIFY(explicitKey->arguments.contains(QStringLiteral("-oIdentitiesOnly=yes")));
    QVERIFY(explicitKey->arguments.contains(QStringLiteral("/tmp/explicit-key")));
    qunsetenv("SSH_ASKPASS"); qunsetenv("BIT8SHELL_SFTP_EXECUTABLE");
}
void SecurityTest::privateFilesRejectLinksAndSpecialFiles() {
    QTemporaryDir directory;
    QString error;
    const QString path = directory.filePath(QStringLiteral("private/nested/file"));
    QVERIFY2(PrivateFile::write(path, "sentinel", error), qPrintable(error));
    const auto readableByOthers = QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther;
    QVERIFY(!(QFileInfo(path).permissions() & readableByOthers));
    QVERIFY(!(QFileInfo(QFileInfo(path).absolutePath()).permissions() & readableByOthers));
    const QString symlink = directory.filePath(QStringLiteral("symlink"));
    QVERIFY(QFile::link(path, symlink));
    QByteArray bytes;
    QVERIFY(!PrivateFile::read(symlink, bytes, error));
    QVERIFY(!PrivateFile::write(symlink, "overwrite", error));
    const QString dirLink = directory.filePath(QStringLiteral("dir-link"));
    QVERIFY(QFile::link(QFileInfo(path).absolutePath(), dirLink));
    QVERIFY(!PrivateFile::write(dirLink + QStringLiteral("/file"), "overwrite", error));
    const QString fifo = directory.filePath(QStringLiteral("fifo"));
    QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
    QElapsedTimer timer; timer.start();
    QVERIFY(!PrivateFile::read(fifo, bytes, error));
    QVERIFY(timer.elapsed() < 1000);
    const QString hardlink = directory.filePath(QStringLiteral("hardlink"));
    QCOMPARE(::link(QFile::encodeName(path).constData(), QFile::encodeName(hardlink).constData()), 0);
    QFile log(hardlink);
    QVERIFY(!PrivateFile::openLog(log, false, error));
    QVERIFY(PrivateFile::read(path, bytes, error)); QCOMPARE(bytes, QByteArray("sentinel"));
}
void SecurityTest::logRotationIsPrivateAndRejectsSymlinks() {
    QTemporaryDir directory;
    SessionLogger logger;
    LogOptions options{.rootDirectory = directory.path(), .directoryTemplate = QStringLiteral("logs"),
        .fileNameTemplate = QStringLiteral("session.log"), .rotateAfterBytes = 5, .retainedRotations = 2};
    QString error;
    QVERIFY2(logger.start(options, {}, &error), qPrintable(error));
    SessionLogger concurrent;
    QVERIFY(!concurrent.start(options, {}, &error));
    QVERIFY(error.contains(QStringLiteral("already in use")));
    QVERIFY(logger.writeOutput("12345", &error));
    QVERIFY(logger.writeOutput("67890", &error));
    const QString path = logger.filePath();
    logger.stop();
    QVERIFY(concurrent.start(options, {}, &error));
    concurrent.stop();
    QByteArray bytes;
    QVERIFY(PrivateFile::read(path, bytes, error)); QCOMPARE(bytes, QByteArray("67890"));
    QVERIFY(PrivateFile::read(path + QStringLiteral(".1"), bytes, error)); QCOMPARE(bytes, QByteArray("12345"));
    QVERIFY(!(QFileInfo(path).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
    const QString protectedPath = directory.filePath(QStringLiteral("sentinel"));
    QVERIFY(PrivateFile::write(protectedPath, "untouched", error));
    QVERIFY(QFile::link(protectedPath, path + QStringLiteral(".2")));
    QVERIFY(logger.start(options, {}, &error));
    QVERIFY(!logger.writeOutput("rotate", &error));
    QVERIFY(PrivateFile::read(protectedPath, bytes, error)); QCOMPARE(bytes, QByteArray("untouched"));
}
void SecurityTest::yamlBudgetsBeforeObjectConstruction() {
    QVariantMap output; QString error;
    const QByteArray header = "version: 1\nkind: 8bitshell/sessions\n";
    for (const auto& data : {header + "a: &a [*a]\n", header + "a: &a yes\nb: *a\n",
        header + "value: " + QByteArray(10000, '[') + "null" + QByteArray(10000, ']'),
        header + "a: one\na: two\n", header + "a: !custom x\n", header + "---\na: b\n"}) {
        QVERIFY(!YamlFile::decode(data, QStringLiteral("8bitshell/sessions"), output, error));
    }
    QVERIFY(!YamlFile::decode(QByteArray(8 * 1024 * 1024 + 1, ' '), QStringLiteral("8bitshell/sessions"), output, error));
}
void SecurityTest::deterministicParserMutationCorpus() {
    std::mt19937 generator(0x535348U);
    const QByteArray seed = "version: 1\nkind: 8bitshell/sessions\nsessions: [{host: example.test, port: 22}]\nfolders: []\n";
    for (int sample = 0; sample < 1500; ++sample) {
        QByteArray input = seed;
        for (int edit = 0; edit < 1 + sample % 12; ++edit) {
            const auto index = static_cast<qsizetype>(generator() % static_cast<unsigned>(input.size()));
            input[index] = static_cast<char>(generator() & 0xffU);
        }
        QVariantMap output; QString error;
        (void)YamlFile::decode(input, QStringLiteral("8bitshell/sessions"), output, error);
    }
}
void SecurityTest::xmlRejectsDtdAndExcessiveNesting() {
    QTemporaryDir directory;
    SessionStore store;
    const int before = store.count();
    const QString path = directory.filePath(QStringLiteral("import.xml"));
    QString error;
    const QList<QByteArray> invalid{
        "<!DOCTYPE x [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><Connections><Node Name='&x;'/></Connections>",
        QByteArray("<Connections>") + QByteArray("<Node Type='Container' Name='a'>").repeated(100)
            + QByteArray("</Node>").repeated(100) + "</Connections>"};
    for (const auto& input : invalid) {
        QVERIFY(PrivateFile::write(path, input, error));
        QCOMPARE(store.importMRemoteNg(path), 0);
        QVERIFY(!store.lastError().isEmpty()); QCOMPARE(store.count(), before);
    }
}
void SecurityTest::regularExpressionsHaveFiniteWork() {
    const auto regex = boundedRegex(QStringLiteral("(*NO_AUTO_POSSESS)(*NO_START_OPT)(a+)+$"));
    QVERIFY(regex.isValid());
    QElapsedTimer timer; timer.start();
    QVERIFY(!regex.match(QString(10000, u'a') + u'!').hasMatch());
    QVERIFY2(timer.elapsed() < 2000, "Pathological regex exceeded the test time budget");
    const auto override = boundedRegex(QStringLiteral("(*LIMIT_MATCH=999999999)(*NO_AUTO_POSSESS)(a+)+$"));
    QVERIFY(!override.match(QString(10000, u'a') + u'!').hasMatch());
    QVERIFY(timer.elapsed() < 2000);
    QVERIFY(!boundedRegex(QString(1025, u'a')).isValid());
}

int main(int argc, char** argv) {
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QCoreApplication app(argc, argv);
    SecurityTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "SecurityTest.moc"
