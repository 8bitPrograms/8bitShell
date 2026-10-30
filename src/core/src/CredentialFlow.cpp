#include <bitshell/core/CredentialFlow.h>
#include <bitshell/core/SecureMemory.h>

#include <algorithm>
#include <cstring>

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
}

CredentialFlow::CredentialFlow(CredentialSecrets secrets, Scope scope)
    : m_secrets(std::move(secrets)), m_scope(scope) {}

CredentialFlow::~CredentialFlow() {
    m_secrets.wipe();
    secureZero(m_tail.data(), m_tail.size());
}

void CredentialFlow::restart() {
    m_tailSize = 0;
    m_quietBytes = 0;
    m_spent = 0;
    m_engaged = false;
    m_stopped = false;
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

CredentialFlow::Step CredentialFlow::stop(bool rejected) {
    m_stopped = true;
    Step step;
    step.action = Action::Stop;
    step.rejected = rejected;
    return step;
}

// After an answer, a line that is not ssh's means the login is over.
bool CredentialFlow::leftLogin() const {
    const QByteArrayView tail(m_tail.data(), m_tailSize);
    const qsizetype last = tail.lastIndexOf('\n');
    for (qsizetype begin = 0; begin <= last;) {
        const qsizetype end = tail.indexOf('\n', begin);
        QByteArrayView raw = tail.sliced(begin, end - begin);
        while (!raw.isEmpty() && raw.back() == '\r') raw.chop(1);
        const QByteArray line = lower(promptLine(raw));
        if (!line.isEmpty() && !line.startsWith("permission denied") && !fromSsh(line, m_user, m_host))
            return true;
        begin = end + 1;
    }
    return false;
}

CredentialFlow::Step CredentialFlow::decide(Prompt prompt) {
    // Someone else's prompt after an answer: ssh is past authentication.
    if (prompt == Prompt::Other && m_spent) return stop();
    if ((prompt == Prompt::Unknown || prompt == Prompt::Other) && m_scope == Scope::Shell && !m_engaged) return {};
    Step step;
    step.prompt = prompt;
    m_tailSize = 0;
    m_quietBytes = 0;
    if (prompt == Prompt::OneTimeCode || prompt == Prompt::Unknown || prompt == Prompt::Other) {
        m_engaged = m_engaged || prompt == Prompt::OneTimeCode;
        step.action = Action::HandToUser;
        return step;
    }
    m_engaged = true;
    const QByteArray* answer = &m_secrets.password;
    std::uint8_t bit = SpentPassword;
    if (prompt == Prompt::Username) { answer = &m_secrets.username; bit = SpentUsername; }
    if (prompt == Prompt::Passphrase) { answer = &m_secrets.passphrase; bit = SpentPassphrase; }
    if (m_spent & bit) {
        step = stop(true);
        step.prompt = prompt;
        return step;
    }
    if (answer->isEmpty()) {
        step.action = Action::HandToUser;
        return step;
    }
    m_spent |= bit;
    step.action = Action::Send;
    step.bytes.reserve(answer->size() + 1);
    step.bytes.append(*answer).append('\r');
    return step;
}

CredentialFlow::Step CredentialFlow::feed(QByteArrayView chunk) {
    if (m_stopped || chunk.isEmpty()) return {};
    append(chunk);
    if (endsLikePrompt()) {
        const QByteArray line = promptLine(QByteArrayView(m_tail.data(), m_tailSize));
        const Prompt prompt = classify(line, m_scope, m_user, m_host, m_secrets.keyPath);
        if (prompt != Prompt::None) {
            Step step = decide(prompt);
            if (step.action != Action::None) return step;
        }
    } else if (m_spent && leftLogin()) {
        return stop();
    }
    m_quietBytes += chunk.size();
    // A Shell flow without a target waits for one at no cost.
    const qint64 limit = m_spent ? settledBytes
        : m_scope == Scope::OwnedSsh ? preambleBytes
        : m_host.isEmpty() ? -1 : shellWindowBytes;
    if (limit >= 0 && m_quietBytes > limit) return stop();
    return {};
}

} // namespace bitshell::core
