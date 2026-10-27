#include <bitshell/core/ConnectionRouting.h>
#include <bitshell/core/SshSecurity.h>
#include <bitshell/core/YamlFile.h>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>

namespace bitshell::core {
namespace {
bool text(const QVariantMap& map, const QString& key, int limit = 255) {
    return !map.contains(key) || (map[key].typeId() == QMetaType::QString && map[key].toString().size() <= limit);
}
bool segment(const QString& value, bool empty = false) {
    static const QRegularExpression pattern(QStringLiteral("\\A[A-Za-z0-9_][A-Za-z0-9_.+-]*\\z"));
    return (empty && value.isEmpty()) || (value.size() <= 128 && pattern.match(value).hasMatch());
}
}
bool ConnectionRouting::validIdentity(const QString& value) { return segment(value, true); }
bool ConnectionRouting::validateAccount(const QVariantMap& account, QString& error) {
    if (!YamlFile::fields(account, {QStringLiteral("source"), QStringLiteral("name"), QStringLiteral("suffix"), QStringLiteral("domain")}, error)) return false;
    for (const auto& key : account.keys()) if (!text(account, key, 128)) { error = QStringLiteral("Account fields must be bounded text."); return false; }
    const auto source = account.value(QStringLiteral("source")).toString();
    const bool personal = source == QStringLiteral("identity");
    if ((!personal && source != QStringLiteral("literal"))
        || (personal && account.contains(QStringLiteral("name")))
        || (!personal && account.contains(QStringLiteral("suffix")))
        || !segment(account.value(personal ? QStringLiteral("suffix") : QStringLiteral("name")).toString(), personal)
        || !segment(account.value(QStringLiteral("domain")).toString(), true)) {
        error = QStringLiteral("Choose an identity suffix or a literal account, with an optional domain. Delimiters and control characters are not allowed."); return false;
    }
    return true;
}
bool ConnectionRouting::validateRoute(const QVariantMap& route, QString& error) {
    if (!YamlFile::fields(route, {QStringLiteral("type"), QStringLiteral("name"), QStringLiteral("host"), QStringLiteral("port"),
        QStringLiteral("account"), QStringLiteral("allowSftp"), QStringLiteral("allowMonitoring")}, error)) return false;
    const auto port = route.value(QStringLiteral("port"), 22);
    if (!text(route, QStringLiteral("name")) || !text(route, QStringLiteral("host"))
        || route.value(QStringLiteral("type")).toString() != QStringLiteral("cyberark-psmp")
        || !SshSecurity::safeHost(route.value(QStringLiteral("host")).toString())
        || (port.typeId() != QMetaType::Int && port.typeId() != QMetaType::LongLong && port.typeId() != QMetaType::Double)
        || port.toDouble() != port.toInt()
        || port.toInt() < 1 || port.toInt() > 65535) {
        error = QStringLiteral("A CyberArk route needs a valid gateway host and port (1–65535)."); return false;
    }
    for (const auto& key : {QStringLiteral("allowSftp"), QStringLiteral("allowMonitoring")})
        if (route.contains(key) && route[key].typeId() != QMetaType::Bool) { error = QStringLiteral("Protocol capabilities must be true or false."); return false; }
    if (route.value(QStringLiteral("account")).typeId() != QMetaType::QVariantMap) { error = QStringLiteral("A route needs an account rule."); return false; }
    return validateAccount(route.value(QStringLiteral("account")).toMap(), error);
}
bool ConnectionRouting::validateSelection(const QVariantMap& selection, QString& error) {
    if (!YamlFile::fields(selection, {QStringLiteral("mode"), QStringLiteral("route"), QStringLiteral("account")}, error)) return false;
    const auto mode = selection.value(QStringLiteral("mode")).toString();
    if (!QStringList{QStringLiteral("direct"), QStringLiteral("inherit"), QStringLiteral("route")}.contains(mode)
        || !text(selection, QStringLiteral("route"), 128)
        || (mode == QStringLiteral("route") && !segment(selection.value(QStringLiteral("route")).toString()))
        || (mode != QStringLiteral("route") && selection.contains(QStringLiteral("route")))
        || (mode == QStringLiteral("direct") && selection.contains(QStringLiteral("account")))) {
        error = QStringLiteral("Connection must explicitly select direct, inherit, or a named route."); return false;
    }
    if (selection.contains(QStringLiteral("account"))) {
        if (selection[QStringLiteral("account")].typeId() != QMetaType::QVariantMap) { error = QStringLiteral("Account override must be a mapping."); return false; }
        return validateAccount(selection[QStringLiteral("account")].toMap(), error);
    }
    return true;
}
QString ConnectionRouting::fingerprint(const QVariantMap& route) {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument::fromVariant(route).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
std::expected<QVariantMap, QString> ConnectionRouting::prepare(const QVariantMap& profile,
    const QVariantMap& routes, const QVariantMap& folders, const QString& username) {
    if (profile.contains(QStringLiteral("psmp"))) return std::unexpected(QStringLiteral("Resolved route data cannot be imported or resolved again."));
    auto selection = profile.value(QStringLiteral("connection"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("direct")}}).toMap();
    QString error;
    if (!validateSelection(selection, error)) return std::unexpected(error);
    const auto overrideAccount = selection.value(QStringLiteral("account"));
    QString folder = profile.value(QStringLiteral("group")).toString();
    if (selection.value(QStringLiteral("mode")) == QStringLiteral("inherit")) {
        for (;;) {
            if (folders.contains(folder)) {
                selection = folders.value(folder).toMap();
                if (!validateSelection(selection, error)) return std::unexpected(error);
                if (selection.value(QStringLiteral("mode")) != QStringLiteral("inherit")) break;
            }
            if (folder.isEmpty()) { selection = {{QStringLiteral("mode"), QStringLiteral("direct")}}; break; }
            folder = folder.contains(u'/') ? folder.left(folder.lastIndexOf(u'/')) : QString{};
        }
    }
    auto prepared = profile;
    prepared.remove(QStringLiteral("connection"));
    if (selection.value(QStringLiteral("mode")) == QStringLiteral("direct")) {
        if (overrideAccount.isValid()) return std::unexpected(QStringLiteral("An inherited account override requires a CyberArk route, not direct SSH."));
        return prepared;
    }
    const QString id = selection.value(QStringLiteral("route")).toString();
    if (!routes.contains(id)) return std::unexpected(QStringLiteral("Required route '%1' is missing. Direct fallback is blocked.").arg(id));
    const auto route = routes.value(id).toMap();
    if (!validateRoute(route, error)) return std::unexpected(error);
    if (username.isEmpty() || !validIdentity(username)) return std::unexpected(QStringLiteral("Set your employee username in Personal profile before using CyberArk."));
    const auto account = overrideAccount.isValid() ? overrideAccount : selection.value(QStringLiteral("account"), route.value(QStringLiteral("account")));
    prepared[QStringLiteral("psmp")] = QVariantMap{{QStringLiteral("route"), route}, {QStringLiteral("operator"), username},
        {QStringLiteral("account"), account}, {QStringLiteral("routeId"), id}};
    prepared[QStringLiteral("user")] = QString{}; // Direct-only username is not the PSMP login.
    const auto user = login(prepared);
    if (!user) return std::unexpected(user.error());
    return prepared;
}
std::expected<QString, QString> ConnectionRouting::login(const QVariantMap& profile) {
    const auto psmp = profile.value(QStringLiteral("psmp"));
    QString error;
    if (psmp.typeId() != QMetaType::QVariantMap) return std::unexpected(QStringLiteral("Invalid resolved PSMP plan."));
    const auto plan = psmp.toMap();
    if (!YamlFile::fields(plan, {QStringLiteral("route"), QStringLiteral("operator"), QStringLiteral("account"), QStringLiteral("routeId")}, error)
        || plan.value(QStringLiteral("route")).typeId() != QMetaType::QVariantMap
        || !validateRoute(plan.value(QStringLiteral("route")).toMap(), error)
        || plan.value(QStringLiteral("account")).typeId() != QMetaType::QVariantMap
        || !validateAccount(plan.value(QStringLiteral("account")).toMap(), error))
        return std::unexpected(error.isEmpty() ? QStringLiteral("Invalid PSMP plan.") : error);
    const auto employee = plan.value(QStringLiteral("operator")).toString();
    const auto host = profile.value(QStringLiteral("host")).toString();
    if (!text(plan, QStringLiteral("operator"), 128) || employee.isEmpty() || !validIdentity(employee)
        || !SshSecurity::safeHost(host) || host.contains(u':')
        || profile.value(QStringLiteral("port"), 22).toInt() != 22
        || !profile.value(QStringLiteral("proxyJump")).toString().isEmpty())
        return std::unexpected(QStringLiteral("PSMP requires a valid employee ID, DNS/IPv4 target on port 22, and no extra ProxyJump."));
    const auto account = plan.value(QStringLiteral("account")).toMap();
    const auto name = account.value(QStringLiteral("source")) == QStringLiteral("identity")
        ? employee + account.value(QStringLiteral("suffix")).toString() : account.value(QStringLiteral("name")).toString();
    const auto domain = account.value(QStringLiteral("domain")).toString();
    const auto result = employee + u'@' + name + (domain.isEmpty() ? QString{} : u'%' + domain) + u'@' + host;
    if (result.size() > 255) return std::unexpected(QStringLiteral("Resolved PSMP username exceeds 255 characters."));
    return result;
}
}
