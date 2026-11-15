// SPDX-License-Identifier: GPL-3.0-or-later
// Installs that outlive the window, the download folders they leave,
// appstream:// links and the Start menu search entries.
#include "applink.h"
#include "background.h"
#include "catalog.h"
#include "cleanup.h"
#include "history.h"
#include "installer.h"
#include "runnermatch.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <unistd.h>

namespace
{

class FakeBackend : public JobBackend
{
    Q_OBJECT
public:
    using JobBackend::JobBackend;
    int cancels = 0;
    void start(const JobSpec &) override { }
    void cancel() override { ++cancels; }
    void checkUpdates() override { }
    void end(bool ok)
    {
        JobResult r;
        r.ok = ok;
        Q_EMIT finished(r);
    }
};

QVariantMap flatpakApp(const QString &id, const QString &name)
{
    return {{QStringLiteral("appId"), id}, {QStringLiteral("name"), name},
            {QStringLiteral("source"), QStringLiteral("flatpak")},
            {QStringLiteral("ref"), QStringLiteral("app/%1/x86_64/stable").arg(id)}};
}

App app(const QString &id, const QString &name, const QString &summary, qint64 popularity = 0)
{
    App a;
    a.id = id;
    a.name = name;
    a.summary = summary;
    a.source = QStringLiteral("flatpak");
    a.ref = QStringLiteral("app/%1/x86_64/stable").arg(id);
    a.icon = QStringLiteral("/var/lib/flatpak/appstream/icons/%1.png").arg(id);
    a.popularity = popularity;
    return a;
}

} // namespace

