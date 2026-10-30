#include <QtTest>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTimer>
#include <bitshell/core/PasswordBroker.h>

using bitshell::core::PasswordBroker;

namespace {
QVariantMap profile(const QString& host = QStringLiteral("password.example.test"),
                    const QString& user = QStringLiteral("operator")) {
    return {{QStringLiteral("id"), QStringLiteral("broker-fixture")},
        {QStringLiteral("host"), host}, {QStringLiteral("user"), user},
        {QStringLiteral("port"), 2222}, {QStringLiteral("authenticationMode"), QStringLiteral("password")},
        {QStringLiteral("connection"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("direct")}}}};
}

QString prompt(const QVariantMap& values) {
    return values.value(QStringLiteral("user")).toString() + u'@'
        + values.value(QStringLiteral("host")).toString() + QStringLiteral("'s password: ");
}

void startHelper(QProcess& process, const QProcessEnvironment& additions, const QString& question,
                 const QString& purpose = {}) {
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto& key : additions.keys()) environment.insert(key, additions.value(key));
    environment.remove(QStringLiteral("SSH_ASKPASS_PROMPT"));
    if (!purpose.isEmpty()) environment.insert(QStringLiteral("SSH_ASKPASS_PROMPT"), purpose);
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(PasswordBroker::helperPath(), {question});
}

const QByteArray fixtureSecret = QStringLiteral("fixture-only-p\u00e4ss-\u20ba-42").toUtf8();
} // namespace

class PasswordBrokerTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY2(QFileInfo(PasswordBroker::helperPath()).isExecutable(),
                 "Build the packaged askpass helper before the broker tests.");
    }

    void actualHelperExchangesPasswordOnlyOnce() {
        PasswordBroker broker;
        const auto values = profile();
        const auto additions = broker.start(values, fixtureSecret);
        QVERIFY2(additions.has_value(), qPrintable(additions ? QString{} : additions.error()));
        QCOMPARE(additions->value(QStringLiteral("SSH_ASKPASS_REQUIRE")), QStringLiteral("force"));
        QCOMPARE(additions->value(QStringLiteral("BIT8SHELL_ASKPASS_TOKEN")).size(), 64);
        QVERIFY(!additions->toStringList().join(u'\n').toUtf8().contains(fixtureSecret));
        QProcess first;
        startHelper(first, *additions, prompt(values));
        QTRY_COMPARE_WITH_TIMEOUT(first.state(), QProcess::NotRunning, 10000);
        const auto helperError = first.readAllStandardError();
        QVERIFY2(first.exitStatus() == QProcess::NormalExit && first.exitCode() == 0,
                 qPrintable(QStringLiteral("status %1 code %2 stderr %3")
                     .arg(int(first.exitStatus())).arg(first.exitCode()).arg(QString::fromUtf8(helperError))));
        QVERIFY2(helperError.isEmpty(), helperError.constData());
        auto result = first.readAllStandardOutput();
        QCOMPARE(result, fixtureSecret + '\n');
        result.fill('\0');

        QProcess replay;
        startHelper(replay, *additions, prompt(values));
        QTRY_COMPARE_WITH_TIMEOUT(replay.state(), QProcess::NotRunning, 10000);
        QCOMPARE(replay.exitCode(), 1);
        QVERIFY(replay.readAllStandardOutput().isEmpty());
        QVERIFY(replay.readAllStandardError().isEmpty());
    }

    void wrongTokenCannotConsumeThePassword() {
        PasswordBroker broker;
        const auto additions = broker.start(profile(), fixtureSecret);
        QVERIFY(additions.has_value());
        auto badEnvironment = *additions;
        badEnvironment.insert(QStringLiteral("BIT8SHELL_ASKPASS_TOKEN"), QString(64, u'0'));
        QProcess wrong;
        startHelper(wrong, badEnvironment, prompt(profile()));
        QTRY_COMPARE_WITH_TIMEOUT(wrong.state(), QProcess::NotRunning, 10000);
        QCOMPARE(wrong.exitCode(), 1);
        QVERIFY(wrong.readAllStandardOutput().isEmpty());

        QProcess correct;
        startHelper(correct, *additions, prompt(profile()));
        QTRY_COMPARE_WITH_TIMEOUT(correct.state(), QProcess::NotRunning, 10000);
        QCOMPARE(correct.exitCode(), 0);
        QVERIFY(correct.readAllStandardOutput() == fixtureSecret + '\n');
    }

    void unrelatedPromptsNeverReceiveThePassword_data() {
        QTest::addColumn<QString>("question");
        QTest::addColumn<QString>("purpose");
        QTest::newRow("key-passphrase") << QStringLiteral("Enter passphrase for key '/tmp/key': ") << QString{};
        QTest::newRow("generic-password") << QStringLiteral("Password: ") << QString{};
        QTest::newRow("otp") << QStringLiteral("Verification code: ") << QString{};
        QTest::newRow("other-user") << QStringLiteral("root@password.example.test's password: ") << QString{};
        QTest::newRow("other-host") << QStringLiteral("operator@different.example.test's password: ") << QString{};
        QTest::newRow("host-confirmation") << prompt(profile()) << QStringLiteral("confirm");
        QTest::newRow("notification") << prompt(profile()) << QStringLiteral("none");
    }

    void unrelatedPromptsNeverReceiveThePassword() {
        QFETCH(QString, question); QFETCH(QString, purpose);
        PasswordBroker broker;
        const auto additions = broker.start(profile(), fixtureSecret);
        QVERIFY(additions.has_value());
        QProcess process;
        startHelper(process, *additions, question, purpose);
        QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 10000);
        QCOMPARE(process.exitCode(), 1);
        QVERIFY(process.readAllStandardOutput().isEmpty());
        QVERIFY(process.readAllStandardError().isEmpty());
    }

    void stopExpiryAndRestartRetirePreviousCapabilities() {
        PasswordBroker broker;
        const auto cancelled = broker.start(profile(), fixtureSecret);
        QVERIFY(cancelled.has_value());
        broker.stop();
        QProcess stopped;
        startHelper(stopped, *cancelled, prompt(profile()));
        QTRY_COMPARE_WITH_TIMEOUT(stopped.state(), QProcess::NotRunning, 10000);
        QCOMPARE(stopped.exitCode(), 1);
        QVERIFY(stopped.readAllStandardOutput().isEmpty());

        const auto expired = broker.start(profile(), fixtureSecret);
        QVERIFY(expired.has_value());
        auto* timer = broker.findChild<QTimer*>(QStringLiteral("passwordBrokerExpiry"));
        QVERIFY(timer);
        timer->start(1);
        QTRY_VERIFY(!timer->isActive());
        QProcess timedOut;
        startHelper(timedOut, *expired, prompt(profile()));
        QTRY_COMPARE_WITH_TIMEOUT(timedOut.state(), QProcess::NotRunning, 10000);
        QCOMPARE(timedOut.exitCode(), 1);
        QVERIFY(timedOut.readAllStandardOutput().isEmpty());

        const auto replaced = broker.start(profile(), fixtureSecret);
        const auto current = broker.start(profile(), QByteArrayLiteral("replacement-fixture"));
        QVERIFY(replaced.has_value() && current.has_value());
        QVERIFY(replaced->value(QStringLiteral("BIT8SHELL_ASKPASS_TOKEN"))
                != current->value(QStringLiteral("BIT8SHELL_ASKPASS_TOKEN")));
        QProcess old;
        startHelper(old, *replaced, prompt(profile()));
        QTRY_COMPARE_WITH_TIMEOUT(old.state(), QProcess::NotRunning, 10000);
        QCOMPARE(old.exitCode(), 1);
        QVERIFY(old.readAllStandardOutput().isEmpty());
        QProcess fresh;
        startHelper(fresh, *current, prompt(profile()));
        QTRY_COMPARE_WITH_TIMEOUT(fresh.state(), QProcess::NotRunning, 10000);
        const auto freshError = fresh.readAllStandardError();
        const auto freshOutput = fresh.readAllStandardOutput();
        QVERIFY2(fresh.exitStatus() == QProcess::NormalExit && fresh.exitCode() == 0,
                 qPrintable(QStringLiteral("status %1 code %2 stderr %3 stdout %4")
                     .arg(int(fresh.exitStatus())).arg(fresh.exitCode())
                     .arg(QString::fromUtf8(freshError), QString::fromUtf8(freshOutput))));
        QVERIFY(freshOutput == QByteArrayLiteral("replacement-fixture\n"));
    }

    void simultaneousConnectionsKeepSecretsSeparate() {
        PasswordBroker firstBroker, secondBroker;
        const auto firstProfile = profile(QStringLiteral("first.example.test"));
        const auto secondProfile = profile(QStringLiteral("second.example.test"));
        const auto firstEnvironment = firstBroker.start(firstProfile, QByteArrayLiteral("first-fixture"));
        const auto secondEnvironment = secondBroker.start(secondProfile, QByteArrayLiteral("second-fixture"));
        QVERIFY(firstEnvironment.has_value() && secondEnvironment.has_value());
        QProcess first, second;
        startHelper(first, *firstEnvironment, prompt(firstProfile));
        startHelper(second, *secondEnvironment, prompt(secondProfile));
        QTRY_COMPARE_WITH_TIMEOUT(first.state(), QProcess::NotRunning, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(second.state(), QProcess::NotRunning, 10000);
        QCOMPARE(first.exitCode(), 0); QCOMPARE(second.exitCode(), 0);
        QVERIFY(first.readAllStandardOutput() == QByteArrayLiteral("first-fixture\n"));
        QVERIFY(second.readAllStandardOutput() == QByteArrayLiteral("second-fixture\n"));
    }

    void invalidProfilesAndSecretsFailBeforeStarting() {
        PasswordBroker broker;
        auto value = profile();
        value[QStringLiteral("user")] = QString{};
        QVERIFY(!broker.start(value, fixtureSecret));
        value = profile(); value[QStringLiteral("proxyJump")] = QStringLiteral("gateway.example.test");
        QVERIFY(!broker.start(value, fixtureSecret));
        value = profile(); value[QStringLiteral("psmp")] = QVariantMap{};
        QVERIFY(!broker.start(value, fixtureSecret));
        value = profile(); value[QStringLiteral("connection")] = QVariantMap{{QStringLiteral("mode"), QStringLiteral("route")}};
        QVERIFY(!broker.start(value, fixtureSecret));
        QVERIFY(!broker.start(profile(), {}));
        QVERIFY(!broker.start(profile(), QByteArrayLiteral("line\nbreak")));
        QVERIFY(!broker.start(profile(), QByteArrayLiteral("carriage\rreturn")));
        QVERIFY(!broker.start(profile(), QByteArray("null\0byte", 9)));
        QVERIFY(!broker.start(profile(), QByteArray(4097, 'a')));
    }

    void malformedAndOversizedRequestsAreBounded() {
        PasswordBroker broker;
        const auto additions = broker.start(profile(), fixtureSecret);
        QVERIFY(additions.has_value());
        for (const auto& bytes : {QByteArrayLiteral("not json\n"), QByteArray(5000, 'a') + '\n'}) {
            QLocalSocket socket;
            socket.connectToServer(additions->value(QStringLiteral("BIT8SHELL_ASKPASS_SOCKET")));
            QVERIFY(socket.waitForConnected(3000));
            QCOMPARE(socket.write(bytes), bytes.size());
            QTRY_COMPARE_WITH_TIMEOUT(socket.state(), QLocalSocket::UnconnectedState, 10000);
            QVERIFY(socket.readAll().isEmpty());
        }
        QProcess valid;
        startHelper(valid, *additions, prompt(profile()));
        QTRY_COMPARE_WITH_TIMEOUT(valid.state(), QProcess::NotRunning, 10000);
        QCOMPARE(valid.exitCode(), 0);
        QVERIFY(valid.readAllStandardOutput() == fixtureSecret + '\n');
    }

    void helperWithoutCapabilityHasNoOutput() {
        auto clean = QProcessEnvironment::systemEnvironment();
        clean.remove(QStringLiteral("BIT8SHELL_ASKPASS_SOCKET"));
        clean.remove(QStringLiteral("BIT8SHELL_ASKPASS_TOKEN"));
        QProcess process;
        process.setProcessEnvironment(clean);
        process.start(PasswordBroker::helperPath(), {prompt(profile())});
        QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 10000);
        QCOMPARE(process.exitCode(), 1);
        QVERIFY(process.readAllStandardOutput().isEmpty());
        QVERIFY(process.readAllStandardError().isEmpty());
    }
};

QTEST_GUILESS_MAIN(PasswordBrokerTest)
#include "PasswordBrokerTest.moc"
