#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <bitshell/terminal/TerminalView.h>
#include <bitshell/terminal/BroadcastController.h>
#include <bitshell/core/SessionStore.h>
#include <bitshell/core/AppSettings.h>
#include <bitshell/core/HighlightRuleStore.h>
#include <bitshell/core/SftpController.h>
#include <bitshell/core/MacroStore.h>
#include <bitshell/core/LocalProfileStore.h>
#include <bitshell/core/HostMonitor.h>
#include <bitshell/core/LocalFileModel.h>
#include <bitshell/core/PersonalProfile.h>
#include <bitshell/core/SshKeyStore.h>
#include <bitshell/core/CredentialStore.h>

namespace bitshell::app {
struct LocalProfileStoreRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::core::LocalProfileStore)
    QML_NAMED_ELEMENT(LocalProfileStore)
    QML_SINGLETON
    QML_ADDED_IN_VERSION(1, 0)
};
struct BroadcastControllerRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::terminal::BroadcastController)
    QML_NAMED_ELEMENT(BroadcastController)
    QML_ADDED_IN_VERSION(1, 0)
};
struct PersonalProfileRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::core::PersonalProfile)
    QML_NAMED_ELEMENT(PersonalProfile)
    QML_SINGLETON
    QML_ADDED_IN_VERSION(1, 0)
};
struct SshKeyStoreRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::core::SshKeyStore)
    QML_NAMED_ELEMENT(SshKeyStore)
    QML_SINGLETON
    QML_ADDED_IN_VERSION(1, 0)
};
struct CredentialStoreRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::core::CredentialStore)
    QML_NAMED_ELEMENT(CredentialStore)
    QML_SINGLETON
    QML_ADDED_IN_VERSION(1, 0)
};
struct LocalFileModelRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::core::LocalFileModel)
    QML_NAMED_ELEMENT(LocalFileModel)
    QML_ADDED_IN_VERSION(1, 0)
};
struct MacroStoreRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::core::MacroStore)
    QML_NAMED_ELEMENT(MacroStore)
    QML_SINGLETON
    QML_ADDED_IN_VERSION(1, 0)
};
struct HostMonitorRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::core::HostMonitor)
    QML_NAMED_ELEMENT(HostMonitor)
    QML_ADDED_IN_VERSION(1, 0)
};

struct TerminalViewRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::terminal::TerminalView)
    QML_NAMED_ELEMENT(TerminalView)
    QML_ADDED_IN_VERSION(1, 0)
};

struct SessionStoreRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::core::SessionStore)
    QML_NAMED_ELEMENT(SessionStore)
    QML_SINGLETON
    QML_ADDED_IN_VERSION(1, 0)
};

struct AppSettingsRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::core::AppSettings)
    QML_NAMED_ELEMENT(AppSettings)
    QML_SINGLETON
    QML_ADDED_IN_VERSION(1, 0)
};

struct SftpControllerRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::core::SftpController)
    QML_NAMED_ELEMENT(SftpController)
    QML_ADDED_IN_VERSION(1, 0)
};

struct HighlightRuleStoreRegistration {
    Q_GADGET
    QML_FOREIGN(bitshell::core::HighlightRuleStore)
    QML_NAMED_ELEMENT(HighlightRuleStore)
    QML_SINGLETON
    QML_ADDED_IN_VERSION(1, 0)
};

} // namespace bitshell::app
