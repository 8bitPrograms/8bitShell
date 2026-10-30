#include <bitshell/core/CredentialFlow.h>
#include <bitshell/core/SecureMemory.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace bitshell::core {
namespace {
constexpr char escape = '\x1b';

// In place: a detach-then-fill would zero a fresh copy and leave the original.
void zero(QByteArray& value) { secureWipe(value); }

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isWord(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || (c >= 'A' && c <= 'Z');
}

QByteArray lower(QByteArrayView text) {
    QByteArray result(text.data(), text.size());
    for (char& c : result)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return result;
}

bool containsWord(const QByteArray& line, QByteArrayView word) {
    for (qsizetype at = line.indexOf(word); at >= 0; at = line.indexOf(word, at + 1)) {
        const qsizetype end = at + word.size();
        if ((at == 0 || !isWord(line.at(at - 1))) && (end == line.size() || !isWord(line.at(end))))
            return true;
    }
    return false;
}

bool oneTimeCode(const QByteArray& line) {
    // Phrases are matched as substrings, short tokens as whole words.
    static constexpr const char* phrases[] = {"verification", "one-time", "one time", "two-factor",
        "two factor", "passcode", "authenticator", "security code", "auth code", "sms code",
        "securid", "yubikey", "yubico", "second password", "next password"};
    static constexpr const char* words[] = {"otp", "totp", "hotp", "2fa", "mfa", "token", "duo", "pin", "code", "sms"};
    for (const char* phrase : phrases)
        if (line.contains(phrase)) return true;
    for (const char* word : words)
        if (containsWord(line, word)) return true;
    return false;
}

bool usernamePrompt(QByteArrayView body) {
    for (const char* label : {"login", "login as", "username", "user name", "user", "user id"})
        if (body == QByteArrayView(label)) return true;
    return body.endsWith(" login");
}

QByteArray target(QByteArrayView user, QByteArrayView host) { return lower(user) + '@' + lower(host); }

// OpenSSH's own prompt forms for this target, given the lowercased line.
bool fromSsh(const QByteArray& text, QByteArrayView user, QByteArrayView host) {
    if (text.startsWith("enter passphrase for") || text.startsWith("bad passphrase")) return true;
    if (user.isEmpty() || host.isEmpty()) return false;
    const QByteArray who = target(user, host);
    return text == who + "'s password:" || text.startsWith('(' + who + ") ");
}

// The user@host an OpenSSH prompt names, given the lowercased line:
// "(user@host) ..." or "user@host's password:". Empty for anything else.
QByteArrayView sshWho(const QByteArray& text) {
    QByteArrayView who;
    if (text.startsWith('(')) {
        const qsizetype close = text.indexOf(") ");
        if (close < 0) return {};
        who = QByteArrayView(text).sliced(1, close - 1);
    } else if (text.endsWith("'s password:")) {
        who = QByteArrayView(text).chopped(12);
    } else {
        return {};
    }
    const qsizetype at = who.lastIndexOf('@');
    if (at <= 0 || at + 1 == who.size() || who.contains(' ')) return {};
    return who;
}

// ssh's own lines before it authenticates: the host-key question with its
// echoed answer, the key's names, and the post-quantum warning.
bool sshMessage(const QByteArray& text) {
    static constexpr const char* prefixes[] = {"permission denied", "the authenticity of host",
        "are you sure you want to continue connecting", "please type 'yes', 'no' or the fingerprint",
        "this key is not known by any other names", "this host key is known by the following other names",
        "warning: permanently added", "** warning: connection is not using a post-quantum",
        "** this session may be vulnerable", "** the server may need to be upgraded"};
    for (const char* prefix : prefixes)
        if (text.startsWith(prefix)) return true;
    return text.contains(" key fingerprint is ") || text.contains("known_hosts:");
}
} // namespace

void CredentialSecrets::wipe() {
    zero(username);
    zero(password);
    zero(passphrase);
    keyPath.clear();
}

void CredentialFlow::setTarget(QByteArrayView user, QByteArrayView host) {
    m_user = lower(user);
    m_host = lower(host);
    m_quietBytes = 0; // a bound tab's window starts at the typed ssh
    m_sawNonRaw = m_scope == Scope::OwnedSsh; // an owned ssh starts on a fresh, cooked PTY
}

