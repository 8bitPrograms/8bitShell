#include <bitshell/core/ConnectionResolver.h>
#include <bitshell/core/SshSecurity.h>

#include <algorithm>
#include <limits>

#include <QRegularExpression>

namespace bitshell::core {
namespace {

bool policyMatches(const ConnectionRequest& request,
                   const SessionTarget& target,
                   const ConnectionPolicy& policy) {
    if (!policy.enabled) {
        return false;
    }

    if (!target.policyId.isEmpty() && target.policyId != policy.id) {
        return false;
    }

    if (!policy.requiredProfile.isEmpty()
        && !request.activeProfiles.contains(policy.requiredProfile)) {
        return false;
    }

    for (const auto& tag : policy.requiredTags) {
        if (!target.tags.contains(tag)) {
            return false;
        }
    }

    return true;
}

QString shellQuoteForPreview(const QString& value) {
    static const QRegularExpression safePattern(QStringLiteral("^[A-Za-z0-9_./:@%+=,-]+$"));
    if (safePattern.match(value).hasMatch()) {
        return value;
    }

    QString escaped = value;
    escaped.replace(u'\'', QStringLiteral("'\\''"));
    return QStringLiteral("'") + escaped + QStringLiteral("'");
}

} // namespace

ConnectionResolver::Result ConnectionResolver::resolve(
    const ConnectionRequest& request,
    const SessionTarget& target,
    const QList<ConnectionPolicy>& policies) const {
    if (request.target.trimmed().isEmpty()) {
        return std::unexpected(ResolveError{ResolveError::Code::EmptyTarget,
                                            QStringLiteral("Connection target is empty")});
    }
    if (target.host.trimmed().isEmpty()) {
        return std::unexpected(ResolveError{ResolveError::Code::MissingHost,
                                            QStringLiteral("Resolved session has no host")});
    }

    const auto* policy = selectPolicy(request, target, policies);
    if (!target.policyId.isEmpty() && policy == nullptr) {
        return std::unexpected(ResolveError{
            ResolveError::Code::MissingPolicy,
            QStringLiteral("Required connection policy '%1' is not available or active")
                .arg(target.policyId)});
    }

    const auto variables = buildVariables(request, target, policy);
    const QString hostTemplate =
        policy == nullptr ? QStringLiteral("{{target.host}}") : policy->routeHostTemplate;
    const QString userTemplate =
        policy == nullptr ? QStringLiteral("{{target.user}}") : policy->routeUserTemplate;

    auto routeHost = expandTemplate(hostTemplate, variables);
    if (!routeHost) {
        return std::unexpected(routeHost.error());
    }
    auto routeUser = expandTemplate(userTemplate, variables);
    if (!routeUser) {
        return std::unexpected(routeUser.error());
    }
    if (routeHost->trimmed().isEmpty()) {
        return std::unexpected(ResolveError{ResolveError::Code::InvalidPolicy,
                                            QStringLiteral("Policy produced an empty route host")});
    }

    ResolvedConnection resolved;
    resolved.requestTarget = request.target;
    resolved.targetId = target.id;
    resolved.targetHost = target.host;
    resolved.routeHost = routeHost->trimmed();
    resolved.routeUser = routeUser->trimmed();
    resolved.routePort = policy != nullptr && policy->routePort != 0 ? policy->routePort
                                                                     : target.port;
    resolved.policyId = policy == nullptr ? QStringLiteral("direct") : policy->id;
    resolved.backend = policy == nullptr ? QStringLiteral("openssh") : policy->backend;
    resolved.forwardX11 = policy != nullptr && policy->forwardX11;
    resolved.trustedX11 = resolved.forwardX11 && policy->trustedX11;
    const auto command = SshSecurity::command({{QStringLiteral("host"), resolved.routeHost},
        {QStringLiteral("user"), resolved.routeUser}, {QStringLiteral("port"), resolved.routePort},
        {QStringLiteral("forwardX11"), resolved.forwardX11}, {QStringLiteral("trustedX11"), resolved.trustedX11}},
        SshSecurity::Transport::Terminal);
    if (!command || resolved.backend != QStringLiteral("openssh"))
        return std::unexpected(ResolveError{ResolveError::Code::InvalidPolicy,
            command ? QStringLiteral("Unsupported connection backend") : command.error()});
    resolved.program = command->program;
    resolved.arguments = command->arguments;

    resolved.trace << QStringLiteral("request: %1").arg(request.target)
                   << QStringLiteral("target: %1 (%2)")
                          .arg(target.displayName.isEmpty() ? target.id : target.displayName,
                               target.host)
                   << QStringLiteral("policy: %1").arg(resolved.policyId)
                   << QStringLiteral("route: %1:%2")
                          .arg(resolved.endpoint(), QString::number(resolved.routePort))
                   << QStringLiteral("backend: %1").arg(resolved.backend);
    if (resolved.forwardX11) {
        resolved.trace << QStringLiteral("x11: %1")
                              .arg(resolved.trustedX11 ? QStringLiteral("trusted")
                                                       : QStringLiteral("untrusted"));
    }

    return resolved;
}

QString ConnectionResolver::commandPreview(const ResolvedConnection& connection) {
    QStringList pieces;
    pieces << shellQuoteForPreview(connection.program);
    for (const auto& argument : connection.arguments) {
        pieces << shellQuoteForPreview(argument);
    }
    return pieces.join(u' ');
}

const ConnectionPolicy* ConnectionResolver::selectPolicy(
    const ConnectionRequest& request,
    const SessionTarget& target,
    const QList<ConnectionPolicy>& policies) {
    const ConnectionPolicy* selected = nullptr;
    int selectedPriority = std::numeric_limits<int>::min();

    for (const auto& policy : policies) {
        if (!policyMatches(request, target, policy)) {
            continue;
        }
        if (selected == nullptr || policy.priority > selectedPriority
            || (policy.priority == selectedPriority && policy.id < selected->id)) {
            selected = &policy;
            selectedPriority = policy.priority;
        }
    }

    return selected;
}

ConnectionResolver::Variables ConnectionResolver::buildVariables(
    const ConnectionRequest& request,
    const SessionTarget& target,
    const ConnectionPolicy* policy) {
    Variables variables;
    variables.insert(QStringLiteral("request.target"), request.target);
    variables.insert(QStringLiteral("request.operator"), request.operatorName);
    variables.insert(QStringLiteral("request.user"), request.requestedUser);
    variables.insert(QStringLiteral("target.id"), target.id);
    variables.insert(QStringLiteral("target.name"), target.displayName);
    variables.insert(QStringLiteral("target.host"), target.host);
    variables.insert(QStringLiteral("target.port"), QString::number(target.port));
    variables.insert(QStringLiteral("target.user"),
                     request.requestedUser.isEmpty() ? target.defaultUser : request.requestedUser);
    variables.insert(QStringLiteral("target.account"), target.account);

    for (auto it = request.variables.cbegin(); it != request.variables.cend(); ++it) {
        variables.insert(QStringLiteral("request.") + it.key(), it.value());
    }
    if (policy != nullptr) {
        for (auto it = policy->variables.cbegin(); it != policy->variables.cend(); ++it) {
            variables.insert(QStringLiteral("policy.") + it.key(), it.value());
        }
    }
    return variables;
}

std::expected<QString, ResolveError>
ConnectionResolver::expandTemplate(const QString& source, const Variables& variables) {
    static const QRegularExpression tokenPattern(
        QStringLiteral(R"(\{\{\s*([A-Za-z0-9_.-]+)\s*\}\})"));

    QString expanded;
    qsizetype cursor = 0;
    auto matches = tokenPattern.globalMatch(source);
    while (matches.hasNext()) {
        const auto match = matches.next();
        expanded += source.sliced(cursor, match.capturedStart() - cursor);
        const auto name = match.captured(1);
        const auto value = variables.constFind(name);
        if (value == variables.cend()) {
            return std::unexpected(ResolveError{
                ResolveError::Code::MissingVariable,
                QStringLiteral("Template variable '%1' is missing").arg(name)});
        }
        expanded += *value;
        cursor = match.capturedEnd();
    }
    expanded += source.sliced(cursor);
    return expanded;
}

} // namespace bitshell::core
