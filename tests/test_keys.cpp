// SPDX-License-Identifier: GPL-3.0-or-later
// The keyboard on the Store's pages (src/qml/ui/keys.js): the arrows,
// Page Up and Down, Home and End and Space scroll the page; in a grid
// the arrows go from card to card; a text field keeps its keys.
#include <QFile>
#include <QJSEngine>
#include <QRegularExpression>
#include <QTest>

namespace {
// the same values as Qt's, which keys.js writes out
constexpr int Space = 0x20, Home = 0x01000010, End = 0x01000011, Left = 0x01000012, Up = 0x01000013, Right = 0x01000014,
              Down = 0x01000015, PageUp = 0x01000016, PageDown = 0x01000017, Tab = 0x01000001;
constexpr int Shift = 0x02000000, Control = 0x04000000;

bool load(QJSEngine &js)
{
    QFile f(QStringLiteral(RAYE_TESTS_DIR "/../src/qml/ui/keys.js"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QString text = QString::fromUtf8(f.readAll());
    text.remove(QRegularExpression(QStringLiteral("^\\.[^\\n]*$"), QRegularExpression::MultilineOption));
    return !js.evaluate(text).isError();
}

QJSValue call(QJSEngine &js, const char *name, const QJSValueList &args)
{
    return js.globalObject().property(QLatin1String(name)).call(args);
}
} // namespace

class TestKeys : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void keysScrollThePage()
    {
        QJSEngine js;
        QVERIFY(load(js));
        // a page 600 px high over 3000 px of content, 1000 px down
        const auto to = [&js](int key, int mods = 0, double y = 1000) {
            return call(js, "scrollTo", {key, mods, y, 600, 3000, 0});
        };
        QVERIFY(to(Down).toNumber() > 1000 && to(Down).toNumber() < 1100);
        QVERIFY(to(Up).toNumber() < 1000 && to(Up).toNumber() > 900);
        QVERIFY(to(PageDown).toNumber() >= 1500);
        QVERIFY(to(PageUp).toNumber() <= 500);
        QCOMPARE(to(Space).toNumber(), to(PageDown).toNumber());
        QCOMPARE(to(Space, Shift).toNumber(), to(PageUp).toNumber());
        QCOMPARE(to(Home).toNumber(), 0.);
        QCOMPARE(to(End).toNumber(), 2400.);
        // never past an end
        QCOMPARE(to(Up, 0, 10).toNumber(), 0.);
        QCOMPARE(to(PageDown, 0, 2300).toNumber(), 2400.);
        // a list whose start is not at 0 (a grid's originY)
        QCOMPARE(call(js, "scrollTo", {Home, 0, 500, 600, 3000, -40}).toNumber(), -40.);
        // other keys, and keys with Ctrl, are not the page's
        QVERIFY(to(Tab).isNull());
        QVERIFY(to(Down, Control).isNull());
    }

    void fieldsAndListsKeepTheirKeys()
    {
        QJSEngine js;
        QVERIFY(load(js));
        const auto takes = [&js](int key, bool text, bool list) { return call(js, "pageTakes", {key, text, list}).toBool(); };
        for (int key : {Up, Down, PageUp, PageDown, Home, End, Space})
            QVERIFY(!takes(key, true, false));
        QVERIFY(!takes(Down, false, true));
        QVERIFY(!takes(Right, false, true));
        QVERIFY(takes(PageDown, false, true));
        QVERIFY(takes(Space, false, false));
        QVERIFY(takes(Down, false, false));
    }

    void arrowsGoFromCardToCard()
    {
        QJSEngine js;
        QVERIFY(load(js));
        // 10 cards, 4 a row: 0-3, 4-7, 8-9
        const auto move = [&js](int key, int index) { return call(js, "gridMove", {key, index, 10, 4}).toInt(); };
        QCOMPARE(move(Right, 1), 2);
        QCOMPARE(move(Left, 1), 0);
        QCOMPARE(move(Left, 0), 0);
        QCOMPARE(move(Down, 1), 5);
        QCOMPARE(move(Up, 5), 1);
        QCOMPARE(move(Up, 2), 2);
        // into the short last row: its last card
        QCOMPARE(move(Down, 7), 9);
        QCOMPARE(move(Down, 9), 9);
        QCOMPARE(move(Right, 9), 9);
        QCOMPARE(move(Home, 6), 0);
        QCOMPARE(move(End, 2), 9);
        // nothing focused yet: the first card
        QCOMPARE(move(Down, -1), 0);
        QCOMPARE(move(Space, 3), -1);
        QCOMPARE(call(js, "gridMove", {Down, -1, 0, 4}).toInt(), -1);
    }
};

QTEST_GUILESS_MAIN(TestKeys)
#include "test_keys.moc"
