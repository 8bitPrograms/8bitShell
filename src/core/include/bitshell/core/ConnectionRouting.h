#pragma once
#include <QVariantMap>
#include <expected>

namespace bitshell::core {
// Declarative PSMP rules only. No scripts, shell expansions or SSH option text.
class ConnectionRouting {
public:
    static bool validIdentity(const QString& value);
    static bool validateAccount(const QVariantMap& account, QString& error);
    static bool validateRoute(const QVariantMap& route, QString& error);
    static bool validateSelection(const QVariantMap& selection, QString& error);
    static QString fingerprint(const QVariantMap& route);
    static std::expected<QVariantMap, QString> prepare(const QVariantMap& profile,
        const QVariantMap& routes, const QVariantMap& folders, const QString& username);
    static std::expected<QString, QString> login(const QVariantMap& profile);
};
}
