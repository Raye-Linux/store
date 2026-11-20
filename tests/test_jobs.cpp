// SPDX-License-Identifier: GPL-3.0-or-later
// The job queue, its states and reasons, updates and the history, with a
// fake backend; the extras helper protocol with a fake helper; and, when
// RAYE_STORE_LIVE_FLATPAK names a small Flathub app, one real install and
// removal through libflatpak.
#include "catalog.h"
#include "history.h"
#include "installer.h"
#include "uninstallcommand.h"

#include <KLocalizedString>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{

class FakeBackend : public JobBackend
{
    Q_OBJECT
public:
    using JobBackend::JobBackend;
    QList<JobSpec> started;
    int cancels = 0;
    int checks = 0;

    void start(const JobSpec &job) override { started.append(job); }
    void cancel() override { ++cancels; }
    void checkUpdates() override { ++checks; }

    void step(int percent, qint64 done = 0, qint64 total = 0, bool downloading = true)
    {
        JobProgress p;
        p.percent = percent;
        p.bytesDone = done;
        p.bytesTotal = total;
        p.downloading = downloading;
        Q_EMIT progress(p);
    }
    void end(bool ok, const QString &detail = {}, bool cancelled = false, Installer::Reason reason = Installer::ReasonNone)
    {
        JobResult r;
        r.ok = ok;
        r.cancelled = cancelled;
        r.detail = detail;
        r.reason = reason;
        Q_EMIT finished(r);
    }
};

QVariantMap flatpakApp(const QString &id, const QString &name = {})
{
    return {{QStringLiteral("appId"), QString(id + QStringLiteral(".desktop"))},
            {QStringLiteral("name"), name.isEmpty() ? id : name},
            {QStringLiteral("iconSource"), QString(QStringLiteral("/icons/") + id + QStringLiteral(".png"))},
            {QStringLiteral("source"), QStringLiteral("flatpak")},
            {QStringLiteral("ref"), QStringLiteral("app/%1/x86_64/stable").arg(id)}};
}

QString refOf(const QString &id)
{
    return QStringLiteral("app/%1/x86_64/stable").arg(id);
}

HistoryEntry entry(const QString &ref, const QString &action, const QString &result, int minutesAgo = 0)
{
    HistoryEntry e;
    e.appId = ref;
    e.name = ref;
    e.ref = ref;
    e.source = QStringLiteral("flatpak");
    e.action = action;
    e.result = result;
    e.time = QDateTime::currentDateTimeUtc().addSecs(-60 * minutesAgo);
    return e;
}

} // namespace

