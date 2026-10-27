#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

namespace bitshell::core {

struct ConnectionRequest {
    QString target;
    QString operatorName;
    QString requestedUser;
    QSet<QString> activeProfiles;
    QHash<QString, QString> variables;
};

struct SessionTarget {
    QString id;
    QString displayName;
    QString host;
    quint16 port = 22;
    QString defaultUser;
    QString account;
    QString policyId;
    QSet<QString> tags;
};

struct ConnectionPolicy {
    QString id;
    QString displayName;
    bool enabled = true;
    int priority = 0;
    QSet<QString> requiredTags;
    QString requiredProfile;
    QString backend = QStringLiteral("openssh");
    QString routeHostTemplate = QStringLiteral("{{target.host}}");
    QString routeUserTemplate = QStringLiteral("{{target.user}}");
    quint16 routePort = 0;
    bool forwardX11 = false;
    bool trustedX11 = false;
    QHash<QString, QString> variables;
};

struct ResolvedConnection {
    QString requestTarget;
    QString targetId;
    QString targetHost;
    QString routeHost;
    quint16 routePort = 22;
    QString routeUser;
    QString policyId;
    QString backend;
    bool forwardX11 = false;
    bool trustedX11 = false;
    QString program;
    QStringList arguments;
    QStringList trace;

    [[nodiscard]] QString endpoint() const {
        if (routeUser.isEmpty()) {
            return routeHost;
        }
        return routeUser + QStringLiteral("@") + routeHost;
    }
};

struct ResolveError {
    enum class Code {
        EmptyTarget,
        MissingHost,
        MissingPolicy,
        MissingVariable,
        InvalidPolicy,
    };

    Code code = Code::InvalidPolicy;
    QString message;
};

} // namespace bitshell::core

