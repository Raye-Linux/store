// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog.h"
#include "installer.h"
#include "windowsprograms.h"

#include <QFile>
#include <QTest>

namespace {
QString dir(const char *name)
{
    return QStringLiteral(RAYE_TESTS_DIR "/fixtures/windows/") + QLatin1String(name);
}

QByteArray programs()
{
    QFile f(dir("programs.json"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QHash<QString, WindowsTitle> titles()
{
    return readWindowsTitles({dir("titles/shipped"), dir("titles/local")});
}

const App *byId(const QList<App> &apps, const QString &id)
{
    for (const App &a : apps)
        if (a.id == id)
            return &a;
    return nullptr;
}
} // namespace

class TestWindowsPrograms : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void titlesLaterFileWins()
    {
        const auto t = titles();
        QCOMPARE(t.value(QStringLiteral("winrar")).state, QStringLiteral("works"));
        // a later folder overrides the shipped answer, whole record and all
        QCOMPARE(t.value(QStringLiteral("apex")).state, QStringLiteral("untested"));
        QCOMPARE(t.value(QStringLiteral("apex")).whyFr, QString());
        QCOMPARE(t.value(QStringLiteral("fortnite")).state, QStringLiteral("blocked"));
        // an indented line goes on with the value
        QCOMPARE(t.value(QStringLiteral("winrar")).whyEn, QStringLiteral("Installs, extracts archives and uninstalls here."));
    }

    void labels()
    {
        const WindowsListing l = readWindowsListing(programs(), dir("icons"), titles(), dir("profiles"), false, true);
        const App *winrar = byId(l.apps, QStringLiteral("windows.winrar"));
        const App *tool = byId(l.apps, QStringLiteral("windows.tool"));
        const App *studio = byId(l.apps, QStringLiteral("windows.studio"));
        QVERIFY(winrar && tool && studio);
        // a profile of its own whose title Raye marked works
        QCOMPARE(winrar->windowsState, QStringLiteral("optimized"));
        QCOMPARE(winrar->windowsNote, QStringLiteral("Installs, extracts archives and uninstalls here."));
        QCOMPARE(winrar->ref, QStringLiteral("windows:winrar"));
        QCOMPARE(winrar->source, QStringLiteral("windows"));
        QCOMPARE(winrar->downloadSize, 100);
        QCOMPARE(winrar->icon, dir("icons") + QStringLiteral("/windows/winrar.png"));
        // a profile with no checked title, and one checked but not working
        QCOMPARE(tool->windowsState, QStringLiteral("works"));
        QCOMPARE(studio->windowsState, QStringLiteral("works"));
        QCOMPARE(studio->windowsNote, QStringLiteral("Signing in has not worked here yet."));
    }

    void refusedEntries()
    {
        const WindowsListing l = readWindowsListing(programs(), dir("icons"), titles(), dir("profiles"), false, true);
        QVERIFY(!byId(l.apps, QStringLiteral("windows.plain")));
        QVERIFY(!byId(l.apps, QStringLiteral("windows.short")));
        QVERIFY(!byId(l.apps, QStringLiteral("windows.path")));
        // without a runner on the PC nothing is offered for install
        const WindowsListing none = readWindowsListing(programs(), dir("icons"), titles(), dir("profiles"), false, false);
        QVERIFY(!byId(none.apps, QStringLiteral("windows.winrar")));
        QCOMPARE(none.natives.value(QStringLiteral("com.discordapp.Discord")), QStringLiteral("Discord"));
    }

    // a setup its maker gives only to signed-in accounts: the Store sends
    // people to the maker's page, and the runner takes the file from there
    void downloadPage()
    {
        const WindowsListing l = readWindowsListing(programs(), dir("icons"), titles(), dir("profiles"), false, true);
        const App *live = byId(l.apps, QStringLiteral("windows.live"));
        QVERIFY(live);
        QCOMPARE(live->downloadPage, QStringLiteral("https://example.org/live/download"));
        QCOMPARE(live->ref, QStringLiteral("windows:live"));
        QCOMPARE(live->windowsState, QStringLiteral("works"));
        QCOMPARE(live->downloadSize, 0);
        QVERIFY(byId(l.apps, QStringLiteral("windows.winrar"))->downloadPage.isEmpty());
        QVERIFY(!byId(l.apps, QStringLiteral("windows.liveplain")));
        const WindowsListing none = readWindowsListing(programs(), dir("icons"), titles(), dir("profiles"), false, false);
        QVERIFY(!byId(none.apps, QStringLiteral("windows.live")));
        // kept by the cache, and handed to the page with the other links
        QList<App> back;
        Feeds feeds;
        QStringList order;
        QVERIFY(decodeCatalog(encodeCatalog({*live}, {}, {}, QStringLiteral("s")), QStringLiteral("s"), back, feeds, order));
        QCOMPARE(back.value(0).downloadPage, live->downloadPage);
        App shown = *live;
        shown.listed = true;
        Catalog catalog;
        catalog.setApps({shown});
        QCOMPARE(catalog.index(0, 0).data(Catalog::UrlsRole).toMap().value(QStringLiteral("download")).toString(),
                 live->downloadPage);
    }

    void blockedGames()
    {
        const WindowsListing l = readWindowsListing(programs(), dir("icons"), titles(), dir("profiles"), true, true);
        const App *fortnite = byId(l.apps, QStringLiteral("windows-title.fortnite"));
        QVERIFY(fortnite);
        QCOMPARE(fortnite->windowsState, QStringLiteral("blocked"));
        QCOMPARE(fortnite->summary, QStringLiteral("Epic n'a pas activé la partie Linux de son anti-triche."));
        QVERIFY(fortnite->ref.isEmpty());
        QCOMPARE(fortnite->kind(), QStringLiteral("game"));
        QVERIFY(fortnite->windowsAntiCheat);
        const App *pinball = byId(l.apps, QStringLiteral("windows-title.pinball"));
        QVERIFY(pinball && !pinball->windowsAntiCheat);
        // overridden later: no longer blocked
        QVERIFY(!byId(l.apps, QStringLiteral("windows-title.apex")));
    }

    void french()
    {
        const WindowsListing l = readWindowsListing(programs(), dir("icons"), titles(), dir("profiles"), true, true);
        const App *winrar = byId(l.apps, QStringLiteral("windows.winrar"));
        QCOMPARE(winrar->summary, QStringLiteral("Ouvre les archives"));
        QCOMPARE(winrar->windowsNote, QStringLiteral("S'installe, extrait les archives et se désinstalle ici."));
        // a plain string is the same in both languages
        QCOMPARE(byId(l.apps, QStringLiteral("windows.tool"))->summary, QStringLiteral("A tool"));
    }

    void order()
    {
        const WindowsListing l = readWindowsListing(programs(), dir("icons"), titles(), dir("profiles"), false, true);
        QCOMPARE(l.order, (QStringList{QStringLiteral("windows.winrar"), QStringLiteral("windows.tool"), QStringLiteral("windows.studio"),
                                       QStringLiteral("windows.live"), QStringLiteral("com.discordapp.Discord")}));

        QList<App> apps = l.apps;
        for (App &a : apps)
            a.listed = true;
        App discord;
        discord.id = QStringLiteral("com.discordapp.Discord");
        discord.name = QStringLiteral("Discord");
        discord.windowsState = QStringLiteral("native");
        discord.listed = true;
        apps.append(discord);
        Catalog catalog;
        catalog.setWindowsOrder(l.order);
        catalog.setApps(apps);
        QCOMPARE(catalog.windowsPrograms(QStringLiteral("optimized")), QStringList{QStringLiteral("windows.winrar")});
        QCOMPARE(catalog.windowsPrograms().size(), 5);
        QCOMPARE(catalog.windowsPrograms().last(), QStringLiteral("com.discordapp.Discord"));
    }

    void blockedOnlyInSearch()
    {
        const WindowsListing l = readWindowsListing(programs(), dir("icons"), titles(), dir("profiles"), false, true);
        QList<App> apps = l.apps;
        for (App &a : apps)
            a.listed = true;
        Catalog catalog;
        catalog.setApps(apps);
        CatalogFilter f;
        f.setSourceModel(&catalog);
        f.setKind(QStringLiteral("game"));
        for (int i = 0; i < f.rowCount(); ++i)
            QVERIFY(f.index(i, 0).data(Catalog::WindowsStateRole).toString() != QLatin1String("blocked"));
        f.setKind(QString());
        f.setText(QStringLiteral("fortnite"));
        QCOMPARE(f.rowCount(), 1);
        QCOMPARE(f.index(0, 0).data(Catalog::WindowsStateRole).toString(), QStringLiteral("blocked"));
    }

    // With the titles and profiles the image ships (the wine and iso
    // repositories next to this one), WinRAR is the program Raye checked
    // one by one: it has the label and the home row is there
    void shippedDataMarksWinrar()
    {
        const QString root = QStringLiteral(RAYE_TESTS_DIR "/../../");
        const QString titleDir = root + QStringLiteral("wine/release/windows/titles");
        const QString profileDir = root + QStringLiteral("iso/packages/raye-base/release/windows/profiles");
        if (!QFile::exists(titleDir) || !QFile::exists(profileDir))
            QSKIP("the wine and iso repositories are not next to this one");
        QFile data(QStringLiteral(RAYE_TESTS_DIR "/../data/windows-programs.json"));
        QVERIFY(data.open(QIODevice::ReadOnly));
        const auto shipped = readWindowsTitles({titleDir});
        const WindowsListing l = readWindowsListing(data.readAll(), dir("icons"), shipped, profileDir, false, true);
        const App *winrar = byId(l.apps, QStringLiteral("windows.winrar"));
        QVERIFY(winrar);
        QCOMPARE(winrar->windowsState, QStringLiteral("optimized"));
        QList<App> apps = l.apps;
        for (App &a : apps)
            a.listed = true;
        Catalog catalog;
        catalog.setWindowsOrder(l.order);
        catalog.setApps(apps);
        QVERIFY(catalog.windowsPrograms(QStringLiteral("optimized")).contains(QStringLiteral("windows.winrar")));
    }

    void refs()
    {
        QVERIFY(Installer::validRef(QStringLiteral("windows"), QStringLiteral("windows:fl-studio")));
        QVERIFY(!Installer::validRef(QStringLiteral("windows"), QStringLiteral("windows:../x")));
        QVERIFY(!Installer::validRef(QStringLiteral("windows"), QStringLiteral("fl-studio")));
        QVERIFY(!Installer::validRef(QStringLiteral("windows"), QStringLiteral("windows:-rf")));
        QCOMPARE(Installer::command(QStringLiteral("install"), QStringLiteral("windows"), QStringLiteral("windows:winrar")),
                 (QStringList{QStringLiteral("/usr/libexec/raye-store-windows"), QStringLiteral("install"), QStringLiteral("windows:winrar")}));
        QCOMPARE(windowsSpace(QStringLiteral("windows:winrar")), QStringLiteral("winrar"));
    }
};

QTEST_GUILESS_MAIN(TestWindowsPrograms)
#include "test_windows_programs.moc"