class TestJobs : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    FakeBackend *m_flatpak = nullptr;
    FakeBackend *m_apt = nullptr;
    FakeBackend *m_raye = nullptr;
    History *m_history = nullptr;
    Installer *m_installer = nullptr;

    QString historyPath() const { return m_dir.filePath(QStringLiteral("state/raye-store/history.json")); }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-store");
    }

    void init()
    {
        QFile::remove(historyPath());
        m_flatpak = new FakeBackend;
        m_apt = new FakeBackend;
        m_raye = new FakeBackend;
        m_history = new History(historyPath());
        m_installer = new Installer({{QStringLiteral("flatpak"), m_flatpak},
                                     {QStringLiteral("apt"), m_apt},
                                     {QStringLiteral("raye"), m_raye}},
                                    m_history);
    }

    void cleanup()
    {
        delete m_installer; // owns the backends and the history
        m_installer = nullptr;
    }

    void webAppAccessFollowsFlatpakInstallsAndRemovals()
    {
        QList<QPair<QString, bool>> calls;
        m_installer->setWebAppAccess([&calls](const QString &id, bool installed) { calls.append({id, installed}); });
        m_installer->setStillInstalled([](const JobSpec &) { return Installer::Absent; });
        const QVariantMap chrome = flatpakApp(QStringLiteral("com.google.Chrome"), QStringLiteral("Google Chrome"));
        m_installer->get(chrome);
        m_flatpak->end(false, QStringLiteral("no space"));
        QVERIFY(calls.isEmpty());
        m_installer->get(chrome);
        m_flatpak->end(true);
        QCOMPARE(calls, (QList<QPair<QString, bool>>{{QStringLiteral("com.google.Chrome"), true}}));
        m_installer->uninstall(chrome);
        m_flatpak->end(true);
        QCOMPARE(calls.size(), 2);
        QCOMPARE(calls.last(), (QPair<QString, bool>{QStringLiteral("com.google.Chrome"), false}));
        // an apt package is not a Flatpak: no override
        m_installer->get({{QStringLiteral("appId"), QStringLiteral("chromium.desktop")}, {QStringLiteral("name"), QStringLiteral("Chromium")},
                          {QStringLiteral("source"), QStringLiteral("apt")}, {QStringLiteral("ref"), QStringLiteral("chromium")}});
        m_apt->end(true);
        QCOMPARE(calls.size(), 2);
    }

    void gameLaunchersAreSetUpAfterInstall()
    {
        QStringList calls;
        m_installer->setGameSetup([&calls](const QString &id) { calls.append(id); });
        m_installer->setStillInstalled([](const JobSpec &) { return Installer::Absent; });
        const QVariantMap steam = flatpakApp(QStringLiteral("com.valvesoftware.Steam"), QStringLiteral("Steam"));
        m_installer->get(steam);
        m_flatpak->end(false, QStringLiteral("no space"));
        QVERIFY(calls.isEmpty());
        m_installer->get(steam);
        m_flatpak->end(true);
        QCOMPARE(calls, QStringList{QStringLiteral("com.valvesoftware.Steam")});
        // removing it sets nothing up
        m_installer->uninstall(steam);
        m_flatpak->end(true);
        QCOMPARE(calls.size(), 1);
        QVERIFY(Installer::gameLaunchers().contains(QStringLiteral("com.valvesoftware.Steam")));
    }

    void getsQueueUpOneRuns()
    {
        QSignalSpy busy(m_installer, &Installer::busyChanged);
        const int first = m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC"), QStringLiteral("VLC")));
        const int second = m_installer->get(flatpakApp(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP")));
        QVERIFY(first > 0);
        QVERIFY(second > first);
        QVERIFY(m_installer->busy());
        QVERIFY(busy.count() >= 1);
        QCOMPARE(m_flatpak->started.size(), 1);
        QCOMPARE(m_flatpak->started.first().ref, refOf(QStringLiteral("org.videolan.VLC")));
        QCOMPARE(m_flatpak->started.first().action, QStringLiteral("install"));
        QCOMPARE(m_flatpak->started.first().name, QStringLiteral("VLC"));
        QCOMPARE(m_installer->jobs()->rowCount(), 2);

        // started, nothing heard yet: still "Waiting…"
        QVariantMap vlc = m_installer->job(refOf(QStringLiteral("org.videolan.VLC")));
        QCOMPARE(vlc.value(QStringLiteral("state")).toInt(), int(Installer::Pending));
        QCOMPARE(vlc.value(QStringLiteral("position")).toInt(), 0);
        QVariantMap gimp = m_installer->job(refOf(QStringLiteral("org.gimp.GIMP")));
        QCOMPARE(gimp.value(QStringLiteral("state")).toInt(), int(Installer::Pending));
        QCOMPARE(gimp.value(QStringLiteral("position")).toInt(), 1);

        m_flatpak->step(45, 45LL * 1000 * 1000, 120LL * 1000 * 1000);
        vlc = m_installer->job(refOf(QStringLiteral("org.videolan.VLC")));
        QCOMPARE(vlc.value(QStringLiteral("state")).toInt(), int(Installer::Downloading));
        QCOMPARE(vlc.value(QStringLiteral("percent")).toInt(), 45);
        QCOMPARE(vlc.value(QStringLiteral("bytesTotal")).toLongLong(), 120LL * 1000 * 1000);
        QVERIFY(!vlc.value(QStringLiteral("progressText")).toString().isEmpty());

        m_flatpak->step(-1, 0, 0, false);
        QCOMPARE(m_installer->job(refOf(QStringLiteral("org.videolan.VLC"))).value(QStringLiteral("state")).toInt(),
                 int(Installer::Installing));

        // the second starts only once the first ended
        QCOMPARE(m_flatpak->started.size(), 1);
        m_flatpak->end(true);
        QCOMPARE(m_flatpak->started.size(), 2);
        QCOMPARE(m_flatpak->started.last().ref, refOf(QStringLiteral("org.gimp.GIMP")));
        QCOMPARE(m_installer->job(refOf(QStringLiteral("org.videolan.VLC"))).value(QStringLiteral("state")).toInt(),
                 int(Installer::Idle));
        QCOMPARE(m_installer->jobs()->rowCount(), 1);
        m_flatpak->end(true);
        QVERIFY(!m_installer->busy());
        QCOMPARE(m_installer->jobs()->rowCount(), 0);
        QCOMPARE(m_history->count(), 2);
        QCOMPARE(m_history->entries().first().name, QStringLiteral("GIMP"));
    }

    void eachSourceHasItsBackend()
    {
        m_installer->get({{QStringLiteral("appId"), QStringLiteral("gimp")}, {QStringLiteral("source"), QStringLiteral("apt")},
                          {QStringLiteral("ref"), QStringLiteral("gimp")}});
        QCOMPARE(m_apt->started.size(), 1);
        m_apt->end(true);
        m_installer->uninstall({{QStringLiteral("source"), QStringLiteral("raye")},
                                {QStringLiteral("ref"), QStringLiteral("artcraft-launcher")}});
        QCOMPARE(m_raye->started.size(), 1);
        QCOMPARE(m_raye->started.first().action, QStringLiteral("remove"));
        // a removal shows as Removing from the start
        QCOMPARE(m_installer->job(QStringLiteral("artcraft-launcher")).value(QStringLiteral("state")).toInt(),
                 int(Installer::Removing));
        m_raye->end(true);
        QCOMPARE(m_flatpak->started.size(), 0);
    }

    void oldCallsStillWork()
    {
        QSignalSpy finished(m_installer, &Installer::finished);
        m_installer->install(QStringLiteral("flatpak"), refOf(QStringLiteral("org.videolan.VLC")));
        QCOMPARE(m_installer->working(), refOf(QStringLiteral("org.videolan.VLC")));
        m_flatpak->end(true);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toBool(), true);
        QCOMPARE(finished.first().at(1).toString(), refOf(QStringLiteral("org.videolan.VLC")));
        m_installer->remove(QStringLiteral("apt"), QStringLiteral("gimp"));
        QCOMPARE(m_apt->started.first().action, QStringLiteral("remove"));
        m_apt->end(true);
        QVERIFY(m_installer->working().isEmpty());
    }

    void sameJobTwiceIsOneJob()
    {
        const int a = m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC")));
        const int b = m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC")));
        QCOMPARE(a, b);
        QCOMPARE(m_installer->jobs()->rowCount(), 1);
        // a removal after the install waits behind it
        const int c = m_installer->uninstall(flatpakApp(QStringLiteral("org.videolan.VLC")));
        QVERIFY(c != a);
        QCOMPARE(m_installer->jobs()->rowCount(), 2);
    }

    // Firefox's Flathub page, Firefox is a system package here
    void systemCopyStaysTheSystems()
    {
        App firefox;
        firefox.id = QStringLiteral("org.mozilla.firefox");
        firefox.name = QStringLiteral("Firefox");
        firefox.source = QStringLiteral("flatpak");
        firefox.ref = refOf(firefox.id);
        Catalog catalog;
        catalog.setApps({firefox});
        catalog.setSystemLaunchers({{QStringLiteral("firefox"), QStringLiteral("Firefox")}});
        m_installer->setCatalog(&catalog);
        const QVariantMap app = flatpakApp(firefox.id);
        QTemporaryDir data;
        QVERIFY(QDir(data.path()).mkpath(QStringLiteral("applications")));
        const QString launcher = data.filePath(QStringLiteral("applications/firefox.desktop"));
        QFile file(launcher);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const QByteArray dirsBefore = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_DIRS", data.path().toUtf8());
        const QStringList command = m_installer->openCommand(app);
        qputenv("XDG_DATA_DIRS", dirsBefore);
        QCOMPARE(command, (QStringList{QStringLiteral("kioclient"), QStringLiteral("exec"), launcher}));
        // no second copy, and nothing to remove from Flathub
        QCOMPARE(m_installer->get(app), 0);
        QCOMPARE(m_installer->uninstall(app), 0);
        QVERIFY(m_flatpak->started.isEmpty());
        m_installer->setCatalog(nullptr);
        QCOMPARE(m_installer->openCommand(app),
                 (QStringList{QStringLiteral("flatpak"), QStringLiteral("run"), firefox.id}));
    }

    void refusedRequests()
    {
        QCOMPARE(m_installer->get({{QStringLiteral("source"), QStringLiteral("snap")}, {QStringLiteral("ref"), QStringLiteral("x")}}), 0);
        QCOMPARE(m_installer->get({{QStringLiteral("source"), QStringLiteral("apt")}, {QStringLiteral("ref"), QStringLiteral("--purge")}}), 0);
        QCOMPARE(m_installer->get({{QStringLiteral("source"), QStringLiteral("apt")}, {QStringLiteral("ref"), QStringLiteral("../x")}}), 0);
        QCOMPARE(m_installer->get({{QStringLiteral("source"), QStringLiteral("flatpak")}, {QStringLiteral("ref"), QStringLiteral("app/../../etc/x86_64/stable")}}), 0);
        QCOMPARE(m_installer->get({{QStringLiteral("source"), QStringLiteral("flatpak")}, {QStringLiteral("ref"), QStringLiteral("-y")}}), 0);
        QCOMPARE(m_installer->queue(QStringLiteral("explode"), flatpakApp(QStringLiteral("org.videolan.VLC"))), 0);
        // system packages are updated by Raye Update, not here
        QCOMPARE(m_installer->update({{QStringLiteral("source"), QStringLiteral("apt")}, {QStringLiteral("ref"), QStringLiteral("gimp")}}), 0);
        QVERIFY(!m_installer->busy());
        QCOMPARE(m_flatpak->started.size() + m_apt->started.size() + m_raye->started.size(), 0);
    }

    void validRefs()
    {
        QVERIFY(Installer::validRef(QStringLiteral("flatpak"), QStringLiteral("app/org.gimp.GIMP/x86_64/stable")));
        QVERIFY(Installer::validRef(QStringLiteral("flatpak"), QStringLiteral("runtime/org.freedesktop.Platform/x86_64/24.08")));
        QVERIFY(!Installer::validRef(QStringLiteral("flatpak"), QStringLiteral("org.gimp.GIMP")));
        QVERIFY(!Installer::validRef(QStringLiteral("flatpak"), QStringLiteral("app/-x/x86_64/stable")));
        QVERIFY(!Installer::validRef(QStringLiteral("flatpak"), QStringLiteral("app/a b/x86_64/stable")));
        QVERIFY(Installer::validRef(QStringLiteral("apt"), QStringLiteral("libreoffice-writer")));
        QVERIFY(Installer::validRef(QStringLiteral("apt"), QStringLiteral("g++")));
        QVERIFY(!Installer::validRef(QStringLiteral("apt"), QStringLiteral("Gimp")));
        QVERIFY(!Installer::validRef(QStringLiteral("apt"), QStringLiteral("a;rm")));
        QVERIFY(!Installer::validRef(QStringLiteral("raye"), QStringLiteral("")));
        QVERIFY(!Installer::validRef(QStringLiteral("other"), QStringLiteral("gimp")));
    }

    void cancelWaitingAndRunning()
    {
        m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC")));
        m_installer->get(flatpakApp(QStringLiteral("org.gimp.GIMP")));
        // the waiting one just leaves: nothing ran, nothing to remember
        QVERIFY(m_installer->cancel(refOf(QStringLiteral("org.gimp.GIMP"))));
        QCOMPARE(m_installer->jobs()->rowCount(), 1);
        QCOMPARE(m_flatpak->cancels, 0);
        QCOMPARE(m_history->count(), 0);

        // the running one is asked to stop and ends as cancelled
        QVERIFY(m_installer->cancel(refOf(QStringLiteral("org.videolan.VLC"))));
        QCOMPARE(m_flatpak->cancels, 1);
        m_flatpak->end(false, QStringLiteral("Operation was cancelled"), true);
        QCOMPARE(m_installer->job(refOf(QStringLiteral("org.videolan.VLC"))).value(QStringLiteral("state")).toInt(),
                 int(Installer::Idle));
        QCOMPARE(m_history->count(), 1);
        QCOMPARE(m_history->entries().first().result, QStringLiteral("cancelled"));
        QVERIFY(!m_installer->cancel(QStringLiteral("nothing")));
    }

    void notCancellableWhileInstalling()
    {
        m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC")));
        Q_EMIT m_flatpak->cancellableChanged(false);
        QCOMPARE(m_installer->job(refOf(QStringLiteral("org.videolan.VLC"))).value(QStringLiteral("cancellable")).toBool(), false);
        QVERIFY(!m_installer->cancel(refOf(QStringLiteral("org.videolan.VLC"))));
        QCOMPARE(m_flatpak->cancels, 0);
        Q_EMIT m_flatpak->cancellableChanged(true);
        QVERIFY(m_installer->cancel(refOf(QStringLiteral("org.videolan.VLC"))));
    }

    void failureSaysWhy()
    {
        QSignalSpy finished(m_installer, &Installer::finished);
        QSignalSpy rich(m_installer, &Installer::jobFinished);
        m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC"), QStringLiteral("VLC")));
        m_flatpak->end(false, QStringLiteral("While pulling app/org.videolan.VLC: Could not resolve hostname"));
        const QVariantMap job = m_installer->job(refOf(QStringLiteral("org.videolan.VLC")));
        QCOMPARE(job.value(QStringLiteral("state")).toInt(), int(Installer::Failed));
        QCOMPARE(job.value(QStringLiteral("reason")).toInt(), int(Installer::ReasonNoNetwork));
        QCOMPARE(job.value(QStringLiteral("reasonText")).toString(), Installer::reasonText(Installer::ReasonNoNetwork));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toBool(), false);
        QCOMPARE(finished.first().at(2).toString(), Installer::reasonText(Installer::ReasonNoNetwork));
        QCOMPARE(rich.count(), 1);
        const QVariantMap info = rich.first().at(0).toMap();
        QCOMPARE(info.value(QStringLiteral("name")).toString(), QStringLiteral("VLC"));
        QCOMPARE(info.value(QStringLiteral("appId")).toString(), QStringLiteral("org.videolan.VLC.desktop"));
        QCOMPARE(info.value(QStringLiteral("action")).toString(), QStringLiteral("install"));
        QCOMPARE(info.value(QStringLiteral("state")).toInt(), int(Installer::Failed));
        QCOMPARE(m_history->entries().first().result, QStringLiteral("failed"));
        QCOMPARE(m_history->entries().first().message, Installer::reasonText(Installer::ReasonNoNetwork));

        // closing the InfoBar forgets it; so does trying again
        m_installer->dismiss(refOf(QStringLiteral("org.videolan.VLC")));
        QCOMPARE(m_installer->job(refOf(QStringLiteral("org.videolan.VLC"))).value(QStringLiteral("state")).toInt(),
                 int(Installer::Idle));
        m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC")));
        m_flatpak->end(false, {}, false, Installer::ReasonNoSpace);
        QCOMPARE(m_installer->job(refOf(QStringLiteral("org.videolan.VLC"))).value(QStringLiteral("reason")).toInt(),
                 int(Installer::ReasonNoSpace));
        m_installer->get(flatpakApp(QStringLiteral("org.videolan.VLC")));
        QCOMPARE(m_installer->job(refOf(QStringLiteral("org.videolan.VLC"))).value(QStringLiteral("state")).toInt(),
                 int(Installer::Pending));
    }

    // the owner's Vesktop: "Something went wrong", and the app was gone
    void removalIsJudgedByWhatIsThere()
    {
        const QVariantMap app = flatpakApp(QStringLiteral("dev.vencord.Vesktop"), QStringLiteral("Vesktop"));
        const QString ref = refOf(QStringLiteral("dev.vencord.Vesktop"));
        bool there = false;
        m_installer->setStillInstalled([&there](const JobSpec &) { return there ? Installer::Present : Installer::Absent; });
        QSignalSpy finished(m_installer, &Installer::finished);

        // a step failed at the end, the app is gone: it worked
        m_installer->uninstall(app);
        m_flatpak->end(false, QStringLiteral("Could not unmount revokefs-fuse filesystem"));
        QCOMPARE(m_installer->job(ref).value(QStringLiteral("state")).toInt(), int(Installer::Idle));
        QCOMPARE(finished.last().at(0).toBool(), true);
        QCOMPARE(m_history->entries().first().result, QStringLiteral("done"));

        // it failed and the app is still there: the system's own words
        there = true;
        m_installer->uninstall(app);
        m_flatpak->end(false, QStringLiteral("Could not unmount revokefs-fuse filesystem"));
        QVariantMap job = m_installer->job(ref);
        QCOMPARE(job.value(QStringLiteral("state")).toInt(), int(Installer::Failed));
        const QString said = job.value(QStringLiteral("reasonText")).toString();
        QVERIFY2(said.contains(QStringLiteral("revokefs-fuse")), qPrintable(said));
        QVERIFY2(!said.contains(QStringLiteral("Something went wrong")), qPrintable(said));

        // "done", and still there: not done
        m_installer->uninstall(app);
        m_flatpak->end(true);
        job = m_installer->job(ref);
        QCOMPARE(job.value(QStringLiteral("state")).toInt(), int(Installer::Failed));
        QVERIFY2(job.value(QStringLiteral("reasonText")).toString().contains(QStringLiteral("Vesktop")),
                 qPrintable(job.value(QStringLiteral("reasonText")).toString()));

        // an install is not touched by any of this
        there = false;
        m_installer->get(app);
        m_flatpak->end(false, QStringLiteral("Could not resolve hostname"));
        QCOMPARE(m_installer->job(ref).value(QStringLiteral("reason")).toInt(), int(Installer::ReasonNoNetwork));
    }

    // flatpak list failed, so it said nothing: that is not "removed"
    void removalNotJudgedWhenTheCheckCannotRun()
    {
        const QVariantMap app = flatpakApp(QStringLiteral("dev.vencord.Vesktop"), QStringLiteral("Vesktop"));
        const QString ref = refOf(QStringLiteral("dev.vencord.Vesktop"));
        m_installer->setStillInstalled([](const JobSpec &) { return Installer::Unknown; });

        m_installer->uninstall(app);
        m_flatpak->end(false, QStringLiteral("Could not unmount revokefs-fuse filesystem"));
        const QVariantMap job = m_installer->job(ref);
        QCOMPARE(job.value(QStringLiteral("state")).toInt(), int(Installer::Failed));
        const QString said = job.value(QStringLiteral("reasonText")).toString();
        QVERIFY2(said.contains(QStringLiteral("revokefs-fuse")), qPrintable(said));
        QCOMPARE(m_history->entries().first().result, QStringLiteral("failed"));

        // the backend said done and nothing says otherwise
        m_installer->uninstall(app);
        m_flatpak->end(true);
        QCOMPARE(m_installer->job(ref).value(QStringLiteral("state")).toInt(), int(Installer::Idle));
        QCOMPARE(m_history->entries().first().result, QStringLiteral("done"));
    }

    // "Try again" clicked twice: one refresh, so the end of the first cannot
    // clear the download folders of the second
    void catalogRefreshRunsOnce()
    {
        QTemporaryDir bin;
        QVERIFY(bin.isValid());
        QFile f(bin.filePath(QStringLiteral("flatpak")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\necho run >> \"$(dirname \"$0\")/runs\"\nsleep 0.3\n");
        f.close();
        QVERIFY(f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", QByteArray(QFile::encodeName(bin.path()) + ":" + path));
        const auto restore = qScopeGuard([&path] { qputenv("PATH", path); });

        QSignalSpy refreshed(m_installer, &Installer::catalogRefreshed);
        m_installer->refreshCatalog();
        m_installer->refreshCatalog();
        QVERIFY(refreshed.wait(5000));
        QTest::qWait(600);
        QCOMPARE(refreshed.count(), 1);
        QFile runs(bin.filePath(QStringLiteral("runs")));
        QVERIFY(runs.open(QIODevice::ReadOnly));
        QCOMPARE(runs.readAll(), QByteArray("run\n"));
    }

    // the real check, with fake flatpak and dpkg-query found on PATH
    void presenceAsksTheSystem()
    {
        QTemporaryDir bin;
        QVERIFY(bin.isValid());
        const auto tool = [&bin](const QString &name, const QByteArray &body) {
            QFile f(bin.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\n" + body + "\n");
            f.close();
            QVERIFY(f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        };
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(bin.path()));
        const auto restore = qScopeGuard([&path] { qputenv("PATH", path); });

        JobSpec app;
        app.source = QStringLiteral("flatpak");
        app.ref = refOf(QStringLiteral("dev.vencord.Vesktop"));
        JobSpec deb;
        deb.source = QStringLiteral("apt");
        deb.ref = QStringLiteral("vlc");

        // nothing on PATH: neither can run
        QCOMPARE(Installer::presence(app, 2000), Installer::Unknown);
        QCOMPARE(Installer::presence(deb, 2000), Installer::Unknown);

        tool(QStringLiteral("flatpak"), "echo 'error: cannot open the installation' >&2; exit 1");
        QCOMPARE(Installer::presence(app, 2000), Installer::Unknown);
        tool(QStringLiteral("flatpak"), "kill -9 $$");
        QCOMPARE(Installer::presence(app, 2000), Installer::Unknown);
        tool(QStringLiteral("flatpak"), "echo dev.vencord.Vesktop/x86_64/stable; echo org.other.App/x86_64/stable");
        QCOMPARE(Installer::presence(app, 2000), Installer::Present);
        tool(QStringLiteral("flatpak"), "echo org.other.App/x86_64/stable");
        QCOMPARE(Installer::presence(app, 2000), Installer::Absent);

        tool(QStringLiteral("dpkg-query"), "echo 'dpkg-query: error: database is locked' >&2; exit 2");
        QCOMPARE(Installer::presence(deb, 2000), Installer::Unknown);
        tool(QStringLiteral("dpkg-query"), "echo \"dpkg-query: no packages found matching $3\" >&2; exit 1");
        QCOMPARE(Installer::presence(deb, 2000), Installer::Absent);
        tool(QStringLiteral("dpkg-query"), "printf 'rc '");
        QCOMPARE(Installer::presence(deb, 2000), Installer::Absent);
        tool(QStringLiteral("dpkg-query"), "printf 'ii '");
        QCOMPARE(Installer::presence(deb, 2000), Installer::Present);
    }

    void plainReasonWinsOverTheSystemsWords()
    {
        QCOMPARE(Installer::failureText(Installer::ReasonNoSpace, QStringLiteral("no space, code 28")),
                 Installer::reasonText(Installer::ReasonNoSpace));
        QCOMPARE(Installer::failureText(Installer::ReasonUnknown, QString()), Installer::reasonText(Installer::ReasonUnknown));
        const QString shown = Installer::failureText(Installer::ReasonUnknown, QStringLiteral("first line\nsecond line"));
        QVERIFY2(shown.contains(QStringLiteral("first line")) && !shown.contains(QStringLiteral("second")), qPrintable(shown));
        QCOMPARE(Installer::classify(QStringLiteral("Can't remove org.x, it is needed for: org.y")), Installer::ReasonNeeded);
    }

    // removing an open app asks first, and closes it only when told to
    void openAppIsClosedOnlyWhenAsked()
    {
        const QVariantMap app = flatpakApp(QStringLiteral("dev.vencord.Vesktop"));
        QStringList running{QStringLiteral("dev.vencord.Vesktop"), QStringLiteral("org.other.App")};
        QStringList closed;
        m_installer->setRunningApps([&running] { return running; });
        m_installer->setCloseApp([&closed, &running](const QString &id) {
            closed.append(id);
            running.removeAll(id);
        });
        QVERIFY(m_installer->isRunning(app));
        QVERIFY(!m_installer->isRunning(flatpakApp(QStringLiteral("org.third.App"))));
        // a system package is not looked for here
        QVERIFY(!m_installer->isRunning({{QStringLiteral("source"), QStringLiteral("apt")}, {QStringLiteral("ref"), QStringLiteral("vlc")}}));
        m_installer->uninstall(app);
        QVERIFY(closed.isEmpty());
        m_flatpak->end(true);
        QVERIFY(m_installer->closeAndUninstall(app) > 0);
        QCOMPARE(closed, QStringList{QStringLiteral("dev.vencord.Vesktop")});
        QCOMPARE(m_flatpak->started.last().action, QStringLiteral("remove"));
    }

    void uninstallResolvesWhatSettingsNames()
    {
        const QStringList refs{QStringLiteral("app/dev.vencord.Vesktop/x86_64/stable")};
        const auto owner = [](const QString &desktop) {
            return desktop == QLatin1String("vlc.desktop") ? QStringLiteral("vlc") : QString();
        };
        // a ref stands for itself, an id goes through what is installed
        QCOMPARE(resolveUninstallTarget(QStringLiteral("app/org.gimp.GIMP/x86_64/stable"), refs, owner).source, QStringLiteral("flatpak"));
        QCOMPARE(resolveUninstallTarget(QStringLiteral("dev.vencord.Vesktop"), refs, owner).ref, refs.first());
        QCOMPARE(resolveUninstallTarget(QStringLiteral("dev.vencord.Vesktop.desktop"), refs, owner).ref, refs.first());
        const UninstallTarget vlc = resolveUninstallTarget(QStringLiteral("vlc"), refs, owner);
        QCOMPARE(vlc.source, QStringLiteral("apt"));
        QCOMPARE(vlc.ref, QStringLiteral("vlc"));
        // nothing of the kind, or something that is not a name
        QVERIFY(resolveUninstallTarget(QStringLiteral("nothing.here"), refs, owner).source.isEmpty());
        QVERIFY(resolveUninstallTarget(QStringLiteral("--force"), refs, owner).source.isEmpty());
        QVERIFY(resolveUninstallTarget(QStringLiteral("../../etc/passwd"), refs, owner).source.isEmpty());
        QVERIFY(resolveUninstallTarget(QStringLiteral("app/../x/y"), refs, owner).source.isEmpty());
    }

    // the lines Settings reads: progress, then done or failed with the reason
    void uninstallCommandPrintsItsProgress()
    {
        QBuffer out;
        out.open(QIODevice::WriteOnly);
        UninstallCommand command(m_installer, &out);
        QSignalSpy done(&command, &UninstallCommand::done);
        command.start({QStringLiteral("flatpak"), refOf(QStringLiteral("dev.vencord.Vesktop")), QStringLiteral("dev.vencord.Vesktop")});
        QCOMPARE(m_flatpak->started.last().action, QStringLiteral("remove"));
        m_flatpak->step(40);
        m_flatpak->end(true);
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.first().first().toInt(), 0);
        QList<QByteArray> lines = out.data().trimmed().split('\n');
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines.at(1), QByteArrayLiteral("{\"percent\":40,\"state\":\"removing\"}"));
        QCOMPARE(lines.last(), QByteArrayLiteral("{\"percent\":100,\"state\":\"done\"}"));

        out.buffer().clear();
        out.seek(0);
        UninstallCommand again(m_installer, &out);
        QSignalSpy failed(&again, &UninstallCommand::done);
        again.start({QStringLiteral("flatpak"), refOf(QStringLiteral("org.x.Y")), QStringLiteral("org.x.Y")});
        m_flatpak->end(false, {}, false, Installer::ReasonNotAllowed);
        QCOMPARE(failed.first().first().toInt(), 1);
        const QJsonObject last = QJsonDocument::fromJson(out.data().trimmed().split('\n').last()).object();
        QCOMPARE(last.value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
        QCOMPARE(last.value(QStringLiteral("message")).toString(), Installer::reasonText(Installer::ReasonNotAllowed));

        // nothing to remove: a clear failure, nothing started
        out.buffer().clear();
        out.seek(0);
        UninstallCommand nothing(m_installer, &out);
        QSignalSpy refused(&nothing, &UninstallCommand::done);
        const int before = m_flatpak->started.size();
        nothing.start({});
        QTRY_COMPARE(refused.count(), 1);
        QCOMPARE(refused.first().first().toInt(), 1);
        QCOMPARE(m_flatpak->started.size(), before);
        QVERIFY(out.data().contains("failed"));
    }

    void reasons_data()
    {
        QTest::addColumn<QString>("detail");
        QTest::addColumn<int>("reason");
        QTest::newRow("dns") << QStringLiteral("Could not resolve hostname dl.flathub.org") << int(Installer::ReasonNoNetwork);
        QTest::newRow("curl") << QStringLiteral("Couldn't resolve host name") << int(Installer::ReasonNoNetwork);
        QTest::newRow("unreachable") << QStringLiteral("Error: Network is unreachable") << int(Installer::ReasonNoNetwork);
        QTest::newRow("timeout") << QStringLiteral("Timeout was reached") << int(Installer::ReasonNoNetwork);
        QTest::newRow("dns-temp") << QStringLiteral("Temporary failure in name resolution") << int(Installer::ReasonNoNetwork);
        QTest::newRow("helper-net") << QStringLiteral("raye-store-extra: <urlopen error [Errno -3] Temporary failure in name resolution>")
                                    << int(Installer::ReasonNoNetwork);
        QTest::newRow("enospc") << QStringLiteral("Error writing to file: No space left on device") << int(Installer::ReasonNoSpace);
        QTest::newRow("nospace") << QStringLiteral("Not enough disk space to complete this operation") << int(Installer::ReasonNoSpace);
        QTest::newRow("cancel") << QStringLiteral("Operation was cancelled") << int(Installer::ReasonCancelled);
        QTest::newRow("auth") << QStringLiteral("GDBus.Error:org.freedesktop.Flatpak.Error.NotAuthorized: Flatpak system operation Deploy not allowed for user")
                              << int(Installer::ReasonNotAllowed);
        QTest::newRow("pkexec") << QStringLiteral("Error executing command as another user: Not authorized") << int(Installer::ReasonNotAllowed);
        QTest::newRow("lock") << QStringLiteral("E: Could not get lock /var/lib/dpkg/lock-frontend") << int(Installer::ReasonBusy);
        QTest::newRow("nothing") << QStringLiteral("Nothing matches org.nope in remote flathub") << int(Installer::ReasonNotFound);
        QTest::newRow("noref") << QStringLiteral("No remote refs found for 'org.nope'") << int(Installer::ReasonNotFound);
        QTest::newRow("helper-lookup") << QStringLiteral("raye-store-extra: foo is not offered by Raye") << int(Installer::ReasonNotFound);
        QTest::newRow("odd") << QStringLiteral("Segmentation fault") << int(Installer::ReasonUnknown);
        QTest::newRow("empty") << QStringLiteral("") << int(Installer::ReasonUnknown);
    }

    void reasons()
    {
        QFETCH(QString, detail);
        QFETCH(int, reason);
        QCOMPARE(int(Installer::classify(detail)), reason);
        QVERIFY(!Installer::reasonText(Installer::Reason(reason)).isEmpty());
    }

    void progressText()
    {
        QVERIFY(Installer::progressText(0, 0).isEmpty());
        const QString both = Installer::progressText(45LL * 1024 * 1024, 120LL * 1024 * 1024);
        QVERIFY2(both.contains(QStringLiteral("45")) && both.contains(QStringLiteral("120")), qPrintable(both));
        QVERIFY2(!both.contains(QLatin1Char('.')), qPrintable(both)); // whole megabytes
        QVERIFY(Installer::progressText(2LL * 1024 * 1024, 0).contains(QStringLiteral("2")));
        // a gigabyte or more keeps one decimal
        QVERIFY(Installer::progressText(1536LL * 1024 * 1024, 0).contains(QStringLiteral("1.5")));
        // the start of a big download reads "0 MB of 907 MB", not "0 bytes"
        QCOMPARE(Installer::progressText(0, 907LL * 1024 * 1024), QStringLiteral("0 MB of 907 MB"));
        QCOMPARE(Installer::progressText(512, 2048), QStringLiteral("1 KB of 2 KB"));
    }

    void updatesAndUpdateAll()
    {
        QSignalSpy checking(m_installer, &Installer::checkingChanged);
        m_installer->checkUpdates();
        QVERIFY(m_installer->checking());
        QCOMPARE(m_flatpak->checks, 1);
        // a second check while one runs is the same check
        m_installer->checkUpdates();
        QCOMPARE(m_flatpak->checks, 1);

        UpdateInfo vlc{QStringLiteral("org.videolan.VLC"), refOf(QStringLiteral("org.videolan.VLC")), QStringLiteral("VLC"), {},
                       QStringLiteral("3.0.20"), QStringLiteral("3.0.21"), QStringLiteral("<p>Fixes</p>"), 0};
        UpdateInfo gimp{QStringLiteral("org.gimp.GIMP"), refOf(QStringLiteral("org.gimp.GIMP")), QStringLiteral("GIMP"), {},
                        QStringLiteral("3.0"), {}, {}, 0};
        Q_EMIT m_flatpak->updatesChecked({vlc, gimp}, {QStringLiteral("runtime/org.freedesktop.Platform/x86_64/24.08")}, {});
        QVERIFY(!m_installer->checking());
        QCOMPARE(m_installer->updateCount(), 2);
        QCOMPARE(m_installer->property("flatpakUpdates").toInt(), 2);
        QVERIFY(m_installer->lastChecked().isValid());
        QVERIFY(m_installer->updatesError().isEmpty());
        QCOMPARE(checking.count(), 2);

        m_installer->updateAll();
        QCOMPARE(m_installer->jobs()->rowCount(), 3); // two apps, then the runtimes
        QCOMPARE(m_flatpak->started.first().action, QStringLiteral("update"));
        QCOMPARE(m_installer->working(), QStringLiteral("*"));
        QCOMPARE(m_installer->updates()->data(m_installer->updates()->index(0, 0), UpdatesModel::QueuedRole).toBool(), true);
        // update all again does not queue twice
        m_installer->updateAll();
        QCOMPARE(m_installer->jobs()->rowCount(), 3);

        // a check asked while jobs run waits for the queue to drain
        m_installer->checkUpdates();
        QCOMPARE(m_flatpak->checks, 1);
        m_flatpak->end(true);
        QCOMPARE(m_installer->updateCount(), 1); // VLC is done
        m_flatpak->end(false, QStringLiteral("Could not resolve hostname"));
        QCOMPARE(m_installer->updateCount(), 1); // GIMP still waits
        // the shared runtimes: one job that updates every one of them
        QCOMPARE(m_flatpak->started.last().ref, QStringLiteral("runtime/*"));
        QVERIFY(!m_flatpak->started.last().name.isEmpty());
        QCOMPARE(m_flatpak->checks, 1);
        m_flatpak->end(true);
        QCOMPARE(m_flatpak->checks, 2);
        QCOMPARE(m_history->count(), 3);
    }

    // "Get updates" in the Library: it checks and installs right away, the
    // shared components too (a new runtime or graphics extension alone)
    void getUpdatesInstallsWhatItFinds()
    {
        m_installer->getUpdates();
        QVERIFY(m_installer->checking());
        QCOMPARE(m_flatpak->checks, 1);
        UpdateInfo vlc{QStringLiteral("org.videolan.VLC"), refOf(QStringLiteral("org.videolan.VLC")), QStringLiteral("VLC"), {},
                       QStringLiteral("3.0.20"), QStringLiteral("3.0.21"), {}, 0};
        Q_EMIT m_flatpak->updatesChecked({vlc}, {QStringLiteral("runtime/org.freedesktop.Platform.GL.nvidia-580-95-05/x86_64/1.4")}, {});
        QCOMPARE(m_installer->jobs()->rowCount(), 2); // VLC, then the components
        QCOMPARE(m_flatpak->started.first().action, QStringLiteral("update"));
        QCOMPARE(m_flatpak->started.first().ref, refOf(QStringLiteral("org.videolan.VLC")));
        m_flatpak->end(true);
        QCOMPARE(m_flatpak->started.last().ref, QStringLiteral("runtime/*"));
        m_flatpak->end(true);
    }

    void getUpdatesWithOnlyComponents()
    {
        m_installer->getUpdates();
        Q_EMIT m_flatpak->updatesChecked({}, {QStringLiteral("runtime/org.kde.Platform/x86_64/6.9")}, {});
        QCOMPARE(m_installer->jobs()->rowCount(), 1);
        QCOMPARE(m_flatpak->started.last().ref, QStringLiteral("runtime/*"));
        m_flatpak->end(true);
    }

    void getUpdatesWithNothingNewOrNoNetwork()
    {
        m_installer->getUpdates();
        Q_EMIT m_flatpak->updatesChecked({}, {}, {});
        QCOMPARE(m_installer->jobs()->rowCount(), 0);
        m_installer->getUpdates();
        UpdateInfo vlc{QStringLiteral("org.videolan.VLC"), refOf(QStringLiteral("org.videolan.VLC")), QStringLiteral("VLC"), {},
                       {}, {}, {}, 0};
        Q_EMIT m_flatpak->updatesChecked({vlc}, {}, QStringLiteral("Could not resolve hostname"));
        QCOMPARE(m_installer->jobs()->rowCount(), 0);
        // a plain check later only lists: it never installs on its own
        m_installer->checkUpdates();
        Q_EMIT m_flatpak->updatesChecked({vlc}, {}, {});
        QCOMPARE(m_installer->updateCount(), 1);
        QCOMPARE(m_installer->jobs()->rowCount(), 0);
    }

    void failedCheckSaysWhy()
    {
        m_installer->checkUpdates();
        Q_EMIT m_flatpak->updatesChecked({}, {}, QStringLiteral("Unable to load summary from remote flathub: Could not resolve hostname"));
        QCOMPARE(m_installer->updatesError(), Installer::reasonText(Installer::ReasonNoNetwork));
        QCOMPARE(m_installer->updateCount(), 0);
    }

    void updatesFromTheCatalog()
    {
        Catalog catalog;
        App vlc;
        vlc.id = QStringLiteral("org.videolan.VLC.desktop");
        vlc.name = QStringLiteral("VLC media player");
        vlc.icon = QStringLiteral("/icons/vlc.png");
        vlc.source = QStringLiteral("flatpak");
        vlc.ref = refOf(QStringLiteral("org.videolan.VLC"));
        vlc.releases = {{QStringLiteral("3.0.21"), QDateTime(), QStringLiteral("<p>Plays <em>more</em>\n files.</p><ul><li>Fixes</li></ul>")}};
        App gimp = vlc;
        gimp.id = QStringLiteral("org.gimp.GIMP.desktop");
        gimp.name = QStringLiteral("GIMP");
        gimp.ref = refOf(QStringLiteral("org.gimp.GIMP"));
        gimp.releases = {{QStringLiteral("3.0"), QDateTime(), {}}};
        catalog.setApps({vlc, gimp});
        m_installer->setCatalog(&catalog);

        m_installer->checkUpdates();
        Q_EMIT m_flatpak->updatesChecked({{QStringLiteral("org.videolan.VLC"), vlc.ref, QStringLiteral("org.videolan.VLC"), {},
                                           QStringLiteral("3.0.20"), {}, {}, 0},
                                          {QStringLiteral("org.gimp.GIMP"), gimp.ref, {}, {}, QStringLiteral("3.0"), {}, {}, 0}},
                                         {}, {});
        QAbstractItemModel *updates = m_installer->updates();
        const QModelIndex first = updates->index(0, 0);
        QCOMPARE(updates->data(first, UpdatesModel::NameRole).toString(), QStringLiteral("VLC media player"));
        QCOMPARE(updates->data(first, UpdatesModel::IconRole).toString(), QStringLiteral("/icons/vlc.png"));
        QCOMPARE(updates->data(first, UpdatesModel::OldVersionRole).toString(), QStringLiteral("3.0.20"));
        QCOMPARE(updates->data(first, UpdatesModel::NewVersionRole).toString(), QStringLiteral("3.0.21"));
        QCOMPARE(updates->data(first, UpdatesModel::NotesTextRole).toString(), QStringLiteral("Plays more files. Fixes"));
        // the catalog has not seen the new version yet: no "3.0 -> 3.0"
        const QModelIndex second = updates->index(1, 0);
        QCOMPARE(updates->data(second, UpdatesModel::NameRole).toString(), QStringLiteral("GIMP"));
        QVERIFY(updates->data(second, UpdatesModel::NewVersionRole).toString().isEmpty());
        QVERIFY(updates->roleNames().values().contains("newVersion"));
    }

    void historyKeepsFifty()
    {
        for (int i = 0; i < 60; ++i)
            m_history->add(entry(QStringLiteral("app%1").arg(i), QStringLiteral("install"), QStringLiteral("done"), 60 - i));
        QCOMPARE(m_history->count(), History::MaxEntries);
        QCOMPARE(m_history->entries().first().ref, QStringLiteral("app59"));
        QCOMPARE(m_history->entries().last().ref, QStringLiteral("app10"));
        // on disk too, and read back by a new model
        History again(historyPath());
        QCOMPARE(again.count(), History::MaxEntries);
        QCOMPARE(again.entries().first().ref, QStringLiteral("app59"));
        QCOMPARE(again.data(again.index(0), History::OkRole).toBool(), true);
    }

    void historyMergesOtherWriters()
    {
        History other(historyPath());
        other.add(entry(QStringLiteral("a"), QStringLiteral("install"), QStringLiteral("done")));
        m_history->add(entry(QStringLiteral("b"), QStringLiteral("remove"), QStringLiteral("done")));
        QCOMPARE(m_history->count(), 2);
        other.reload();
        QCOMPARE(other.entries().first().ref, QStringLiteral("b"));
    }

    void historyFormat()
    {
        const QList<HistoryEntry> list{entry(QStringLiteral("x"), QStringLiteral("update"), QStringLiteral("failed"))};
        QList<HistoryEntry> back = History::parse(History::serialize(list));
        QCOMPARE(back.size(), 1);
        QCOMPARE(back.first().action, QStringLiteral("update"));
        QCOMPARE(back.first().time.toSecsSinceEpoch(), list.first().time.toSecsSinceEpoch());
        QVERIFY(History::parse("not json").isEmpty());
        QVERIFY(History::parse("{\"entries\": 3}").isEmpty());
        // lines missing what, to what or when are skipped, good ones kept
        const QByteArray mixed = "{\"entries\":["
                                 "{\"ref\":\"a\",\"action\":\"install\",\"result\":\"done\",\"time\":\"2026-10-30T10:00:00Z\"},"
                                 "{\"ref\":\"b\",\"action\":\"explode\",\"result\":\"done\",\"time\":\"2026-10-30T10:00:00Z\"},"
                                 "{\"ref\":\"c\",\"action\":\"install\",\"result\":\"done\",\"time\":\"yesterday\"},"
                                 "{\"action\":\"install\",\"result\":\"done\",\"time\":\"2026-10-30T10:00:00Z\"},"
                                 "\"junk\"]}";
        back = History::parse(mixed);
        QCOMPARE(back.size(), 1);
        QCOMPARE(back.first().name, QStringLiteral("a"));
    }

    void historyGarbledFile()
    {
        QDir().mkpath(QFileInfo(historyPath()).absolutePath());
        QFile f(historyPath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{{{ broken");
        f.close();
        History h(historyPath());
        QCOMPARE(h.count(), 0);
        h.add(entry(QStringLiteral("a"), QStringLiteral("install"), QStringLiteral("done")));
        QCOMPARE(History(historyPath()).count(), 1);
    }

    void historyPathFollowsXdg()
    {
        qputenv("XDG_STATE_HOME", "/somewhere/state");
        QCOMPARE(History::defaultPath(), QStringLiteral("/somewhere/state/raye-store/history.json"));
        qputenv("XDG_STATE_HOME", "relative/state");
        QVERIFY(History::defaultPath().endsWith(QStringLiteral("/.local/state/raye-store/history.json")));
        qunsetenv("XDG_STATE_HOME");
        QVERIFY(History::defaultPath().endsWith(QStringLiteral("/.local/state/raye-store/history.json")));
    }

    // the real extras backend against a fake helper that speaks the
    // same lines as raye-store-extra
    void extrasHelperProtocol()
    {
        const QString helper = m_dir.filePath(QStringLiteral("fake-extra"));
        QFile f(helper);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/usr/bin/python3 -I\n"
                "import select, sys, time\n"
                "verb, pkg = sys.argv[1], sys.argv[2]\n"
                "if pkg == 'slow':\n"
                "    print('phase downloading', flush=True)\n"
                "    for i in range(200):\n"
                "        print(f'progress {i // 2}', flush=True)\n"
                "        print(f'bytes {i * 1000} 200000', flush=True)\n"
                "        r, _, _ = select.select([sys.stdin], [], [], 0.05)\n"
                "        if r and sys.stdin.readline().strip() == 'cancel':\n"
                "            print('raye-store-extra: cancelled', file=sys.stderr, flush=True)\n"
                "            sys.exit(3)\n"
                "    sys.exit(0)\n"
                "if pkg == 'own-window':\n"
                "    print('phase downloading', flush=True)\n"
                "    print('phase setup', flush=True)\n"
                "    print('progress 40', flush=True)\n"
                "    sys.exit(0)\n"
                "if pkg == 'broken':\n"
                "    print('raye-store-extra: the download does not match the SHA-256 in the catalog', file=sys.stderr)\n"
                "    sys.exit(1)\n"
                "print('phase downloading', flush=True)\n"
                "print('progress 50', flush=True)\n"
                "print('bytes 500 1000', flush=True)\n"
                "print('phase installing', flush=True)\n"
                "print('Setting up ' + pkg, flush=True)\n");
        f.close();
        QVERIFY(f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        JobBackend *backend = Backends::extras({helper});
        QList<JobProgress> steps;
        QList<bool> cancellable;
        QList<JobResult> results;
        connect(backend, &JobBackend::progress, this, [&](const JobProgress &p) { steps.append(p); });
        connect(backend, &JobBackend::cancellableChanged, this, [&](bool c) { cancellable.append(c); });
        connect(backend, &JobBackend::finished, this, [&](const JobResult &r) { results.append(r); });

        JobSpec job{1, QStringLiteral("install"), QStringLiteral("raye"), QStringLiteral("artcraft-launcher"), {}, {}, {}};
        backend->start(job);
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 10000);
        QVERIFY(results.first().ok);
        bool sawHalf = false;
        for (const JobProgress &p : std::as_const(steps))
            sawHalf |= p.percent == 50 && p.bytesDone == 500 && p.bytesTotal == 1000 && p.downloading;
        QVERIFY(sawHalf);
        QVERIFY(!steps.last().downloading);
        QVERIFY(cancellable.contains(false)); // not while it installs
        QVERIFY(!steps.last().setupWindow);

        // a setup in its own window says so, and how far it is still counts
        steps.clear();
        job.ref = QStringLiteral("own-window");
        backend->start(job);
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 10000);
        QVERIFY(!steps.last().downloading);
        QVERIFY(steps.last().setupWindow);
        QCOMPARE(steps.last().percent, 40);
        results.removeLast();

        job.ref = QStringLiteral("broken");
        backend->start(job);
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 10000);
        QVERIFY(!results.last().ok);
        QVERIFY(results.last().detail.contains(QStringLiteral("SHA-256")));

        steps.clear();
        job.ref = QStringLiteral("slow");
        backend->start(job);
        QTRY_VERIFY_WITH_TIMEOUT(!steps.isEmpty() && steps.last().percent >= 2, 10000);
        backend->cancel();
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 3, 10000);
        QVERIFY(results.last().cancelled);
        QVERIFY(!results.last().ok);
        delete backend;
    }

    // RAYE_STORE_LIVE_FLATPAK=app/<id>/x86_64/stable: a real install and
    // removal (as root, against Flathub)
    void liveFlatpak()
    {
        const QString ref = qEnvironmentVariable("RAYE_STORE_LIVE_FLATPAK");
        if (ref.isEmpty())
            QSKIP("set RAYE_STORE_LIVE_FLATPAK to a small Flathub app ref to run");
        auto *history = new History(m_dir.filePath(QStringLiteral("live/history.json")));
        Installer live({{QStringLiteral("flatpak"), Backends::flatpak()}}, history);
        QList<int> percents;
        QList<int> states;
        QStringList texts;
        connect(&live, &Installer::jobsChanged, this, [&] {
            const QVariantMap job = live.job(ref);
            const int state = job.value(QStringLiteral("state")).toInt();
            states.append(state);
            if (state == Installer::Downloading || state == Installer::Installing)
                percents.append(job.value(QStringLiteral("percent")).toInt());
            const QString text = job.value(QStringLiteral("progressText")).toString();
            if (!text.isEmpty() && (texts.isEmpty() || texts.last() != text))
                texts.append(text);
        });
        QSignalSpy finished(&live, &Installer::finished);
        const QVariantMap app{{QStringLiteral("source"), QStringLiteral("flatpak")}, {QStringLiteral("ref"), ref}};

        // first a Get cancelled while it downloads
        QVERIFY(live.get(app) > 0);
        QTRY_VERIFY_WITH_TIMEOUT(live.job(ref).value(QStringLiteral("percent")).toInt() >= 2, 10 * 60 * 1000);
        QVERIFY(live.cancel(ref));
        QVERIFY(finished.wait(5 * 60 * 1000));
        qInfo() << "cancelled:" << finished.first() << history->entries().first().result;
        QCOMPARE(finished.first().at(0).toBool(), false);
        QCOMPARE(history->entries().first().result, QStringLiteral("cancelled"));
        QCOMPARE(live.job(ref).value(QStringLiteral("state")).toInt(), int(Installer::Idle));
        finished.clear();
        states.clear();
        percents.clear();
        texts.clear();

        QVERIFY(live.get(app) > 0);
        QVERIFY(finished.wait(30 * 60 * 1000));
        qInfo() << "install:" << finished.first() << "states seen" << QSet<int>(states.cbegin(), states.cend())
                << "highest percent" << *std::max_element(percents.cbegin(), percents.cend());
        QVERIFY(finished.first().at(0).toBool());
        QVERIFY(states.contains(int(Installer::Downloading)));
        qInfo() << "progress texts" << texts.mid(0, 3) << "..." << texts.mid(std::max<qsizetype>(0, texts.size() - 3));
        // the bar never runs backwards
        QList<int> known;
        for (int p : std::as_const(percents))
            if (p >= 0)
                known.append(p);
        QVERIFY(std::is_sorted(known.cbegin(), known.cend()));

        live.checkUpdates();
        QTRY_VERIFY_WITH_TIMEOUT(!live.checking(), 120000);
        qInfo() << "updates after install:" << live.updateCount() << live.updatesError();

        finished.clear();
        QVERIFY(live.uninstall({{QStringLiteral("source"), QStringLiteral("flatpak")}, {QStringLiteral("ref"), ref}}) > 0);
        QVERIFY(finished.wait(10 * 60 * 1000));
        qInfo() << "remove:" << finished.first();
        QVERIFY(finished.first().at(0).toBool());
        QCOMPARE(history->count(), 3);
    }

    // the Store's own Installer (real backends, history file, taskbar
    // progress on the session bus) on the same live app
    void liveStoreInstaller()
    {
        const QString ref = qEnvironmentVariable("RAYE_STORE_LIVE_FLATPAK");
        if (ref.isEmpty())
            QSKIP("set RAYE_STORE_LIVE_FLATPAK to a small Flathub app ref to run");
        qputenv("XDG_STATE_HOME", m_dir.filePath(QStringLiteral("store-state")).toUtf8());
        Installer store;
        QSignalSpy finished(&store, &Installer::finished);
        const QVariantMap app{{QStringLiteral("source"), QStringLiteral("flatpak")}, {QStringLiteral("ref"), ref},
                              {QStringLiteral("name"), QStringLiteral("Live test")}};
        QVERIFY(store.get(app) > 0);
        QVERIFY(finished.wait(30 * 60 * 1000));
        QVERIFY(finished.first().at(0).toBool());
        QVERIFY(store.uninstall(app) > 0);
        QVERIFY(finished.wait(10 * 60 * 1000));
        QVERIFY(finished.last().at(0).toBool());
        QCOMPARE(History(History::defaultPath()).count(), 2);
        qunsetenv("XDG_STATE_HOME");
    }
};

QTEST_GUILESS_MAIN(TestJobs)
#include "test_jobs.moc"
