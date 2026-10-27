#pragma once

#include <expected>

#include <QList>
#include <QString>

#include <bitshell/core/ConnectionTypes.h>

namespace bitshell::core {

class ConnectionResolver {
public:
    using Result = std::expected<ResolvedConnection, ResolveError>;

    [[nodiscard]] Result resolve(const ConnectionRequest& request,
                                 const SessionTarget& target,
                                 const QList<ConnectionPolicy>& policies) const;

    [[nodiscard]] static QString commandPreview(const ResolvedConnection& connection);

private:
    using Variables = QHash<QString, QString>;

    [[nodiscard]] static const ConnectionPolicy*
    selectPolicy(const ConnectionRequest& request,
                 const SessionTarget& target,
                 const QList<ConnectionPolicy>& policies);

    [[nodiscard]] static Variables buildVariables(const ConnectionRequest& request,
                                                  const SessionTarget& target,
                                                  const ConnectionPolicy* policy);

    [[nodiscard]] static std::expected<QString, ResolveError>
    expandTemplate(const QString& source, const Variables& variables);
};

} // namespace bitshell::core

