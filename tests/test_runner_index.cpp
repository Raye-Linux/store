// SPDX-License-Identifier: GPL-3.0-or-later
// The Start menu runner's own small copy of the catalog: only what the
// search reads, kept on disk so a new start does not read AppStream again.
#include "runnermatch.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace
{

App fullApp(const QString &id, const QString &name)
{
    App a;
    a.id = id;
    a.name = name;
    a.summary = QStringLiteral("Summary of ") + name;
    a.description = QStringLiteral("<p>A long description nobody searches.</p>");
    a.icon = QStringLiteral("/var/lib/flatpak/appstream/icons/") + id + QStringLiteral(".png");
    a.iconSize = 128;
    a.screenshot = QStringLiteral("https://example.com/shot.png");
    a.screenshots = {Screenshot{QStringLiteral("t"), QStringLiteral("f"), QStringLiteral("c")}};
    a.releases = {Release{QStringLiteral("1.0"), QDateTime::currentDateTimeUtc(), QStringLiteral("notes")}};
    a.developer = QStringLiteral("Someone");
    a.homepage = QStringLiteral("https://example.com");
    a.source = QStringLiteral("flatpak");
    a.ref = QStringLiteral("app/") + id + QStringLiteral("/x86_64/stable");
    a.categories = {QStringLiteral("Network")};
    a.keywords = {QStringLiteral("chat"), QStringLiteral("voice")};
    a.popularity = 42;
    return a;
}

void touch(const QString &path, qint64 secondsAgo)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(-secondsAgo), QFileDevice::FileModificationTime));
}

} // namespace

