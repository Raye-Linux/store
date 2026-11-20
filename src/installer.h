// SPDX-License-Identifier: GPL-3.0-or-later
// Installs, removes and updates apps. Every Get joins a queue and one job
// runs at a time, with its progress, a way to cancel it, a plain reason
// when it fails, and a line in the history when it ends. Flathub apps go
// through libflatpak (the flatpak command when the library cannot open
// the installation), system packages through PackageKit, apps Raye offers
// itself through raye-store-extra.
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

#include <functional>

class Catalog;
class History;
class JobBackend;
class LauncherEntry;

// One thing to do to one app.
struct JobSpec {
    int id = 0;
    QString action; // "install", "remove" or "update"
    QString source; // "flatpak", "apt" or "raye"
    QString ref;    // flatpak ref ("app/<id>/<arch>/<branch>") or package name
    QString appId;
    QString name;
    QString icon;
};

// How far the running job is.
struct JobProgress {
    int percent = -1;        // 0 to 100, -1 while it cannot say
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;   // 0 when the size is not known
    bool downloading = true; // false once nothing is left to download
    bool setupWindow = false; // a Windows setup that finishes in its own window
};

// A newer version waiting for an installed app.
struct UpdateInfo {
    QString appId;
    QString ref;
    QString name;
    QString icon;
    QString oldVersion;
    QString newVersion;
    QString notes;           // AppStream markup of the new release
    qint64 downloadSize = 0;
};

struct JobResult;

// The list of running and waiting jobs ("Downloads" in the Library).
class JobsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1, AppIdRole, NameRole, IconRole, RefRole, SourceRole, ActionRole,
        StateRole, PercentRole, BytesDoneRole, BytesTotalRole, ProgressTextRole, CancellableRole, PositionRole,
        CancellingRole, SetupWindowRole
    };
    struct Row {
        JobSpec spec;
        int state = 0;           // Installer::State
        JobProgress progress;
        bool cancellable = true;
        bool cancelling = false; // Cancel was pressed, the job winds down
    };

    using QAbstractListModel::QAbstractListModel;
    int count() const { return int(m_rows.size()); }
    const QList<Row> &rows() const { return m_rows; }
    int indexOf(int id) const;
    int indexOfRef(const QString &ref) const;

    void append(const Row &row);
    void removeAt(int row);
    void update(int row, const Row &value);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void countChanged();

private:
    QList<Row> m_rows; // the first one runs, the others wait in order
};

// Apps with an update waiting ("Updates available").
class UpdatesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        AppIdRole = Qt::UserRole + 1, NameRole, IconRole, RefRole, SourceRole, OldVersionRole,
        NewVersionRole, NotesRole, NotesTextRole, DownloadSizeRole, QueuedRole
    };

    explicit UpdatesModel(QObject *parent = nullptr);
    int count() const { return int(m_updates.size()); }
    const QList<UpdateInfo> &updates() const { return m_updates; }
    void set(QList<UpdateInfo> updates);
    void removeRef(const QString &ref);
    // names, icons, versions and notes from the catalog when it has them
    void setCatalog(const Catalog *catalog);
    // what the Installer has in its queue, for the Update buttons
    void setQueued(const QStringList &refs);
    // the update as the page shows it, catalog details filled in
    UpdateInfo resolved(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void countChanged();

private:
    void reindex();

    QList<UpdateInfo> m_updates;
    QPointer<const Catalog> m_catalog;
    QHash<QString, int> m_catalogRow; // ref -> catalog index
    QStringList m_queued;
};

class Installer : public QObject
{
    Q_OBJECT
    // a job runs or waits
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // the ref of the running job, "*" while it is an update
    Q_PROPERTY(QString working READ working NOTIFY busyChanged)
    Q_PROPERTY(int flatpakUpdates READ updateCount NOTIFY updatesChanged)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY updatesChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    // plain words when the last check for updates failed, or empty
    Q_PROPERTY(QString updatesError READ updatesError NOTIFY updatesChanged)
    Q_PROPERTY(QDateTime lastChecked READ lastChecked NOTIFY updatesChanged)
    Q_PROPERTY(QAbstractItemModel *jobs READ jobs CONSTANT)
    Q_PROPERTY(QAbstractItemModel *updates READ updates CONSTANT)
    Q_PROPERTY(QAbstractItemModel *history READ history CONSTANT)
    // goes up on every change of a job: bind job(ref) to it
    Q_PROPERTY(int revision READ revision NOTIFY jobsChanged)
public:
    // what one app is going through
    enum State { Idle, Pending, Downloading, Installing, Removing, Done, Failed, Cancelled };
    Q_ENUM(State)
    // why a job failed, said plainly by reasonText()
    enum Reason {
        ReasonNone, ReasonNoNetwork, ReasonNoSpace, ReasonCancelled, ReasonNotAllowed,
        ReasonBusy, ReasonNotFound, ReasonUnknown, ReasonNeeded
    };
    Q_ENUM(Reason)
    // is an app on this PC: a check that could not run says Unknown, never Absent
    enum Presence { Present, Absent, Unknown };

