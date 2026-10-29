#include <QtTest>

#include <QDir>
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <bitshell/core/ConnectionRouting.h>
#include <bitshell/core/PersonalProfile.h>
#include <bitshell/core/SessionStore.h>

#include <memory>

// This fixture only records requests. No process, SSH client or network is used.
class SshSuggestionTerminal : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantMap localSshQuery READ query WRITE setQuery NOTIFY localSshQueryChanged)
    Q_PROPERTY(bool localSshSuggestionsActive MEMBER suggestionsActive)
    Q_PROPERTY(bool localSshSelectionReady MEMBER selectionReady)
    Q_PROPERTY(qreal cursorX MEMBER cursorLeft NOTIFY cursorChanged)
    Q_PROPERTY(qreal cursorY MEMBER cursorTop NOTIFY cursorChanged)
    Q_PROPERTY(qreal cursorHeight MEMBER cursorHeight CONSTANT)
public:
    using QQuickItem::QQuickItem;
    QVariantMap query() const { return m_query; }
    void setQuery(const QVariantMap& value) {
        if (m_query == value) return;
        m_query = value;
        emit localSshQueryChanged();
    }
    Q_INVOKABLE bool runLocalSsh(const QVariantMap& profile, const QVariantMap& expectedQuery) {
        if (expectedQuery != m_query) {
            emit localSshError(QStringLiteral("The input changed."));
            return false;
        }
        launches.append(profile);
        snapshots.append(expectedQuery);
        return true;
    }
    Q_INVOKABLE void dismissLocalSsh() { ++dismissals; setQuery({}); }

    bool suggestionsActive = false;
    bool selectionReady = false;
    qreal cursorLeft = 24;
    qreal cursorTop = 36;
    qreal cursorHeight = 18;
    int dismissals = 0;
    QVariantList launches;
    QVariantList snapshots;
signals:
    void localSshQueryChanged();
    void cursorChanged();
    void localSshNavigate(int delta);
    void localSshAccept();
    void localSshDismiss();
    void localSshError(const QString& message);
private:
    QVariantMap m_query;
};

namespace {
QVariantMap saved(const QString& id, const QString& host, const QString& user = {}) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), id},
            {QStringLiteral("host"), host}, {QStringLiteral("user"), user},
            {QStringLiteral("port"), 22}, {QStringLiteral("identityKey"), QStringLiteral("none")}};
}
QVariantMap route() {
    return {{QStringLiteral("type"), QStringLiteral("cyberark-psmp")},
            {QStringLiteral("name"), QStringLiteral("Production gateway")},
            {QStringLiteral("host"), QStringLiteral("gateway.example.test")},
            {QStringLiteral("port"), 22},
            {QStringLiteral("account"), QVariantMap{{QStringLiteral("source"), QStringLiteral("literal")},
                {QStringLiteral("name"), QStringLiteral("oracle")}}}};
}
QVariantMap query(const QString& host) {
    return {{QStringLiteral("host"), host}, {QStringLiteral("user"), QString{}},
            {QStringLiteral("port"), 0}, {QStringLiteral("command"), QStringLiteral("ssh ") + host},
            {QStringLiteral("shellKind"), QStringLiteral("powershell")}, {QStringLiteral("token"), QStringLiteral("test-prompt-1")}};
}
QVariant asVariant(const QVariant& value) {
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}
}

class LocalSshSuggestionsTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void selectsSavedAccountsAndRequestsTheSameTerminal();
    void routesRequireExplicitSelectionAndGatewayReview();
    void changedAndRemovedRowsAreResolvedAgain();
    void requiredRoutesNeverFallBackWhenDisabledOrRemoved();
    void x11RequiresConsentAndRechecksTheInput();
    void reviewButtonsSupportKeyboardNavigation_data();
    void reviewButtonsSupportKeyboardNavigation();
    void panelTracksCursorAndStaysInsideSmallViewports();
private:
    QVariantList candidates() const { return asVariant(m_helper->property("candidates")).toList(); }
    QObject* dialog(const char* name) const { return m_window->findChild<QObject*>(QString::fromLatin1(name)); }
    bool invokeActivate(const QVariantMap& row, const QVariantMap& expected) {
        return QMetaObject::invokeMethod(m_helper, "activate", Q_ARG(QVariant, row),
                                        Q_ARG(QVariant, expected), Q_ARG(QVariant, QString{}));
    }
    void capture(const QString& name) const;
    QTemporaryDir m_configRoot;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow* m_window = nullptr;
    SshSuggestionTerminal* m_terminal = nullptr;
    QQuickItem* m_helper = nullptr;
    bitshell::core::SessionStore* m_store = nullptr;
    bitshell::core::PersonalProfile* m_personal = nullptr;
    QStringList m_warnings;
};