class TestRunnerIndex : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void keepsOnlyWhatTheSearchReads()
    {
        App hidden = fullApp(QStringLiteral("com.example.Hidden"), QStringLiteral("Hidden"));
        hidden.listed = false;
        App console = fullApp(QStringLiteral("com.example.Tool"), QStringLiteral("Tool"));
        console.noDisplay = true;
        const QList<App> apps{fullApp(QStringLiteral("com.discordapp.Discord"), QStringLiteral("Discord")), hidden, console};

        const QList<App> entries = searchEntries(apps);
        QCOMPARE(entries.size(), 1); // nothing the search would never offer
        const App &e = entries.first();
        QCOMPARE(e.id, QStringLiteral("com.discordapp.Discord"));
        QCOMPARE(e.name, QStringLiteral("Discord"));
        QCOMPARE(e.summary, QStringLiteral("Summary of Discord"));
        QCOMPARE(e.icon, QStringLiteral("/var/lib/flatpak/appstream/icons/com.discordapp.Discord.png"));
        QCOMPARE(e.ref, QStringLiteral("app/com.discordapp.Discord/x86_64/stable"));
        QCOMPARE(e.keywords, (QStringList{QStringLiteral("chat"), QStringLiteral("voice")}));
        QCOMPARE(e.popularity, qint64(42));
        QVERIFY(e.listed);
        QVERIFY(!e.noDisplay);
        // the heavy parts are gone
        QVERIFY(e.description.isEmpty());
        QVERIFY(e.screenshots.isEmpty());
        QVERIFY(e.releases.isEmpty());
        QVERIFY(e.screenshot.isEmpty());
        QVERIFY(e.developer.isEmpty());
        QVERIFY(e.homepage.isEmpty());
        QVERIFY(e.categories.isEmpty());

        // and the search finds the same thing in it
        const auto none = [](const QString &) { return false; };
        QCOMPARE(storeMatches(entries, none, QStringLiteral("discord")).size(), 1);
        QCOMPARE(storeMatches(entries, none, QStringLiteral("voice")).size(), 1);
    }

    void blockedGameNotOffered()
    {
        App game = fullApp(QStringLiteral("windows-title.fortnite"), QStringLiteral("Fortnite"));
        game.windowsState = QStringLiteral("blocked");
        game.ref.clear();
        const QList<App> entries = searchEntries({game, fullApp(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP"))});
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().name, QStringLiteral("GIMP"));
    }

    void indexRoundTrip()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("sub/search-index"));
        const QList<App> entries = searchEntries({fullApp(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP")),
                                                  fullApp(QStringLiteral("org.videolan.VLC"), QStringLiteral("VLC"))});
        QVERIFY(writeSearchIndex(file, QStringLiteral("stamp-1"), entries));

        const auto back = readSearchIndex(file, QStringLiteral("stamp-1"));
        QVERIFY(back.has_value());
        QCOMPARE(back->size(), 2);
        for (qsizetype i = 0; i < entries.size(); ++i) {
            QCOMPARE(back->at(i).id, entries.at(i).id);
            QCOMPARE(back->at(i).name, entries.at(i).name);
            QCOMPARE(back->at(i).summary, entries.at(i).summary);
            QCOMPARE(back->at(i).icon, entries.at(i).icon);
            QCOMPARE(back->at(i).ref, entries.at(i).ref);
            QCOMPARE(back->at(i).keywords, entries.at(i).keywords);
            QCOMPARE(back->at(i).popularity, entries.at(i).popularity);
        }
    }

    void staleOrBrokenIndexIsNotUsed()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("search-index"));
        QVERIFY(!readSearchIndex(file, QStringLiteral("s")).has_value()); // missing

        QVERIFY(writeSearchIndex(file, QStringLiteral("old"), searchEntries({fullApp(QStringLiteral("a.b.C"), QStringLiteral("C"))})));
        QVERIFY(!readSearchIndex(file, QStringLiteral("new")).has_value()); // other sources

        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadWrite));
        const QByteArray all = f.readAll();
        f.resize(all.size() / 2); // cut short
        f.close();
        QVERIFY(!readSearchIndex(file, QStringLiteral("old")).has_value());

        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("not an index at all");
        f.close();
        QVERIFY(!readSearchIndex(file, QStringLiteral("old")).has_value());
    }

    void stampFollowsTheSources()
    {
        QTemporaryDir dir;
        const QString flatpak = dir.filePath(QStringLiteral("flatpak"));
        QVERIFY(QDir().mkpath(flatpak + QStringLiteral("/flathub/x86_64/abc")));
        touch(flatpak + QStringLiteral("/flathub/x86_64/abc/appstream.xml.gz"), 100);
        const QString feeds = dir.filePath(QStringLiteral("feeds"));
        QVERIFY(QDir().mkpath(feeds));
        touch(feeds + QStringLiteral("/popular.json"), 100);
        const QString extras = dir.filePath(QStringLiteral("extras.json"));
        touch(extras, 100);
        const QString missing = dir.filePath(QStringLiteral("not-yet"));
        const QStringList roots{flatpak, feeds, extras, missing};

        const QString first = sourceStamp(roots, QStringLiteral("en_CA"));
        QCOMPARE(sourceStamp(roots, QStringLiteral("en_CA")), first); // nothing changed
        QVERIFY(sourceStamp(roots, QStringLiteral("fr_CA")) != first); // names come translated

        touch(feeds + QStringLiteral("/popular.json"), 10); // the feeds timer ran
        const QString second = sourceStamp(roots, QStringLiteral("en_CA"));
        QVERIFY(second != first);

        // a new AppStream checkout, three levels down
        QVERIFY(QDir().mkpath(flatpak + QStringLiteral("/flathub/x86_64/def")));
        const QString third = sourceStamp(roots, QStringLiteral("en_CA"));
        QVERIFY(third != second);

        touch(missing, 5); // a source that shows up
        QVERIFY(sourceStamp(roots, QStringLiteral("en_CA")) != third);
    }

    void defaultRootsCoverAppStreamAndFeeds()
    {
        const QStringList roots = defaultSourceRoots();
        QVERIFY(roots.contains(QStringLiteral("/var/lib/flatpak/appstream")));
        QVERIFY(roots.contains(QStringLiteral("/var/lib/swcatalog")));
        QVERIFY(roots.contains(QStringLiteral("/var/cache/raye-store/feeds")));
        QVERIFY(roots.contains(QStringLiteral("/usr/share/raye-store/extras.json")));
        QVERIFY(roots.contains(QStringLiteral("/usr/share/applications")));
    }
};

QTEST_GUILESS_MAIN(TestRunnerIndex)
#include "test_runner_index.moc"
