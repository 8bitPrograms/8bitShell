#include "TerminalViewCredentials.h"

#include <bitshell/core/CredentialStore.h>
#include <bitshell/core/PasswordStore.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/PrivateFile.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <utility>

namespace bitshell::terminal {
namespace {

using core::CredentialFlow;

// Owned ssh needs the flow only while it logs in. An ad hoc tab keeps its
// binding for an hour of typing ssh commands, then forgets the secret.
constexpr int ownedDeadlineMs = 120 * 1000;
constexpr int boundDeadlineMs = 60 * 60 * 1000;
// Lifetime of a key that an ad hoc tab adds to ssh-agent.
constexpr int agentKeySeconds = 300;

void wipe(QByteArray& bytes) {
    bytes.fill('\0');
    bytes.clear();
}

// A private directory only this user can read. Linux: $XDG_RUNTIME_DIR
// (tmpfs, 0700, removed at logout). macOS: the per-user $TMPDIR. Windows: the
// profile's local app data, with PrivateFile's owner-only ACL.
QString keyDirectory() {
#ifdef Q_OS_WIN
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
#else
    const auto base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
#endif
    return base.isEmpty() ? QString{} : QDir(base).filePath(QStringLiteral("8bitShell-keys"));
}

void shred(const QString& path) {
    if (path.isEmpty()) return;
    QFile file(path);
    if (file.open(QIODevice::ReadWrite)) {
        file.write(QByteArray(file.size(), '\0'));
        file.flush();
        file.close();
    }
    QFile::remove(path);
}

// ssh reads keys only from files, so a key credential is copied to a 0600
// file for the length of one login, then overwritten and deleted.
QString writeKeyFile(QByteArray& key, QString& error) {
    const auto directory = keyDirectory();
    if (directory.isEmpty()) { wipe(key); error = QStringLiteral("No private folder is available for the key."); return {}; }
    const auto path = QDir(directory).filePath(QStringLiteral("id-%1").arg(QUuid::createUuid().toString(QUuid::Id128)));
    const bool written = core::PrivateFile::write(path, key, error);
    wipe(key);
    if (!written) { shred(path); return {}; }
    return QFileInfo(path).absoluteFilePath();
}

QByteArray typed(const Win32Input& mode, const QByteArray& bytes) {
    if (!mode.enabled()) return bytes;
    // ConPTY win32-input mode: a down/up pair per character, as in
    // completePendingLocalSsh.
    QString text = QString::fromUtf8(bytes);
    QByteArray input;
    for (QChar character : std::as_const(text)) {
        const QString key(character);
        const auto code = character == u'\r' ? Qt::Key_Return : Qt::Key_unknown;
        QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier, key);
        QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier, key);
        input += Win32Input::encode(press, true) + Win32Input::encode(release, false);
    }
    text.fill(u'\0');
    return input;
}

// "credential 'name'" or, for a remembered session password, "the saved password".
QString sourceOf(const QString& name) {
    return name.isEmpty() ? QStringLiteral("this session's saved password") : QStringLiteral("credential '%1'").arg(name);
}

QString promptWord(CredentialFlow::Prompt prompt) {
    switch (prompt) {
    case CredentialFlow::Prompt::Password: return QStringLiteral("password");
    case CredentialFlow::Prompt::Passphrase: return QStringLiteral("key passphrase");
    case CredentialFlow::Prompt::Username: return QStringLiteral("username");
    default: return QStringLiteral("answer");
    }
}

} // namespace

TerminalView::CredentialSession::CredentialSession(core::CredentialSecrets secrets, CredentialFlow::Scope scope,
                                                   QString idValue, QString nameValue, QString keyFileValue)
    : flow(std::move(secrets), scope), id(std::move(idValue)), name(std::move(nameValue)),
      keyFile(std::move(keyFileValue)) {}

TerminalView::CredentialSession::~CredentialSession() { shred(keyFile); }

