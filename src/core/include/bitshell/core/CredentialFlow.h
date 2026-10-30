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
// One-time-code prompts are never answered. Every answer is sent at most once:
// a prompt that asks for a spent answer means it was rejected, so the flow stops.
class CredentialFlow {
public:
    // OwnedSsh: 8bitShell launched this ssh, so any password prompt is ours.
    // Shell: a credential bound to a shell tab. Only an SSH password prompt for
    // the bound user is answered, so sudo/su never receive the secret.
    enum class Scope : std::uint8_t { OwnedSsh, Shell };
    enum class Prompt : std::uint8_t { None, OneTimeCode, Passphrase, Password, Username, Unknown };
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

    CredentialFlow(CredentialSecrets secrets, Scope scope);
    ~CredentialFlow();
    CredentialFlow(const CredentialFlow&) = delete;
    CredentialFlow& operator=(const CredentialFlow&) = delete;

    Step feed(QByteArrayView chunk);
    // Starts a new round with all answers available again (ad hoc tab, back
    // at the local prompt). The secrets are kept.
    void restart();

    [[nodiscard]] bool stopped() const { return m_stopped; }
    [[nodiscard]] bool engaged() const { return m_engaged; }
    [[nodiscard]] Scope scope() const { return m_scope; }

    // Exposed for tests. Classifies one already-stripped prompt line.
    [[nodiscard]] static Prompt classify(QByteArrayView line, Scope scope,
                                         QByteArrayView user, QByteArrayView keyPath);
    // The last line of text with escape codes and trailing whitespace removed.
    [[nodiscard]] static QByteArray promptLine(QByteArrayView text);

private:
    enum Spent : std::uint8_t { SpentUsername = 1, SpentPassword = 2, SpentPassphrase = 4 };

    void append(QByteArrayView chunk);
    [[nodiscard]] bool endsLikePrompt() const;
    Step decide(Prompt prompt);

    CredentialSecrets m_secrets;
    std::array<char, tailCapacity> m_tail{};
    qsizetype m_tailSize = 0;
    qint64 m_quietBytes = 0;
    Scope m_scope;
    std::uint8_t m_spent = 0;
    bool m_engaged = false;
    bool m_stopped = false;
};

} // namespace bitshell::core
