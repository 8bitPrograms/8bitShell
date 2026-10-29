#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQmlIncubationController>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <bitshell/core/AppSettings.h>
#include <bitshell/core/LocalProfileStore.h>
#include <bitshell/core/PasswordStore.h>
#include <bitshell/core/SessionStore.h>
#include <bitshell/terminal/TerminalView.h>
#include "TerminalTestAccess.h"

#include <memory>

using bitshell::core::AppSettings;
using bitshell::core::LocalProfileStore;
using bitshell::core::PasswordStore;
using bitshell::core::SessionStore;
using bitshell::terminal::TerminalTestAccess;
using bitshell::terminal::TerminalView;

namespace {
QVariant nativeVariant(const QVariant& value) {
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}
const QByteArray testPassword = QByteArrayLiteral("Synthetic-ui-fixture-47!NoRealAccount");
QVariantMap target() {
    return {{QStringLiteral("host"), QStringLiteral("192.0.2.87")},
            {QStringLiteral("user"), QStringLiteral("fixture-user")}, {QStringLiteral("port"), 22},
            {QStringLiteral("connection"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("direct")}}}};
}
QByteArray prompt(const TerminalView& terminal) {
    const auto token = TerminalTestAccess::integrationToken(terminal).toLatin1();
    return QByteArray("\r\nPS> \x1b]777;8bitshell;") + token + ";powershell\a"
        + "\x1b]133;A;" + token + '\a' + "\x1b]133;B;" + token + '\a';
}
}

class PasswordUiTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void passwordModeMasksInputAndSupportsKeyboardNavigation();
    void rememberedPasswordIsWriteOnlyAndBlankEditKeepsIt();
    void forgetRequiresSaveAndCancelClearsTheEditor();
    void exportedAndImportedProfilesNeverCarryThePassword();
    void localSuggestionsDescribePasswordAuthenticationWithoutConnecting();
private:
    QVariantList fixtureCommand() const;
    QObject* named(const QString& name) const { return m_window ? m_window->findChild<QObject*>(name) : nullptr; }
    QObject* editor() const { return named(QStringLiteral("sessionEditorDialog")); }
    QQuickItem* item(const QString& name) const { return qobject_cast<QQuickItem*>(named(name)); }
    TerminalView* terminal() const {
        return m_pane ? qobject_cast<TerminalView*>(m_pane->property("activeTerminal").value<QObject*>()) : nullptr;
    }
    bool openCreate();
    bool openEdit(int index);
    bool save();
    bool createRemembered();
    void captureEmptyEditor() const;

    QTemporaryDir m_profile;
    QString m_profileDirectory;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow* m_window = nullptr;
    QObject* m_pane = nullptr;
    SessionStore* m_sessions = nullptr;
    LocalProfileStore* m_localProfiles = nullptr;
    QStringList m_warnings;
};

QVariantList PasswordUiTest::fixtureCommand() const {
#ifdef Q_OS_WIN
    return {QCoreApplication::applicationDirPath() + QStringLiteral("/windows_terminal_fixture.exe"), QStringLiteral("broadcast")};
#else
    return {QStringLiteral("/bin/sh"), QStringLiteral("-c"),
        QStringLiteral("stty -echo -isig; printf 'READY\\n'; while IFS= read -r line; do :; done")};
#endif
}

