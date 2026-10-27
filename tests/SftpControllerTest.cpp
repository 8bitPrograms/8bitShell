#include <QtTest>

#include <QFile>
#include <QTemporaryDir>
#include <QUrl>

#include <bitshell/core/SftpController.h>

using bitshell::core::SftpController;

class SftpControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void listsNavigatesAndUploadsWithOpenSshBatchProtocol();
    void explainsAuthenticationFailure();

    static QString makeExecutable(QTemporaryDir& directory,
                                  const QString& name,
                                  const QByteArray& contents);
};

QString SftpControllerTest::makeExecutable(QTemporaryDir& directory,
                                           const QString& name,
                                           const QByteArray& contents) {
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
        return {};
    }
    file.close();
    const auto permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner
                             | QFileDevice::ExeOwner;
    if (!QFile::setPermissions(path, permissions)) {
        return {};
    }
    return path;
}

void SftpControllerTest::listsNavigatesAndUploadsWithOpenSshBatchProtocol() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString executable = makeExecutable(
        directory,
        QStringLiteral("fake-sftp"),
        QByteArrayLiteral(
            "#!/bin/sh\n"
            "IFS= read -r first || exit 4\n"
            "printf '%s\\n' 'Remote working directory: /srv/data'\n"
            "printf '%s\\n' 'drwxr-xr-x 2 user group 4096 Sep 11 11:38 archive'\n"
            "printf '%s\\n' '-rw-r--r-- 1 user group 18874368 Sep 11 11:41 alert.log'\n"));
    QVERIFY(!executable.isEmpty());
    SftpController controller;
    controller.setTransportExecutableForTesting(executable);
    const QVariantMap profile{
        {QStringLiteral("host"), QStringLiteral("files.example.test")},
        {QStringLiteral("user"), QStringLiteral("operator")},
        {QStringLiteral("port"), 2222},
    };
    controller.connectToProfile(profile);

    QTRY_VERIFY_WITH_TIMEOUT(controller.state() == SftpController::State::Ready, 3000);
    QVERIFY(controller.connected());
    QCOMPARE(controller.target(), QStringLiteral("operator@files.example.test"));
    QCOMPARE(controller.currentPath(), QStringLiteral("/srv/data"));
    QCOMPARE(controller.count(), 2);
    QCOMPARE(controller.data(controller.index(0), SftpController::NameRole).toString(),
             QStringLiteral("archive"));
    QVERIFY(controller.data(controller.index(0), SftpController::DirectoryRole).toBool());
    QCOMPARE(controller.data(controller.index(1), SftpController::SizeTextRole).toString(),
             QStringLiteral("18 MB"));

    controller.openEntry(0);
    QTRY_VERIFY_WITH_TIMEOUT(controller.state() == SftpController::State::Ready, 3000);

    const QString uploadPath = directory.filePath(QStringLiteral("upload.txt"));
    QFile upload(uploadPath);
    QVERIFY(upload.open(QIODevice::WriteOnly));
    QVERIFY(upload.write("hello\n") > 0);
    upload.close();
    controller.upload(QUrl::fromLocalFile(uploadPath));
    QTRY_VERIFY_WITH_TIMEOUT(controller.lastTransferText().contains(
                                 QStringLiteral("Uploaded upload.txt")),
                             3000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.state() == SftpController::State::Ready, 3000);
}

void SftpControllerTest::explainsAuthenticationFailure() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString executable = makeExecutable(
        directory,
        QStringLiteral("failing-sftp"),
        QByteArrayLiteral(
            "#!/bin/sh\n"
            "IFS= read -r first || exit 4\n"
            "printf '%s\\n' 'operator@host: Permission denied (publickey,password).' >&2\n"
            "exit 255\n"));
    QVERIFY(!executable.isEmpty());
    SftpController controller;
    controller.setTransportExecutableForTesting(executable);
    controller.connectToProfile(QVariantMap{
        {QStringLiteral("host"), QStringLiteral("files.example.test")},
    });

    QTRY_VERIFY_WITH_TIMEOUT(controller.state() == SftpController::State::Error, 3000);
    QVERIFY(!controller.connected());
    QVERIFY(controller.statusText().contains(QStringLiteral("key or agent")));
}

QTEST_GUILESS_MAIN(SftpControllerTest)

#include "SftpControllerTest.moc"