TerminalView::KeyLoader::KeyLoader(core::CredentialSecrets secrets, QString nameValue, QString keyFileValue)
    : flow(std::move(secrets), CredentialFlow::Scope::OwnedSsh), name(std::move(nameValue)),
      keyFile(std::move(keyFileValue)) {}

TerminalView::KeyLoader::~KeyLoader() {
    QObject::disconnect(&process, nullptr, nullptr, nullptr);
    process.stop();
    shred(keyFile);
}

core::SecretVault& TerminalView::credentialVault() const {
    return m_credentialVault ? *m_credentialVault : core::SecretVault::system();
}

QString TerminalView::credentialName() const { return m_credential ? m_credential->name : QString{}; }

bool TerminalView::credentialBound() const {
    return m_credential && m_credential->flow.scope() == CredentialFlow::Scope::Shell;
}

// Decides how this launch logs in. It never blocks the launch: when a secret
// cannot be read, ssh starts as usual, the user types the answer, and notice
// says why. Without a credential or saved password, effective is the profile.
void TerminalView::prepareCredential(const QVariantMap& profile, QVariantMap& effective, QString& notice) {
    m_preparedCredential.reset();
    effective = profile;
    notice.clear();
    const auto id = profile.value(QStringLiteral("credential")).toString();
    if (id.isEmpty()) { prepareSavedPassword(profile, notice); return; }
    effective.remove(QStringLiteral("credential"));
    auto resolved = core::CredentialStore::resolve(id, credentialVault(),
        m_credentialPath.isEmpty() ? core::CredentialStore::defaultPath() : m_credentialPath);
    if (!resolved) {
        // No metadata (deleted, or the list is unreadable): log in as the session says.
        notice = QStringLiteral("%1 Log in yourself.").arg(resolved.error());
        return;
    }
    // The credential decides who logs in, and answers prompts itself, so the
    // saved-password path and password-only ssh options stay out of the way.
    // CyberArk routes build their own gateway login, so they keep it.
    if (!profile.contains(QStringLiteral("psmp")))
        effective.insert(QStringLiteral("user"), QString::fromUtf8(resolved->secrets.username));
    effective.insert(QStringLiteral("authenticationMode"), QStringLiteral("auto"));
    if (!resolved->secretError.isEmpty()) {
        resolved->secrets.wipe();
        notice = QStringLiteral("Could not read the saved secret for credential '%1': %2 Type it yourself.")
            .arg(resolved->name, resolved->secretError);
        return;
    }
    QString keyFile;
    if (resolved->type == QStringLiteral("key")) {
        QString error;
        keyFile = writeKeyFile(resolved->privateKey, error);
        if (keyFile.isEmpty()) {
            resolved->secrets.wipe();
            notice = QStringLiteral("Could not prepare the key for credential '%1': %2 Log in yourself.").arg(resolved->name, error);
            return;
        }
        effective.insert(QStringLiteral("identityFile"), keyFile);
        effective.insert(QStringLiteral("identityKey"), QString{});
        resolved->secrets.keyPath = keyFile.toUtf8();
    }
    m_preparedCredential = std::make_unique<CredentialSession>(std::move(resolved->secrets),
        CredentialFlow::Scope::OwnedSsh, resolved->id, resolved->name, keyFile);
}

// A session in password mode with a remembered password. The password is
// typed into the matched password prompt once, like a credential, so
// keyboard-interactive logins and 2FA steps keep working.
void TerminalView::prepareSavedPassword(const QVariantMap& profile, QString& notice) {
    if (core::PasswordStore::binding(profile).isEmpty() || !core::PasswordStore::has(profile)) return;
    auto password = core::PasswordStore::read(profile);
    if (!password) {
        notice = QStringLiteral("Could not read the saved password: %1 Type it yourself.").arg(password.error());
        return;
    }
    if (password->isEmpty()) {
        notice = QStringLiteral("The saved password is missing from %1. Type it yourself.").arg(core::PasswordStore::backendName());
        return;
    }
    core::CredentialSecrets secrets;
    secrets.username = profile.value(QStringLiteral("user")).toString().toUtf8();
    secrets.password = std::move(*password);
    m_preparedCredential = std::make_unique<CredentialSession>(std::move(secrets),
        CredentialFlow::Scope::OwnedSsh, QString{}, QString{}, QString{});
}