    // the real backends, the history file and the taskbar progress
    explicit Installer(QObject *parent = nullptr);
    // backends by source ("flatpak", "apt", "raye"), used by the tests;
    // the Installer owns the backends and the history
    Installer(const QHash<QString, JobBackend *> &backends, History *history, QObject *parent = nullptr);
    ~Installer() override;

    bool busy() const;
    QString working() const;
    int updateCount() const { return m_updates->count(); }
    bool checking() const { return m_checking; }
    QString updatesError() const { return m_updatesError; }
    QDateTime lastChecked() const { return m_lastChecked; }
    QAbstractItemModel *jobs() const;
    QAbstractItemModel *updates() const;
    QAbstractItemModel *history() const;
    int revision() const { return m_revision; }

    // an app as the catalog gives it (appId, name, iconSource, source, ref);
    // returns the job id, 0 when it was refused
    Q_INVOKABLE int get(const QVariantMap &app);
    Q_INVOKABLE int uninstall(const QVariantMap &app);
    // the app is open right now, so removing it should ask first
    Q_INVOKABLE bool isRunning(const QVariantMap &app) const;
    Q_INVOKABLE int closeAndUninstall(const QVariantMap &app);
    Q_INVOKABLE int update(const QVariantMap &app);
    Q_INVOKABLE int queue(const QString &action, const QVariantMap &app);
    // the first Store's calls, kept for its pages
    Q_INVOKABLE void install(const QString &source, const QString &ref);
    Q_INVOKABLE void remove(const QString &source, const QString &ref);
    // one update job per app with an update, then the shared runtimes
    Q_INVOKABLE void updateAll();
    // a waiting job leaves the queue; the running one stops when it can
    Q_INVOKABLE bool cancel(const QString &ref);
    Q_INVOKABLE bool cancelJob(int id);
    // forgets the last failure of this app (the InfoBar was closed)
    Q_INVOKABLE void dismiss(const QString &ref);
    // state, percent, bytesDone, bytesTotal, progressText, cancellable,
    // cancelling, position, action, reason, reasonText of the app's job,
    // or state Idle
    Q_INVOKABLE QVariantMap job(const QString &ref) const;
    Q_INVOKABLE void checkUpdates();
    // "Get updates": check, then update every app found and the components
    // they share (runtimes, the graphics driver's extensions) right away
    Q_INVOKABLE void getUpdates();
    // fresh Flathub metadata, then catalogRefreshed()
    Q_INVOKABLE void refreshCatalog();
    // names, icons and release notes for the updates list
    Q_INVOKABLE void setCatalog(QObject *catalog);
    // starts an installed app; false when it cannot
    Q_INVOKABLE bool open(const QVariantMap &app);
    // the command that starts it, empty when there is none
    QStringList openCommand(const QVariantMap &app) const;

    // what runs once the queue is empty and nothing downloads: removes
    // the download folders installs leave (the tests put their own)
    void setCleanup(std::function<void()> cleanup) { m_cleanup = std::move(cleanup); }
    // what is running, how to close one app, and whether a job's app is
    // on this PC: the tests put their own
    void setRunningApps(std::function<QStringList()> list) { m_runningApps = std::move(list); }
    void setCloseApp(std::function<void(const QString &)> close) { m_closeApp = std::move(close); }
    void setStillInstalled(std::function<Presence(const JobSpec &)> check) { m_stillInstalled = std::move(check); }
    // what runs once a Flathub app is on this PC or gone from it: lets a
    // Chromium browser make apps of sites (webappaccess.h)
    void setWebAppAccess(std::function<void(const QString &, bool)> hook) { m_webAppAccess = std::move(hook); }
    // what runs once a game launcher from Flathub is on this PC: sets it
    // up the Raye way (raye-store-games); the tests put their own
    void setGameSetup(std::function<void(const QString &)> hook) { m_gameSetup = std::move(hook); }
    // the Flathub ids raye-store-games knows
    static QStringList gameLaunchers();
    // whether a job's app is on this PC, asked of flatpak or dpkg-query;
    // Unknown when they did not start, hung or failed
    static Presence presence(const JobSpec &job, int timeoutMs = 20000);

