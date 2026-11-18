// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog.h"
#include "installer.h"
#include "searchsuggestions.h"
#include "storesettings.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStringListModel>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTest>

namespace {
App make(const QString &id, const QString &name, const QString &source, const QString &ref,
         const QStringList &cats = {}, const QString &summary = {})
{
    App a;
    a.id = id;
    a.name = name;
    a.source = source;
    a.ref = ref;
    a.categories = cats;
    a.summary = summary;
    return a;
}

// an app that passes every listing rule
App listed(const QString &id, const QString &name, qint64 popularity = 0, const QString &summary = QStringLiteral("Does things"))
{
    App a = make(id, name, QStringLiteral("flatpak"), QStringLiteral("app/") + id, {}, summary);
    a.icon = QStringLiteral("/icons/") + id + QStringLiteral(".png");
    a.iconSize = 128;
    a.popularity = popularity;
    return a;
}

QString fixture(const char *name)
{
    return QStringLiteral(RAYE_TESTS_DIR "/fixtures/") + QLatin1String(name);
}

QStringList names(const QAbstractItemModel &m)
{
    QStringList out;
    for (int i = 0; i < m.rowCount(); ++i)
        out << m.index(i, 0).data(Catalog::NameRole).toString();
    return out;
}
} // namespace

class TestStore : public QObject
{
    Q_OBJECT
private Q_SLOTS:

    // --- the list kept between runs