void LocalSshSuggestionsTest::initTestCase() {
    QVERIFY(m_configRoot.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("local-ssh-suggestions-test"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qmlRegisterType<SshSuggestionTerminal>("SshSuggestionTest", 1, 0, "SshSuggestionTerminal");
}

void LocalSshSuggestionsTest::init() {
    const auto root = m_configRoot.filePath(QString::fromLatin1(QTest::currentTestFunction()));
    QVERIFY(QDir().mkpath(root));
    qputenv("BIT8SHELL_PROFILE", root.toUtf8());
    qputenv("XDG_CONFIG_HOME", root.toUtf8());
    m_warnings.clear();
    m_engine = std::make_unique<QQmlApplicationEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) m_warnings << warning.toString();
    });
    m_engine->loadData(QByteArrayLiteral(R"QML(
import QtQuick
import QtQuick.Controls.Basic
import BitShell
import SshSuggestionTest
ApplicationWindow {
    width: 800; height: 500; visible: true
    SshSuggestionTerminal { id: input; objectName: "testTerminal"; anchors.fill: parent; focus: true }
    LocalSshSuggestions { terminal: input; anchors.fill: parent }
}
)QML"));
    QVERIFY2(!m_engine->rootObjects().isEmpty(), qPrintable(m_warnings.join(u'\n')));
    m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
    QVERIFY(m_window);
    m_terminal = m_window->findChild<SshSuggestionTerminal*>(QStringLiteral("testTerminal"));
    m_helper = m_window->findChild<QQuickItem*>(QStringLiteral("localSshSuggestions"));
    QVERIFY(m_terminal && m_helper);
    m_store = m_engine->singletonInstance<bitshell::core::SessionStore*>("BitShell", "SessionStore");
    m_personal = m_engine->singletonInstance<bitshell::core::PersonalProfile*>("BitShell", "PersonalProfile");
    QVERIFY(m_store && m_personal);
    QCOMPARE(m_store->count(), 0);
    m_terminal->forceActiveFocus();
    QTRY_VERIFY(m_terminal->hasActiveFocus());
}

void LocalSshSuggestionsTest::cleanup() {
    if (m_window) m_window->close();
    m_engine.reset();
    m_window = nullptr; m_terminal = nullptr; m_helper = nullptr; m_store = nullptr; m_personal = nullptr;
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(u'\n')));
}

void LocalSshSuggestionsTest::capture(const QString& name) const {
    const auto directory = qEnvironmentVariable("BIT8SHELL_UI_TEST_CAPTURE_DIR");
    if (directory.isEmpty()) return;
    QVERIFY(QDir().mkpath(directory));
    QTest::qWait(70);
    const auto image = m_window->grabWindow();
    QVERIFY(!image.isNull());
    QVERIFY(image.save(QDir(directory).filePath(name)));
}

void LocalSshSuggestionsTest::selectsSavedAccountsAndRequestsTheSameTerminal() {
    const auto command = query(QStringLiteral("10.20.30.4"));
    auto first = saved(QStringLiteral("a-operations"), QStringLiteral("10.20.30.4"), QStringLiteral("ops"));
    first[QStringLiteral("port")] = 2222;
    first[QStringLiteral("compression")] = true;
    QCOMPARE(m_store->addSession(first), 0);
    QCOMPARE(m_store->addSession(saved(QStringLiteral("b-deploy"), QStringLiteral("10.20.30.4"), QStringLiteral("deploy"))), 1);
    m_terminal->setQuery(command);
    QTRY_COMPARE(candidates().size(), 2);
    QCOMPARE(m_helper->property("selectedIndex").toInt(), 0);
    QTRY_VERIFY(m_terminal->suggestionsActive && m_terminal->selectionReady);
    capture(QStringLiteral("local-ssh-saved-accounts.png"));
    emit m_terminal->localSshNavigate(1);
    QCOMPARE(m_helper->property("selectedIndex").toInt(), 1);
    emit m_terminal->localSshAccept();
    QCOMPARE(m_terminal->launches.size(), 1);
    QCOMPARE(m_terminal->launches.first().toMap().value(QStringLiteral("user")).toString(), QStringLiteral("deploy"));
    QCOMPARE(m_terminal->snapshots.first().toMap(), command);
    QCOMPARE(m_terminal->query(), command); // The UI itself never edits or executes shell input.
    emit m_terminal->localSshNavigate(-1);
    emit m_terminal->localSshAccept();
    QCOMPARE(m_terminal->launches.size(), 2);
    QCOMPARE(m_terminal->launches.last().toMap().value(QStringLiteral("port")).toInt(), 2222);
    QVERIFY(m_terminal->launches.last().toMap().value(QStringLiteral("compression")).toBool());
    m_terminal->dismissLocalSsh();
    QCOMPARE(m_terminal->dismissals, 1);
    QTRY_VERIFY(!m_terminal->suggestionsActive);
}