void TerminalView::armPreparedCredential() {
    if (!m_preparedCredential) return;
    const bool owned = m_preparedCredential->flow.scope() == CredentialFlow::Scope::OwnedSsh;
    m_credential = std::move(m_preparedCredential);
    const auto generation = ++m_credentialGeneration;
    // One timer per arming, not a poll: it only ends a flow that never settled.
    QTimer::singleShot(owned ? ownedDeadlineMs : boundDeadlineMs, this, [this, generation] {
        if (m_credential && m_credentialGeneration == generation) releaseCredential();
    });
    emit credentialChanged();
}

void TerminalView::releaseCredential() {
    m_preparedCredential.reset();
    if (!m_credential) return;
    ++m_credentialGeneration;
    m_credential.reset();
    emit credentialChanged();
}

bool TerminalView::bindCredential(const QString& id) {
    if (!m_process.isRunning()) { setStatusText(QStringLiteral("Start a session in this tab first.")); return false; }
    auto resolved = core::CredentialStore::resolve(id, credentialVault(),
        m_credentialPath.isEmpty() ? core::CredentialStore::defaultPath() : m_credentialPath);
    if (!resolved) { setStatusText(resolved.error()); return false; }
    if (!resolved->secretError.isEmpty()) {
        setStatusText(QStringLiteral("Could not read the saved secret for credential '%1': %2").arg(resolved->name, resolved->secretError));
        return false;
    }
    if (resolved->type == QStringLiteral("key")) {
#ifdef Q_OS_WIN
        resolved->secrets.wipe(); wipe(resolved->privateKey);
        setStatusText(QStringLiteral("Key credentials work only in saved sessions on Windows."));
        return false;
#else
        if (qEnvironmentVariableIsEmpty("SSH_AUTH_SOCK")) {
            resolved->secrets.wipe(); wipe(resolved->privateKey);
            setStatusText(QStringLiteral("No ssh-agent is running, so the key cannot be offered to ssh typed here. Attach it to a saved session instead."));
            return false;
        }
        QString error;
        const auto keyFile = writeKeyFile(resolved->privateKey, error);
        if (keyFile.isEmpty()) { resolved->secrets.wipe(); setStatusText(error); return false; }
        resolved->secrets.keyPath = keyFile.toUtf8();
        m_keyLoader.reset();
        m_keyLoader = std::make_unique<KeyLoader>(std::move(resolved->secrets), resolved->name, keyFile);
        auto* loader = m_keyLoader.get();
        connect(&loader->process, &PtyProcess::dataReceived, this, [this, loader](const QByteArray& data) {
            auto step = loader->flow.feed(QByteArrayView(data));
            if (step.action == CredentialFlow::Action::Send) {
                loader->process.write(step.bytes);
                wipe(step.bytes);
            } else if (step.action == CredentialFlow::Action::HandToUser
                       || (step.action == CredentialFlow::Action::Stop && step.rejected)) {
                loader->failure = QStringLiteral("ssh-add did not accept the saved passphrase.");
                // Not from inside the PTY's own read callback.
                QTimer::singleShot(0, this, [this, loader] { if (m_keyLoader.get() == loader) loader->process.stop(); });
            }
        });
        connect(&loader->process, &PtyProcess::runningChanged, this, [this, loader](bool running) {
            if (running) return;
            shred(loader->keyFile);
            loader->keyFile.clear();
            // finished may follow on this stack; delete the loader afterwards.
            QTimer::singleShot(0, this, [this, loader] { if (m_keyLoader.get() == loader) m_keyLoader.reset(); });
        });
        connect(&loader->process, &PtyProcess::finished, this, [this, loader](int exitCode) {
            if (loader->failure.isEmpty() && exitCode == 0)
                setStatusText(QStringLiteral("Key from credential '%1' is in your ssh-agent for %2 minutes. Type ssh in this tab.")
                    .arg(loader->name).arg(agentKeySeconds / 60));
            else
                setStatusText(loader->failure.isEmpty()
                    ? QStringLiteral("ssh-add could not load the key from credential '%1'.").arg(loader->name)
                    : loader->failure);
        });
        const auto sshAdd = QFileInfo(core::Platform::sshExecutable()).dir().filePath(QStringLiteral("ssh-add"));
        if (!loader->process.start(sshAdd, {QStringLiteral("-t"), QString::number(agentKeySeconds), keyFile}, 80, 24, &error)) {
            m_keyLoader.reset();
            setStatusText(QStringLiteral("Could not run ssh-add: %1").arg(error));
            return false;
        }
        QTimer::singleShot(30 * 1000, this, [this, loader] {
            if (m_keyLoader.get() != loader) return;
            loader->failure = QStringLiteral("ssh-add did not finish. The key was not added.");
            loader->process.stop();
        });
        setStatusText(QStringLiteral("Adding the key from credential '%1' to your ssh-agent…").arg(resolved->name));
        return true;
#endif
    }
    releaseCredential();
    const auto user = QString::fromUtf8(resolved->secrets.username);
    m_preparedCredential = std::make_unique<CredentialSession>(std::move(resolved->secrets),
        CredentialFlow::Scope::Shell, resolved->id, resolved->name, QString{});
    armPreparedCredential();
    setStatusText(QStringLiteral("Credential '%1' will answer the password prompt when you type ssh %2@host in this tab.")
        .arg(m_credential->name, user));
    return true;
}

