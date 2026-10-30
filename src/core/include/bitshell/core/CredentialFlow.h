#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include <array>
#include <cstdint>

namespace bitshell::core {

// Answers for one credential. Zeroed by wipe() and by the flow's destructor.
struct CredentialSecrets {
    QByteArray username;
    QByteArray password;
    QByteArray passphrase;
    // Set for key credentials: a passphrase prompt must name this file.
    QByteArray keyPath;
    void wipe();
};

// Answers login prompts in the order a server asks them, by matching the
// prompt text. Pure: no timers, no I/O, no Qt objects. The caller feeds each
// output chunk and acts on the returned step.
//
// Only OpenSSH's own prompts for the target user@host are answered:
//   user@host's password:          (password authentication)
//   (user@host) Password:          (keyboard-interactive, OpenSSH 8.4+)
//   Enter passphrase for key '<keyPath>':
// Anything else (sudo, su, a vault, a bare "Password:") never gets a secret.
// One-time-code prompts are never answered. Every answer is sent at most once:
// a prompt that asks for a spent answer means it was rejected, so the flow stops.
// After an answer, the first line of output that is not part of the login, or
// any prompt that is not ssh's, ends the flow: the login is over.
class CredentialFlow {
public:
    // OwnedSsh: 8bitShell launched this ssh. Shell: a credential bound to a
    // shell tab; it answers only after the tab names a target (a typed ssh).
    enum class Scope : std::uint8_t { OwnedSsh, Shell };
    // Unknown: an ssh prompt for the target that no saved answer covers.
    // Other: a prompt that is not ssh's for the target. Never answered.
    enum class Prompt : std::uint8_t { None, OneTimeCode, Passphrase, Password, Username, Unknown, Other };
    enum class Action : std::uint8_t { None, Send, HandToUser, Stop };

    struct Step {
        Action action = Action::None;
        Prompt prompt = Prompt::None;
        // Send: answer bytes followed by CR. The caller should wipe it after writing.
        QByteArray bytes;
        // Stop: true when a spent answer was asked for again (wrong secret).
        bool rejected = false;
    };

    static constexpr qsizetype tailCapacity = 256;
    // Ordinary output after the last answer that means "logged in".
    static constexpr qint64 settledBytes = 4096;
    // Output allowed before any prompt in an owned ssh (banners, host-key text).
    static constexpr qint64 preambleBytes = 16384;
    // Output allowed before the prompt in a bound shell tab, from the typed
    // ssh to its password prompt. ssh prints a few hundred bytes at most
    // (host-key question, warnings); a server banner fits too.
    static constexpr qint64 shellWindowBytes = 8192;

    CredentialFlow(CredentialSecrets secrets, Scope scope);
    ~CredentialFlow();
    CredentialFlow(const CredentialFlow&) = delete;
    CredentialFlow& operator=(const CredentialFlow&) = delete;

    Step feed(QByteArrayView chunk);
    // The user@host that ssh names in its prompts. Compared case-insensitively.
    // Without one, nothing is answered except an owned key's passphrase.
    void setTarget(QByteArrayView user, QByteArrayView host);
    // Starts a new round with all answers available again (ad hoc tab, back
    // at the local prompt). The secrets are kept; a Shell flow drops its target.
    void restart();

    [[nodiscard]] bool stopped() const { return m_stopped; }
    [[nodiscard]] bool engaged() const { return m_engaged; }
    [[nodiscard]] Scope scope() const { return m_scope; }

    // Exposed for tests. Classifies one already-stripped prompt line.
    [[nodiscard]] static Prompt classify(QByteArrayView line, Scope scope, QByteArrayView user,
                                         QByteArrayView host, QByteArrayView keyPath);
    // The last line of text with escape codes and trailing whitespace removed.
    [[nodiscard]] static QByteArray promptLine(QByteArrayView text);

private:
    enum Spent : std::uint8_t { SpentUsername = 1, SpentPassword = 2, SpentPassphrase = 4 };

    void append(QByteArrayView chunk);
    [[nodiscard]] bool endsLikePrompt() const;
    [[nodiscard]] bool leftLogin() const;
    Step decide(Prompt prompt);
    Step stop(bool rejected = false);

    CredentialSecrets m_secrets;
    QByteArray m_user;
    QByteArray m_host;
    std::array<char, tailCapacity> m_tail{};
    qsizetype m_tailSize = 0;
    qint64 m_quietBytes = 0;
    Scope m_scope;
    std::uint8_t m_spent = 0;
    bool m_engaged = false;
    bool m_stopped = false;
};

} // namespace bitshell::core
