#pragma once

// Private to bitshell_terminal. Complete types for TerminalView's credential
// members, shared by TerminalView.cpp (lifetime) and TerminalViewCredentials.cpp.

#include <bitshell/terminal/TerminalView.h>
#include <bitshell/core/CredentialFlow.h>

namespace bitshell::terminal {

// One armed prompt flow. Destroying it wipes the secrets and the key file.
struct TerminalView::CredentialSession {
    CredentialSession(core::CredentialSecrets secrets, core::CredentialFlow::Scope scope,
                      QString id, QString name, QString keyFile);
    ~CredentialSession();
    CredentialSession(const CredentialSession&) = delete;
    CredentialSession& operator=(const CredentialSession&) = delete;

    core::CredentialFlow flow;
    QString id;
    QString name;
    QString keyFile; // temporary private key copy, empty for passwords
    bool restartPending = false;
};

// Hidden `ssh-add` that puts a key credential into the user's agent for an
// ad hoc tab. It answers only its own passphrase prompt.
struct TerminalView::KeyLoader {
    KeyLoader(core::CredentialSecrets secrets, QString name, QString keyFile);
    ~KeyLoader();
    KeyLoader(const KeyLoader&) = delete;
    KeyLoader& operator=(const KeyLoader&) = delete;

    PtyProcess process;
    core::CredentialFlow flow;
    QString name;
    QString keyFile;
    QString failure;
};

} // namespace bitshell::terminal