void PasswordUiTest::initTestCase() {
    QVERIFY(m_profile.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("password-ui-test"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
}

void PasswordUiTest::init() {
    m_profileDirectory = m_profile.filePath(QString::fromLatin1(QTest::currentTestFunction()));
    QVERIFY(QDir().mkpath(m_profileDirectory));
    qputenv("BIT8SHELL_PROFILE", m_profileDirectory.toUtf8());
    qputenv("XDG_CONFIG_HOME", m_profileDirectory.toUtf8());
    qputenv("XDG_CACHE_HOME", QDir(m_profileDirectory).filePath(QStringLiteral("cache")).toUtf8());
    m_warnings.clear();
    m_engine = std::make_unique<QQmlApplicationEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) m_warnings.append(warning.toString());
    });
    // Main runs an isolated idle fixture, never a real SSH client or distro.
    m_engine->setInitialProperties({{QStringLiteral("startupCommand"), fixtureCommand()}});
    m_engine->loadFromModule(QStringLiteral("BitShell"), QStringLiteral("Main"));
    QVERIFY2(!m_engine->rootObjects().isEmpty(), qPrintable(m_warnings.join(u'\n')));
    m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
    QVERIFY(m_window);
    m_window->setWidth(1080); m_window->setHeight(840);
    m_window->show(); m_window->requestActivate();
    m_pane = named(QStringLiteral("terminalPane"));
    QVERIFY(m_pane);
    m_sessions = m_engine->singletonInstance<SessionStore*>("BitShell", "SessionStore");
    m_localProfiles = m_engine->singletonInstance<LocalProfileStore*>("BitShell", "LocalProfileStore");
    auto* settings = m_engine->singletonInstance<AppSettings*>("BitShell", "AppSettings");
    QVERIFY(m_sessions && m_localProfiles && settings);
    settings->setLogByDefault(false);
    QTRY_COMPARE(m_pane->property("tabCount").toInt(), 1);
    QTRY_VERIFY(terminal() && terminal()->isRunning());
    terminal()->setLogEnabled(false);
    terminal()->forceActiveFocus();
}

void PasswordUiTest::cleanup() {
    QCoreApplication::processEvents();
    if (m_engine && m_engine->incubationController())
        QTRY_COMPARE(m_engine->incubationController()->incubatingObjectCount(), 0);
    // Let the read-only WSL enumeration finish before destroying its owner.
    if (m_localProfiles) QTRY_VERIFY_WITH_TIMEOUT(!m_localProfiles->discovering(), 10000);
    if (m_window) m_window->close();
    m_engine.reset();
    m_window = nullptr; m_pane = nullptr; m_sessions = nullptr; m_localProfiles = nullptr;
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(u'\n')));
}

bool PasswordUiTest::openCreate() {
    if (!QMetaObject::invokeMethod(m_window, "ensureOverlay", Q_ARG(QVariant, QStringLiteral("sessionEditorDialog")))) return false;
    if (!editor() || !QMetaObject::invokeMethod(editor(), "openCreateTarget", Q_ARG(QVariant, target()))) return false;
    if (!QTest::qWaitFor([this] { return editor()->property("opened").toBool(); }, 5000)) return false;
    auto* connection = named(QStringLiteral("sessionConnectionChoice"));
    const QVariantMap direct{{QStringLiteral("mode"), QStringLiteral("direct")}};
    if (!connection || !QMetaObject::invokeMethod(connection, "load", Q_ARG(QVariant, direct))) return false;
    auto* combo = named(QStringLiteral("sessionAuthenticationCombo"));
    if (!combo) return false;
    combo->setProperty("currentIndex", 1);
    if (!QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 1))) return false;
    QCoreApplication::processEvents();
    return editor()->property("selectedAuthenticationMode").toString() == QStringLiteral("password");
}

bool PasswordUiTest::openEdit(int index) {
    if (!editor() || !QMetaObject::invokeMethod(editor(), "openEdit", Q_ARG(QVariant, index))) return false;
    if (!QTest::qWaitFor([this] { return editor()->property("opened").toBool(); }, 5000)) return false;
    QCoreApplication::processEvents();
    return true;
}

bool PasswordUiTest::save() {
    if (!QMetaObject::invokeMethod(editor(), "saveSession")) return false;
    return QTest::qWaitFor([this] { return !editor()->property("visible").toBool(); }, 5000);
}

bool PasswordUiTest::createRemembered() {
    if (!openCreate()) return false;
    auto* remember = named(QStringLiteral("sessionRememberPasswordCheck"));
    auto* password = named(QStringLiteral("sessionPasswordField"));
    if (!remember || !password || !remember->property("enabled").toBool()) return false;
    remember->setProperty("checked", true);
    password->setProperty("text", QString::fromUtf8(testPassword));
    return save() && m_sessions->count() == 1;
}

