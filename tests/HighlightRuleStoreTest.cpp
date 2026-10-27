#include <QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <bitshell/core/HighlightRuleStore.h>

using bitshell::core::HighlightRuleStore;

class HighlightRuleStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void validatesAndPersistsCustomRules();

private:
    QTemporaryDir m_configRoot;
};

void HighlightRuleStoreTest::initTestCase() {
    QVERIFY(m_configRoot.isValid());
    qputenv("XDG_CONFIG_HOME", m_configRoot.path().toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("highlight-rule-store-test"));
}

void HighlightRuleStoreTest::validatesAndPersistsCustomRules() {
    HighlightRuleStore store;
    QCOMPARE(store.count(), 4);

    const QVariantMap custom{
        {QStringLiteral("name"), QStringLiteral("Incident IDs")},
        {QStringLiteral("pattern"), QStringLiteral(R"(\bINC-[0-9]{6,}\b)")},
        {QStringLiteral("foreground"), QStringLiteral("#ad8ee6")},
        {QStringLiteral("background"), QStringLiteral("#101620")},
        {QStringLiteral("bold"), true},
        {QStringLiteral("underline"), false},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("caseSensitive"), true},
        {QStringLiteral("priority"), 65},
    };
    const int customRow = store.addRule(custom);
    QCOMPARE(customRow, 4);
    QCOMPARE(store.count(), 5);
    QCOMPARE(store.rules().size(), 5);
    QCOMPARE(store.get(customRow).value(QStringLiteral("name")).toString(),
             QStringLiteral("Incident IDs"));

    const int rejected = store.addRule(QVariantMap{
        {QStringLiteral("pattern"), QStringLiteral("(")},
        {QStringLiteral("foreground"), QStringLiteral("#ffffff")},
    });
    QCOMPARE(rejected, -1);
    QCOMPARE(store.count(), 5);
    QVERIFY(store.lastError().contains(QStringLiteral("regular expression"),
                                       Qt::CaseInsensitive));

    QVERIFY(store.setRuleEnabled(customRow, false));
    QVERIFY(!store.get(customRow).value(QStringLiteral("enabled")).toBool());
    const int duplicateRow = store.duplicateRule(customRow);
    QCOMPARE(duplicateRow, 5);
    QCOMPARE(store.count(), 6);
    QVERIFY(store.removeRule(duplicateRow));
    QCOMPARE(store.count(), 5);

    HighlightRuleStore reloaded;
    QCOMPARE(reloaded.count(), 5);
    QCOMPARE(reloaded.get(customRow).value(QStringLiteral("pattern")).toString(),
             QStringLiteral(R"(\bINC-[0-9]{6,}\b)"));
    QVERIFY(!reloaded.get(customRow).value(QStringLiteral("enabled")).toBool());

    QFile catalog(store.storagePath());
    QVERIFY(catalog.open(QIODevice::ReadOnly));
    const QByteArray contents = catalog.readAll();
    QVERIFY(contents.contains(QByteArrayLiteral("\"schema\"")));
    QVERIFY(contents.contains(QByteArrayLiteral("Incident IDs")));
}

QTEST_GUILESS_MAIN(HighlightRuleStoreTest)

#include "HighlightRuleStoreTest.moc"