    // the command of the flatpak / pkgcli fallback, public for the tests
    static QStringList command(const QString &verb, const QString &source, const QString &ref);
    // a ref the given source can take: never an option, never a path
    static bool validRef(const QString &source, const QString &ref);
    // the reason hidden in a backend's message
    static Reason classify(const QString &detail);
    static QString reasonText(Reason reason);
    // reasonText, with the system's own words when no plain reason fits
    static QString failureText(Reason reason, const QString &detail);
    // "45 MB of 120 MB", "45 MB", or empty
    static QString progressText(qint64 done, qint64 total);

Q_SIGNALS:
    void busyChanged();
    void updatesChanged();
    void checkingChanged();
    void jobsChanged();
    void revisionMoved(); // a job moved on, for what is not QML
    // after every job; message is the plain reason when it failed
    void finished(bool ok, const QString &ref, const QString &message);
    // the same, with everything the "X is ready" notification needs
    void jobFinished(const QVariantMap &job);
    void catalogRefreshed(); // the Flathub metadata on disk is new

private:
    struct Outcome {
        int state = Idle;
        Reason reason = ReasonNone;
        QString message;
    };

    void setup();
    void startNext();
    void onProgress(JobBackend *backend, const JobProgress &progress);
    void onCancellable(JobBackend *backend, bool cancellable);
    void onFinished(JobBackend *backend, const JobResult &result);
    void onUpdatesChecked(const QList<UpdateInfo> &apps, const QStringList &runtimes, const QString &error);
    void changed();
    void syncQueued();
    void cleanUpWhenIdle();
    JobBackend *runningBackend() const;

    QHash<QString, JobBackend *> m_backends;
    History *m_history = nullptr;
    JobsModel *m_jobs = nullptr;
    UpdatesModel *m_updates = nullptr;
    QPointer<Catalog> m_catalog;
    LauncherEntry *m_launcher = nullptr;
    QHash<QString, Outcome> m_outcomes; // last failure by ref
    QStringList m_runtimeUpdates;
    QString m_updatesError;
    QDateTime m_lastChecked;
    int m_nextId = 1;
    int m_revision = 0;
    bool m_running = false;
    bool m_checking = false;
    bool m_checkAfterQueue = false;
    bool m_installAfterCheck = false;
    bool m_refreshing = false;
    std::function<void()> m_cleanup;
    std::function<QStringList()> m_runningApps;
    std::function<void(const QString &)> m_closeApp;
    std::function<Presence(const JobSpec &)> m_stillInstalled;
    std::function<void(const QString &, bool)> m_webAppAccess;
    std::function<void(const QString &)> m_gameSetup;
};

// How a job ended.
struct JobResult {
    bool ok = false;
    bool cancelled = false;
    Installer::Reason reason = Installer::ReasonNone; // worked out from detail when None
    QString detail;          // the backend's own words, for the logs
};

// What does the work for one source. The Installer starts one job at a
// time and waits for finished() before the next.
class JobBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void start(const JobSpec &job) = 0;
    // stops the running job when it can; finished() still follows
    virtual void cancel() = 0;
    // answers with updatesChecked(); only Flathub has app updates here
    virtual void checkUpdates();

Q_SIGNALS:
    void progress(const JobProgress &progress);
    void cancellableChanged(bool cancellable);
    void finished(const JobResult &result);
    // apps with an update, and the runtimes "Update all" updates too
    void updatesChecked(const QList<UpdateInfo> &apps, const QStringList &runtimes, const QString &error);
};

// The real backends, public so the tests can drive them.
namespace Backends
{
// libflatpak; the flatpak command when the library cannot open the
// system installation (or RAYE_STORE_FLATPAK_CLI=1 is set)
JobBackend *flatpak(QObject *parent = nullptr);
// the flatpak command only
JobBackend *flatpakCommand(QObject *parent = nullptr);
JobBackend *packageKit(QObject *parent = nullptr);
// helper: the command before "install <package>", pkexec raye-store-extra
JobBackend *extras(const QStringList &helper = {}, QObject *parent = nullptr);
}