QByteArray CredentialFlow::targetLogin() const { return m_host.isEmpty() ? QByteArray{} : target(m_user, m_host); }

CredentialFlow::CredentialFlow(CredentialSecrets secrets, Scope scope)
    : m_secrets(std::move(secrets)), m_scope(scope) {}

CredentialFlow::~CredentialFlow() {
    m_secrets.wipe();
    secureZero(m_tail.data(), m_tail.size());
}

void CredentialFlow::setJumpHosts(QByteArrayView proxyJump) {
    m_jumps.clear();
    const QByteArray list = lower(proxyJump);
    for (qsizetype begin = 0; begin <= list.size();) {
        qsizetype end = list.indexOf(',', begin);
        if (end < 0) end = list.size();
        const QByteArrayView hop = QByteArrayView(list).sliced(begin, end - begin).trimmed();
        begin = end + 1;
        const qsizetype at = hop.lastIndexOf('@');
        Jump jump;
        if (at > 0) jump.user = hop.first(at).toByteArray();
        QByteArrayView host = hop.sliced(at + 1);
        if (host.startsWith('[')) {
            const qsizetype close = host.indexOf(']');
            host = close > 0 ? host.sliced(1, close - 1) : QByteArrayView{};
        } else if (const qsizetype colon = host.indexOf(':'); colon >= 0) {
            host = host.first(colon);
        }
        if (host.isEmpty()) continue;
        jump.host = host.toByteArray();
        m_jumps.push_back(std::move(jump));
    }
}

bool CredentialFlow::noteUserInput() {
    if (m_stopped || m_scope != Scope::Shell || m_host.isEmpty() || m_awaitingUser) return false;
    m_stopped = true;
    m_stopReason = StopReason::Typed;
    return true;
}

void CredentialFlow::restart() {
    m_tailSize = 0;
    m_quietBytes = 0;
    m_spent = 0;
    m_engaged = false;
    m_stopped = false;
    m_stopReason = StopReason::None;
    m_pending = Prompt::None;
    m_sawNonRaw = m_scope == Scope::OwnedSsh;
    m_promptSeen = false;
    m_awaitingUser = false;
    if (m_scope == Scope::Shell) setTarget({}, {});
}

void CredentialFlow::append(QByteArrayView chunk) {
    if (chunk.size() >= tailCapacity) {
        std::memcpy(m_tail.data(), chunk.data() + chunk.size() - tailCapacity, tailCapacity);
        m_tailSize = tailCapacity;
        return;
    }
    const qsizetype overflow = m_tailSize + chunk.size() - tailCapacity;
    if (overflow > 0) {
        std::memmove(m_tail.data(), m_tail.data() + overflow, static_cast<size_t>(m_tailSize - overflow));
        m_tailSize -= overflow;
    }
    std::memcpy(m_tail.data() + m_tailSize, chunk.data(), static_cast<size_t>(chunk.size()));
    m_tailSize += chunk.size();
}

// Looks only at the last meaningful byte, skipping blanks and trailing CSI or
// BEL-terminated OSC sequences (ConPTY and some servers add them after a prompt).
bool CredentialFlow::endsLikePrompt() const {
    qsizetype end = m_tailSize;
    const char* tail = m_tail.data();
    for (;;) {
        while (end > 0 && (tail[end - 1] == ' ' || tail[end - 1] == '\t')) --end;
        if (end == 0) return false;
        const char c = tail[end - 1];
        if (c == ':' || c == '?') return true;
        if (c >= 0x40 && c <= 0x7e) {
            qsizetype at = end - 2;
            while (at >= 0 && tail[at] >= 0x20 && tail[at] <= 0x3f) --at;
            if (at >= 1 && tail[at] == '[' && tail[at - 1] == escape) { end = at - 1; continue; }
            return false;
        }
        if (c == '\a') {
            qsizetype at = end - 2;
            while (at >= 1 && !(tail[at] == ']' && tail[at - 1] == escape)) --at;
            if (at >= 1) { end = at - 1; continue; }
        }
        return false;
    }
}