void LocalSshSuggestionsTest::routesRequireExplicitSelectionAndGatewayReview() {
    QVERIFY(m_store->saveRoute(QStringLiteral("production"), route()));
    QVERIFY(m_personal->setProfile(QStringLiteral("Test operator"), QStringLiteral("employee"), true));
    const auto command = query(QStringLiteral("new-host.example.test"));
    m_terminal->setQuery(command);
    QTRY_COMPARE(candidates().size(), 1);
    QCOMPARE(m_helper->property("selectedIndex").toInt(), -1);
    QVERIFY(!m_terminal->selectionReady);
    emit m_terminal->localSshAccept();
    QVERIFY(m_terminal->launches.isEmpty());
    emit m_terminal->localSshNavigate(1);
    QVERIFY(m_terminal->selectionReady);
    emit m_terminal->localSshAccept();
    auto* review = dialog("localSshGatewayReview");
    QVERIFY(review);
    QTRY_VERIFY(review->property("visible").toBool());
    QVERIFY(m_terminal->launches.isEmpty());
    QVERIFY(!m_terminal->suggestionsActive);
    capture(QStringLiteral("local-ssh-cyberark-review.png"));
    QVERIFY(QMetaObject::invokeMethod(review, "reject"));
    QTRY_VERIFY(!review->property("visible").toBool());
    QCOMPARE(m_terminal->query(), command);
    QVERIFY(m_terminal->launches.isEmpty());
    QTRY_VERIFY(m_terminal->hasActiveFocus());

    // Approving the exact route definition permits the same native-terminal request.
    QVERIFY(m_personal->approveRoute(bitshell::core::ConnectionRouting::fingerprint(route())));
    emit m_terminal->localSshAccept();
    QCOMPARE(m_terminal->launches.size(), 1);
    const auto profile = m_terminal->launches.first().toMap();
    QCOMPARE(profile.value(QStringLiteral("host")).toString(), QStringLiteral("new-host.example.test"));
    QCOMPARE(profile.value(QStringLiteral("psmp")).toMap().value(QStringLiteral("operator")).toString(), QStringLiteral("employee"));
    QVERIFY(profile.value(QStringLiteral("user")).toString().isEmpty());
    QCOMPARE(m_terminal->snapshots.first().toMap(), command);
}

void LocalSshSuggestionsTest::changedAndRemovedRowsAreResolvedAgain() {
    const QString host = QStringLiteral("db.example.test");
    QCOMPARE(m_store->addSession(saved(QStringLiteral("saved"), host, QStringLiteral("before"))), 0);
    m_terminal->setQuery(query(host));
    QTRY_COMPARE(candidates().size(), 1);
    const auto oldRow = candidates().first().toMap();
    const auto oldQuery = m_terminal->query();
    QVERIFY(m_store->updateSession(0, {{QStringLiteral("user"), QStringLiteral("after")}, {QStringLiteral("port"), 2222}}));
    QVERIFY(invokeActivate(oldRow, oldQuery));
    QCOMPARE(m_terminal->launches.size(), 1);
    QCOMPARE(m_terminal->launches.first().toMap().value(QStringLiteral("user")).toString(), QStringLiteral("after"));
    QCOMPARE(m_terminal->launches.first().toMap().value(QStringLiteral("port")).toInt(), 2222);
    QVERIFY(m_store->removeSession(0));
    QTRY_VERIFY(candidates().isEmpty());
    QVERIFY(invokeActivate(oldRow, oldQuery));
    QTRY_VERIFY(dialog("localSshErrorDialog")->property("visible").toBool());
    QCOMPARE(m_terminal->launches.size(), 1);
    QVERIFY(dialog("localSshErrorDialog")->property("message").toString().contains(QStringLiteral("removed")));
}

