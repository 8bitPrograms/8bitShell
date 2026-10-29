#include "UpdateClient.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <cstring>
#include <stdexcept>

namespace {
QByteArray release(QString tag = "v1.0.10", QString repo = "8bitPrograms/8bitPad",
    QString system = "windows", QString cpu = "x64", QString extension = ".zip") {
    const QString version = tag.startsWith('v') ? tag.mid(1) : tag;
    const QString name = "8bitPad-" + version + '-' + system + '-' + cpu + extension;
    const QString base = "https://github.com/" + repo + "/releases/download/" + tag + '/';
    return QJsonDocument(QJsonObject{{"tag_name",tag},{"draft",false},{"prerelease",false},
        {"body","A tested release."},{"assets",QJsonArray{
            QJsonObject{{"name",name},{"size",7},{"browser_download_url",base+name}},
            QJsonObject{{"name","SHA256SUMS.txt"},{"browser_download_url",base+"SHA256SUMS.txt"}}}}}).toJson();
}
class Reply final : public QNetworkReply {
public:
    Reply(QNetworkRequest req, QByteArray bytes, int status, QObject *parent) : QNetworkReply(parent), data(std::move(bytes)) {
        setRequest(req); setUrl(req.url()); setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute,status); open(QIODevice::ReadOnly);
        if (status != 200) setError(ContentNotFoundError,"Not found");
        QTimer::singleShot(0,this,[this] { emit readyRead(); setFinished(true); emit finished(); });
    }
    void abort() override { setError(OperationCanceledError,"Cancelled"); }
    qint64 bytesAvailable() const override { return data.size()-position+QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *target,qint64 maximum) override {
        const qint64 count=qMin(maximum,qint64(data.size()-position));
        if(count<=0)return -1;
        std::memcpy(target,data.constData()+position,size_t(count));position+=count;return count;
    }
private:
    QByteArray data; qint64 position=0;
};
class Transport final : public QNetworkAccessManager {
public:
    QHash<QString,QByteArray> responses;
    int requests=0;
protected:
    QNetworkReply *createRequest(Operation,const QNetworkRequest &request,QIODevice *) override {
        ++requests; const QString url=request.url().toString();
        return new Reply(request,responses.value(url),responses.contains(url)?200:404,this);
    }
};
}
class UpdateClientTest : public QObject {
    Q_OBJECT
private slots:
    void comparesNumericVersionsAndSelectsArchitecture() {
        const auto info=UpdateClient::parseRelease(release(),"8bitPad","8bitPrograms/8bitPad","1.0.2","windows","x64",false);
        QVERIFY(info.newer); QCOMPARE(info.version,QString("1.0.10")); QVERIFY(info.assetUrl.isValid());
        const auto wrong=UpdateClient::parseRelease(release(),"8bitPad","8bitPrograms/8bitPad","1.0.2","windows","arm64",false);
        QVERIFY(!wrong.assetUrl.isValid());
        const auto same=UpdateClient::parseRelease(release(),"8bitPad","8bitPrograms/8bitPad","1.0.10","windows","x64",false);
        QVERIFY(!same.newer);
        const auto installed=UpdateClient::parseRelease(release("v1.0.10","8bitPrograms/8bitPad","windows","arm64",".msi"),
            "8bitPad","8bitPrograms/8bitPad","1.0.0","windows","arm64",true);
        QVERIFY(installed.assetName.endsWith("arm64.msi")); QVERIFY(installed.assetUrl.isValid());
    }
    void rejectsUnstableAndForeignAssets() {
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,UpdateClient::parseRelease(release("v2.0.0-beta"),"8bitPad","8bitPrograms/8bitPad","1.0.0","windows","x64",false));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,UpdateClient::parseRelease(release("v1.0.10","attacker/8bitPad"),"8bitPad","8bitPrograms/8bitPad","1.0.0","windows","x64",false));
        auto json=QJsonDocument::fromJson(release()).object();json["draft"]=true;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,UpdateClient::parseRelease(QJsonDocument(json).toJson(),"8bitPad","8bitPrograms/8bitPad","1.0.0","windows","x64",false));
        auto assets=QJsonDocument::fromJson(release()).object()["assets"].toArray();assets.append(assets.first());json["draft"]=false;json["assets"]=assets;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,UpdateClient::parseRelease(QJsonDocument(json).toJson(),"8bitPad","8bitPrograms/8bitPad","1.0.0","windows","x64",false));
    }
    void checksumsMustBeExactAndUnambiguous() {
        const QByteArray hash(64,'a');const QByteArray row=hash+"  app.zip\n";
        QCOMPARE(UpdateClient::checksumFor(row,"app.zip"),QString::fromLatin1(hash));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,UpdateClient::checksumFor(row,"other.zip"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,UpdateClient::checksumFor(row+row,"app.zip"));
    }
    void emptyRepositoryAndOptionalSettingsDoNotBlock() {
        QTemporaryDir fixture;Transport transport;
        UpdateClient client("8bitPad","8bitPrograms/8bitPad","1.0.0",fixture.filePath("settings.ini"),nullptr,&transport,fixture.filePath("cache"));
        QVERIFY(client.autoCheck()); QVERIFY(!client.autoUpdate()); QCOMPARE(transport.requests,0);
        client.setAutoCheck(false);client.check();
        QTRY_VERIFY(!client.checking()); QVERIFY(!client.available()); QVERIFY(client.status().contains("No release"));
        QVERIFY(!client.ready());
    }
    void downloadIsVerifiedAndSkippedVersionsStayOptional_data() {
        QTest::addColumn<bool>("tampered");QTest::newRow("valid")<<false;QTest::newRow("tampered")<<true;
    }
    void downloadIsVerifiedAndSkippedVersionsStayOptional() {
        QFETCH(bool,tampered);QTemporaryDir fixture;Transport transport;
        const QString cpu=UpdateClient::architecture();
#ifdef Q_OS_WIN
        const QString system="windows", extension=".zip";
#elif defined(Q_OS_MACOS)
        const QString system="macos", extension=".dmg";
#else
        const QString system="linux", extension=".tar.gz";
#endif
        const QString name="8bitPad-1.0.10-"+system+'-'+cpu+extension;
        const QString base="https://github.com/8bitPrograms/8bitPad/releases/download/v1.0.10/";
        transport.responses["https://api.github.com/repos/8bitPrograms/8bitPad/releases/latest"]=release("v1.0.10","8bitPrograms/8bitPad",system,cpu,extension);
        const QByteArray bytes="payload";
        transport.responses[base+"SHA256SUMS.txt"]=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()+"  "+name.toUtf8()+"\n";
        transport.responses[base+name]=tampered?QByteArray("corrupt"):bytes;
        UpdateClient client("8bitPad","8bitPrograms/8bitPad","1.0.0",fixture.filePath("settings.ini"),nullptr,&transport,fixture.filePath("cache"));
        QSignalSpy closes(&client,&UpdateClient::closeRequested);
        client.check();QTRY_VERIFY(!client.checking());QVERIFY(client.available());QVERIFY(!client.autoUpdate());
        client.download();QTRY_VERIFY(!client.downloading());
        QCOMPARE(client.ready(),!tampered);QCOMPARE(closes.size(),0);
        if(tampered)QVERIFY(client.status().contains("verification"));
        client.skip();QVERIFY(!client.available());QVERIFY(!client.ready());
        client.check(false);QTRY_VERIFY(!client.checking());QVERIFY(!client.available());
        client.check(true);QTRY_VERIFY(!client.checking());QVERIFY(client.available());
        // Skip can be clicked while refreshing a previously available release.
        client.check();QVERIFY(client.checking());client.skip();QVERIFY(!client.checking());
        client.check();QTRY_VERIFY(!client.checking());QVERIFY(client.available());
    }
};
QTEST_GUILESS_MAIN(UpdateClientTest)
#include "UpdateClientTest.moc"
