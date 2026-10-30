// The zeroing helpers credential and PTY code use for secrets.
#include <QtTest>
#include <bitshell/core/SecureMemory.h>

using namespace bitshell::core;

class SecureMemoryTest : public QObject {
    Q_OBJECT
private slots:
    void wipeZeroesTheWholeBlock() {
        QByteArray secret("TOPSECRET-abc");
        secret.detach();
        secret.remove(0, 3); // spare capacity at the front still holds "TOP"
        secureWipe(secret);
        QVERIFY(secret.isEmpty());
        const QByteArray block = allocatedBytes(secret).toByteArray();
        QVERIFY(!block.isEmpty());
        QCOMPARE(block.count('\0'), block.size());
    }
    // Wiping one holder never corrupts another copy of the same block.
    void wipeLeavesSharedCopiesIntact() {
        QByteArray stored("vault-copy");
        stored.detach();
        QByteArray copy = stored;
        secureWipe(copy);
        QVERIFY(copy.isEmpty());
        QCOMPARE(stored, QByteArray("vault-copy"));
    }
    void appendAndConsumeLeaveNoOldBytes() {
        QByteArray buffer;
        secureAppend(buffer, "abc");
        secureAppend(buffer, QByteArray(1000, 'x')); // forces a new block
        QCOMPARE(buffer.size(), 1003);
        QVERIFY(buffer.startsWith("abc"));
        secureConsume(buffer, 3);
        QVERIFY(!allocatedBytes(buffer).toByteArray().contains("abc"));
        QCOMPARE(buffer, QByteArray(1000, 'x'));
    }
};

QTEST_GUILESS_MAIN(SecureMemoryTest)
#include "SecureMemoryTest.moc"