QByteArray CredentialFlow::promptLine(QByteArrayView text) {
    const qsizetype newline = text.lastIndexOf('\n');
    const QByteArrayView last = text.sliced(newline + 1);
    QByteArray line;
    line.reserve(last.size());
    for (qsizetype at = 0; at < last.size(); ++at) {
        const char c = last[at];
        if (c == escape && at + 1 < last.size()) {
            const char kind = last[at + 1];
            at += 2;
            if (kind == '[') {
                while (at < last.size() && !(last[at] >= 0x40 && last[at] <= 0x7e)) ++at;
            } else if (kind == ']') {
                while (at < last.size() && last[at] != '\a'
                       && !(last[at] == escape && at + 1 < last.size() && last[at + 1] == '\\')) ++at;
                if (at < last.size() && last[at] == escape) ++at;
            } else {
                --at;
            }
            continue;
        }
        if (c == '\r') { line.clear(); continue; }
        if (static_cast<unsigned char>(c) >= 0x20 || c == '\t') line.append(c);
    }
    qsizetype begin = 0, end = line.size();
    while (begin < end && isSpace(line.at(begin))) ++begin;
    while (end > begin && isSpace(line.at(end - 1))) --end;
    return line.sliced(begin, end - begin);
}

CredentialFlow::Prompt CredentialFlow::classify(QByteArrayView line, Scope scope, QByteArrayView user,
                                                QByteArrayView host, QByteArrayView keyPath) {
    if (line.isEmpty() || (line.back() != ':' && line.back() != '?')) return Prompt::None;
    const QByteArray text = lower(line);
    // ssh asks for the temporary key's passphrase; ssh-add asks again with "Bad passphrase".
    if (text.startsWith("enter passphrase for") || text.startsWith("bad passphrase")) {
        const bool ours = scope == Scope::OwnedSsh && !keyPath.isEmpty() && line.contains(keyPath);
        return ours ? Prompt::Passphrase : Prompt::Other;
    }
    if (text.startsWith("are you sure you want to continue connecting")) return Prompt::HostKey;
    if (!fromSsh(text, user, host)) {
        // Not ssh's prompt for the target. A code prompt is still worth a
        // hint in an owned login (OpenSSH before 8.4 prints them bare).
        return scope == Scope::OwnedSsh && oneTimeCode(text) ? Prompt::OneTimeCode : Prompt::Other;
    }
    // Password authentication. Matched whole, so a host name never looks like a code.
    if (text.endsWith("'s password:") && !text.startsWith('(')) return Prompt::Password;
    // Keyboard-interactive: "(user@host) <server's prompt>".
    const qsizetype close = text.indexOf(") ");
    const QByteArrayView body = QByteArrayView(text).sliced(close + 2).chopped(1).trimmed();
    if (oneTimeCode(body.toByteArray())) return Prompt::OneTimeCode;
    const QByteArray name = lower(user);
    if (line.back() == ':'
        && (body == "password" || body == "enter password" || body == "enter your password"
            || body == "password for " + name || body == name + "'s password"))
        return Prompt::Password;
    if (line.back() == ':' && usernamePrompt(body)) return Prompt::Username;
    return Prompt::Unknown;
}

CredentialFlow::Step CredentialFlow::stop(StopReason reason) {
    m_stopped = true;
    m_stopReason = reason;
    Step step;
    step.action = Action::Stop;
    step.rejected = reason == StopReason::Rejected;
    return step;
}

// Whether a complete, lowercased line belongs to the login: ssh's prompts for
// the target or a jump host, ssh's own messages, or the typed command's echo
// (the typed-ssh grammar always ends it with the host).
bool CredentialFlow::loginLine(const QByteArray& text) const {
    if (text.isEmpty() || fromSsh(text, m_user, m_host) || sshMessage(text) || jump(sshWho(text))) return true;
    return m_scope == Scope::Shell && text.contains("ssh")
        && (text.endsWith(' ' + m_host) || text.endsWith('@' + m_host));
}

// Whether a lowercased user@host is one of the ProxyJump hosts.
bool CredentialFlow::jump(QByteArrayView who) const {
    const qsizetype at = who.lastIndexOf('@');
    if (at <= 0) return false;
    const QByteArrayView name = who.first(at), host = who.sliced(at + 1);
    return std::any_of(m_jumps.cbegin(), m_jumps.cend(), [&](const Jump& jump) {
        return jump.host == host && (jump.user.isEmpty() || jump.user == name);
    });
}

