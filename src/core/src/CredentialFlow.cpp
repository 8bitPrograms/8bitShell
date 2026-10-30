#include <bitshell/core/CredentialFlow.h>

#include <algorithm>
#include <cstring>

namespace bitshell::core {
namespace {
constexpr char escape = '\x1b';

void zero(QByteArray& value) {
    if (!value.isEmpty()) {
        value.detach();
        std::fill(value.begin(), value.end(), '\0');
    }
    value.clear();
}

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
        "two factor", "passcode", "authenticator", "security code", "auth code", "sms code"};
    static constexpr const char* words[] = {"otp", "totp", "2fa", "mfa", "token", "duo", "pin", "code"};
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
} // namespace

void CredentialSecrets::wipe() {
    zero(username);
    zero(password);
    zero(passphrase);
    keyPath.clear();
}

CredentialFlow::CredentialFlow(CredentialSecrets secrets, Scope scope)
    : m_secrets(std::move(secrets)), m_scope(scope) {}

CredentialFlow::~CredentialFlow() {
    m_secrets.wipe();
    std::fill(m_tail.begin(), m_tail.end(), '\0');
}

void CredentialFlow::restart() {
    m_tailSize = 0;
    m_quietBytes = 0;
    m_spent = 0;
    m_engaged = false;
    m_stopped = false;
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

CredentialFlow::Prompt CredentialFlow::classify(QByteArrayView line, Scope scope,
                                                QByteArrayView user, QByteArrayView keyPath) {
    if (line.isEmpty() || (line.back() != ':' && line.back() != '?')) return Prompt::None;
    const QByteArray text = lower(line);
    if (text.startsWith("enter passphrase for")) {
        if (!keyPath.isEmpty() && !line.contains(keyPath)) return Prompt::Unknown;
        return scope == Scope::OwnedSsh ? Prompt::Passphrase : Prompt::Unknown;
    }
    // OpenSSH's own forms. Checked first so a host name never looks like a code.
    const bool sshPassword = text.endsWith("'s password:")
                             || (text.startsWith('(') && text.endsWith(") password:"));
    if (sshPassword) {
        if (scope == Scope::OwnedSsh) return Prompt::Password;
        // Ad hoc: the prompt must name the bound user, so sudo/su never match.
        if (user.isEmpty()) return Prompt::None;
        const QByteArray owner = lower(user) + '@';
        const qsizetype at = text.indexOf(owner);
        return at == 0 || (at == 1 && text.at(0) == '(') ? Prompt::Password : Prompt::None;
    }
    if (oneTimeCode(text)) return Prompt::OneTimeCode;
    if (text.contains("password") && line.back() == ':')
        return scope == Scope::OwnedSsh ? Prompt::Password : Prompt::None;
    if (line.back() == ':') {
        QByteArrayView body = QByteArrayView(text).chopped(1).trimmed();
        if (usernamePrompt(body)) return Prompt::Username;
    }
    return Prompt::Unknown;
}

CredentialFlow::Step CredentialFlow::decide(Prompt prompt) {
    Step step;
    step.prompt = prompt;
    if (prompt == Prompt::Unknown && m_scope == Scope::Shell && !m_engaged) return {};
    m_tailSize = 0;
    m_quietBytes = 0;
    if (prompt == Prompt::OneTimeCode || prompt == Prompt::Unknown) {
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
        m_stopped = true;
        step.action = Action::Stop;
        step.rejected = true;
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
        const Prompt prompt = classify(line, m_scope, m_secrets.username, m_secrets.keyPath);
        if (prompt != Prompt::None) {
            Step step = decide(prompt);
            if (step.action != Action::None) return step;
        }
    }
    m_quietBytes += chunk.size();
    const qint64 limit = m_spent ? settledBytes : (m_scope == Scope::OwnedSsh ? preambleBytes : -1);
    if (limit >= 0 && m_quietBytes > limit) {
        m_stopped = true;
        Step step;
        step.action = Action::Stop;
        return step;
    }
    return {};
}

} // namespace bitshell::core