void PasswordUiTest::captureEmptyEditor() const {
    QVERIFY(named(QStringLiteral("sessionPasswordField")));
    QVERIFY(named(QStringLiteral("sessionPasswordField"))->property("text").toString().isEmpty());
    const auto directory = qEnvironmentVariable("BIT8SHELL_UI_TEST_CAPTURE_DIR");
    if (directory.isEmpty()) return;
    QVERIFY(QDir().mkpath(directory));
    QTest::qWait(80);
    const auto frame = m_window->grabWindow();
    QVERIFY(!frame.isNull());
    QVERIFY(frame.save(QDir(directory).filePath(QStringLiteral("password-session-editor-empty.png"))));
}

void PasswordUiTest::passwordModeMasksInputAndSupportsKeyboardNavigation() {
    QVERIFY(openCreate());
    auto* combo = item(QStringLiteral("sessionAuthenticationCombo"));
    auto* field = item(QStringLiteral("sessionPasswordField"));
    auto* remember = item(QStringLiteral("sessionRememberPasswordCheck"));
    auto* keys = item(QStringLiteral("sessionKeyControls"));
    QVERIFY(combo && field && remember && keys);
    QVERIFY(!keys->isVisible());
    QCOMPARE(field->property("echoMode").toInt(), 2); // TextInput.Password, never PasswordEchoOnEdit.
    const auto hints = field->property("inputMethodHints").toInt();
    QVERIFY(hints & Qt::ImhHiddenText);
    QVERIFY(hints & Qt::ImhSensitiveData);
    QVERIFY(hints & Qt::ImhNoPredictiveText);
    QVERIFY(field->property("text").toString().isEmpty());
    if (!PasswordStore::available()) {
        QVERIFY(!remember->isEnabled());
        captureEmptyEditor();
        return;
    }
    combo->forceActiveFocus();
    QTest::keyClick(m_window, Qt::Key_Tab);
    QTRY_VERIFY(remember->hasActiveFocus());
    QVERIFY(!remember->property("checked").toBool());
    QTest::keyClick(m_window, Qt::Key_Space);
    QTRY_VERIFY(remember->property("checked").toBool());
    QTRY_VERIFY(field->isEnabled());
    QTest::keyClick(m_window, Qt::Key_Tab);
    QTRY_VERIFY(field->hasActiveFocus());
    captureEmptyEditor();
    for (const char key : QByteArrayLiteral("fixture-mask-only")) QTest::keyClick(m_window, key);
    QVERIFY(field->property("text").toString() == QStringLiteral("fixture-mask-only"));
    QVERIFY(!field->property("displayText").toString().contains(QStringLiteral("fixture-mask-only")));
    QTest::keyClick(m_window, Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(remember->hasActiveFocus());
    combo->setProperty("currentIndex", 0);
    QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 0)));
    QTRY_VERIFY(keys->isVisible());
    QVERIFY(field->property("text").toString().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(editor(), "reject"));
    QTRY_VERIFY(field->property("text").toString().isEmpty());
    QCOMPARE(m_sessions->count(), 0);
}

void PasswordUiTest::rememberedPasswordIsWriteOnlyAndBlankEditKeepsIt() {
    if (!PasswordStore::available()) QSKIP("OS credential storage is unavailable in this test environment.");
    QVERIFY2(createRemembered(), qPrintable(editor() ? editor()->property("formError").toString() : QStringLiteral("No session editor")));
    const auto profile = m_sessions->get(0);
    QCOMPARE(profile.value(QStringLiteral("authenticationMode")).toString(), QStringLiteral("password"));
    QVERIFY(!QJsonDocument::fromVariant(profile).toJson().contains(testPassword));
    QVERIFY(m_sessions->passwordStatus(profile).value(QStringLiteral("stored")).toBool());
    auto stored = PasswordStore::read(profile);
    QVERIFY(stored.has_value());
    QVERIFY(*stored == testPassword); // Never print a recovered credential in a failure message.
    stored->fill('\0');
    QVERIFY(named(QStringLiteral("sessionPasswordField"))->property("text").toString().isEmpty());

    QVERIFY(openEdit(0));
    QVERIFY(editor()->property("hasStoredPassword").toBool());
    QVERIFY(named(QStringLiteral("sessionPasswordField"))->property("text").toString().isEmpty());
    QVERIFY(named(QStringLiteral("sessionRememberPasswordCheck"))->property("checked").toBool());
    QVERIFY(named(QStringLiteral("sessionNameField")));
    named(QStringLiteral("sessionNameField"))->setProperty("text", QStringLiteral("Renamed password profile"));
    QVERIFY(save());
    QCOMPARE(m_sessions->get(0).value(QStringLiteral("name")).toString(), QStringLiteral("Renamed password profile"));
    stored = PasswordStore::read(m_sessions->get(0));
    QVERIFY(stored.has_value());
    QVERIFY(*stored == testPassword);
    stored->fill('\0');
    QVERIFY(m_sessions->reload());
    QVERIFY(m_sessions->passwordStatus(m_sessions->get(0)).value(QStringLiteral("stored")).toBool());
    QCOMPARE(m_pane->property("tabCount").toInt(), 1);
}