void LocalSshSuggestionsTest::requiredRoutesNeverFallBackWhenDisabledOrRemoved() {
    QVERIFY(m_store->saveRoute(QStringLiteral("production"), route()));
    auto profile = saved(QStringLiteral("routed"), QStringLiteral("10.20.30.4"), QStringLiteral("do-not-use-directly"));
    profile[QStringLiteral("connection")] = QVariantMap{{QStringLiteral("mode"), QStringLiteral("route")},
        {QStringLiteral("route"), QStringLiteral("production")}};
    QCOMPARE(m_store->addSession(profile), 0);
    m_terminal->setQuery(query(QStringLiteral("10.20.30.4")));
    QTRY_COMPARE(candidates().size(), 1);
    QVERIFY(!candidates().first().toMap().value(QStringLiteral("valid")).toBool());
    emit m_terminal->localSshAccept();
    auto* error = dialog("localSshErrorDialog");
    QTRY_VERIFY(error->property("visible").toBool());
    QVERIFY(m_terminal->launches.isEmpty());
    QVERIFY(QMetaObject::invokeMethod(error, "accept"));
    QTRY_VERIFY(!error->property("visible").toBool());
    QVERIFY(m_personal->setProfile(QStringLiteral("Operator"), QStringLiteral("employee"), true));
    QTRY_COMPARE(candidates().size(), 2);
    QVERIFY(candidates().first().toMap().value(QStringLiteral("valid")).toBool());
    QVERIFY(m_store->removeRoute(QStringLiteral("production")));
    QTRY_COMPARE(candidates().size(), 1);
    QVERIFY(!candidates().first().toMap().value(QStringLiteral("valid")).toBool());
    emit m_terminal->localSshAccept();
    QTRY_VERIFY(error->property("visible").toBool());
    QVERIFY(m_terminal->launches.isEmpty());
    QVERIFY(error->property("message").toString().contains(QStringLiteral("Direct fallback is blocked")));
}

void LocalSshSuggestionsTest::x11RequiresConsentAndRechecksTheInput() {
    auto profile = saved(QStringLiteral("x11"), QStringLiteral("graphics.example.test"));
    profile[QStringLiteral("forwardX11")] = true;
    QCOMPARE(m_store->addSession(profile), 0);
    const auto command = query(QStringLiteral("graphics.example.test"));
    m_terminal->setQuery(command);
    emit m_terminal->localSshAccept();
    auto* review = dialog("localSshX11Review");
    QTRY_VERIFY(review->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(review, "reject"));
    QTRY_VERIFY(!review->property("visible").toBool());
    QVERIFY(m_terminal->launches.isEmpty());
    QCOMPARE(m_terminal->query(), command);
    emit m_terminal->localSshAccept();
    QTRY_VERIFY(review->property("visible").toBool());
    m_terminal->setQuery(query(QStringLiteral("different.example.test")));
    QVERIFY(QMetaObject::invokeMethod(review, "accept"));
    QTRY_VERIFY(dialog("localSshErrorDialog")->property("visible").toBool());
    QVERIFY(m_terminal->launches.isEmpty());
    QVERIFY(dialog("localSshErrorDialog")->property("message").toString().contains(QStringLiteral("command changed")));
}

void LocalSshSuggestionsTest::reviewButtonsSupportKeyboardNavigation_data() {
    QTest::addColumn<bool>("cyberArk");
    QTest::newRow("cyberark-gateway") << true;
    QTest::newRow("x11-forwarding") << false;
}

