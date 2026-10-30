#if defined(__APPLE__)
#define __STDC_WANT_LIB_EXT1__ 1 // memset_s
#endif
#include "bitshell/core/SecureMemory.h"

#include <cstring>
#include <string.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bitshell::core {

void secureZero(void* data, std::size_t size) {
    if (!data || !size) return;
#if defined(_WIN32)
    SecureZeroMemory(data, size);
#elif defined(__APPLE__)
    memset_s(data, size, 0, size);
#elif defined(__GLIBC__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    explicit_bzero(data, size);
#else
    auto* bytes = static_cast<volatile unsigned char*>(data);
    while (size--) *bytes++ = 0;
#endif
}

namespace {
bool owned(const QByteArray& bytes) { return bytes.data_ptr().constAllocatedCapacity() > 0; }
bool unshared(const QByteArray& bytes) { return owned(bytes) && !bytes.data_ptr().isShared(); }
}

QByteArrayView allocatedBytes(const QByteArray& bytes) {
    if (!owned(bytes)) return {};
    const auto& d = bytes.data_ptr();
    const char* start = bytes.constData() - d.freeSpaceAtBegin();
    return {start, d.constAllocatedCapacity()};
}

void secureWipe(QByteArray& bytes) {
    // Only the last holder zeroes: zeroing a shared block would corrupt the
    // other holders (a vault cache, a caller's copy). Each of them wipes its
    // own reference, so the bytes are gone when the last one does.
    if (!unshared(bytes)) { bytes = QByteArray(); return; }
    const auto block = allocatedBytes(bytes);
    secureZero(const_cast<char*>(block.data()), std::size_t(block.size()));
    bytes.truncate(0); // keep the (now zero) block
}

void secureAppend(QByteArray& buffer, QByteArrayView data) {
    if (data.isEmpty()) return;
    if (unshared(buffer) && buffer.data_ptr().freeSpaceAtEnd() >= data.size()) {
        buffer.append(data); // fits: no reallocation
        return;
    }
    const qsizetype needed = buffer.size() + data.size();
    QByteArray grown;
    grown.reserve(qMax<qsizetype>(needed * 2, 256));
    grown.append(buffer.constData(), buffer.size());
    grown.append(data);
    if (unshared(buffer)) secureWipe(buffer);
    buffer.swap(grown);
}

void secureConsume(QByteArray& buffer, qsizetype count) {
    count = qBound<qsizetype>(0, count, buffer.size());
    if (!count) return;
    if (unshared(buffer)) secureZero(buffer.data(), std::size_t(count));
    buffer.remove(0, count);
}

} // namespace bitshell::core