void TerminalView::unbindCredential() {
    if (!m_credential) return;
    const auto name = m_credential->name;
    releaseCredential();
    setStatusText(QStringLiteral("Stopped using credential '%1' in this tab.").arg(name));
}

// Runs only while a flow is armed. The flow keeps a 256-byte tail, so the
// cost is one bounded scan of each chunk's end.
void TerminalView::answerCredentialPrompt(const QByteArray& data) {
    auto& session = *m_credential;
    if (session.restartPending) {
        session.restartPending = false;
        session.flow.restart();
    }
    auto step = session.flow.feed(QByteArrayView(data));
    switch (step.action) {
    case CredentialFlow::Action::None:
        return;
    case CredentialFlow::Action::Send: {
        auto input = typed(m_win32Input, step.bytes);
        wipe(step.bytes);
        if (!m_process.canWrite(input.size())) {
            wipe(input);
            setStatusText(QStringLiteral("The terminal input queue is full. Type the %1 yourself.").arg(promptWord(step.prompt)));
            forceActiveFocus();
            return;
        }
        m_process.write(input);
        wipe(input);
        setStatusText(QStringLiteral("Sent the saved %1 from %2.").arg(promptWord(step.prompt), sourceOf(session.name)));
        return;
    }
    case CredentialFlow::Action::HandToUser:
        forceActiveFocus();
        if (step.prompt == CredentialFlow::Prompt::OneTimeCode)
            setStatusText(QStringLiteral("Type the verification code yourself. The login continues after it."));
        else if (step.prompt == CredentialFlow::Prompt::Unknown)
            setStatusText(QStringLiteral("The server asked something %1 does not cover. Answer it yourself.").arg(sourceOf(session.name)));
        else
            setStatusText(QStringLiteral("There is no saved %1 in %2. Type it yourself.").arg(promptWord(step.prompt), sourceOf(session.name)));
        return;
    case CredentialFlow::Action::Stop: {
        const auto name = session.name;
        const bool bound = session.flow.scope() == CredentialFlow::Scope::Shell;
        if (step.rejected) {
            forceActiveFocus();
            setStatusText(QStringLiteral("The server asked again, so the saved %1 from %2 was wrong. It was not sent twice. Continue yourself.")
                .arg(promptWord(step.prompt), sourceOf(name)));
        }
        // A bound tab waits for its next local prompt; an owned login is done.
        if (!bound) releaseCredential();
        return;
    }
    }
}

} // namespace bitshell::terminal
