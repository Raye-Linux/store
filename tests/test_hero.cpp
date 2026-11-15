// SPDX-License-Identifier: GPL-3.0-or-later
// The big picture on top of Home and Apps comes from Raye's own list of
// well known apps, never from a feed (Flathub once put a password cracker
// there).
#include <QFile>
#include <QJSEngine>
#include <QRegularExpression>
#include <QTest>

namespace {
QString read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// a .js of the interface, without its QML-only lines (.pragma)
bool load(QJSEngine &js, const QString &path)
{
    QString text = read(path);
    text.remove(QRegularExpression(QStringLiteral("^\\.[^\\n]*$"), QRegularExpression::MultilineOption));
    return !text.isEmpty() && !js.evaluate(text, path).isError();
}

QStringList strings(const QJSValue &v)
{
    QStringList out;
    const int n = v.property(QStringLiteral("length")).toInt();
    for (int i = 0; i < n; ++i)
        out << v.property(i).toString();
    return out;
}
} // namespace

class TestHero : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void shortListOfKnownApps()
    {
        QJSEngine js;
        QVERIFY(load(js, QStringLiteral(RAYE_TESTS_DIR "/../src/qml/ui/hero.js")));
        const QStringList apps = strings(js.globalObject().property(QStringLiteral("apps")));
        QVERIFY(apps.size() >= 8 && apps.size() <= 20);
        for (const char *id : {"org.mozilla.firefox", "com.discordapp.Discord", "com.valvesoftware.Steam", "com.obsproject.Studio",
                               "org.gimp.GIMP", "org.libreoffice.LibreOffice", "org.videolan.VLC", "com.spotify.Client"})
            QVERIFY2(apps.contains(QLatin1String(id)), id);
        static const QRegularExpression bad(QStringLiteral("crack|hash|password|passwd|lockpick|hack|exploit|keylog|brute|john|hashcat|wifite"),
                                            QRegularExpression::CaseInsensitiveOption);
        for (const QString &id : apps)
            QVERIFY2(!bad.match(id).hasMatch(), qPrintable(id));
    }

    void anotherLeadsEachDay()
    {
        QJSEngine js;
        QVERIFY(load(js, QStringLiteral(RAYE_TESTS_DIR "/../src/qml/ui/hero.js")));
        const QStringList apps = strings(js.globalObject().property(QStringLiteral("apps")));
        QJSValue forDay = js.globalObject().property(QStringLiteral("forDay"));
        const QStringList monday = strings(forDay.call({20400}));
        const QStringList tuesday = strings(forDay.call({20401}));
        QCOMPARE(monday.size(), apps.size());
        QVERIFY(monday.first() != tuesday.first());
        QStringList sorted = monday;
        sorted.sort();
        QStringList all = apps;
        all.sort();
        QCOMPARE(sorted, all);
        QVERIFY(!strings(js.evaluate(QStringLiteral("today(new Date())"))).isEmpty());
    }

    // the pages fill the hero from that list, and from no feed
    void pagesUseTheList()
    {
        for (const char *page : {"HomePage.qml", "BrowsePage.qml"}) {
            const QString qml = read(QStringLiteral(RAYE_TESTS_DIR "/../src/qml/") + QLatin1String(page));
            QVERIFY2(!qml.isEmpty(), page);
            const qsizetype at = qml.indexOf(QLatin1String("featured = "));
            QVERIFY2(at >= 0, page);
            const QString fill = qml.mid(at, qml.indexOf(QLatin1Char('\n'), qml.indexOf(QLatin1Char('\n'), at) + 1) - at);
            QVERIFY2(fill.contains(QLatin1String("Hero.today(")), qPrintable(fill));
            QVERIFY2(!fill.contains(QLatin1String("pickOfTheDay")) && !fill.contains(QLatin1String("picks-of-the-week")), qPrintable(fill));
        }
    }
};

QTEST_GUILESS_MAIN(TestHero)
#include "test_hero.moc"