void PasswordUiTest::forgetRequiresSaveAndCancelClearsTheEditor() {
    if (!PasswordStore::available()) QSKIP("OS credential storage is unavailable in this test environment.");
    QVERIFY(createRemembered());
    const auto profile = m_sessions->get(0);
    QVERIFY(openEdit(0));
    auto* forget = named(QStringLiteral("sessionForgetPasswordButton"));
    QVERIFY(forget && forget->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(forget, "clicked"));
    QVERIFY(editor()->property("removeStoredPassword").toBool());
    QVERIFY(PasswordStore::has(profile));
    QVERIFY(QMetaObject::invokeMethod(editor(), "reject"));
    QTRY_VERIFY(!editor()->property("visible").toBool());
    QVERIFY(!editor()->property("removeStoredPassword").toBool());
    QVERIFY(PasswordStore::has(profile));

    QVERIFY(openEdit(0));
    named(QStringLiteral("sessionPasswordField"))->setProperty("text", QStringLiteral("Cancelled-synthetic-replacement"));
    QVERIFY(QMetaObject::invokeMethod(editor(), "reject"));
    QTRY_VERIFY(named(QStringLiteral("sessionPasswordField"))->property("text").toString().isEmpty());
    auto stored = PasswordStore::read(profile);
    QVERIFY(stored.has_value());
    QVERIFY(*stored == testPassword);
    stored->fill('\0');

    QVERIFY(openEdit(0));
    QVERIFY(QMetaObject::invokeMethod(named(QStringLiteral("sessionForgetPasswordButton")), "clicked"));
    QVERIFY(save());
    QVERIFY(!PasswordStore::has(profile));
    stored = PasswordStore::read(profile);
    QVERIFY(stored.has_value());
    QVERIFY(stored->isEmpty());
    QVERIFY(!m_sessions->passwordStatus(m_sessions->get(0)).value(QStringLiteral("stored")).toBool());
    QCOMPARE(m_sessions->get(0).value(QStringLiteral("authenticationMode")).toString(), QStringLiteral("password"));
}

void PasswordUiTest::exportedAndImportedProfilesNeverCarryThePassword() {
    if (!PasswordStore::available()) QSKIP("OS credential storage is unavailable in this test environment.");
    QVERIFY(createRemembered());
    const auto oldProfile = m_sessions->get(0);
    const auto path = QDir(m_profileDirectory).filePath(QStringLiteral("password-profile-export.yaml"));
    QVERIFY(m_sessions->exportYaml(path));
    QFile exported(path);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    const auto document = exported.readAll();
    exported.close();
    QVERIFY(!document.contains(testPassword));
    QVERIFY(!document.contains("password:"));
    QVERIFY(!document.contains("credentialRef"));
    QVERIFY(document.contains("authenticationMode: password"));
    QFile catalog(m_sessions->storagePath());
    QVERIFY(catalog.open(QIODevice::ReadOnly));
    QVERIFY(!catalog.readAll().contains(testPassword));
    catalog.close();

    QVERIFY(m_sessions->removeSession(0));
    QVERIFY(!PasswordStore::has(oldProfile));
    QCOMPARE(m_sessions->importYaml(path), 1);
    QCOMPARE(m_sessions->count(), 1);
    const auto imported = m_sessions->get(0);
    QCOMPARE(imported.value(QStringLiteral("authenticationMode")).toString(), QStringLiteral("password"));
    QVERIFY(!m_sessions->passwordStatus(imported).value(QStringLiteral("stored")).toBool());
    const auto noSecret = PasswordStore::read(imported);
    QVERIFY(noSecret.has_value());
    QVERIFY(noSecret->isEmpty());
    QVERIFY(!QJsonDocument::fromVariant(imported).toJson().contains(testPassword));
    QCOMPARE(m_pane->property("tabCount").toInt(), 1);
}