// ssh's prompt for another user@host (a ProxyJump hop, an alias) or for a key
// that is not the credential's own, read with echo off, goes to the user, and
// the window stays open for the target. Read in any other mode it is a remote's
// output: ignored (an empty step). Without the terminal mode, only a ProxyJump
// hop's goes to the user. Nullopt: not another login's prompt.
std::optional<CredentialFlow::Step> CredentialFlow::otherLogin(const QByteArray& line, TtyMode mode) {
    const QByteArray text = lower(line);
    const QByteArrayView who = sshWho(text);
    const bool host = !who.isEmpty() && who != target(m_user, m_host);
    const bool key = !host && mode != TtyMode::Unavailable && text.startsWith("enter passphrase for key '")
        && classify(line, m_scope, m_user, m_host, m_secrets.keyPath) != Prompt::Passphrase;
    if (mode == TtyMode::Unavailable ? !(host && jump(who)) : !(host || key)) return std::nullopt;
    if (mode != TtyMode::Unavailable && mode != TtyMode::SecretRead && mode != TtyMode::SecretReadGarbled) return Step{};
    Step step;
    if (host) {
        const qsizetype at = who.lastIndexOf('@');
        step.prompt = classify(line, m_scope, who.first(at), who.sliced(at + 1), {});
        if (step.prompt != Prompt::OneTimeCode && step.prompt != Prompt::Password) step.prompt = Prompt::Unknown;
        step.asker = who.toByteArray();
    } else {
        step.prompt = Prompt::Passphrase;
        step.asker = line.sliced(21).chopped(1); // key '<path>'
    }
    m_quietBytes = 0;
    m_awaitingUser = true;
    step.action = Action::HandToUser;
    return step;
}

CredentialFlow::Step CredentialFlow::decide(Prompt prompt, TtyMode mode) {
    Step step;
    step.prompt = prompt;
    m_quietBytes = 0;
    switch (prompt) {
    case Prompt::HostKey: // ssh's own question, before it authenticates
        m_awaitingUser = true;
        if (m_scope == Scope::OwnedSsh) step.action = Action::HandToUser;
        return step;
    case Prompt::Other: // someone else asks: whatever happens next, it is not this login
        m_stopped = true;
        m_stopReason = StopReason::Foreign;
        step.action = m_scope == Scope::OwnedSsh && !m_spent ? Action::HandToUser : Action::Stop;
        return step;
    case Prompt::OneTimeCode:
    case Prompt::Unknown:
        m_engaged = true;
        m_awaitingUser = true;
        step.action = Action::HandToUser;
        return step;
    default:
        break;
    }
    // ssh reads every answer with echo off. A prompt read in any other mode is
    // not ssh's question (a remote's output, a prompt printed before -echo).
    // Raw here was raw at every read since the target: a raw outer terminal.
    if (mode == TtyMode::Cooked || mode == TtyMode::Raw)
        return stop(mode == TtyMode::Raw ? StopReason::RawTerminal : StopReason::NotReading);
    if (mode == TtyMode::SecretReadGarbled) return stop(StopReason::Garbled);
    m_engaged = true;
    const QByteArray* answer = &m_secrets.password;
    std::uint8_t bit = SpentPassword;
    if (prompt == Prompt::Username) { answer = &m_secrets.username; bit = SpentUsername; }
    if (prompt == Prompt::Passphrase) { answer = &m_secrets.passphrase; bit = SpentPassphrase; }
    if (m_spent & bit) {
        step = stop(StopReason::Rejected);
        step.prompt = prompt;
        return step;
    }
    if (answer->isEmpty()) {
        m_awaitingUser = true;
        step.action = Action::HandToUser;
        return step;
    }
    if (mode == TtyMode::SecretRead) {
        m_pending = prompt;
        step.action = Action::Pending;
        return step;
    }
    return send(prompt);
}

CredentialFlow::Step CredentialFlow::send(Prompt prompt) {
    const QByteArray* answer = &m_secrets.password;
    std::uint8_t bit = SpentPassword;
    if (prompt == Prompt::Username) { answer = &m_secrets.username; bit = SpentUsername; }
    if (prompt == Prompt::Passphrase) { answer = &m_secrets.passphrase; bit = SpentPassphrase; }
    m_spent |= bit;
    Step step;
    step.prompt = prompt;
    step.action = Action::Send;
    step.bytes.reserve(answer->size() + 1);
    step.bytes.append(*answer).append('\r');
    return step;
}