class TestBackground : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    FakeBackend *m_flatpak = nullptr;
    Installer *m_installer = nullptr;
    int m_cleanups = 0;

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-store");
    }

    void init()
    {
        m_flatpak = new FakeBackend;
        m_cleanups = 0;
        m_installer = new Installer({{QStringLiteral("flatpak"), m_flatpak}},
                                    new History(m_dir.filePath(QStringLiteral("history.json"))));
        m_installer->setCleanup([this] { ++m_cleanups; });
    }

    void cleanup()
    {
        delete m_installer;
        m_installer = nullptr;
    }

    // --- the window closes, the install goes on

    void closingWhenIdleQuits()
    {
        Background background(m_installer);
        QSignalSpy quit(&background, &Background::quitRequested);
        QVERIFY(!background.windowClosing());
        QCOMPARE(quit.count(), 1);
        QVERIFY(!background.hidden());
    }

    void closingWhileInstallingHides()
    {
        Background background(m_installer);
        background.setQuitDelay(0);
        QSignalSpy quit(&background, &Background::quitRequested);
        QSignalSpy kept(&background, &Background::keptRunning);
        m_installer->get(flatpakApp(QStringLiteral("com.discordapp.Discord"), QStringLiteral("Discord")));
        m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC"), QStringLiteral("VLC")));

        QVERIFY(background.windowClosing());
        QVERIFY(background.hidden());
        QCOMPARE(kept.count(), 1);
        QCOMPARE(kept.first().first().toStringList(), (QStringList{QStringLiteral("Discord"), QStringLiteral("VLC")}));
        QCOMPARE(m_flatpak->cancels, 0); // closing never stops an install

        // one done, one left: still there
        m_flatpak->end(true);
        QTest::qWait(20);
        QCOMPARE(quit.count(), 0);
        // the last one done: the Store goes
        m_flatpak->end(true);
        QTRY_COMPARE(quit.count(), 1);
    }

    void shownAgainStays()
    {
        Background background(m_installer);
        background.setQuitDelay(50);
        QSignalSpy quit(&background, &Background::quitRequested);
        m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC"), QStringLiteral("VLC")));
        QVERIFY(background.windowClosing());
        m_flatpak->end(true);
        // opened again before the delay was over
        background.windowShown();
        QVERIFY(!background.hidden());
        QTest::qWait(120);
        QCOMPARE(quit.count(), 0);
    }

    void newJobDuringTheDelayKeepsIt()
    {
        Background background(m_installer);
        background.setQuitDelay(50);
        QSignalSpy quit(&background, &Background::quitRequested);
        m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC"), QStringLiteral("VLC")));
        QVERIFY(background.windowClosing());
        m_flatpak->end(true);
        m_installer->get(flatpakApp(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP")));
        QTest::qWait(120);
        QCOMPARE(quit.count(), 0);
        m_flatpak->end(true);
        QTRY_COMPARE(quit.count(), 1);
    }

    // --- what installs leave behind

    void cleanupOnlyWhenTheQueueIsEmpty()
    {
        m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC"), QStringLiteral("VLC")));
        m_installer->get(flatpakApp(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP")));
        QCOMPARE(m_cleanups, 0);
        m_flatpak->end(true);
        QCOMPARE(m_cleanups, 0); // GIMP still needs the download folder
        m_flatpak->end(false);
        QCOMPARE(m_cleanups, 1); // after a failure too
        m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC"), QStringLiteral("VLC")));
        m_installer->cancel(QStringLiteral("app/org.videolan.VLC/x86_64/stable"));
        JobResult cancelled;
        cancelled.cancelled = true;
        Q_EMIT m_flatpak->finished(cancelled);
        QCOMPARE(m_cleanups, 2); // and after a cancel
    }

    void cancelSaysCancelling()
    {
        const QString ref = QStringLiteral("app/org.gnome.Calculator/x86_64/stable");
        m_installer->get(flatpakApp(QStringLiteral("org.gnome.Calculator"), QStringLiteral("Calculator")));
        QVERIFY(m_installer->cancel(ref));
        QCOMPARE(m_flatpak->cancels, 1);
        QVariantMap job = m_installer->job(ref);
        QVERIFY(job.value(QStringLiteral("cancelling")).toBool());
        QVERIFY(!job.value(QStringLiteral("cancellable")).toBool());
        // a late "you can cancel" from the backend does not undo it
        Q_EMIT m_flatpak->cancellableChanged(true);
        QVERIFY(!m_installer->job(ref).value(QStringLiteral("cancellable")).toBool());
        QVERIFY(!m_installer->cancel(ref)); // pressed twice: once is enough
        QCOMPARE(m_flatpak->cancels, 1);
        JobResult cancelled;
        cancelled.cancelled = true;
        Q_EMIT m_flatpak->finished(cancelled);
        QCOMPARE(m_installer->job(ref).value(QStringLiteral("state")).toInt(), int(Installer::Idle));
    }

    void staleCaches()
    {
        QTemporaryDir tmp;
        QDir dir(tmp.path());
        QVERIFY(dir.mkpath(QStringLiteral("flatpak-cache-AAAAAA/repo-1")));
        QVERIFY(dir.mkpath(QStringLiteral("flatpak-cache-BBBBBB/com.discordapp.Discord-1")));
        QVERIFY(dir.mkpath(QStringLiteral("flatpak-cache-CCCCCC")));
        QVERIFY(dir.mkpath(QStringLiteral("other-folder")));
        QVERIFY(dir.mkpath(QStringLiteral("elsewhere")));
        QFile file(dir.filePath(QStringLiteral("flatpak-cache-file")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QVERIFY(QFile::link(dir.filePath(QStringLiteral("elsewhere")), dir.filePath(QStringLiteral("flatpak-cache-LINK"))));

        // B is mounted for flatpak's helper, C is a process's working folder
        const QSet<QString> busy{dir.filePath(QStringLiteral("flatpak-cache-BBBBBB/com.discordapp.Discord-1")),
                                 dir.filePath(QStringLiteral("flatpak-cache-CCCCCC")), QStringLiteral("/usr/lib")};
        QCOMPARE(Cleanup::staleFlatpakCaches(tmp.path(), ::getuid(), busy),
                 QStringList{dir.filePath(QStringLiteral("flatpak-cache-AAAAAA"))});
        // nothing in use: every real folder of ours, never the link or the file
        QCOMPARE(Cleanup::staleFlatpakCaches(tmp.path(), ::getuid(), {}).size(), 3);
        // another user's folders are never touched
        QVERIFY(Cleanup::staleFlatpakCaches(tmp.path(), ::getuid() + 1, {}).isEmpty());

        QVERIFY(Cleanup::inUse(QStringLiteral("/var/tmp/flatpak-cache-X"), {QStringLiteral("/var/tmp/flatpak-cache-X/a")}));
        QVERIFY(!Cleanup::inUse(QStringLiteral("/var/tmp/flatpak-cache-X"), {QStringLiteral("/var/tmp/flatpak-cache-XY")}));
    }

    void findThenRemove()
    {
        QTemporaryDir tmp;
        const QString cache = tmp.filePath(QStringLiteral("flatpak-cache-OLD111"));
        QVERIFY(QDir().mkpath(cache + QStringLiteral("/repo-1")));
        const QStringList found = Cleanup::findFlatpakCaches(tmp.path());
        QCOMPARE(found, QStringList{cache});
        QCOMPARE(Cleanup::removeFolders(found), 1);
        QVERIFY(!QDir(cache).exists());
        QVERIFY(Cleanup::findFlatpakCaches(tmp.path()).isEmpty());
    }

    void openFilesCountAsInUse()
    {
        QTemporaryDir tmp;
        const QString cache = tmp.filePath(QStringLiteral("flatpak-cache-OPEN11"));
        QVERIFY(QDir().mkpath(cache));
        const QByteArray lock = QFile::encodeName(cache + QStringLiteral("/repo-lock"));
        const int fd = ::open(lock.constData(), O_CREAT | O_RDWR, 0600);
        QVERIFY(fd >= 0);
        QVERIFY(Cleanup::staleFlatpakCaches(tmp.path(), ::getuid(), Cleanup::pathsInUse()).isEmpty());
        ::close(fd);
        QCOMPARE(Cleanup::staleFlatpakCaches(tmp.path(), ::getuid(), Cleanup::pathsInUse()), QStringList{cache});
    }

    // --- appstream:// links

    void links_data()
    {
        QTest::addColumn<QString>("argument");
        QTest::addColumn<QString>("id");
        QTest::newRow("plain") << "com.discordapp.Discord" << "com.discordapp.Discord";
        QTest::newRow("link") << "appstream://com.discordapp.Discord" << "com.discordapp.Discord";
        QTest::newRow("lower-cased host") << "appstream://com.discordapp.discord" << "com.discordapp.discord";
        QTest::newRow("no slashes") << "appstream:org.gimp.GIMP" << "org.gimp.GIMP";
        QTest::newRow("scheme case") << "AppStream://org.gimp.GIMP/" << "org.gimp.GIMP";
        QTest::newRow("desktop id") << "appstream://org.kde.krita.desktop" << "org.kde.krita.desktop";
        QTest::newRow("query") << "appstream://org.gimp.GIMP?from=web" << "org.gimp.GIMP";
        QTest::newRow("encoded") << "appstream://org.gimp%2EGIMP" << "org.gimp.GIMP";
        QTest::newRow("option") << "--help" << "";
        QTest::newRow("path") << "appstream://../../etc/passwd" << "";
        QTest::newRow("slash inside") << "appstream://org.gimp/GIMP" << "";
        QTest::newRow("space") << "appstream://org gimp" << "";
        QTest::newRow("empty") << "appstream://" << "";
        QTest::newRow("new line") << "appstream:org.gimp.GIMP%0A" << "";
    }

    void links()
    {
        QFETCH(QString, argument);
        QFETCH(QString, id);
        QCOMPARE(appIdFromLink(argument), id);
    }

    // how Settings, the Start menu and a second start name an app
    void arguments()
    {
        const QString gimp = QStringLiteral("org.gimp.GIMP");
        QCOMPARE(appFromArguments({gimp}), gimp);
        QCOMPARE(appFromArguments({QStringLiteral("--open"), gimp}), gimp);
        QCOMPARE(appFromArguments({QStringLiteral("--open=org.gimp.GIMP")}), gimp);
        QCOMPARE(appFromArguments({QStringLiteral("app/org.gimp.GIMP/x86_64/stable")}), gimp);
        QCOMPARE(appFromArguments({QStringLiteral("--open"), QStringLiteral("appstream://org.gimp.GIMP")}), gimp);
        QCOMPARE(appFromArguments({QStringLiteral("--open")}), QString());
        QCOMPARE(appFromArguments({}), QString());
    }

    void findIgnoresCase()
    {
        Catalog catalog;
        catalog.setApps({app(QStringLiteral("com.discordapp.Discord"), QStringLiteral("Discord"), QStringLiteral("Talk")),
                         app(QStringLiteral("org.gimp.GIMP.desktop"), QStringLiteral("GIMP"), QStringLiteral("Edit"))});
        QCOMPARE(catalog.find(QStringLiteral("com.discordapp.discord")).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Discord"));
        QCOMPARE(catalog.find(QStringLiteral("org.gimp.gimp")).value(QStringLiteral("name")).toString(), QStringLiteral("GIMP"));
        QVERIFY(catalog.find(QStringLiteral("org.gimp")).isEmpty());
    }

    void installedFollowsChangesMadeElsewhere()
    {
        QTemporaryDir tmp;
        const QString mark = tmp.filePath(QStringLiteral(".changed"));
        Catalog catalog;
        catalog.setInstalled({QStringLiteral("app/com.example.NotReally/x86_64/stable")});
        // the mark does not exist yet: flatpak creates it on its first change
        catalog.watchInstalled({mark});
        QSignalSpy changed(&catalog, &Catalog::installedChanged);
        QFile file(mark);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);
        QVERIFY(!catalog.isInstalled(QStringLiteral("app/com.example.NotReally/x86_64/stable")));
    }

    // --- the Start menu search

    void searchOffersAppsToGet()
    {
        App installed = app(QStringLiteral("org.videolan.VLC"), QStringLiteral("VLC"), QStringLiteral("Plays Discord videos"));
        App hidden = app(QStringLiteral("com.example.Hidden"), QStringLiteral("Discord Hidden"), QStringLiteral("x"));
        hidden.listed = false;
        App remote = app(QStringLiteral("dev.vencord.Vesktop"), QStringLiteral("Vesktop"), QStringLiteral("Customizable Discord client"), 50);
        remote.icon = QStringLiteral("https://dl.flathub.org/icon.png");
        const QList<App> apps{app(QStringLiteral("com.discordapp.Discord"), QStringLiteral("Discord"), QStringLiteral("Talk, play, hang out"), 10),
                              remote, installed, hidden,
                              app(QStringLiteral("io.github.Equibop"), QStringLiteral("Equibop"), QStringLiteral("A snappy Discord desktop client"), 5),
                              app(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP"), QStringLiteral("Edit images"))};
        const auto isInstalled = [](const QString &ref) { return ref.contains(QLatin1String("VLC")); };

        const QList<StoreMatch> found = storeMatches(apps, isInstalled, QStringLiteral("discord"));
        QCOMPARE(found.size(), 3);
        QCOMPARE(found.at(0).id, QStringLiteral("com.discordapp.Discord")); // the name first
        QCOMPARE(found.at(0).text, QStringLiteral("Get Discord…"));
        QCOMPARE(found.at(0).subtext, QStringLiteral("Talk, play, hang out"));
        QCOMPARE(found.at(0).icon, QStringLiteral("/var/lib/flatpak/appstream/icons/com.discordapp.Discord.png"));
        QCOMPARE(found.at(1).id, QStringLiteral("dev.vencord.Vesktop")); // then by popularity
        QCOMPARE(found.at(1).icon, QStringLiteral("system-software-install"));
        QCOMPARE(found.at(2).id, QStringLiteral("io.github.Equibop"));
        QVERIFY(found.at(0).relevance > found.at(1).relevance);

        QVERIFY(storeMatches(apps, isInstalled, QStringLiteral("di")).isEmpty());
        QCOMPARE(storeMatches(apps, isInstalled, QStringLiteral("discord"), 1).size(), 1);
        QCOMPARE(storeLinkArguments(QStringLiteral("org.gimp.GIMP.desktop")), QStringList{QStringLiteral("appstream://org.gimp.GIMP")});
    }

    // Start offers no app named after the desktop or the Windows layer
    // underneath. The Store itself still lists them.
    void storeMatchesLeaveOutTheLayersUnderneath()
    {
        const QStringList hidden = {QStringLiteral("PlasmaTube"), QStringLiteral("Plasma Camera"),
                                    QStringLiteral("KDE Connect"), QStringLiteral("WineZGUI"),
                                    QStringLiteral("Winetricks"), QStringLiteral("Wine"),
                                    QStringLiteral("KWalletManager"), QStringLiteral("Wineglass")};
        for (const QString &name : hidden) {
            QVERIFY2(namesTheLayersUnderneath(name), qPrintable(name));
            App app;
            app.id = name;
            app.name = name;
            app.listed = true;
            app.popularity = 1;
            const auto none = [](const QString &) { return false; };
            QVERIFY2(storeMatches({app}, none, name).isEmpty(), qPrintable(name));
        }
        for (const QString &name : {QStringLiteral("Winery Manager"), QStringLiteral("Twine Writer"), QStringLiteral("Okular")}) {
            QVERIFY2(!namesTheLayersUnderneath(name), qPrintable(name));
            App app;
            app.id = name;
            app.name = name;
            app.listed = true;
            const auto none = [](const QString &) { return false; };
            QCOMPARE(storeMatches({app}, none, name).size(), 1);
        }
    }
};

QTEST_GUILESS_MAIN(TestBackground)
#include "test_background.moc"
