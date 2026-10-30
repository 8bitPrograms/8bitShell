#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include <array>
#include <cstdint>
#include <vector>

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
// The flow stops for good, whether or not it answered anything, at any prompt
// that is not ssh's (ssh's host-key question aside) and at the first complete
// line that is not part of the login: in a bound tab from the typed ssh on, in
// an owned ssh once ssh has asked for anything. A bound tab also stops at the
// user's first keystroke unless an ssh prompt is waiting for the user.
class CredentialFlow {
public:
    // OwnedSsh: 8bitShell launched this ssh. Shell: a credential bound to a
    // shell tab; it answers only after the tab names a target (a typed ssh).
    enum class Scope : std::uint8_t { OwnedSsh, Shell };
    // Unknown: an ssh prompt for the target that no saved answer covers.
    // HostKey: ssh asks to trust the host, before it authenticates.
    // Other: a prompt that is not ssh's for the target. Never answered.
    enum class Prompt : std::uint8_t { None, OneTimeCode, Passphrase, Password, Username, Unknown, HostKey, Other };
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
    // (host-key question, warnings).
    static constexpr qint64 shellWindowBytes = 8192;

    CredentialFlow(CredentialSecrets secrets, Scope scope);
    ~CredentialFlow();
    CredentialFlow(const CredentialFlow&) = delete;
    CredentialFlow& operator=(const CredentialFlow&) = delete;

    Step feed(QByteArrayView chunk);
    // The user@host that ssh names in its prompts. Compared case-insensitively.
    // Without one, nothing is answered except an owned key's passphrase.
    void setTarget(QByteArrayView user, QByteArrayView host);
    // The session's ProxyJump value, "[user@]host[:port],...". A jump host's
    // ssh prompts are part of the login: the password prompt for the
    // credential's own user there is answered once per hop, any other goes to
    // the user, and the window stays open for the target.
    void setJumpHosts(QByteArrayView proxyJump);
    // The user typed into the terminal. Ends a bound tab's window unless an
    // ssh prompt is waiting for the user's answer. True when this stopped it.
    bool noteUserInput();
    // Starts a new round with all answers available again (ad hoc tab, back
    // at the local prompt). The secrets are kept; a Shell flow drops its target.
    void restart();

    [[nodiscard]] bool stopped() const { return m_stopped; }
    [[nodiscard]] Scope scope() const { return m_scope; }

    // Exposed for tests. Classifies one already-stripped prompt line.
    [[nodiscard]] static Prompt classify(QByteArrayView line, Scope scope, QByteArrayView user,
                                         QByteArrayView host, QByteArrayView keyPath);
    // The last line of text with escape codes and trailing whitespace removed.
    [[nodiscard]] static QByteArray promptLine(QByteArrayView text);

private:
    enum Spent : std::uint8_t { SpentUsername = 1, SpentPassword = 2, SpentPassphrase = 4 };
    struct Jump {
        QByteArray user; // empty: ssh picks it, so any user at this host
        QByteArray host;
        bool spent = false;
    };

    void append(QByteArrayView chunk);
    [[nodiscard]] bool endsLikePrompt() const;
    [[nodiscard]] bool loginLine(const QByteArray& text) const;
    [[nodiscard]] qsizetype jumpFor(const QByteArray& text, QByteArray& user) const;
    Step decide(Prompt prompt);
    Step decideJump(Jump& jump, const QByteArray& user, const QByteArray& text);
    Step stop(bool rejected = false);

    CredentialSecrets m_secrets;
    QByteArray m_user;
    QByteArray m_host;
    std::vector<Jump> m_jumps;
    // The current line, up to its last tailCapacity bytes.
    std::array<char, tailCapacity> m_tail{};
    qsizetype m_tailSize = 0;
    qint64 m_quietBytes = 0;
    Scope m_scope;
    std::uint8_t m_spent = 0;
    bool m_engaged = false;
    bool m_stopped = false;
    bool m_promptSeen = false;   // the current line's prompt was already handled
    bool m_awaitingUser = false; // an ssh prompt on the current line is the user's to answer
};

} // namespace bitshell::core