void LocalSshSuggestionsTest::reviewButtonsSupportKeyboardNavigation() {
    QFETCH(bool, cyberArk);
    const QString host = QStringLiteral("keyboard.example.test");
    if (cyberArk) {
        QVERIFY(m_store->saveRoute(QStringLiteral("production"), route()));
        QVERIFY(m_personal->setProfile(QStringLiteral("Operator"), QStringLiteral("employee"), true));
    } else {
        auto profile = saved(QStringLiteral("x11-keyboard"), host);
        profile[QStringLiteral("forwardX11")] = true;
        QCOMPARE(m_store->addSession(profile), 0);
    }
    m_terminal->setQuery(query(host));
    if (cyberArk) emit m_terminal->localSshNavigate(1);
    auto* review = dialog(cyberArk ? "localSshGatewayReview" : "localSshX11Review");
    QVERIFY(review);
    const auto focusedText = [&] {
        return m_window->activeFocusItem() ? m_window->activeFocusItem()->property("text").toString() : QString{};
    };
    emit m_terminal->localSshAccept();
    QTRY_VERIFY(review->property("opened").toBool());
    QTRY_COMPARE(focusedText(), QStringLiteral("Cancel"));

    // Return must activate the focused Cancel, never a hidden default OK.
    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_VERIFY(!review->property("visible").toBool());
    QVERIFY(m_terminal->launches.isEmpty());
    QTRY_VERIFY(m_terminal->hasActiveFocus());
    emit m_terminal->localSshAccept();
    QTRY_VERIFY(review->property("opened").toBool());
    QTRY_COMPARE(focusedText(), QStringLiteral("Cancel"));

    QTest::keyClick(m_window, Qt::Key_Left);
    QTRY_COMPARE(focusedText(), QStringLiteral("OK"));
    auto* focused = m_window->activeFocusItem();
    QVERIFY(focused && focused->hasActiveFocus());
    auto* outline = focused->property("background").value<QObject*>();
    QVERIFY(outline);
    QCOMPARE(QQmlProperty::read(outline, QStringLiteral("border.width")).toInt(), 2);
    QTest::keyClick(m_window, Qt::Key_Right);
    QTRY_COMPARE(focusedText(), QStringLiteral("Cancel"));
    QTest::keyClick(m_window, Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_COMPARE(focusedText(), QStringLiteral("OK"));
    QTest::keyClick(m_window, Qt::Key_Tab);
    QTRY_COMPARE(focusedText(), QStringLiteral("Cancel"));
    QTest::keyClick(m_window, Qt::Key_Tab);
    QTRY_COMPARE(focusedText(), QStringLiteral("OK"));
    QVERIFY(m_terminal->launches.isEmpty());
    capture(cyberArk ? QStringLiteral("keyboard-cyberark-ok-focus.png") : QStringLiteral("keyboard-x11-ok-focus.png"));
    // Use actual key events for both native activation keys.
    QTest::keyClick(m_window, cyberArk ? Qt::Key_Space : Qt::Key_Enter);
    QTRY_COMPARE(m_terminal->launches.size(), 1);
    QTRY_VERIFY(!review->property("visible").toBool());
    QTRY_VERIFY(m_terminal->hasActiveFocus());

    // Single-action error dialogs also place focus on their only button.
    emit m_terminal->localSshError(QStringLiteral("Keyboard test message"));
    auto* error = dialog("localSshErrorDialog");
    QTRY_VERIFY(error->property("opened").toBool());
    QTRY_COMPARE(focusedText(), QStringLiteral("OK"));
    QTest::keyClick(m_window, Qt::Key_Right);
    QTRY_COMPARE(focusedText(), QStringLiteral("OK"));
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!error->property("visible").toBool());
    QTRY_VERIFY(m_terminal->hasActiveFocus());
    QCOMPARE(m_terminal->launches.size(), 1);
}

void LocalSshSuggestionsTest::panelTracksCursorAndStaysInsideSmallViewports() {
    const QString host = QStringLiteral("10.20.30.4");
    for (int index = 0; index < 9; ++index)
        QCOMPARE(m_store->addSession(saved(QStringLiteral("account-%1").arg(index), host, QStringLiteral("ops%1").arg(index))), index);
    m_terminal->setQuery(query(host));
    auto* panel = m_window->findChild<QQuickItem*>(QStringLiteral("localSshSuggestionPanel"));
    QVERIFY(panel);
    QTRY_VERIFY(panel->isVisible());
    QVERIFY(panel->y() > m_terminal->cursorTop);
    m_terminal->cursorTop = 470;
    m_terminal->cursorLeft = 760;
    emit m_terminal->cursorChanged();
    QTRY_VERIFY(panel->y() + panel->height() <= m_terminal->cursorTop);
    QVERIFY(panel->x() + panel->width() <= m_helper->width());
    m_window->setWidth(230); m_window->setHeight(190);
    QTRY_COMPARE(m_helper->width(), 230.0);
    QTRY_VERIFY(panel->x() >= 0 && panel->y() >= 0);
    QVERIFY(panel->x() + panel->width() <= m_helper->width());
    QVERIFY(panel->y() + panel->height() <= m_helper->height());
    for (int index = 0; index < 8; ++index) emit m_terminal->localSshNavigate(1);
    QCOMPARE(m_helper->property("selectedIndex").toInt(), 8);
    capture(QStringLiteral("local-ssh-small-viewport.png"));
    m_helper->setProperty("blocked", true);
    QTRY_VERIFY(!panel->isVisible());
    QVERIFY(!m_terminal->suggestionsActive);
}

int main(int argc, char* argv[]) {
    QTemporaryDir configRoot(QDir::tempPath() + QStringLiteral("/bitshell-ssh-ui-XXXXXX"));
    if (!configRoot.isValid()) return EXIT_FAILURE;
    qputenv("BIT8SHELL_PROFILE", configRoot.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", configRoot.path().toUtf8());
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QStringLiteral("offscreen"))
        qputenv("QT_QPA_FONTDIR", (qEnvironmentVariable("SystemRoot") + QStringLiteral("/Fonts")).toUtf8());
#endif
    QGuiApplication app(argc, argv);
    LocalSshSuggestionsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "LocalSshSuggestionsTest.moc"
