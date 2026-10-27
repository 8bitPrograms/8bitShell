#include <QtTest>

#include <bitshell/core/ConnectionResolver.h>
#include <bitshell/core/SshSecurity.h>

using namespace bitshell::core;

class ConnectionResolverTest : public QObject {
    Q_OBJECT

private slots:
    void directConnectionUsesTargetDetails();
    void cyberArkPolicyBuildsStructuredRoute();
    void higherPriorityPolicyWins();
    void missingTemplateVariableFailsClosed();
    void userMayBeDeferredToOpenSshConfig();
};

void ConnectionResolverTest::directConnectionUsesTargetDetails() {
    const ConnectionRequest request{
        .target = QStringLiteral("db-01"),
        .operatorName = QStringLiteral("berry"),
    };
    const SessionTarget target{
        .id = QStringLiteral("db-01"),
        .displayName = QStringLiteral("Database 01"),
        .host = QStringLiteral("10.20.30.40"),
        .port = 2222,
        .defaultUser = QStringLiteral("admin"),
    };

    const auto result = ConnectionResolver{}.resolve(request, target, {});

    QVERIFY(result.has_value());
    QCOMPARE(result->routeHost, QStringLiteral("10.20.30.40"));
    QCOMPARE(result->routeUser, QStringLiteral("admin"));
    QCOMPARE(result->routePort, quint16{2222});
    QCOMPARE(result->policyId, QStringLiteral("direct"));
    QCOMPARE(result->arguments,
             QStringList({QStringLiteral("-F"), SshSecurity::configurationPath(), QStringLiteral("-p"),
                          QStringLiteral("2222"),
                          QStringLiteral("--"),
                          QStringLiteral("admin@10.20.30.40")}));
}

void ConnectionResolverTest::cyberArkPolicyBuildsStructuredRoute() {
    const ConnectionRequest request{
        .target = QStringLiteral("db-prod-01"),
        .operatorName = QStringLiteral("berry"),
        .activeProfiles = {QStringLiteral("company")},
    };
    const SessionTarget target{
        .id = QStringLiteral("db-prod-01"),
        .displayName = QStringLiteral("Production database"),
        .host = QStringLiteral("db-prod-01.internal"),
        .defaultUser = QStringLiteral("sys"),
        .account = QStringLiteral("privileged-sys"),
        .policyId = QStringLiteral("company-cyberark"),
        .tags = {QStringLiteral("cyberark"), QStringLiteral("production")},
    };
    const ConnectionPolicy policy{
        .id = QStringLiteral("company-cyberark"),
        .displayName = QStringLiteral("Company CyberArk"),
        .priority = 100,
        .requiredTags = {QStringLiteral("cyberark")},
        .requiredProfile = QStringLiteral("company"),
        .routeHostTemplate = QStringLiteral("{{policy.gateway}}"),
        .routeUserTemplate = QStringLiteral(
            "{{request.operator}}@{{target.account}}@{{target.host}}"),
        .forwardX11 = true,
        .variables = {{QStringLiteral("gateway"), QStringLiteral("psmp.example.test")}},
    };

    const auto result = ConnectionResolver{}.resolve(request, target, {policy});

    QVERIFY(result.has_value());
    QCOMPARE(result->routeHost, QStringLiteral("psmp.example.test"));
    QCOMPARE(result->routeUser,
             QStringLiteral("berry@privileged-sys@db-prod-01.internal"));
    QVERIFY(result->forwardX11);
    QVERIFY(!result->trustedX11);
    QCOMPARE(result->arguments.last(),
             QStringLiteral("berry@privileged-sys@db-prod-01.internal@psmp.example.test"));
    QVERIFY(result->trace.contains(QStringLiteral("policy: company-cyberark")));
}

void ConnectionResolverTest::higherPriorityPolicyWins() {
    const ConnectionRequest request{
        .target = QStringLiteral("host"),
        .operatorName = QStringLiteral("operator"),
    };
    const SessionTarget target{
        .id = QStringLiteral("host"),
        .host = QStringLiteral("host.example.test"),
        .defaultUser = QStringLiteral("root"),
        .tags = {QStringLiteral("managed")},
    };
    const ConnectionPolicy low{
        .id = QStringLiteral("low"),
        .priority = 10,
        .requiredTags = {QStringLiteral("managed")},
        .routeHostTemplate = QStringLiteral("low.example.test"),
    };
    const ConnectionPolicy high{
        .id = QStringLiteral("high"),
        .priority = 50,
        .requiredTags = {QStringLiteral("managed")},
        .routeHostTemplate = QStringLiteral("high.example.test"),
    };

    const auto result = ConnectionResolver{}.resolve(request, target, {low, high});

    QVERIFY(result.has_value());
    QCOMPARE(result->policyId, QStringLiteral("high"));
    QCOMPARE(result->routeHost, QStringLiteral("high.example.test"));
}

void ConnectionResolverTest::missingTemplateVariableFailsClosed() {
    const ConnectionRequest request{.target = QStringLiteral("host")};
    const SessionTarget target{
        .id = QStringLiteral("host"),
        .host = QStringLiteral("host.example.test"),
        .defaultUser = QStringLiteral("root"),
        .policyId = QStringLiteral("broken"),
    };
    const ConnectionPolicy policy{
        .id = QStringLiteral("broken"),
        .routeHostTemplate = QStringLiteral("{{policy.not-defined}}"),
    };

    const auto result = ConnectionResolver{}.resolve(request, target, {policy});

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, ResolveError::Code::MissingVariable);
}

void ConnectionResolverTest::userMayBeDeferredToOpenSshConfig() {
    const ConnectionRequest request{.target = QStringLiteral("configured-alias")};
    const SessionTarget target{
        .id = QStringLiteral("configured-alias"),
        .host = QStringLiteral("configured-alias"),
    };

    const auto result = ConnectionResolver{}.resolve(request, target, {});

    QVERIFY(result.has_value());
    QVERIFY(result->routeUser.isEmpty());
    QCOMPARE(result->endpoint(), QStringLiteral("configured-alias"));
}

QTEST_APPLESS_MAIN(ConnectionResolverTest)
#include "ConnectionResolverTest.moc"