CredentialFlow::Step CredentialFlow::confirm(TtyMode mode) {
    if (m_stopped || m_pending == Prompt::None) return {};
    const Prompt prompt = std::exchange(m_pending, Prompt::None);
    if (mode != TtyMode::SecretRead)
        return stop(mode == TtyMode::Failed ? StopReason::ModeFailed
                    : mode == TtyMode::SecretReadGarbled ? StopReason::Garbled : StopReason::NotReading);
    return send(prompt);
}

CredentialFlow::Step CredentialFlow::feed(QByteArrayView chunk, TtyMode mode) {
    // A Shell flow without a target waits for one at no cost.
    if (m_stopped || chunk.isEmpty() || (m_scope == Scope::Shell && m_host.isEmpty())) return {};
    // New output replaces a pending prompt and its line is judged afresh: the last prompt wins.
    if (m_pending != Prompt::None) {
        m_pending = Prompt::None;
        m_promptSeen = false;
    }
    if (mode == TtyMode::Failed) return stop(StopReason::ModeFailed);
    // ssh's session goes raw once it is logged in (key, agent, ControlMaster or
    // an answered prompt). Raw at every read is a raw outer terminal (tmux).
    if (mode == TtyMode::Raw) {
        if (m_sawNonRaw) return stop(StopReason::Finished);
    } else if (mode != TtyMode::Unavailable) {
        m_sawNonRaw = true;
    }
    // Counted before the prompt check, so one oversized read ending in a prompt gets nothing.
    m_quietBytes += chunk.size();
    if (m_quietBytes > (m_spent ? settledBytes : preambleBytes)) return stop(StopReason::Budget);
    // Without the mode, every complete line is judged, not only the last: a bound tab's window
    // ends at the first one that is not the login's, an owned login's once
    // ssh has asked for anything. The one exception: once ssh has asked, the
    // lines that arrive with a keyboard-interactive prompt for the target are
    // that prompt's instructions (Duo, "Login incorrect"). An answer already
    // given is never given again, so they cannot pull a secret out twice.
    bool instructions = false;
    if (m_engaged && mode == TtyMode::Unavailable) {
        const QByteArray last = lower(promptLine(chunk.sliced(chunk.lastIndexOf('\n') + 1)));
        instructions = (last.endsWith(':') || last.endsWith('?')) && last.startsWith('(' + target(m_user, m_host) + ") ");
    }
    // Lines that cannot end the window (an owned login's banner, any line once
    // the mode is known) are skipped without being copied.
    // ssh -T never goes raw: in a bound tab, once an answer was sent, a cooked
    // line that is not the login's (ssh's "Permission denied" is) means logged in.
    const bool loggedIn = m_scope == Scope::Shell && m_spent && mode == TtyMode::Cooked;
    const bool judge = loggedIn || (mode == TtyMode::Unavailable && (m_scope == Scope::Shell || m_engaged) && !instructions);
    for (qsizetype newline = chunk.indexOf('\n'); newline >= 0; newline = chunk.indexOf('\n')) {
        const QByteArrayView piece = chunk.first(newline);
        chunk = chunk.sliced(newline + 1);
        m_promptSeen = false;
        m_awaitingUser = false;
        if (!judge) {
            m_tailSize = 0;
            continue;
        }
        append(piece);
        QByteArrayView raw(m_tail.data(), m_tailSize);
        while (!raw.isEmpty() && raw.back() == '\r') raw.chop(1);
        const QByteArray line = lower(promptLine(raw));
        m_tailSize = 0;
        if (!loginLine(line)) return stop(loggedIn ? StopReason::Finished : StopReason::Output);
    }
    append(chunk);
    // The current line is a prompt once; bytes added to it later (an echoed
    // answer, a cursor sequence) never ask again.
    if (m_promptSeen || !endsLikePrompt()) return {};
    m_promptSeen = true;
    const QByteArray line = promptLine(QByteArrayView(m_tail.data(), m_tailSize));
    if (auto other = otherLogin(line, mode)) return *other;
    const Prompt prompt = classify(line, m_scope, m_user, m_host, m_secrets.keyPath);
    return prompt == Prompt::None ? Step{} : decide(prompt, mode);
}

} // namespace bitshell::core
