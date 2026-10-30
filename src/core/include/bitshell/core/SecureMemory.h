#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include <cstddef>

namespace bitshell::core {

// Helpers for byte buffers that hold a secret. Qt's implicit sharing and its
// allocator never zero memory: QByteArray::remove(0, n) only moves the start
// pointer, growth copies into a new block and frees the old one, and fill()
// on a shared array detaches and zeroes the copy. These helpers keep a secret
// buffer in one owned block and zero it in place.

// Zeroes memory in a way the compiler cannot drop as a dead store.
void secureZero(void* data, std::size_t size);

// Zeroes every byte of the array's block in place, including the spare
// capacity on both sides, then empties it. A block shared with other copies
// is only released: zeroing it would corrupt them, and the last holder's wipe
// zeroes it. Raw (non-owning) data is left alone.
void secureWipe(QByteArray& bytes);

// Appends without leaving a copy behind: writes in place when the owned block
// has room, otherwise moves everything into a new block and zeroes the old one.
void secureAppend(QByteArray& buffer, QByteArrayView data);

// Removes the first `count` bytes after zeroing them.
void secureConsume(QByteArray& buffer, qsizetype count);

// The whole block behind an array, spare capacity included. For tests that
// check no secret byte remains anywhere in a buffer.
QByteArrayView allocatedBytes(const QByteArray& bytes);

} // namespace bitshell::core