    void cacheReadsBackWhatWasWritten()
    {
        App a = listed(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP"), 900);
        a.screenshots = {{QStringLiteral("t.png"), QStringLiteral("f.png"), QStringLiteral("Edit")}};
        a.releases = {{QStringLiteral("3.0"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("<p>x</p>")}};
        a.ratingHistogram = {1, 2, 3, 4, 5};
        a.windowsState = QStringLiteral("works");
        a.listed = false;
        Feeds feeds;
        feeds.lists.insert(QStringLiteral("popular"), {QStringLiteral("org.gimp.GIMP")});
        feeds.ratings.insert(QStringLiteral("org.gimp.GIMP"), Rating{4.5, 12, {0, 0, 1, 2, 9}});
        feeds.pickOfTheDay = QStringLiteral("org.gimp.GIMP");
        const QByteArray bytes = encodeCatalog({a}, feeds, {QStringLiteral("w1")}, QStringLiteral("s"));

        QList<App> apps;
        Feeds back;
        QStringList order;
        QVERIFY(decodeCatalog(bytes, QStringLiteral("s"), apps, back, order));
        QCOMPARE(apps.size(), 1);
        QCOMPARE(apps.first().name, QStringLiteral("GIMP"));
        QCOMPARE(apps.first().screenshots.first().caption, QStringLiteral("Edit"));
        QCOMPARE(apps.first().releases.first().date.toSecsSinceEpoch(), 1700000000);
        QCOMPARE(apps.first().ratingHistogram, (QList<int>{1, 2, 3, 4, 5}));
        QCOMPARE(apps.first().windowsState, QStringLiteral("works"));
        QVERIFY(!apps.first().listed);
        QCOMPARE(back.ratings.value(QStringLiteral("org.gimp.GIMP")).count, 12);
        QCOMPARE(back.pickOfTheDay, QStringLiteral("org.gimp.GIMP"));
        QCOMPARE(order, QStringList{QStringLiteral("w1")});
        // the same list makes the same bytes: that is how a reload sees nothing changed
        QCOMPARE(encodeCatalog(apps, back, order, QStringLiteral("s")), bytes);

        // another language or version, or a cut file, is not used
        QVERIFY(!decodeCatalog(bytes, QStringLiteral("other"), apps, back, order));
        QVERIFY(!decodeCatalog(bytes.left(bytes.size() - 3), QStringLiteral("s"), apps, back, order));
        QVERIFY(!decodeCatalog("junk", QStringLiteral("s"), apps, back, order));
    }

    void reloadShowsLastListFirst()
    {
        QTemporaryDir tmp;
        const QString file = tmp.filePath(QStringLiteral("catalog"));
        qputenv("RAYE_STORE_CACHE", file.toUtf8());
        qputenv("RAYE_STORE_FEEDS", tmp.filePath(QStringLiteral("none")).toUtf8());
        const QList<App> old{listed(QStringLiteral("a.One"), QStringLiteral("One")), listed(QStringLiteral("b.Two"), QStringLiteral("Two")),
                             listed(QStringLiteral("c.Three"), QStringLiteral("Three"))};
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(encodeCatalog(old, {}, {}, catalogCacheStamp()));
        f.close();

        Catalog catalog;
        int rowsWhenShown = -1;
        connect(&catalog, &Catalog::loadingChanged, this, [&] {
            if (!catalog.loading() && rowsWhenShown < 0)
                rowsWhenShown = catalog.rowCount();
        });
        catalog.reload();
        QVERIFY(catalog.loading());
        QTRY_VERIFY_WITH_TIMEOUT(catalog.settled(), 60000);
        QCOMPARE(rowsWhenShown, 3);
        // and the real read was kept for next time
        QVERIFY(f.open(QIODevice::ReadOnly));
        QList<App> apps;
        Feeds feeds;
        QStringList order;
        QVERIFY(decodeCatalog(f.readAll(), catalogCacheStamp(), apps, feeds, order));
        QCOMPARE(apps.size(), catalog.rowCount());
        qunsetenv("RAYE_STORE_CACHE");
        qunsetenv("RAYE_STORE_FEEDS");
    }

    void installedIsReadOffTheGuiThread()
    {
        // a slow flatpak must not hold the window
        QTemporaryDir bin;
        QFile fake(bin.filePath(QStringLiteral("flatpak")));
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("#!/bin/sh\nsleep 2\necho com.example.Slow/x86_64/stable\n");
        fake.close();
        fake.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", QByteArray(bin.path().toUtf8() + ':' + path));

        Catalog catalog;
        QElapsedTimer timer;
        timer.start();
        catalog.refreshInstalled();
        QVERIFY2(timer.elapsed() < 500, qPrintable(QStringLiteral("blocked %1 ms").arg(timer.elapsed())));
        QTRY_VERIFY_WITH_TIMEOUT(catalog.ownsInstall(QStringLiteral("app/com.example.Slow/x86_64/stable")), 10000);
        qputenv("PATH", path);
    }
    void finishedJobShowsAtOnce()
    {
        Catalog catalog;
        const QString ref = QStringLiteral("windows:winrar");
        QVERIFY(!catalog.ownsInstall(ref));
        catalog.noteInstalled(ref, true);
        QVERIFY(catalog.ownsInstall(ref));
        catalog.noteInstalled(ref, false);
        QVERIFY(!catalog.ownsInstall(ref));
    }
    void flathubWinsOverThePackage()
    {
        const App deb = make(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP"), QStringLiteral("apt"), QStringLiteral("gimp"));
        const App fp = make(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP"), QStringLiteral("flatpak"),
                            QStringLiteral("app/org.gimp.GIMP/x86_64/stable"));
        QVERIFY(preferOver(fp, deb));
        QVERIFY(!preferOver(deb, fp));
        QVERIFY(!preferOver(fp, fp));
    }

    void filterBySearchAndCategory()
    {
        Catalog catalog;
        catalog.setApps({make(QStringLiteral("a"), QStringLiteral("Firefox"), QStringLiteral("apt"), QStringLiteral("firefox"),
                              {QStringLiteral("Network")}, QStringLiteral("Web browser")),
                         make(QStringLiteral("b"), QStringLiteral("GIMP"), QStringLiteral("flatpak"), QStringLiteral("app/b"),
                              {QStringLiteral("Graphics")}, QStringLiteral("Image editor")),
                         make(QStringLiteral("c"), QStringLiteral("Krita"), QStringLiteral("flatpak"), QStringLiteral("app/c"),
                              {QStringLiteral("Graphics")}, QStringLiteral("Digital painting"))});
        CatalogFilter f;
        f.setSourceModel(&catalog);
        QCOMPARE(f.rowCount(), 3);
        f.setCategory(QStringLiteral("Graphics"));
        QCOMPARE(f.rowCount(), 2);
        f.setText(QStringLiteral("paint")); // matches the summary, any case
        QCOMPARE(f.rowCount(), 1);
        QCOMPARE(f.index(0, 0).data(Catalog::NameRole).toString(), QStringLiteral("Krita"));
        f.setCategory({});
        f.setText(QStringLiteral("FIRE"));
        QCOMPARE(f.rowCount(), 1);
    }

    void archiveTwinLeavesTheListing()
    {
        App flathub = listed(QStringLiteral("org.chocolate_doom.ChocolateDoom"), QStringLiteral("Chocolate Doom"));
        flathub.listed = true;
        App deb = make(QStringLiteral("org.chocolate_doom.Doom"), QStringLiteral("Chocolate  doom"), QStringLiteral("apt"),
                       QStringLiteral("chocolate-doom"));
        deb.listed = true;
        App heretic = make(QStringLiteral("org.chocolate_doom.Heretic"), QStringLiteral("Chocolate Heretic"),
                           QStringLiteral("apt"), QStringLiteral("chocolate-doom"));
        heretic.listed = true;
        QList<App> apps{deb, flathub, heretic};
        hideArchiveTwins(apps);
        QVERIFY(!apps.at(0).listed);
        QVERIFY(apps.at(1).listed);
        QVERIFY(apps.at(2).listed);

        // a Flathub app that is not listed itself takes nothing away
        apps = {deb, flathub};
        apps[1].listed = false;
        hideArchiveTwins(apps);
        QVERIFY(apps.at(0).listed);
    }

    // the listing rules of the Store: no picture or no summary, no entry
    void visibilityRules()
    {
        const QSet<QString> allow{QStringLiteral("org.kde.kcalc")};
        App ok = listed(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP"));
        QVERIFY(isListed(ok, allow));

        App noSummary = ok;
        noSummary.summary.clear();
        QVERIFY(!isListed(noSummary, allow));
        App noName = ok;
        noName.name.clear();
        QVERIFY(!isListed(noName, allow));
        App smallIcon = ok;
        smallIcon.iconSize = 32;
        QVERIFY(!isListed(smallIcon, allow));
        App themeName = ok; // a theme name nothing turned into a file
        themeName.icon = QStringLiteral("gimp");
        themeName.iconSize = 0;
        QVERIFY(!isListed(themeName, allow));
        App remote = ok; // remote icons from Flathub are 128 px
        remote.icon = QStringLiteral("https://dl.flathub.org/media/x/icon.png");
        remote.iconSize = 0;
        QVERIFY(isListed(remote, allow));
        App noRef = ok;
        noRef.ref.clear();
        QVERIFY(!isListed(noRef, allow));
        App hidden = ok;
        hidden.noDisplay = true;
        QVERIFY(!isListed(hidden, allow));

        // archive apps: a screenshot, or a place in the allow list
        App deb = ok;
        deb.id = QStringLiteral("org.kde.kcalc.desktop");
        deb.source = QStringLiteral("apt");
        deb.ref = QStringLiteral("kcalc");
        QVERIFY(isListed(deb, allow));
        deb.id = QStringLiteral("org.kde.other");
        QVERIFY(!isListed(deb, allow));
        deb.screenshots = {Screenshot{QStringLiteral("t"), QStringLiteral("f"), {}}};
        QVERIFY(isListed(deb, allow));

        // Raye's own apps are curated: a name and an icon are enough
        App raye = make(QStringLiteral("org.artcraft.Launcher"), QStringLiteral("ArtCraft"), QStringLiteral("raye"), QStringLiteral("artcraft-launcher"),
                        {}, QStringLiteral("Creative apps"));
        raye.icon = QStringLiteral("/usr/share/raye-store/icons/a.png");
        QVERIFY(isListed(raye, allow));
    }

    void hiddenLaunchers()
    {
        QVERIFY(!desktopEntryHidden("[Desktop Entry]\nName=GIMP\nExec=gimp\n"));
        QVERIFY(desktopEntryHidden("[Desktop Entry]\nName=X\nNoDisplay=true\n"));
        QVERIFY(desktopEntryHidden("[Desktop Entry]\nName=X\nHidden=true\n"));
        QVERIFY(desktopEntryHidden("[Desktop Entry]\nName=htop\nTerminal=true\n"));
        // keys of another group do not count
        QVERIFY(!desktopEntryHidden("[Desktop Entry]\nName=X\n[Desktop Action new]\nNoDisplay=true\n"));
        QVERIFY(!desktopEntryHidden("[Desktop Entry]\nNoDisplay=false\n"));
    }

    void iconPick()
    {
        using C = IconCandidate;
        const QList<C> all{{C::Stock, QStringLiteral("gimp"), 0},
                           {C::Remote, QStringLiteral("https://x/128.png"), 128},
                           {C::Cached, QStringLiteral("/c/64/gimp.png"), 64},
                           {C::Cached, QStringLiteral("/c/128/gimp.png"), 128},
                           {C::Cached, QStringLiteral("/c/48/gimp.png"), 48}};
        QCOMPARE(pickIcon(all).icon, QStringLiteral("/c/128/gimp.png"));
        QCOMPARE(pickIcon(all).size, 128);
        // 64 on disk beats the remote 128: no download, no wait
        const QList<C> no128{all.at(0), all.at(1), all.at(2), all.at(4)};
        QCOMPARE(pickIcon(no128).icon, QStringLiteral("/c/64/gimp.png"));
        const QList<C> remoteOnly{all.at(0), all.at(1), all.at(4)};
        QCOMPARE(pickIcon(remoteOnly).icon, QStringLiteral("https://x/128.png"));
        // a 256 on disk is fine too; 128 still wins when there is one
        QCOMPARE(pickIcon({{C::Local, QStringLiteral("/l/256.png"), 256}}).size, 256);
        QCOMPARE(pickIcon({{C::Local, QStringLiteral("/l/256.png"), 256}, all.at(3)}).icon, QStringLiteral("/c/128/gimp.png"));
        // only a theme name: kept, size unknown
        const IconChoice stock = pickIcon({all.at(0)});
        QCOMPARE(stock.icon, QStringLiteral("gimp"));
        QCOMPARE(stock.size, 0);
        QVERIFY(pickIcon({}).icon.isEmpty());

        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("icons/hicolor/48x48/apps")));
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("icons/hicolor/128x128/apps")));
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("icons/hicolor/scalable/apps")));
        auto touch = [&](const QString &rel) {
            QFile f(dir.path() + QLatin1Char('/') + rel);
            QVERIFY(f.open(QIODevice::WriteOnly));
        };
        touch(QStringLiteral("icons/hicolor/48x48/apps/small.png"));
        touch(QStringLiteral("icons/hicolor/128x128/apps/kcalc.png"));
        touch(QStringLiteral("icons/hicolor/scalable/apps/krita.svg"));
        const QStringList roots{QStringLiteral("/nowhere"), dir.path()};
        QCOMPARE(resolveStockIcon(QStringLiteral("kcalc"), roots).icon, dir.path() + QStringLiteral("/icons/hicolor/128x128/apps/kcalc.png"));
        QCOMPARE(resolveStockIcon(QStringLiteral("kcalc"), roots).size, 128);
        QVERIFY(resolveStockIcon(QStringLiteral("krita"), roots).size >= 64);
        QVERIFY(resolveStockIcon(QStringLiteral("small"), roots).icon.isEmpty());
        QVERIFY(resolveStockIcon(QStringLiteral("../../etc/passwd"), roots).icon.isEmpty());
        QVERIFY(resolveStockIcon(QString(), roots).icon.isEmpty());
    }

    void ratingAverage()
    {
        // star0 means "no stars given": not a rating
        const Rating r = parseRating(QJsonDocument::fromJson(
            "{\"star0\":50,\"star1\":1,\"star2\":1,\"star3\":2,\"star4\":6,\"star5\":10,\"total\":70}").object());
        QCOMPARE(r.count, 20);
        QCOMPARE(r.histogram, (QList<int>{1, 1, 2, 6, 10}));
        QVERIFY(qAbs(r.average - 83.0 / 20.0) < 1e-9);
        const Rating none = parseRating(QJsonDocument::fromJson("{\"total\":12}").object());
        QCOMPARE(none.count, 0);
        QCOMPARE(none.average, 0.0);
        const Rating odd = parseRating(QJsonDocument::fromJson("{\"star1\":-4,\"star5\":\"x\",\"star3\":3}").object());
        QCOMPARE(odd.count, 3);
        QCOMPARE(odd.average, 3.0);
    }

    void feedsFromTheCache()
    {
        const Feeds f = loadFeeds(fixture("feeds"));
        QCOMPARE(f.lists.value(QStringLiteral("popular")).value(0), QStringLiteral("org.mozilla.firefox"));
        QCOMPARE(f.lists.value(QStringLiteral("popular")).size(), 6); // the entry without id and the string are skipped
        QCOMPARE(f.lists.value(QStringLiteral("trending")),
                 (QStringList{QStringLiteral("io.github.newthing.App"), QStringLiteral("com.valvesoftware.Steam")}));
        QCOMPARE(f.apps.value(QStringLiteral("org.mozilla.firefox")).installs, 412000);
        QVERIFY(f.apps.value(QStringLiteral("org.mozilla.firefox")).verified);
        QCOMPARE(f.apps.value(QStringLiteral("io.github.newthing.App")).trending, 9.5);
        // dates in seconds, in milliseconds or as text: newest wins
        QCOMPARE(f.apps.value(QStringLiteral("org.gimp.GIMP")).updatedAt, 1793400000);
        QCOMPARE(f.apps.value(QStringLiteral("io.github.newthing.App")).updatedAt,
                 QDateTime::fromString(QStringLiteral("2026-10-28T10:00:00Z"), Qt::ISODate).toSecsSinceEpoch());
        QCOMPARE(f.pickOfTheDay, QStringLiteral("org.kde.krita"));
        QCOMPARE(f.picksOfTheWeek, (QStringList{QStringLiteral("org.gimp.GIMP"), QStringLiteral("org.videolan.VLC")}));
        // ".desktop" is dropped from rating keys
        QCOMPARE(f.ratings.value(QStringLiteral("org.videolan.VLC")).count, 4);
        QCOMPARE(f.ratings.value(QStringLiteral("org.gimp.GIMP")).count, 20);
        QVERIFY(!f.ratings.contains(QStringLiteral("broken")));
    }

    void feedCacheMissingOrGarbled()
    {
        const Feeds none = loadFeeds(QStringLiteral("/nonexistent/raye-store/feeds"));
        QVERIFY(none.apps.isEmpty());
        QVERIFY(none.ratings.isEmpty());
        QVERIFY(none.pickOfTheDay.isEmpty());

        const Feeds bad = loadFeeds(fixture("feeds-garbled"));
        QVERIFY(bad.lists.value(QStringLiteral("popular")).isEmpty());
        QVERIFY(bad.ratings.isEmpty());
        QVERIFY(bad.pickOfTheDay.isEmpty());
        // a good file next to broken ones still counts
        QCOMPARE(bad.apps.value(QStringLiteral("org.kde.krita")).installs, 500);

        // the catalog with no feeds: still sorted, by name in the end
        Catalog catalog;
        catalog.setFeeds(none);
        catalog.setApps({listed(QStringLiteral("b"), QStringLiteral("Beta")), listed(QStringLiteral("a"), QStringLiteral("Alpha"))});
        CatalogFilter filter;
        filter.setSourceModel(&catalog);
        QCOMPARE(names(filter), (QStringList{QStringLiteral("Alpha"), QStringLiteral("Beta")}));
        QVERIFY(catalog.feed(QStringLiteral("popular")).isEmpty());
    }

    void aptRankAndCuration()
    {
        const auto rank = parseAptRank("# package\tinstalls\nfirefox\t200000\nbroken line\nvlc\t50000\nneg\t-3\n\nkcalc\t1000\n");
        QCOMPARE(rank.size(), 3);
        QCOMPARE(rank.value(QStringLiteral("vlc")), 50000);
        QVERIFY(!rank.contains(QStringLiteral("neg")));

        QCOMPARE(parseCurationAllow("{\"allow\":[\"org.kde.kcalc\",3,\"org.kde.kate.desktop\"],\"hero\":[]}"),
                 (QSet<QString>{QStringLiteral("org.kde.kcalc"), QStringLiteral("org.kde.kate")}));
        QVERIFY(parseCurationAllow("nope").isEmpty());

        QList<App> apps{listed(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP")),
                        make(QStringLiteral("org.kde.kcalc.desktop"), QStringLiteral("KCalc"), QStringLiteral("apt"), QStringLiteral("kcalc")),
                        make(QStringLiteral("org.videolan.vlc"), QStringLiteral("VLC"), QStringLiteral("apt"), QStringLiteral("vlc"))};
        apps[0].categories = {QStringLiteral("Graphics")};
        apps[2].categories = {QStringLiteral("AudioVideo"), QStringLiteral("Game")};
        Feeds feeds = loadFeeds(fixture("feeds"));
        applyFeeds(apps, feeds, rank);
        QCOMPARE(apps[0].popularity, 120000);
        QVERIFY(apps[0].verified);
        QCOMPARE(apps[0].updatedAt, 1793400000);
        QCOMPARE(apps[0].ratingCount, 20);
        QCOMPARE(apps[0].kind(), QStringLiteral("app"));
        QCOMPARE(apps[2].kind(), QStringLiteral("game"));
        // archive counts scaled so the top archive app sits with the top
        // Flathub app (412000): vlc 50000 of 200000 -> a quarter
        QCOMPARE(apps[2].popularity, 103000);
        QCOMPARE(apps[1].popularity, 2060);
    }

    void rankingOrder()
    {
        Catalog catalog;
        App firefox = listed(QStringLiteral("org.mozilla.firefox"), QStringLiteral("Firefox"), 412000);
        App gimp = listed(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP"), 120000);
        gimp.verified = true;
        App krita = listed(QStringLiteral("org.kde.krita"), QStringLiteral("Krita"), 120000); // same count, not verified
        App zed = listed(QStringLiteral("dev.zed.Zed"), QStringLiteral("Zed"), 500);
        App steam = listed(QStringLiteral("com.valvesoftware.Steam"), QStringLiteral("Steam"), 250000);
        steam.categories = {QStringLiteral("Game")};
        App hidden = listed(QStringLiteral("x.hidden"), QStringLiteral("Hidden"), 999999);
        hidden.listed = false;
        firefox.addedAt = 100;
        gimp.updatedAt = 500;
        zed.addedAt = 900;
        krita.ratingAvg = 4.8;
        krita.ratingCount = 40;
        gimp.ratingAvg = 4.1;
        gimp.ratingCount = 300;
        zed.ratingAvg = 5.0; // too few to rank first
        zed.ratingCount = 3;
        catalog.setApps({zed, krita, hidden, gimp, firefox, steam});

        CatalogFilter f;
        f.setSourceModel(&catalog);
        QCOMPARE(names(f), (QStringList{QStringLiteral("Firefox"), QStringLiteral("Steam"), QStringLiteral("GIMP"),
                                        QStringLiteral("Krita"), QStringLiteral("Zed")}));
        QSignalSpy count(&f, &CatalogFilter::countChanged);
        f.setKind(QStringLiteral("game"));
        QCOMPARE(names(f), QStringList{QStringLiteral("Steam")});
        QVERIFY(count.count() >= 1);
        QCOMPARE(f.count(), 1);
        f.setKind(QStringLiteral("app"));
        f.setLimit(2);
        QCOMPARE(names(f), (QStringList{QStringLiteral("Firefox"), QStringLiteral("GIMP")}));
        f.setLimit(0);
        f.setSortBy(QStringLiteral("name"));
        QCOMPARE(names(f), (QStringList{QStringLiteral("Firefox"), QStringLiteral("GIMP"), QStringLiteral("Krita"), QStringLiteral("Zed")}));
        f.setSortBy(QStringLiteral("newest"));
        QCOMPARE(names(f), (QStringList{QStringLiteral("Zed"), QStringLiteral("GIMP"), QStringLiteral("Firefox"), QStringLiteral("Krita")}));
        f.setSortBy(QStringLiteral("rating"));
        QCOMPARE(names(f), (QStringList{QStringLiteral("Krita"), QStringLiteral("GIMP"), QStringLiteral("Firefox"), QStringLiteral("Zed")}));
        f.setMinRatingCount(10);
        QCOMPARE(names(f), (QStringList{QStringLiteral("Krita"), QStringLiteral("GIMP")}));
        f.setMinRatingCount(0);
        f.setSortBy(QStringLiteral("bogus")); // unknown: popularity
        QCOMPARE(f.sortBy(), QStringLiteral("popularity"));

        // a hidden app: only by its exact name, with system packages shown
        f.setKind({});
        f.setText(QStringLiteral("hidden"));
        QCOMPARE(f.rowCount(), 0);
        f.setShowSystemPackages(true);
        QCOMPARE(names(f), QStringList{QStringLiteral("Hidden")});
        f.setText(QStringLiteral("hidd"));
        QCOMPARE(f.rowCount(), 0);
        f.setText({});
        QVERIFY(!names(f).contains(QStringLiteral("Hidden")));

        // the library shows what is installed, listed or not
        catalog.setInstalled({hidden.ref});
        CatalogFilter lib;
        lib.setSourceModel(&catalog);
        lib.setInstalledOnly(true);
        QCOMPARE(names(lib), QStringList{QStringLiteral("Hidden")});
    }

    void curatedIdsOrder()
    {
        Catalog catalog;
        catalog.setApps({listed(QStringLiteral("a"), QStringLiteral("Alpha"), 10), listed(QStringLiteral("b"), QStringLiteral("Beta"), 30),
                         listed(QStringLiteral("c.desktop"), QStringLiteral("Gamma"), 20), listed(QStringLiteral("d"), QStringLiteral("Delta"), 40)});
        CatalogFilter f;
        f.setSourceModel(&catalog);
        f.setIds({QStringLiteral("c"), QStringLiteral("missing"), QStringLiteral("a"), QStringLiteral("b")});
        QCOMPARE(names(f), (QStringList{QStringLiteral("Gamma"), QStringLiteral("Alpha"), QStringLiteral("Beta")}));
        f.setLimit(2);
        QCOMPARE(names(f), (QStringList{QStringLiteral("Gamma"), QStringLiteral("Alpha")}));
        // the list follows the model when the apps change
        catalog.setApps({listed(QStringLiteral("b"), QStringLiteral("Beta"), 30), listed(QStringLiteral("a"), QStringLiteral("Alpha"), 10)});
        QCOMPARE(names(f), (QStringList{QStringLiteral("Alpha"), QStringLiteral("Beta")}));
    }

    void foldingAndScore()
    {
        QCOMPARE(foldText(QStringLiteral("Élève ŒUVRE ﬁn Straße")), QStringLiteral("eleve oeuvre fin strasse"));

        App gimp = listed(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP"), 0, QStringLiteral("Create images and edit photographs"));
        gimp.keywords = {QStringLiteral("photoshop"), QStringLiteral("paint")};
        App fire = listed(QStringLiteral("org.mozilla.firefox"), QStringLiteral("Firefox Web Browser"), 0, QStringLiteral("Fast, private browsing"));
        App eleve = listed(QStringLiteral("x.eleve"), QStringLiteral("Élève Plus"), 0, QStringLiteral("Cahier de l'élève"));

        const int exact = searchScore(QStringLiteral("gimp"), gimp);
        const int prefix = searchScore(QStringLiteral("fire"), fire);
        const int word = searchScore(QStringLiteral("web"), fire);
        const int typo = searchScore(QStringLiteral("firefx"), fire);
        const int keyword = searchScore(QStringLiteral("photoshop"), gimp);
        const int summary = searchScore(QStringLiteral("edit photo"), gimp);
        QVERIFY(exact > prefix);
        QVERIFY(prefix > word);
        QVERIFY(word > typo);
        QVERIFY(typo > keyword);
        QVERIFY(keyword > summary);
        QVERIFY(summary > 0);
        QCOMPARE(searchScore(QStringLiteral("GIMP"), gimp), exact);
        QCOMPARE(searchScore(QStringLiteral("eleve"), eleve), searchScore(QStringLiteral("Élève"), eleve));
        QVERIFY(searchScore(QStringLiteral("eleve plus"), eleve) > searchScore(QStringLiteral("eleve"), eleve));
        // one typo is fine from 5 letters, not under
        QVERIFY(searchScore(QStringLiteral("fierfox"), fire) > 0); // two letters swapped
        QCOMPARE(searchScore(QStringLiteral("gimq"), gimp), 0);
        QCOMPARE(searchScore(QStringLiteral("firfx"), fire), 0); // two edits
        QVERIFY(searchScore(QStringLiteral("firfo"), fire) > 0); // typing, one typo in the start
        QCOMPARE(searchScore(QStringLiteral("zzz"), fire), 0);
        QCOMPARE(searchScore(QString(), fire), 0);
    }

    void searchRanking()
    {
        Catalog catalog;
        App fire = listed(QStringLiteral("org.mozilla.firefox"), QStringLiteral("Firefox"), 400000, QStringLiteral("Web browser"));
        App fireRpg = listed(QStringLiteral("x.firerpg"), QStringLiteral("Fire Emblem Fan Game"), 10, QStringLiteral("A game"));
        App ember = listed(QStringLiteral("x.ember"), QStringLiteral("Ember"), 900000, QStringLiteral("Light a fire in the camp"));
        App fires = listed(QStringLiteral("x.fires"), QStringLiteral("Fire"), 5, QStringLiteral("Exact name"));
        catalog.setApps({ember, fire, fireRpg, fires});
        CatalogFilter f;
        f.setSourceModel(&catalog);
        f.setText(QStringLiteral("fire"));
        // exact, then prefix (ties by popularity), then the summary
        QCOMPARE(names(f), (QStringList{QStringLiteral("Fire"), QStringLiteral("Firefox"), QStringLiteral("Fire Emblem Fan Game"),
                                        QStringLiteral("Ember")}));
        f.setText(QStringLiteral("firefoz"));
        QCOMPARE(names(f), QStringList{QStringLiteral("Firefox")});
    }

    void newRolesInFind()
    {
        Catalog catalog;
        App a = listed(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP"), 7);
        a.screenshots = {Screenshot{QStringLiteral("t1"), QStringLiteral("f1"), QStringLiteral("Main window")}};
        a.screenshot = QStringLiteral("f1");
        a.releases = {Release{QStringLiteral("3.0"), QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("<p>New</p>")}};
        a.homepage = QStringLiteral("https://gimp.org");
        a.ratingHistogram = {1, 2, 3, 4, 5};
        a.updatedAt = 1700000000;
        catalog.setApps({a});
        const QVariantMap m = catalog.find(QStringLiteral("org.gimp.GIMP"));
        QCOMPARE(m.value(QStringLiteral("screenshot")).toString(), QStringLiteral("f1"));
        const QVariantMap shot = m.value(QStringLiteral("screenshots")).toList().value(0).toMap();
        QCOMPARE(shot.value(QStringLiteral("thumb")).toString(), QStringLiteral("t1"));
        QCOMPARE(shot.value(QStringLiteral("caption")).toString(), QStringLiteral("Main window"));
        QCOMPARE(m.value(QStringLiteral("releases")).toList().value(0).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("3.0"));
        QCOMPARE(m.value(QStringLiteral("urls")).toMap().value(QStringLiteral("homepage")).toString(), QStringLiteral("https://gimp.org"));
        QCOMPARE(m.value(QStringLiteral("ratingHistogram")).toList().size(), 5);
        QCOMPARE(m.value(QStringLiteral("popularity")).toLongLong(), 7);
        QCOMPARE(m.value(QStringLiteral("kind")).toString(), QStringLiteral("app"));
        QCOMPARE(m.value(QStringLiteral("updatedAt")).toDateTime().toSecsSinceEpoch(), 1700000000);
        QVERIFY(!m.value(QStringLiteral("addedAt")).toDateTime().isValid());
        QVERIFY(m.value(QStringLiteral("listed")).toBool());
    }

    void feedIds()
    {
        Catalog catalog;
        QSignalSpy spy(&catalog, &Catalog::feedsChanged);
        catalog.setFeeds(loadFeeds(fixture("feeds")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(catalog.pickOfTheDay(), QStringLiteral("org.kde.krita"));
        QCOMPARE(catalog.feed(QStringLiteral("trending")).value(0), QStringLiteral("io.github.newthing.App"));
        QCOMPARE(catalog.feed(QStringLiteral("picks-of-the-week")).size(), 2);
        QVERIFY(catalog.feed(QStringLiteral("nothing")).isEmpty());
    }

    // the library follows installs and removals while it is shown
    void libraryFollowsInstalls()
    {
        Catalog catalog;
        catalog.setApps({listed(QStringLiteral("a"), QStringLiteral("Alpha"), 10), listed(QStringLiteral("b"), QStringLiteral("Beta"), 30)});
        CatalogFilter lib;
        lib.setSourceModel(&catalog);
        lib.setSourceModel(&catalog); // twice: still connected
        lib.setInstalledOnly(true);
        QCOMPARE(lib.rowCount(), 0);
        catalog.setInstalled({QStringLiteral("app/a")});
        QTRY_COMPARE(names(lib), QStringList{QStringLiteral("Alpha")});
        catalog.setInstalled({QStringLiteral("app/a"), QStringLiteral("app/b")});
        QTRY_COMPARE(names(lib), (QStringList{QStringLiteral("Beta"), QStringLiteral("Alpha")}));
        catalog.setInstalled({});
        QTRY_COMPARE(lib.rowCount(), 0);
    }

    // Firefox ships as a system package; its page is Flathub's
    void systemCopyIsInstalled()
    {
        App firefox = listed(QStringLiteral("org.mozilla.firefox"), QStringLiteral("Firefox"));
        firefox.ref = QStringLiteral("app/org.mozilla.firefox/x86_64/stable");
        firefox.desktopId = QStringLiteral("org.mozilla.firefox.desktop");
        App kate = listed(QStringLiteral("org.kde.kate"), QStringLiteral("Kate"));
        kate.desktopId = QStringLiteral("org.kde.kate.desktop");
        App vlc = listed(QStringLiteral("org.videolan.VLC"), QStringLiteral("VLC"));
        App office = listed(QStringLiteral("org.libreoffice.LibreOffice"), QStringLiteral("LibreOffice"));
        App gimp = listed(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP"));
        // same last word, another app
        App other = listed(QStringLiteral("io.github.someone.Files"), QStringLiteral("Quick Share"));
        App keep = listed(QStringLiteral("io.github.someone.Firefoxy"), QStringLiteral("Firefox"));

        const QList<SystemLauncher> launchers{{QStringLiteral("firefox"), QStringLiteral("Firefox")},
                                              {QStringLiteral("org.kde.kate"), QStringLiteral("Kate")},
                                              {QStringLiteral("vlc"), QStringLiteral("VLC media player")},
                                              {QStringLiteral("libreoffice-startcenter"), QStringLiteral("LibreOffice")},
                                              {QStringLiteral("files"), QStringLiteral("Files")}};
        QCOMPARE(systemLauncherFor(firefox, launchers), QStringLiteral("firefox.desktop"));
        QCOMPARE(systemLauncherFor(kate, launchers), QStringLiteral("org.kde.kate.desktop"));
        QCOMPARE(systemLauncherFor(vlc, launchers), QStringLiteral("vlc.desktop"));
        QCOMPARE(systemLauncherFor(office, launchers), QStringLiteral("libreoffice-startcenter.desktop"));
        QCOMPARE(systemLauncherFor(gimp, launchers), QString());
        QCOMPARE(systemLauncherFor(other, launchers), QString());
        QCOMPARE(systemLauncherFor(keep, launchers), QString());

        Catalog catalog;
        catalog.setApps({firefox, gimp});
        QVERIFY(!catalog.isInstalled(firefox.ref));
        QSignalSpy changed(&catalog, &Catalog::installedChanged);
        catalog.setSystemLaunchers(launchers);
        QCOMPARE(changed.count(), 1);
        QVERIFY(catalog.isInstalled(firefox.ref));
        QVERIFY(!catalog.ownsInstall(firefox.ref));
        QCOMPARE(catalog.systemCopy(firefox.ref), QStringLiteral("firefox.desktop"));
        QVERIFY(!catalog.isInstalled(gimp.ref));
        // what the rows and the Library see
        QVERIFY(catalog.index(0).data(Catalog::InstalledRole).toBool());
        QCOMPARE(catalog.find(QStringLiteral("org.mozilla.firefox")).value(QStringLiteral("systemCopy")).toString(),
                 QStringLiteral("firefox.desktop"));
        // installed from the listing as well: then it is that one
        catalog.setInstalled({firefox.ref});
        QVERIFY(catalog.ownsInstall(firefox.ref));
        QCOMPARE(catalog.systemCopy(firefox.ref), QString());
    }

    void readsSystemLaunchers()
    {
        QTemporaryDir first, second;
        const auto write = [](const QTemporaryDir &dir, const char *name, const QByteArray &text) {
            QFile f(dir.filePath(QLatin1String(name)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
        };
        write(first, "firefox.desktop", "[Desktop Entry]\nType=Application\nName=Firefox\nName[fr]=Renard\nExec=firefox\n[Desktop Action new]\nName=New Window\n");
        write(second, "firefox.desktop", "[Desktop Entry]\nName=Older\n");
        write(second, "hidden.desktop", "[Desktop Entry]\nName=Hidden\nNoDisplay=true\n");
        write(second, "shell.desktop", "[Desktop Entry]\nName=Shell\nTerminal=true\n");
        write(second, "notes.txt", "Name=Not a launcher\n");
        const QList<SystemLauncher> found = readSystemLaunchers({first.path(), second.path(), QStringLiteral("/nowhere")});
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().id, QStringLiteral("firefox"));
        QCOMPARE(found.first().name, QStringLiteral("Firefox"));
    }

    // the flyout: a few rows, Up and Down, Enter
    void suggestions()
    {
        Catalog catalog;
        QList<App> apps;
        for (int n = 0; n < 12; ++n)
            apps.append(listed(QStringLiteral("org.test.Video%1").arg(n), QStringLiteral("Video %1").arg(n), 100 - n));
        apps.append(listed(QStringLiteral("org.test.Writer"), QStringLiteral("Writer"), 5000));
        catalog.setApps(apps);
        SearchSuggestions s;
        s.setSource(&catalog);
        QSignalSpy opened(&s, &SearchSuggestions::openApp);
        QSignalSpy results(&s, &SearchSuggestions::showResults);

        // nothing until the typing pauses, then at most eight rows
        s.type(QStringLiteral("video"));
        QVERIFY(!s.shown());
        QTRY_VERIFY(s.shown());
        QCOMPARE(s.query(), QStringLiteral("video"));
        QCOMPARE(s.count(), SearchSuggestions::maxRows);
        QCOMPARE(s.current(), -1);

        // Down walks the rows, then "See all", then back to the top
        s.down();
        QCOMPARE(s.current(), 0);
        for (int n = 0; n < SearchSuggestions::maxRows; ++n)
            s.down();
        QCOMPARE(s.current(), s.count());
        s.down();
        QCOMPARE(s.current(), 0);
        s.up();
        QCOMPARE(s.current(), s.count());

        // Enter on "See all" and with no row: the results page
        s.accept();
        QCOMPARE(results.count(), 1);
        QCOMPARE(results.first().first().toString(), QStringLiteral("video"));
        QVERIFY(!s.shown());

        // Enter on a row opens that app
        s.type(QStringLiteral("writ"));
        QTRY_VERIFY(s.shown());
        QCOMPARE(s.count(), 1);
        s.down();
        s.accept();
        QCOMPARE(opened.count(), 1);
        QCOMPARE(opened.first().first().toString(), QStringLiteral("org.test.Writer"));

        // Enter faster than the rows: what was typed, not what was shown
        s.type(QStringLiteral("video"));
        QTRY_VERIFY(s.shown());
        s.type(QStringLiteral("writer"));
        s.accept();
        QCOMPARE(results.last().first().toString(), QStringLiteral("writer"));
        QVERIFY(!s.shown());

        // the page changed and emptied the box: Enter has nothing to search
        s.type(QStringLiteral("video"));
        QTRY_VERIFY(s.shown());
        s.forget();
        const int before = results.count();
        s.accept();
        QCOMPARE(results.count(), before);

        // Esc, and emptying the box, close it
        s.type(QStringLiteral("video"));
        QTRY_VERIFY(s.shown());
        s.dismiss();
        QVERIFY(!s.shown());
        s.type(QStringLiteral("video"));
        s.type(QString());
        QTest::qWait(SearchSuggestions::debounceMs * 2);
        QVERIFY(!s.shown());
    }

    // raye-storerc: the update timer reads it with kreadconfig6
    void settingsFile()
    {
        QTemporaryDir config;
        const QString path = config.filePath(QStringLiteral("raye-storerc"));
        {
            StoreSettings settings(path);
            QVERIFY(settings.autoUpdate());
            QVERIFY(!settings.showSystemPackages());
            QSignalSpy changed(&settings, &StoreSettings::changed);
            settings.setAutoUpdate(false);
            settings.setShowSystemPackages(true);
            QCOMPARE(changed.count(), 2);
        }
        StoreSettings again(path);
        QVERIFY(!again.autoUpdate());
        QVERIFY(again.showSystemPackages());

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        QVERIFY2(text.contains("[Updates]") && text.contains("Automatic=false"), text.constData());

        const QString reader = QStandardPaths::findExecutable(QStringLiteral("kreadconfig6"));
        if (reader.isEmpty())
            QSKIP("kreadconfig6 is not installed here");
        QProcess read;
        read.start(reader, {QStringLiteral("--file"), path, QStringLiteral("--group"), QStringLiteral("Updates"),
                            QStringLiteral("--key"), QStringLiteral("Automatic"), QStringLiteral("--default"), QStringLiteral("true")});
        QVERIFY(read.waitForFinished(10000));
        QCOMPARE(read.readAllStandardOutput().trimmed(), QByteArrayLiteral("false"));
    }

    void commands()
    {
        const QString ref = QStringLiteral("app/org.gimp.GIMP/x86_64/stable");
        QCOMPARE(Installer::command(QStringLiteral("install"), QStringLiteral("flatpak"), ref),
                 (QStringList{QStringLiteral("flatpak"), QStringLiteral("install"), QStringLiteral("--system"), QStringLiteral("-y"),
                              QStringLiteral("--noninteractive"), QStringLiteral("flathub"), ref}));
        QCOMPARE(Installer::command(QStringLiteral("remove"), QStringLiteral("apt"), QStringLiteral("gimp")),
                 (QStringList{QStringLiteral("pkgcli"), QStringLiteral("-y"), QStringLiteral("remove"), QStringLiteral("gimp")}));
        QVERIFY(Installer::command(QStringLiteral("explode"), QStringLiteral("apt"), QStringLiteral("x")).isEmpty());
    }

    // apps Raye offers itself (not on Flathub), from extras.json
    void extrasCatalog()
    {
        // plain string literals: moc does not read raw strings well
        const QByteArray json =
            "{\"apps\":["
            "{\"id\":\"ai.storyteller.artcraft-launcher\",\"name\":\"ArtCraft\",\"summary\":\"Creative apps\",\"package\":\"artcraft-launcher\",\"url\":\"https://example.org/a.deb\",\"sha256\":\"94e7e8c30ec9d50e7c2cf6df32f47dbb9f2863edde923d4ae9e8bbc54da47eb0\",\"icon\":\"/usr/share/raye-store/icons/a.png\",\"categories\":[\"Graphics\"],\"preview\":true},"
            "{\"id\":\"bad.http\",\"name\":\"Plain\",\"package\":\"p\",\"url\":\"http://example.org/p.deb\",\"sha256\":\"94e7e8c30ec9d50e7c2cf6df32f47dbb9f2863edde923d4ae9e8bbc54da47eb0\"},"
            "{\"id\":\"bad.hash\",\"name\":\"NoHash\",\"package\":\"q\",\"url\":\"https://example.org/q.deb\",\"sha256\":\"1234\"},"
            "{\"id\":\"bad.package\",\"name\":\"Odd\",\"package\":\"rm -rf\",\"url\":\"https://example.org/r.deb\",\"sha256\":\"94e7e8c30ec9d50e7c2cf6df32f47dbb9f2863edde923d4ae9e8bbc54da47eb0\"}"
            "]}";
        const QList<App> apps = parseExtras(json);
        QCOMPARE(apps.size(), 1);
        QCOMPARE(apps.first().source, QStringLiteral("raye"));
        QCOMPARE(apps.first().ref, QStringLiteral("artcraft-launcher"));
        QVERIFY(apps.first().preview);
        QCOMPARE(apps.first().categories, QStringList{QStringLiteral("Graphics")});
        QVERIFY(parseExtras("not json").isEmpty());
    }

    // an entry can say things in both languages; plain text is for both
    void extrasInFrench()
    {
        const QByteArray json =
            "{\"apps\":[{\"id\":\"com.teamviewer.TeamViewer\",\"name\":\"TeamViewer\","
            "\"summary\":{\"en\":\"Remote help\",\"fr\":\"Aide \u00e0 distance\"},"
            "\"description\":{\"en\":\"<p>Help</p>\"},\"homepage\":\"https://www.teamviewer.com/\","
            "\"keywords\":[\"remote\",\"support\"],\"desktop\":\"com.teamviewer.TeamViewer.desktop\",\"package\":\"teamviewer\",\"url\":\"https://example.org/t.deb\","
            "\"sha256\":\"94e7e8c30ec9d50e7c2cf6df32f47dbb9f2863edde923d4ae9e8bbc54da47eb0\"}]}";
        const App en = parseExtras(json).value(0);
        QCOMPARE(en.summary, QStringLiteral("Remote help"));
        QCOMPARE(en.homepage, QStringLiteral("https://www.teamviewer.com/"));
        QCOMPARE(en.keywords, (QStringList{QStringLiteral("remote"), QStringLiteral("support")}));
        QCOMPARE(en.desktopId, QStringLiteral("com.teamviewer.TeamViewer.desktop"));
        // a launcher name is a file name, never a path
        QVERIFY(parseExtras(QByteArray(json).replace("com.teamviewer.TeamViewer.desktop", "../x.desktop")).value(0).desktopId.isEmpty());
        const App fr = parseExtras(json, true).value(0);
        QCOMPARE(fr.summary, QStringLiteral("Aide \u00e0 distance"));
        // no French: the English one
        QCOMPARE(fr.description, QStringLiteral("<p>Help</p>"));
    }

    void extraCommands()
    {
        QCOMPARE(Installer::command(QStringLiteral("install"), QStringLiteral("raye"), QStringLiteral("artcraft-launcher")),
                 (QStringList{QStringLiteral("pkexec"), QStringLiteral("/usr/libexec/raye-store-extra"), QStringLiteral("install"),
                              QStringLiteral("artcraft-launcher")}));
        QCOMPARE(Installer::command(QStringLiteral("remove"), QStringLiteral("raye"), QStringLiteral("artcraft-launcher")),
                 (QStringList{QStringLiteral("pkexec"), QStringLiteral("/usr/libexec/raye-store-extra"), QStringLiteral("remove"),
                              QStringLiteral("artcraft-launcher")}));
    }
};

QTEST_GUILESS_MAIN(TestStore)
#include "test_store.moc"
