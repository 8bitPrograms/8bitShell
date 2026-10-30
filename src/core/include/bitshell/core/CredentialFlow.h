#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include <array>
#include <cstdint>
#include <optional>
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

// The terminal's mode when a read returned, sampled by the PTY. ssh reads a
// secret with echo off and line editing on; a session it has logged in to is
// raw. Unavailable: the platform can't tell (Windows). Failed: the mode could
// not be read, or the child has exited (its settings are stale).
// SecretReadGarbled: echo off, but -icrnl (ssh never sees the end of the
// answer) or istrip (a non-ASCII answer loses its top bits).
enum class TtyMode : std::uint8_t { Unavailable, Failed, Cooked, Raw, SecretRead, SecretReadGarbled };

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
// With the terminal mode known, a prompt read with echo off is answered only
// once confirm() sees ssh still reading it; a prompt read in any other mode,
// raw after anything else (the login is over), or an unreadable mode stops the
// flow. ssh's prompts for another user@host or key, read with echo off, go to
// the user and keep the window open; read in any other mode they are a remote's
// output and ignored. In a bound tab, once an answer was sent, the first complete
// line read cooked that is not the login's ends it (ssh -T never goes raw).
// Without the mode (Unavailable), the flow also stops at the first
// complete line that is not part of the login: in a bound tab from the typed
// ssh on, in an owned ssh once ssh has asked for anything.
// The flow stops for good, whether or not it answered anything, at any prompt
// that is not ssh's (ssh's host-key question aside). A bound tab also stops at
// the user's first keystroke unless an ssh prompt is waiting for the user.
class CredentialFlow {
public:
    // OwnedSsh: 8bitShell launched this ssh. Shell: a credential bound to a
    // shell tab; it answers only after the tab names a target (a typed ssh).
    enum class Scope : std::uint8_t { OwnedSsh, Shell };
    // Unknown: an ssh prompt for the target that no saved answer covers.
    // HostKey: ssh asks to trust the host, before it authenticates.
    // Other: a prompt that is not ssh's for the target. Never answered.
    enum class Prompt : std::uint8_t { None, OneTimeCode, Passphrase, Password, Username, Unknown, HostKey, Other };
    // Pending: a prompt read with echo off; call confirm() once output is quiet.
    enum class Action : std::uint8_t { None, Pending, Send, HandToUser, Stop };
    // Why the flow stopped, for the status line.
    enum class StopReason : std::uint8_t { None, Typed, Finished, NotReading, RawTerminal, ModeFailed,
                                           Garbled, Budget, Rejected, Foreign, Output };

    struct Step {
        Action action = Action::None;
        Prompt prompt = Prompt::None;
        // Send: answer bytes followed by CR. The caller should wipe it after writing.
        QByteArray bytes;
        // Stop: true when a spent answer was asked for again (wrong secret).
        bool rejected = false;
        // HandToUser: the other user@host (or "key '<path>'") ssh asked for.
        QByteArray asker;
    };

    static constexpr qsizetype tailCapacity = 256;
    // Ordinary output after the last answer that means "logged in".
    static constexpr qint64 settledBytes = 4096;
    // Output allowed before a prompt (banners, host-key text), in an owned ssh
    // and from a bound tab's typed ssh alike.
    static constexpr qint64 preambleBytes = 16384;

    CredentialFlow(CredentialSecrets secrets, Scope scope);
    ~CredentialFlow();
    CredentialFlow(const CredentialFlow&) = delete;
    CredentialFlow& operator=(const CredentialFlow&) = delete;

    // mode: the terminal's mode at the read that delivered chunk. Unavailable
    // keeps the line rules and answers a prompt at once.
    Step feed(QByteArrayView chunk, TtyMode mode = TtyMode::Unavailable);
    // After a Pending step and quiet output: the terminal's mode now. Send
    // when ssh is still reading with echo off; otherwise the flow stops.
    Step confirm(TtyMode mode);
    // The user@host that ssh names in its prompts. Compared case-insensitively.
    // Without one, nothing is answered except an owned key's passphrase.
    void setTarget(QByteArrayView user, QByteArrayView host);
    // The session's ProxyJump value, "[user@]host[:port],...". Without the
    // terminal mode, only these hosts' ssh prompts go to the user and keep the
    // window open; with it, any other user@host's do. Neither is answered.
    void setJumpHosts(QByteArrayView proxyJump);
    // The user typed into the terminal. Ends a bound tab's window unless an
    // ssh prompt is waiting for the user's answer. True when this stopped it.
    bool noteUserInput();
    // Starts a new round with all answers available again (ad hoc tab, back
    // at the local prompt). The secrets are kept; a Shell flow drops its target.
    void restart();

    [[nodiscard]] bool stopped() const { return m_stopped; }
    [[nodiscard]] Scope scope() const { return m_scope; }
    [[nodiscard]] StopReason stopReason() const { return m_stopReason; }
    // An answer was sent this round.
    [[nodiscard]] bool answered() const { return m_spent != 0; }
    // "user@host" ssh names in the target's prompts; empty without a target.
    [[nodiscard]] QByteArray targetLogin() const;

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
    };

    void append(QByteArrayView chunk);
    [[nodiscard]] bool endsLikePrompt() const;
    [[nodiscard]] bool loginLine(const QByteArray& text) const;
    [[nodiscard]] bool jump(QByteArrayView who) const;
    std::optional<Step> otherLogin(const QByteArray& line, TtyMode mode);
    Step decide(Prompt prompt, TtyMode mode);
    Step send(Prompt prompt);
    Step stop(StopReason reason);

    CredentialSecrets m_secrets;
    QByteArray m_user;
    QByteArray m_host;
    std::vector<Jump> m_jumps;
    // The current line, up to its last tailCapacity bytes.
    std::array<char, tailCapacity> m_tail{};
    qsizetype m_tailSize = 0;
    qint64 m_quietBytes = 0;
    Scope m_scope;
    StopReason m_stopReason = StopReason::None;
    Prompt m_pending = Prompt::None; // read with echo off, awaiting confirm()
    // Reset only by restart(): it survives mode flips, pending fills and reads.
    std::uint8_t m_spent = 0;
    bool m_sawNonRaw = false; // a non-Raw mode since the target was set
    bool m_engaged = false;
    bool m_stopped = false;
    bool m_promptSeen = false;   // the current line's prompt was already handled
    bool m_awaitingUser = false; // an ssh prompt on the current line is the user's to answer
};

} // namespace bitshell::core
