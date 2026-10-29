#pragma once

#include <QString>
#include <QProcessEnvironment>
#include <QVariantMap>
#include <expected>

namespace bitshell::core {

// Creates one command for an already running local shell. The profile must be
// resolved through SessionStore::prepareProfile, including inherited routes.
// No connection is opened and no key material is read or put into the command.
// Windows PowerShell prefers its PATH client, with the packaged client as a
// fallback. Bash and WSL always use the client belonging to their environment.
class NativeSshCommand {
public:
    static std::expected<QString, QString> command(const QVariantMap& profile,
                                                   const QString& shellKind,
                                                   const QProcessEnvironment& askpassEnvironment = {});
};

} // namespace bitshell::core
