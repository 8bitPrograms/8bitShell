#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QUrl>
#include <functional>
#include <memory>
class QNetworkReply;
class QSaveFile;
class QTemporaryDir;
class UpdateClient final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool canDownload READ canDownload NOTIFY changed)
    Q_PROPERTY(bool canInstall READ canInstall CONSTANT)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY changed)
    Q_PROPERTY(bool autoUpdate READ autoUpdate WRITE setAutoUpdate NOTIFY changed)
public:
    struct Release {
        QString version, tag, notes, assetName;
        QUrl assetUrl, checksumUrl;
        qint64 size = 0;
        bool newer = false;
    };
    UpdateClient(QString app, QString repository, QString version, QString settingsFile,
        QObject *parent = nullptr, QNetworkAccessManager *transport = nullptr, QString cacheRoot = {});
    ~UpdateClient() override;
    static Release parseRelease(const QByteArray &, const QString &app, const QString &repository,
        const QString &current, const QString &system, const QString &architecture, bool installed);
    static QString checksumFor(const QByteArray &, const QString &assetName);
    static QString architecture();
    QString currentVersion() const { return m_current; }
    QString latestVersion() const { return m_release.version; }
    QString status() const { return m_status; }
    QString releaseNotes() const { return m_release.notes; }
    bool checking() const { return m_checking; }
    bool available() const { return m_available; }
    bool downloading() const { return m_downloading; }
    bool ready() const { return !m_readyPath.isEmpty(); }
    bool canDownload() const { return m_release.assetUrl.isValid() && m_release.checksumUrl.isValid(); }
    bool canInstall() const { return m_canInstall; }
    double progress() const { return m_progress; }
    bool autoCheck() const;
    bool autoUpdate() const;
    void setAutoCheck(bool);
    void setAutoUpdate(bool);
    void start();
    Q_INVOKABLE void check(bool manual = true);
    Q_INVOKABLE void download();
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancelInstall();
    Q_INVOKABLE void skip();
    Q_INVOKABLE void openReleasePage();
    Q_INVOKABLE void showDownload();
signals:
    void changed();
    void updateFound();
    void closeRequested();
private:
    void fetch(const QUrl &, qsizetype, std::function<void(const QByteArray &)>, bool noReleaseOk = false);
    QNetworkRequest request(const QUrl &) const;
    void fail(const QString &);
    void downloadPayload();
    void applyOnExit();
    bool ownsInstalledDirectory() const;
    QString m_app, m_repository, m_current, m_status, m_expectedHash, m_readyPath, m_cacheRoot;
    Release m_release;
    QSettings m_settings;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QTemporaryDir> m_staging;
    std::unique_ptr<QSaveFile> m_file;
    bool m_checking = false, m_available = false, m_downloading = false;
    bool m_canInstall = false, m_manualInstall = false;
    double m_progress = 0;
};