void PasswordUiTest::localSuggestionsDescribePasswordAuthenticationWithoutConnecting() {
    auto profile = target();
    profile[QStringLiteral("id")] = QStringLiteral("password-fixture");
    profile[QStringLiteral("name")] = QStringLiteral("Password test account");
    profile[QStringLiteral("authenticationMode")] = QStringLiteral("password");
    QCOMPARE(m_sessions->addSession(profile), 0);
    auto* suggestions = named(QStringLiteral("localSshSuggestions"));
    QVERIFY(suggestions);
    const auto command = fixtureCommand();
    QStringList arguments;
    for (const auto& argument : command.mid(1)) arguments.append(argument.toString());
    QByteArray output;
    QObject outputObserver;
    connect(terminal(), &TerminalView::rawOutputReceived, &outputObserver, [&output](const QByteArray& data) { output += data; });
    QVERIFY(TerminalTestAccess::startIntegratedLocal(*terminal(), command.first().toString(), arguments));
    QTRY_VERIFY_WITH_TIMEOUT(output.contains("READY"), 10000);
    terminal()->forceActiveFocus();
    TerminalTestAccess::resetLocalScreen(*terminal());
    TerminalTestAccess::feed(*terminal(), prompt(*terminal()) + "ssh 192.0.2.87");
    QTRY_VERIFY(suggestions->property("showing").toBool());
    const auto rows = nativeVariant(suggestions->property("candidates")).toList();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().toMap().value(QStringLiteral("authenticationMode")).toString(), QStringLiteral("password"));
    QVariant label;
    QVERIFY(QMetaObject::invokeMethod(suggestions, "authentication", Q_RETURN_ARG(QVariant, label), Q_ARG(QVariant, rows.first())));
    QVERIFY(label.toString().contains(QStringLiteral("password"), Qt::CaseInsensitive));
    QVERIFY(!label.toString().contains(QStringLiteral("key"), Qt::CaseInsensitive));
    auto* panel = item(QStringLiteral("localSshSuggestionPanel"));
    QVERIFY(panel && panel->isVisible());
    const auto visibleLabel = [panel] {
        QList<QQuickItem*> pending{panel};
        while (!pending.isEmpty()) {
            auto* child = pending.takeLast();
            const auto text = child->property("text").toString();
            if (child->isVisible() && text.contains(QStringLiteral("Saved connection"))
                    && text.contains(QStringLiteral("password"), Qt::CaseInsensitive)) return true;
            pending.append(child->childItems());
        }
        return false;
    };
    QTRY_VERIFY(visibleLabel());
    QCOMPARE(m_pane->property("tabCount").toInt(), 1);
    QVERIFY(terminal()->currentSshConnection().isEmpty());
    QTest::keyClick(m_window, Qt::Key_Escape); // Dismiss only; never activate the SSH row.
    QTRY_VERIFY(!suggestions->property("showing").toBool());
    TerminalTestAccess::stopFixture(*terminal());
}

int main(int argc, char* argv[]) {
    QTemporaryDir initial(QDir::tempPath() + QStringLiteral("/bitshell-password-ui-XXXXXX"));
    if (!initial.isValid()) return EXIT_FAILURE;
    qputenv("BIT8SHELL_PROFILE", initial.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", initial.path().toUtf8());
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QStringLiteral("offscreen"))
        qputenv("QT_QPA_FONTDIR", (qEnvironmentVariable("SystemRoot") + QStringLiteral("/Fonts")).toUtf8());
#endif
    QGuiApplication app(argc, argv);
    if (QFontDatabase::families().isEmpty()) return EXIT_FAILURE;
    PasswordUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "PasswordUiTest.moc"
