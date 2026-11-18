// SPDX-License-Identifier: GPL-3.0-or-later
// The queue and its models first, then the backends: libflatpak (and the
// flatpak command behind the same interface), PackageKit, and Raye's own
// helper. This file is longer than most in the Store because every way an
// app gets installed lives here, behind the one JobBackend interface.

// GLib before Qt: gio has a struct member called "signals"
#pragma push_macro("signals")
#undef signals
#include <flatpak.h>
#include <gio/gio.h>
#pragma pop_macro("signals")

#include "installer.h"
#include "windowsprograms.h"

#include "applink.h"
#include "catalog.h"
#include "cleanup.h"
#include "history.h"
#include "webappaccess.h"

#include <KLocalizedString>
#include <PackageKit/Daemon>
#include <PackageKit/Transaction>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QFutureWatcher>
#include <QThread>
#include <QtConcurrentRun>

#include <algorithm>
#include <functional>

// ---------------------------------------------------------------------------
// Plain words

namespace
{
const QString runtimesRef = QStringLiteral("runtime/*"); // "the shared runtimes", one job

// AppStream markup to one line of text
QString plainText(const QString &markup)
{
    static const QRegularExpression block(QStringLiteral("<(/?(p|li|ul|ol|br)[^>]*)>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
    QString text = markup;
    text.replace(block, QStringLiteral(" "));
    text.remove(tag);
    text.replace(QLatin1String("&lt;"), QLatin1String("<"));
    text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    text.replace(QLatin1String("&quot;"), QLatin1String("\""));
    text.replace(QLatin1String("&#39;"), QLatin1String("'"));
    text.replace(QLatin1String("&apos;"), QLatin1String("'"));
    text.replace(QLatin1String("&amp;"), QLatin1String("&"));
    return text.simplified();
}

QString nameFromRef(const QString &ref)
{
    // "app/org.gimp.GIMP/x86_64/stable" -> "org.gimp.GIMP"
    const QStringList parts = ref.split(QLatin1Char('/'));
    return parts.size() == 4 ? parts.at(1) : ref;
}
} // namespace

bool Installer::validRef(const QString &source, const QString &ref)
{
    // what the catalog hands over; never something an argv could read as
    // an option or a path
    static const QRegularExpression flatpakRef(
        QStringLiteral("^(app|runtime)/[A-Za-z0-9_][A-Za-z0-9_.-]*/[A-Za-z0-9_]+/[A-Za-z0-9_.][A-Za-z0-9_.-]*$"));
    static const QRegularExpression package(QStringLiteral("^[a-z0-9][a-z0-9+.-]+$"));
    if (source == QLatin1String("flatpak"))
        return flatpakRef.match(ref).hasMatch() && !ref.contains(QLatin1String(".."));
    if (source == QLatin1String("apt") || source == QLatin1String("raye"))
        return package.match(ref).hasMatch() && !ref.contains(QLatin1String(".."));
    if (source == QLatin1String("windows"))
        return isWindowsRef(ref);
    return false;
}

Installer::Reason Installer::classify(const QString &detail)
{
    const QString d = detail.toLower();
    const auto any = [&d](std::initializer_list<const char *> words) {
        return std::any_of(words.begin(), words.end(), [&d](const char *w) { return d.contains(QLatin1String(w)); });
    };
    if (d.isEmpty())
        return ReasonUnknown;
    if (any({"cancelled", "canceled"}))
        return ReasonCancelled;
    if (any({"no space left", "not enough disk space", "not enough space", "enospc", "disk full", "out of space"}))
        return ReasonNoSpace;
    if (any({"not authorized", "not allowed", "permission denied", "access denied", "authentication"}))
        return ReasonNotAllowed;
    if (any({"is needed for", "needed by", "dependency resolution", "dependencies", "depends on"}))
        return ReasonNeeded;
    if (any({"could not get lock", "unable to acquire", "is locked", "lock-frontend", "another process"}))
        return ReasonBusy;
    if (any({"resolve", "network is unreachable", "no route to host", "timeout was reached", "timed out",
             "could not connect", "couldn't connect", "connection refused", "name resolution", "urlopen error",
             "network"}))
        return ReasonNoNetwork;
    if (any({"nothing matches", "no remote refs", "not found", "not offered by raye", "no such ref", "unable to find"}))
        return ReasonNotFound;
    return ReasonUnknown;
}

QString Installer::reasonText(Reason reason)
{
    switch (reason) {
    case ReasonNone: return {};
    case ReasonNoNetwork: return i18n("You're offline. Check your internet connection and try again.");
    case ReasonNoSpace: return i18n("There isn't enough space on this PC. Free up some space and try again.");
    case ReasonCancelled: return i18n("Canceled.");
    case ReasonNotAllowed: return i18n("You need root access to do this.");
    case ReasonBusy: return i18n("Another install or update is running. Try again when it's done.");
    case ReasonNotFound: return i18n("This app isn't available right now.");
    case ReasonNeeded: return i18n("Other apps or Raye itself need this, so it can't be removed.");
    case ReasonUnknown: break;
    }
    return i18n("Something went wrong. Try again later.");
}

QString Installer::failureText(Reason reason, const QString &detail)
{
    if (reason != ReasonUnknown || detail.trimmed().isEmpty())
        return reasonText(reason);
    // no plain words for this one: say what the system said, not "something"
    const QString line = detail.trimmed().section(QLatin1Char('\n'), 0, 0).left(200);
    return i18n("That didn't work. The system says: %1", line);
}

QString Installer::progressText(qint64 done, qint64 total)
{
    if (done <= 0 && total <= 0)
        return {};
    // both numbers in the unit of the larger one, like "0 MB of 907 MB";
    // whole numbers below a gigabyte
    const qint64 larger = std::max(done, total);
    const QLocale locale;
    const auto size = [&locale, larger](qint64 bytes) {
        bytes = std::max<qint64>(bytes, 0);
        if (larger >= 1024LL * 1024 * 1024)
            return i18nc("a size in gigabytes", "%1 GB", locale.toString(bytes / (1024.0 * 1024 * 1024), 'f', 1));
        if (larger >= 1024LL * 1024)
            return i18nc("a size in megabytes", "%1 MB", locale.toString(qRound64(bytes / (1024.0 * 1024))));
        if (larger >= 1024)
            return i18nc("a size in kilobytes", "%1 KB", locale.toString(qRound64(bytes / 1024.0)));
        return i18nc("a size in bytes", "%1 bytes", locale.toString(bytes));
    };
    if (total <= 0)
        return size(done);
    return i18nc("bytes downloaded of the whole download", "%1 of %2", size(std::min(done, total)), size(total));
}

QStringList Installer::command(const QString &verb, const QString &source, const QString &ref)
{
    // Raye's own entries: the helper downloads, checks the SHA-256 and
    // installs; the sudo group may run it without a password
    if (source == QLatin1String("raye") && (verb == QLatin1String("install") || verb == QLatin1String("remove")))
        return {QStringLiteral("pkexec"), QStringLiteral("/usr/libexec/raye-store-extra"), verb, ref};
    // Windows programs go in the user's own spaces: no root needed
    if (source == QLatin1String("windows") && (verb == QLatin1String("install") || verb == QLatin1String("remove")))
        return {QStringLiteral("/usr/libexec/raye-store-windows"), verb, ref};
    const bool flatpak = source == QLatin1String("flatpak");
    if (verb == QLatin1String("install"))
        return flatpak ? QStringList{QStringLiteral("flatpak"), QStringLiteral("install"), QStringLiteral("--system"),
                                     QStringLiteral("-y"), QStringLiteral("--noninteractive"), QStringLiteral("flathub"), ref}
                       : QStringList{QStringLiteral("pkgcli"), QStringLiteral("-y"), QStringLiteral("install"), ref};
    if (verb == QLatin1String("remove"))
        return flatpak ? QStringList{QStringLiteral("flatpak"), QStringLiteral("uninstall"), QStringLiteral("--system"),
                                     QStringLiteral("-y"), QStringLiteral("--noninteractive"), ref}
                       : QStringList{QStringLiteral("pkgcli"), QStringLiteral("-y"), QStringLiteral("remove"), ref};
    if (verb == QLatin1String("update"))
        return {QStringLiteral("flatpak"), QStringLiteral("update"), QStringLiteral("--system"),
                QStringLiteral("-y"), QStringLiteral("--noninteractive")};
    return {};
}

// ---------------------------------------------------------------------------
// Models

int JobsModel::indexOf(int id) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).spec.id == id)
            return i;
    return -1;
}

int JobsModel::indexOfRef(const QString &ref) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).spec.ref == ref)
            return i;
    return -1;
}

void JobsModel::append(const Row &row)
{
    const int at = int(m_rows.size());
    beginInsertRows({}, at, at);
    m_rows.append(row);
    endInsertRows();
    Q_EMIT countChanged();
}

void JobsModel::removeAt(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    // positions of the others moved
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), {PositionRole});
    Q_EMIT countChanged();
}

void JobsModel::update(int row, const Row &value)
{
    if (row < 0 || row >= m_rows.size())
        return;
    m_rows[row] = value;
    Q_EMIT dataChanged(index(row), index(row));
}

int JobsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant JobsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return r.spec.name;
    case IdRole: return r.spec.id;
    case AppIdRole: return r.spec.appId;
    case IconRole: return r.spec.icon;
    case RefRole: return r.spec.ref;
    case SourceRole: return r.spec.source;
    case ActionRole: return r.spec.action;
    case StateRole: return r.state;
    case PercentRole: return r.progress.percent;
    case BytesDoneRole: return r.progress.bytesDone;
    case BytesTotalRole: return r.progress.bytesTotal;
    case ProgressTextRole: return Installer::progressText(r.progress.bytesDone, r.progress.bytesTotal);
    case CancellableRole: return r.cancellable;
    case CancellingRole: return r.cancelling;
    case SetupWindowRole: return r.progress.setupWindow;
    case PositionRole: return index.row();
    }
    return {};
}

QHash<int, QByteArray> JobsModel::roleNames() const
{
    return {
        {IdRole, "jobId"}, {AppIdRole, "appId"}, {NameRole, "name"}, {IconRole, "icon"}, {RefRole, "ref"},
        {SourceRole, "source"}, {ActionRole, "action"}, {StateRole, "state"}, {PercentRole, "percent"},
        {BytesDoneRole, "bytesDone"}, {BytesTotalRole, "bytesTotal"}, {ProgressTextRole, "progressText"},
        {CancellableRole, "cancellable"}, {PositionRole, "position"}, {CancellingRole, "cancelling"},
        {SetupWindowRole, "setupWindow"},
    };
}

UpdatesModel::UpdatesModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void UpdatesModel::set(QList<UpdateInfo> updates)
{
    beginResetModel();
    m_updates = std::move(updates);
    endResetModel();
    Q_EMIT countChanged();
}

void UpdatesModel::removeRef(const QString &ref)
{
    for (int i = 0; i < m_updates.size(); ++i) {
        if (m_updates.at(i).ref == ref) {
            beginRemoveRows({}, i, i);
            m_updates.removeAt(i);
            endRemoveRows();
            Q_EMIT countChanged();
            return;
        }
    }
}

void UpdatesModel::setCatalog(const Catalog *catalog)
{
    if (m_catalog)
        disconnect(m_catalog, nullptr, this, nullptr);
    m_catalog = catalog;
    if (catalog) {
        connect(catalog, &QAbstractItemModel::modelReset, this, &UpdatesModel::reindex);
        connect(catalog, &QAbstractItemModel::rowsInserted, this, &UpdatesModel::reindex);
        connect(catalog, &QAbstractItemModel::rowsRemoved, this, &UpdatesModel::reindex);
    }
    reindex();
}

void UpdatesModel::reindex()
{
    m_catalogRow.clear();
    if (m_catalog) {
        const QList<App> &apps = m_catalog->apps();
        for (int i = 0; i < apps.size(); ++i)
            if (apps.at(i).source == QLatin1String("flatpak"))
                m_catalogRow.insert(apps.at(i).ref, i);
    }
    if (!m_updates.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_updates.size()) - 1));
}

void UpdatesModel::setQueued(const QStringList &refs)
{
    if (refs == m_queued)
        return;
    m_queued = refs;
    if (!m_updates.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_updates.size()) - 1), {QueuedRole});
}

UpdateInfo UpdatesModel::resolved(int row) const
{
    UpdateInfo u = m_updates.value(row);
    const int at = m_catalogRow.value(u.ref, -1);
    if (!m_catalog || at < 0 || at >= m_catalog->apps().size())
        return u;
    const App &app = m_catalog->apps().at(at);
    if (u.name.isEmpty() || u.name == u.appId)
        u.name = app.name;
    if (u.icon.isEmpty())
        u.icon = app.icon;
    if (u.newVersion.isEmpty() && !app.releases.isEmpty()) {
        // the catalog may still describe the installed version: then say nothing
        const Release &newest = app.releases.first();
        if (!newest.version.isEmpty() && newest.version != u.oldVersion) {
            u.newVersion = newest.version;
            if (u.notes.isEmpty())
                u.notes = newest.notes;
        }
    }
    return u;
}

int UpdatesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_updates.size());
}

QVariant UpdatesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_updates.size())
        return {};
    const UpdateInfo u = resolved(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return u.name.isEmpty() ? u.appId : u.name;
    case AppIdRole: return u.appId;
    case IconRole: return u.icon;
    case RefRole: return u.ref;
    case SourceRole: return QStringLiteral("flatpak");
    case OldVersionRole: return u.oldVersion;
    case NewVersionRole: return u.newVersion == u.oldVersion ? QString() : u.newVersion;
    case NotesRole: return u.notes;
    case NotesTextRole: return plainText(u.notes);
    case DownloadSizeRole: return u.downloadSize;
    case QueuedRole: return m_queued.contains(u.ref);
    }
    return {};
}

QHash<int, QByteArray> UpdatesModel::roleNames() const
{
    return {
        {AppIdRole, "appId"}, {NameRole, "name"}, {IconRole, "icon"}, {RefRole, "ref"}, {SourceRole, "source"},
        {OldVersionRole, "oldVersion"}, {NewVersionRole, "newVersion"}, {NotesRole, "notes"},
        {NotesTextRole, "notesText"}, {DownloadSizeRole, "downloadSize"}, {QueuedRole, "queued"},
    };
}

// ---------------------------------------------------------------------------
// Progress on the Store's taskbar button (com.canonical.Unity.LauncherEntry)

class LauncherEntry
{
public:
    LauncherEntry()
        : m_bus(g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr))
    {
    }
    ~LauncherEntry()
    {
        if (m_bus)
            g_object_unref(m_bus);
    }
    LauncherEntry(const LauncherEntry &) = delete;
    LauncherEntry &operator=(const LauncherEntry &) = delete;

    // percent < 0 hides the bar
    void show(int percent)
    {
        if (!m_bus || percent == m_shown)
            return;
        m_shown = percent;
        GVariantBuilder props;
        g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&props, "{sv}", "progress", g_variant_new_double(std::clamp(percent, 0, 100) / 100.0));
        g_variant_builder_add(&props, "{sv}", "progress-visible", g_variant_new_boolean(percent >= 0));
        g_dbus_connection_emit_signal(m_bus, nullptr, "/org/raye/store/launcherentry", "com.canonical.Unity.LauncherEntry",
                                      "Update", g_variant_new("(sa{sv})", "application://org.raye.store.desktop", &props),
                                      nullptr);
    }

private:
    GDBusConnection *m_bus = nullptr;
    int m_shown = -1;
};

// ---------------------------------------------------------------------------
// The queue

void JobBackend::checkUpdates()
{
    Q_EMIT updatesChecked({}, {}, {});
}

Installer::Installer(QObject *parent)
    : QObject(parent)
{
    m_backends = {{QStringLiteral("flatpak"), Backends::flatpak(this)},
                  {QStringLiteral("apt"), Backends::packageKit(this)},
                  {QStringLiteral("raye"), Backends::extras({}, this)},
                  {QStringLiteral("windows"), Backends::extras({QStringLiteral("/usr/libexec/raye-store-windows")}, this)}};
    m_history = new History(History::defaultPath(), this);
    m_launcher = new LauncherEntry;
    m_runningApps = [] {
        QProcess ps;
        ps.start(QStringLiteral("flatpak"), {QStringLiteral("ps"), QStringLiteral("--columns=application")});
        ps.waitForFinished(5000);
        return QString::fromUtf8(ps.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    };
    m_closeApp = [](const QString &id) {
        QProcess kill;
        kill.start(QStringLiteral("flatpak"), {QStringLiteral("kill"), id});
        // "not running" is an error for flatpak, and fine here
        kill.waitForFinished(5000);
    };
    m_stillInstalled = [](const JobSpec &job) { return presence(job); };
    m_webAppAccess = [](const QString &id, bool installed) {
        if (!(installed ? WebAppAccess::grant(id) : WebAppAccess::revoke(id)))
            qWarning("raye-store: could not change the folders %s may write in", qPrintable(id));
    };
    m_gameSetup = [](const QString &id) {
        if (gameLaunchers().contains(id))
            QProcess::startDetached(QStringLiteral("/usr/libexec/raye-store-games"), {QStringLiteral("setup"), id});
    };
    // off the main thread: it looks through /proc
    m_cleanup = [this] {
        auto *watcher = new QFutureWatcher<QStringList>(this);
        connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher] {
            watcher->deleteLater();
            // a job of ours that started meanwhile keeps its folder
            if (m_running || m_refreshing || !m_jobs->rows().isEmpty())
                return;
            const int removed = Cleanup::removeFolders(watcher->result());
            if (removed > 0)
                qInfo("raye-store: removed %d download folder(s) left by installs", removed);
        });
        watcher->setFuture(QtConcurrent::run([] { return Cleanup::findFlatpakCaches(); }));
    };
    setup();
}

Installer::Installer(const QHash<QString, JobBackend *> &backends, History *history, QObject *parent)
    : QObject(parent)
    , m_backends(backends)
    , m_history(history)
{
    for (JobBackend *backend : std::as_const(m_backends))
        backend->setParent(this);
    if (!m_history)
        m_history = new History(History::defaultPath(), this);
    m_history->setParent(this);
    setup();
}

Installer::~Installer()
{
    if (m_launcher)
        m_launcher->show(-1);
    delete m_launcher;
}

void Installer::setup()
{
    m_jobs = new JobsModel(this);
    m_updates = new UpdatesModel(this);
    for (JobBackend *backend : std::as_const(m_backends)) {
        connect(backend, &JobBackend::progress, this, [this, backend](const JobProgress &p) { onProgress(backend, p); });
        connect(backend, &JobBackend::cancellableChanged, this, [this, backend](bool c) { onCancellable(backend, c); });
        connect(backend, &JobBackend::finished, this, [this, backend](const JobResult &r) { onFinished(backend, r); });
    }
    if (JobBackend *flatpak = m_backends.value(QStringLiteral("flatpak")))
        connect(flatpak, &JobBackend::updatesChecked, this, &Installer::onUpdatesChecked);
}

Installer::Presence Installer::presence(const JobSpec &job, int timeoutMs)
{
    QProcess list;
    const bool flatpak = job.source == QLatin1String("flatpak");
    if (job.source == QLatin1String("windows")) {
        // a space with no program left in it is as good as gone
        list.start(QStringLiteral("raye-windows"), {QStringLiteral("list"), windowsSpace(job.ref), QStringLiteral("--json")});
        if (!list.waitForStarted(timeoutMs) || !list.waitForFinished(timeoutMs) || list.exitStatus() != QProcess::NormalExit) {
            list.kill();
            return Unknown;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(list.readAllStandardOutput());
        if (list.exitCode() != 0 || !doc.isArray())
            return Unknown;
        return doc.array().isEmpty() ? Absent : Present;
    }
    if (flatpak)
        list.start(QStringLiteral("flatpak"), {QStringLiteral("list"), QStringLiteral("--app"), QStringLiteral("--columns=ref")});
    else
        list.start(QStringLiteral("dpkg-query"), {QStringLiteral("-W"), QStringLiteral("-f=${db:Status-Abbrev}"), job.ref});
    if (!list.waitForStarted(timeoutMs))
        return Unknown;
    if (!list.waitForFinished(timeoutMs)) {
        list.kill();
        list.waitForFinished(1000);
        return Unknown;
    }
    if (list.exitStatus() != QProcess::NormalExit)
        return Unknown;
    const QByteArray out = list.readAllStandardOutput();
    if (flatpak) {
        if (list.exitCode() != 0)
            return Unknown;
        const QString name = job.ref.startsWith(QLatin1String("app/")) ? job.ref.mid(4) : job.ref;
        return QString::fromUtf8(out).split(QLatin1Char('\n')).contains(name) ? Present : Absent;
    }
    // dpkg-query says 1 and "no packages found" for a name it never saw
    if (list.exitCode() == 1 && list.readAllStandardError().contains("no packages found"))
        return Absent;
    if (list.exitCode() != 0)
        return Unknown;
    return out.startsWith("ii") ? Present : Absent;
}

bool Installer::busy() const
{
    return m_jobs->count() > 0;
}

QString Installer::working() const
{
    if (!m_running || m_jobs->rows().isEmpty())
        return {};
    const JobSpec &spec = m_jobs->rows().first().spec;
    return spec.action == QLatin1String("update") ? QStringLiteral("*") : spec.ref;
}

QAbstractItemModel *Installer::jobs() const
{
    return m_jobs;
}

QAbstractItemModel *Installer::updates() const
{
    return m_updates;
}

QAbstractItemModel *Installer::history() const
{
    return m_history;
}

JobBackend *Installer::runningBackend() const
{
    if (!m_running || m_jobs->rows().isEmpty())
        return nullptr;
    return m_backends.value(m_jobs->rows().first().spec.source);
}

int Installer::get(const QVariantMap &app)
{
    return queue(QStringLiteral("install"), app);
}

int Installer::uninstall(const QVariantMap &app)
{
    return queue(QStringLiteral("remove"), app);
}

bool Installer::isRunning(const QVariantMap &app) const
{
    if (app.value(QStringLiteral("source")).toString() != QLatin1String("flatpak") || !m_runningApps)
        return false;
    return m_runningApps().contains(nameFromRef(app.value(QStringLiteral("ref")).toString()));
}

int Installer::closeAndUninstall(const QVariantMap &app)
{
    if (m_closeApp && isRunning(app))
        m_closeApp(nameFromRef(app.value(QStringLiteral("ref")).toString()));
    return uninstall(app);
}

int Installer::update(const QVariantMap &app)
{
    return queue(QStringLiteral("update"), app);
}

void Installer::install(const QString &source, const QString &ref)
{
    queue(QStringLiteral("install"), {{QStringLiteral("source"), source}, {QStringLiteral("ref"), ref}});
}

void Installer::remove(const QString &source, const QString &ref)
{
    queue(QStringLiteral("remove"), {{QStringLiteral("source"), source}, {QStringLiteral("ref"), ref}});
}

int Installer::queue(const QString &action, const QVariantMap &app)
{
    static const QStringList actions{QStringLiteral("install"), QStringLiteral("remove"), QStringLiteral("update")};
    JobSpec spec;
    spec.action = action;
    spec.source = app.value(QStringLiteral("source")).toString();
    spec.ref = app.value(QStringLiteral("ref")).toString();
    if (!actions.contains(action) || !m_backends.contains(spec.source) || !validRef(spec.source, spec.ref))
        return 0;
    // the system's copy is not this listing's to remove or update, and
    // Install would be a second copy
    if (m_catalog && !m_catalog->systemCopy(spec.ref).isEmpty())
        return 0;
    // system packages are updated by Raye Update
    if (action == QLatin1String("update") && spec.source != QLatin1String("flatpak"))
        return 0;
    spec.appId = app.value(QStringLiteral("appId")).toString();
    spec.name = app.value(QStringLiteral("name")).toString();
    spec.icon = app.value(QStringLiteral("iconSource"), app.value(QStringLiteral("icon"))).toString();
    if (spec.name.isEmpty())
        spec.name = nameFromRef(spec.ref);

    for (const JobsModel::Row &row : m_jobs->rows())
        if (row.spec.ref == spec.ref && row.spec.action == spec.action)
            return row.spec.id;

    spec.id = m_nextId++;
    m_outcomes.remove(spec.ref);
    JobsModel::Row row;
    row.spec = spec;
    row.state = Pending;
    m_jobs->append(row);
    syncQueued();
    changed();
    startNext();
    return spec.id;
}

void Installer::updateAll()
{
    for (int i = 0; i < m_updates->count(); ++i) {
        const UpdateInfo u = m_updates->resolved(i);
        queue(QStringLiteral("update"), {{QStringLiteral("source"), QStringLiteral("flatpak")}, {QStringLiteral("ref"), u.ref},
                                         {QStringLiteral("appId"), u.appId}, {QStringLiteral("name"), u.name},
                                         {QStringLiteral("iconSource"), u.icon}});
    }
    if (m_runtimeUpdates.isEmpty() || m_jobs->indexOfRef(runtimesRef) >= 0)
        return;
    // the runtimes apps share: one job, after the apps
    JobsModel::Row row;
    row.spec = {m_nextId++, QStringLiteral("update"), QStringLiteral("flatpak"), runtimesRef, {},
                i18n("App components"), QStringLiteral("application-x-addon")};
    row.state = Pending;
    m_jobs->append(row);
    syncQueued();
    changed();
    startNext();
}

void Installer::startNext()
{
    if (m_running)
        return;
    if (m_jobs->rows().isEmpty()) {
        if (m_launcher)
            m_launcher->show(-1);
        cleanUpWhenIdle();
        if (m_checkAfterQueue) {
            m_checkAfterQueue = false;
            checkUpdates();
        }
        return;
    }
    JobsModel::Row row = m_jobs->rows().first();
    JobBackend *backend = m_backends.value(row.spec.source);
    m_running = true;
    if (row.spec.action == QLatin1String("remove")) {
        row.state = Removing;
        row.cancellable = false;
    }
    m_jobs->update(0, row);
    changed();
    backend->start(row.spec);
}

void Installer::onProgress(JobBackend *backend, const JobProgress &progress)
{
    if (backend != runningBackend())
        return;
    JobsModel::Row row = m_jobs->rows().first();
    row.progress = progress;
    if (row.spec.action == QLatin1String("remove"))
        row.state = Removing;
    else
        row.state = progress.downloading ? Downloading : Installing;
    m_jobs->update(0, row);
    // an unknown step keeps the bar where it was
    if (m_launcher && progress.percent >= 0)
        m_launcher->show(progress.percent);
    changed();
}

void Installer::onCancellable(JobBackend *backend, bool cancellable)
{
    if (backend != runningBackend())
        return;
    JobsModel::Row row = m_jobs->rows().first();
    // once cancelled it stays so
    row.cancellable = cancellable && !row.cancelling;
    m_jobs->update(0, row);
    changed();
}

void Installer::onFinished(JobBackend *backend, const JobResult &result)
{
    if (backend != runningBackend())
        return;
    const JobSpec spec = m_jobs->rows().first().spec;
    m_jobs->removeAt(0);
    m_running = false;

    Reason reason = ReasonNone;
    State state = Done;
    if (!result.ok) {
        reason = result.reason != ReasonNone ? result.reason : classify(result.detail);
        if (result.cancelled)
            reason = ReasonCancelled;
        state = reason == ReasonCancelled ? Cancelled : Failed;
        if (state == Failed)
            qWarning("raye-store: %s %s failed: %s", qPrintable(spec.action), qPrintable(spec.ref), qPrintable(result.detail));
    }
    QString detail = result.detail;
    // a removal is judged by what is on this PC, not by what the last
    // step of it said
    if (spec.action == QLatin1String("remove") && m_stillInstalled && state != Cancelled) {
        // when it cannot be told, the backend's word stands
        const Presence there = m_stillInstalled(spec);
        if (state == Failed && there == Absent) {
            state = Done;
            reason = ReasonNone;
        } else if (state == Done && there == Present) {
            state = Failed;
            reason = ReasonUnknown;
            detail = i18n("%1 is still on this PC.", spec.name);
        }
    }
    const QString message = state == Failed ? failureText(reason, detail) : reasonText(reason);
    if (state == Failed)
        m_outcomes.insert(spec.ref, {Failed, reason, message});
    else
        m_outcomes.remove(spec.ref);

    HistoryEntry entry;
    entry.appId = spec.appId;
    entry.name = spec.name;
    entry.icon = spec.icon;
    entry.ref = spec.ref;
    entry.source = spec.source;
    entry.action = spec.action;
    entry.result = state == Done ? QStringLiteral("done") : state == Cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed");
    entry.message = message;
    m_history->add(entry);

    if (state == Done && spec.source == QLatin1String("flatpak") && m_webAppAccess
        && (spec.action == QLatin1String("install") || spec.action == QLatin1String("remove")))
        m_webAppAccess(nameFromRef(spec.ref), spec.action == QLatin1String("install"));
    if (state == Done && spec.source == QLatin1String("flatpak") && spec.action == QLatin1String("install") && m_gameSetup)
        m_gameSetup(nameFromRef(spec.ref));

    if (state == Done && spec.action != QLatin1String("install")) {
        if (spec.ref == runtimesRef)
            m_runtimeUpdates.clear();
        m_updates->removeRef(spec.ref);
        Q_EMIT updatesChanged();
    }

    syncQueued();
    changed();
    Q_EMIT finished(state == Done, spec.ref, message);
    Q_EMIT jobFinished({{QStringLiteral("jobId"), spec.id}, {QStringLiteral("appId"), spec.appId},
                        {QStringLiteral("name"), spec.name}, {QStringLiteral("icon"), spec.icon},
                        {QStringLiteral("ref"), spec.ref}, {QStringLiteral("source"), spec.source},
                        {QStringLiteral("action"), spec.action}, {QStringLiteral("state"), int(state)},
                        {QStringLiteral("reason"), int(reason)}, {QStringLiteral("reasonText"), message}});
    startNext();
}

QStringList Installer::gameLaunchers()
{
    return {QStringLiteral("com.valvesoftware.Steam")};
}

bool Installer::cancel(const QString &ref)
{
    const int row = m_jobs->indexOfRef(ref);
    return row >= 0 && cancelJob(m_jobs->rows().at(row).spec.id);
}

bool Installer::cancelJob(int id)
{
    const int row = m_jobs->indexOf(id);
    if (row < 0)
        return false;
    if (row > 0 || !m_running) {
        // still waiting: it just leaves the queue
        m_jobs->removeAt(row);
        syncQueued();
        changed();
        startNext();
        return true;
    }
    JobsModel::Row running = m_jobs->rows().first();
    if (!running.cancellable)
        return false;
    // "Canceling…" while the backend stops and takes back what it put
    running.cancelling = true;
    running.cancellable = false;
    m_jobs->update(0, running);
    changed();
    runningBackend()->cancel();
    return true;
}

void Installer::dismiss(const QString &ref)
{
    if (m_outcomes.remove(ref))
        changed();
}

QVariantMap Installer::job(const QString &ref) const
{
    const int row = m_jobs->indexOfRef(ref);
    if (row >= 0) {
        const JobsModel::Row &r = m_jobs->rows().at(row);
        return {{QStringLiteral("jobId"), r.spec.id},
                {QStringLiteral("state"), r.state},
                {QStringLiteral("action"), r.spec.action},
                {QStringLiteral("source"), r.spec.source},
                {QStringLiteral("percent"), r.progress.percent},
                {QStringLiteral("bytesDone"), r.progress.bytesDone},
                {QStringLiteral("bytesTotal"), r.progress.bytesTotal},
                {QStringLiteral("progressText"), progressText(r.progress.bytesDone, r.progress.bytesTotal)},
                {QStringLiteral("cancellable"), r.cancellable},
                {QStringLiteral("cancelling"), r.cancelling},
                {QStringLiteral("setupWindow"), r.progress.setupWindow},
                {QStringLiteral("position"), row},
                {QStringLiteral("reason"), int(ReasonNone)},
                {QStringLiteral("reasonText"), QString()}};
    }
    const auto outcome = m_outcomes.constFind(ref);
    if (outcome != m_outcomes.cend())
        return {{QStringLiteral("state"), outcome->state},
                {QStringLiteral("reason"), int(outcome->reason)},
                {QStringLiteral("reasonText"), outcome->message}};
    return {{QStringLiteral("state"), int(Idle)}};
}

void Installer::checkUpdates()
{
    if (m_checking)
        return;
    // the list would be stale before the queue is done: check after
    if (busy()) {
        m_checkAfterQueue = true;
        return;
    }
    JobBackend *flatpak = m_backends.value(QStringLiteral("flatpak"));
    if (!flatpak)
        return;
    m_checking = true;
    Q_EMIT checkingChanged();
    flatpak->checkUpdates();
}

void Installer::onUpdatesChecked(const QList<UpdateInfo> &apps, const QStringList &runtimes, const QString &error)
{
    if (!m_checking)
        return;
    m_checking = false;
    if (error.isEmpty()) {
        m_updates->set(apps);
        m_runtimeUpdates = runtimes;
        m_updatesError.clear();
        m_lastChecked = QDateTime::currentDateTimeUtc();
    } else {
        // keep what we knew; say why it is not fresh
        m_updatesError = reasonText(classify(error));
        qWarning("raye-store: checking for updates failed: %s", qPrintable(error));
    }
    syncQueued();
    Q_EMIT updatesChanged();
    Q_EMIT checkingChanged();
}

void Installer::refreshCatalog()
{
    // allowed for everyone by flatpak's own policy. Not one of the jobs:
    // it can run while an app installs, and the catalog reloads once the
    // new metadata is on disk, not before.
    if (m_refreshing)
        return;
    auto *process = new QProcess(this);
    m_refreshing = true;
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        m_refreshing = false;
        if (status == QProcess::NormalExit && code == 0)
            Q_EMIT catalogRefreshed();
        process->deleteLater();
        cleanUpWhenIdle();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        m_refreshing = false;
        process->deleteLater();
    });
    process->start(QStringLiteral("flatpak"),
                   {QStringLiteral("update"), QStringLiteral("--appstream"), QStringLiteral("--system")});
}

void Installer::setCatalog(QObject *catalog)
{
    m_catalog = qobject_cast<Catalog *>(catalog);
    m_updates->setCatalog(m_catalog);
}

bool Installer::open(const QVariantMap &app)
{
    QStringList command = openCommand(app);
    if (command.isEmpty())
        return false;
    const QString program = command.takeFirst();
    return QProcess::startDetached(program, command);
}

QStringList Installer::openCommand(const QVariantMap &app) const
{
    // the copy on this PC, whatever the listing says
    QString desktop = m_catalog ? m_catalog->systemCopy(app.value(QStringLiteral("ref")).toString()) : QString();
    if (desktop.isEmpty() && app.value(QStringLiteral("source")).toString() == QLatin1String("flatpak")) {
        const QString id = appIdFromLink(appKey(app.value(QStringLiteral("appId")).toString()));
        return id.isEmpty() ? QStringList() : QStringList{QStringLiteral("flatpak"), QStringLiteral("run"), id};
    }
    // a Windows program: the Start entry made when it was installed
    if (desktop.isEmpty() && app.value(QStringLiteral("source")).toString() == QLatin1String("windows")) {
        const QString space = windowsSpace(app.value(QStringLiteral("ref")).toString());
        if (space.isEmpty())
            return {};
        QProcess list;
        list.start(QStringLiteral("raye-windows"), {QStringLiteral("list"), space, QStringLiteral("--json")});
        list.waitForFinished(10000);
        for (const QJsonValue &v : QJsonDocument::fromJson(list.readAllStandardOutput()).array()) {
            const QJsonArray files = v.toObject().value(QStringLiteral("desktopFiles")).toArray();
            if (!files.isEmpty() && QFileInfo(files.first().toString()).isFile())
                return {QStringLiteral("kioclient"), QStringLiteral("exec"), files.first().toString()};
        }
        return {};
    }
    // a system app: its launcher, the way the Start menu starts it
    if (desktop.isEmpty())
        desktop = app.value(QStringLiteral("desktopId")).toString();
    if (desktop.isEmpty())
        desktop = app.value(QStringLiteral("appId")).toString();
    if (!desktop.endsWith(QLatin1String(".desktop")))
        desktop += QLatin1String(".desktop");
    if (appIdFromLink(desktop).isEmpty())
        return {};
    const QString file = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktop);
    if (file.isEmpty())
        return {};
    // kstart waits for a window it may never see, and does not bring it up
    return {QStringLiteral("kioclient"), QStringLiteral("exec"), file};
}

void Installer::cleanUpWhenIdle()
{
    // never while something downloads: an install of ours, or the
    // catalog's own refresh
    if (m_running || m_refreshing || !m_jobs->rows().isEmpty() || !m_cleanup)
        return;
    m_cleanup();
}

void Installer::syncQueued()
{
    QStringList refs;
    for (const JobsModel::Row &row : m_jobs->rows())
        if (row.spec.action == QLatin1String("update"))
            refs.append(row.spec.ref);
    m_updates->setQueued(refs);
}

void Installer::changed()
{
    ++m_revision;
    Q_EMIT jobsChanged();
    Q_EMIT revisionMoved();
    // busy and working change together with the queue
    Q_EMIT busyChanged();
}

// ---------------------------------------------------------------------------
// Flathub through libflatpak, on a worker thread

namespace
{

Installer::Reason reasonOf(const GError *error)
{
    if (!error)
        return Installer::ReasonNone;
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return Installer::ReasonCancelled;
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE))
        return Installer::ReasonNoSpace;
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED)
        || g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED)
        || g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_PERMISSION_DENIED))
        return Installer::ReasonNotAllowed;
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NETWORK_UNREACHABLE)
        || g_error_matches(error, G_IO_ERROR, G_IO_ERROR_HOST_NOT_FOUND)
        || g_error_matches(error, G_IO_ERROR, G_IO_ERROR_HOST_UNREACHABLE)
        || g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)
        || error->domain == G_RESOLVER_ERROR)
        return Installer::ReasonNoNetwork;
    if (g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_REF_NOT_FOUND)
        || g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_REMOTE_NOT_FOUND)
        || g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_RUNTIME_NOT_FOUND))
        return Installer::ReasonNotFound;
    return Installer::ReasonNone; // the message decides
}

class FlatpakLibBackend;

// What the transaction callbacks share, on the worker thread.
struct Run {
    FlatpakLibBackend *backend = nullptr;
    bool remove = false;
    int opCount = 1;
    int opIndex = 0;
    qint64 totalBytes = 0;
    qint64 doneBytes = 0;   // finished operations
    qint64 opBytes = 0;     // download size of the running operation
    qint64 transferred = 0; // of the running operation
    int shownPercent = 0;
    bool downloading = true;
    QString error;
    Installer::Reason reason = Installer::ReasonNone;
    QList<QByteArray> installed; // refs this job put in place, in order
    QByteArray current;          // the ref being installed now, if any
    bool currentPulled = false;  // its download is complete: the deploy may be on its way
};

class FlatpakLibBackend : public JobBackend
{
public:
    using JobBackend::JobBackend;

    ~FlatpakLibBackend() override
    {
        cancel();
        g_cancellable_cancel(m_checkCancellable);
        stopThread(m_job);
        stopThread(m_check);
        if (m_cancellable)
            g_object_unref(m_cancellable);
        g_object_unref(m_checkCancellable);
    }

    void start(const JobSpec &job) override
    {
        m_cancellable = g_cancellable_new();
        GCancellable *cancellable = m_cancellable;
        m_job = QThread::create([this, job, cancellable] { run(job, cancellable); });
        connect(m_job, &QThread::finished, m_job, &QObject::deleteLater);
        m_job->start();
    }

    void cancel() override
    {
        if (m_cancellable)
            g_cancellable_cancel(m_cancellable);
    }

    void checkUpdates() override
    {
        if (m_check)
            return;
        g_cancellable_reset(m_checkCancellable);
        m_check = QThread::create([this] { listUpdates(m_checkCancellable); });
        connect(m_check, &QThread::finished, m_check, &QObject::deleteLater);
        m_check->start();
    }

    // called from the worker: hands it to the main thread
    void post(std::function<void()> f)
    {
        QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection);
    }

    void report(Run &run, int opPercent)
    {
        JobProgress p;
        p.bytesDone = run.doneBytes + (run.opBytes > 0 ? std::min(run.opBytes, run.transferred) : 0);
        p.bytesTotal = run.totalBytes;
        // by bytes when the size is known, by operations otherwise; and
        // never backwards (flatpak starts over while it estimates)
        const int byOps = (run.opIndex * 100 + std::clamp(opPercent, 0, 100)) / std::max(run.opCount, 1);
        const int byBytes = run.totalBytes > 0 ? int(std::min(p.bytesDone, run.totalBytes) * 100 / run.totalBytes) : byOps;
        run.shownPercent = std::max(run.shownPercent, std::clamp(byBytes, 0, 100));
        p.percent = run.shownPercent;
        p.downloading = run.downloading;
        const bool cancellable = run.downloading;
        post([this, p, cancellable] {
            Q_EMIT progress(p);
            if (cancellable != m_cancellableShown) {
                m_cancellableShown = cancellable;
                Q_EMIT cancellableChanged(cancellable);
            }
        });
    }

private:
    static void stopThread(QPointer<QThread> &thread)
    {
        if (thread) {
            thread->wait();
            delete thread.data();
        }
    }

    static void onChanged(FlatpakTransactionProgress *progress, gpointer data)
    {
        auto *run = static_cast<Run *>(data);
        run->transferred = qint64(flatpak_transaction_progress_get_bytes_transferred(progress));
        const int percent = flatpak_transaction_progress_get_is_estimating(progress) ? 0 : flatpak_transaction_progress_get_progress(progress);
        // the last download is in: what is left is putting it in place
        const bool lastOp = run->opIndex >= run->opCount - 1;
        const bool pulled = (run->opBytes > 0 && run->transferred >= run->opBytes) || percent >= 100;
        if (!run->current.isEmpty() && pulled)
            run->currentPulled = true;
        if (!run->remove && lastOp && pulled)
            run->downloading = false;
        run->backend->report(*run, percent);
    }

    static void onNewOperation(FlatpakTransaction *, FlatpakTransactionOperation *op, FlatpakTransactionProgress *progress,
                               gpointer data)
    {
        auto *run = static_cast<Run *>(data);
        run->opBytes = qint64(flatpak_transaction_operation_get_download_size(op));
        run->transferred = 0;
        run->currentPulled = false;
        run->current = flatpak_transaction_operation_get_operation_type(op) == FLATPAK_TRANSACTION_OPERATION_INSTALL
            ? QByteArray(flatpak_transaction_operation_get_ref(op))
            : QByteArray();
        flatpak_transaction_progress_set_update_frequency(progress, 250);
        g_signal_connect(progress, "changed", G_CALLBACK(onChanged), run);
        run->backend->report(*run, 0);
    }

    static void onOperationDone(FlatpakTransaction *, FlatpakTransactionOperation *op, const char *, int, gpointer data)
    {
        auto *run = static_cast<Run *>(data);
        if (flatpak_transaction_operation_get_operation_type(op) == FLATPAK_TRANSACTION_OPERATION_INSTALL)
            run->installed.append(QByteArray(flatpak_transaction_operation_get_ref(op)));
        run->current.clear();
        run->doneBytes += run->opBytes;
        run->opBytes = 0;
        run->transferred = 0;
        run->opIndex = std::min(run->opIndex + 1, run->opCount);
    }

    static gboolean onOperationError(FlatpakTransaction *, FlatpakTransactionOperation *, GError *error, int details,
                                     gpointer data)
    {
        auto *run = static_cast<Run *>(data);
        // a related extension that failed does not stop the app
        if (details & FLATPAK_TRANSACTION_ERROR_DETAILS_NON_FATAL)
            return TRUE;
        if (run->error.isEmpty()) {
            run->error = QString::fromUtf8(error->message);
            run->reason = reasonOf(error);
        }
        return FALSE;
    }

    static gboolean onReady(FlatpakTransaction *transaction, gpointer data)
    {
        auto *run = static_cast<Run *>(data);
        GList *ops = flatpak_transaction_get_operations(transaction);
        run->opCount = std::max<int>(1, int(g_list_length(ops)));
        run->totalBytes = 0;
        for (GList *l = ops; l; l = l->next)
            run->totalBytes += qint64(flatpak_transaction_operation_get_download_size(FLATPAK_TRANSACTION_OPERATION(l->data)));
        g_list_free_full(ops, g_object_unref);
        return TRUE;
    }

    static gboolean refuseRebase(FlatpakTransaction *, const char *, const char *, const char *, const char *,
                                 const char **, gpointer)
    {
        return FALSE; // an app that moved to a new id stays as it is
    }

    static gboolean refuseRemote(FlatpakTransaction *, int, const char *, const char *, const char *, gpointer)
    {
        return FALSE; // never a new remote from an app's metadata
    }

    static int firstRemote(FlatpakTransaction *, const char *, const char *, const char *const *, gpointer)
    {
        return 0;
    }

    void finish(const JobResult &result)
    {
        post([this, result] {
            m_cancellableShown = true;
            if (m_cancellable) {
                g_object_unref(m_cancellable);
                m_cancellable = nullptr;
            }
            Q_EMIT finished(result);
        });
    }

    // the worker thread
    void run(const JobSpec &job, GCancellable *cancellable)
    {
        GMainContext *context = g_main_context_new();
        g_main_context_push_thread_default(context);
        GError *error = nullptr;
        JobResult result;
        Run state;
        state.backend = this;
        state.remove = job.action == QLatin1String("remove");
        state.downloading = !state.remove;

        FlatpakInstallation *installation = flatpak_installation_new_system(cancellable, &error);
        FlatpakTransaction *transaction =
            installation ? flatpak_transaction_new_for_installation(installation, cancellable, &error) : nullptr;
        bool added = false;
        bool nothingToDo = false;
        if (transaction) {
            flatpak_transaction_set_no_interaction(transaction, TRUE);
            const QByteArray ref = job.ref.toUtf8();
            if (job.action == QLatin1String("install")) {
                added = flatpak_transaction_add_install(transaction, "flathub", ref.constData(), nullptr, &error);
                nothingToDo = !added && g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED);
            } else if (job.action == QLatin1String("remove")) {
                added = flatpak_transaction_add_uninstall(transaction, ref.constData(), &error);
                nothingToDo = !added && g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_NOT_INSTALLED);
                // not in the system installation: it may be this user's own
                if (nothingToDo) {
                    GError *userError = nullptr;
                    FlatpakInstallation *user = flatpak_installation_new_user(cancellable, &userError);
                    FlatpakTransaction *userTransaction =
                        user ? flatpak_transaction_new_for_installation(user, cancellable, &userError) : nullptr;
                    if (userTransaction && flatpak_transaction_add_uninstall(userTransaction, ref.constData(), &userError)) {
                        flatpak_transaction_set_no_interaction(userTransaction, TRUE);
                        g_object_unref(transaction);
                        g_object_unref(installation);
                        transaction = userTransaction;
                        installation = user;
                        g_clear_error(&error);
                        added = true;
                        nothingToDo = false;
                    } else {
                        if (userTransaction)
                            g_object_unref(userTransaction);
                        if (user)
                            g_object_unref(user);
                    }
                    g_clear_error(&userError);
                }
            } else if (job.ref == runtimesRef) {
                added = addRuntimeUpdates(installation, transaction, cancellable, &error, &nothingToDo);
            } else {
                added = flatpak_transaction_add_update(transaction, ref.constData(), nullptr, nullptr, &error);
            }
        }

        // what was unused before a removal, to take back only what the
        // removed app alone needed
        QSet<QByteArray> unusedBefore;
        const bool removing = job.action == QLatin1String("remove");
        if (added && removing)
            unusedBefore = unusedRefs(installation);

        if (nothingToDo) {
            result.ok = true;
        } else if (added) {
            g_signal_connect(transaction, "new-operation", G_CALLBACK(onNewOperation), &state);
            g_signal_connect(transaction, "operation-done", G_CALLBACK(onOperationDone), &state);
            g_signal_connect(transaction, "operation-error", G_CALLBACK(onOperationError), &state);
            g_signal_connect(transaction, "ready", G_CALLBACK(onReady), &state);
            g_signal_connect(transaction, "add-new-remote", G_CALLBACK(refuseRemote), &state);
            g_signal_connect(transaction, "choose-remote-for-ref", G_CALLBACK(firstRemote), &state);
            g_signal_connect(transaction, "end-of-lifed-with-rebase", G_CALLBACK(refuseRebase), &state);
            result.ok = flatpak_transaction_run(transaction, cancellable, &error);
            if (!result.ok && !state.error.isEmpty()) {
                result.detail = state.error;
                result.reason = state.reason;
            }
            // an uninstall leaves no runtime behind that only this app used
            if (result.ok && removing) {
                QList<QByteArray> orphans;
                for (const QByteArray &ref : unusedRefs(installation))
                    if (!unusedBefore.contains(ref))
                        orphans.append(ref);
                if (!orphans.isEmpty())
                    removeLeftovers(installation, orphans, {});
            }
        }
        if (!result.ok) {
            if (result.detail.isEmpty() && error)
                result.detail = QString::fromUtf8(error->message);
            if (result.reason == Installer::ReasonNone)
                result.reason = reasonOf(error);
            result.cancelled = g_cancellable_is_cancelled(cancellable) && state.error.isEmpty();
            // nothing half installed: the runtimes this job brought for
            // an app that did not make it go again
            // a deploy may still run in flatpak's helper only when the
            // job was cancelled right after a download
            const bool waitDeploy = state.currentPulled && g_cancellable_is_cancelled(cancellable)
                && state.current.startsWith("runtime/");
            if (installation && (!state.installed.isEmpty() || waitDeploy))
                removeLeftovers(installation, state.installed, waitDeploy ? state.current : QByteArray());
        }
        if (error)
            g_error_free(error);
        if (transaction)
            g_object_unref(transaction);
        if (installation)
            g_object_unref(installation);
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
        finish(result);
    }

    static bool isInstalled(FlatpakInstallation *installation, const QByteArray &ref)
    {
        flatpak_installation_drop_caches(installation, nullptr, nullptr);
        GPtrArray *refs = flatpak_installation_list_installed_refs(installation, nullptr, nullptr);
        bool found = false;
        for (guint i = 0; refs && i < refs->len && !found; ++i) {
            gchar *full = flatpak_ref_format_ref(FLATPAK_REF(g_ptr_array_index(refs, i)));
            found = ref == full;
            g_free(full);
        }
        if (refs)
            g_ptr_array_unref(refs);
        return found;
    }

    // The installed runtimes no installed app uses.
    static QSet<QByteArray> unusedRefs(FlatpakInstallation *installation)
    {
        QSet<QByteArray> out;
        flatpak_installation_drop_caches(installation, nullptr, nullptr);
        GPtrArray *unused = flatpak_installation_list_unused_refs(installation, nullptr, nullptr, nullptr);
        for (guint i = 0; unused && i < unused->len; ++i) {
            gchar *full = flatpak_ref_format_ref(FLATPAK_REF(g_ptr_array_index(unused, i)));
            out.insert(QByteArray(full));
            g_free(full);
        }
        if (unused)
            g_ptr_array_unref(unused);
        return out;
    }

    // Uninstalls those of `refs` that no installed app uses. Runs to the
    // end even after a cancel: it is what a cancel asked for. `deploying`
    // was fully downloaded when the job stopped: flatpak's system helper
    // may still be putting it in place, so it is waited for (a minute at
    // most) and taken back too.
    static void removeLeftovers(FlatpakInstallation *installation, QList<QByteArray> refs, const QByteArray &deploying)
    {
        if (!deploying.isEmpty()) {
            for (int second = 0; second < 60 && !isInstalled(installation, deploying); ++second)
                QThread::sleep(1);
            refs.append(deploying);
        }
        GError *error = nullptr;
        flatpak_installation_drop_caches(installation, nullptr, nullptr);
        GPtrArray *unused = flatpak_installation_list_unused_refs(installation, nullptr, nullptr, &error);
        FlatpakTransaction *transaction = unused ? flatpak_transaction_new_for_installation(installation, nullptr, &error) : nullptr;
        int added = 0;
        if (transaction) {
            flatpak_transaction_set_no_interaction(transaction, TRUE);
            for (guint i = 0; i < unused->len; ++i) {
                gchar *full = flatpak_ref_format_ref(FLATPAK_REF(g_ptr_array_index(unused, i)));
                if (refs.contains(QByteArray(full)) && flatpak_transaction_add_uninstall(transaction, full, nullptr))
                    ++added;
                g_free(full);
            }
            if (added > 0 && !flatpak_transaction_run(transaction, nullptr, &error))
                qWarning("raye-store: could not remove what a stopped install left: %s", error ? error->message : "?");
            g_object_unref(transaction);
        } else if (error) {
            qWarning("raye-store: could not list unused runtimes: %s", error->message);
        }
        if (unused)
            g_ptr_array_unref(unused);
        if (error)
            g_error_free(error);
    }

    static bool addRuntimeUpdates(FlatpakInstallation *installation, FlatpakTransaction *transaction,
                                  GCancellable *cancellable, GError **error, bool *nothingToDo)
    {
        GPtrArray *refs = flatpak_installation_list_installed_refs_for_update(installation, cancellable, error);
        if (!refs)
            return false;
        int added = 0;
        bool ok = true;
        for (guint i = 0; i < refs->len && ok; ++i) {
            FlatpakRef *ref = FLATPAK_REF(g_ptr_array_index(refs, i));
            if (flatpak_ref_get_kind(ref) != FLATPAK_REF_KIND_RUNTIME)
                continue;
            gchar *full = flatpak_ref_format_ref(ref);
            ok = flatpak_transaction_add_update(transaction, full, nullptr, nullptr, error);
            g_free(full);
            added += ok ? 1 : 0;
        }
        g_ptr_array_unref(refs);
        *nothingToDo = ok && added == 0;
        return ok && added > 0;
    }

    // the worker thread
    void listUpdates(GCancellable *cancellable)
    {
        GMainContext *context = g_main_context_new();
        g_main_context_push_thread_default(context);
        GError *error = nullptr;
        QList<UpdateInfo> apps;
        QStringList runtimes;
        QString message;
        FlatpakInstallation *installation = flatpak_installation_new_system(cancellable, &error);
        GPtrArray *refs = installation ? flatpak_installation_list_installed_refs_for_update(installation, cancellable, &error) : nullptr;
        if (refs) {
            for (guint i = 0; i < refs->len; ++i) {
                FlatpakInstalledRef *installed = FLATPAK_INSTALLED_REF(g_ptr_array_index(refs, i));
                FlatpakRef *ref = FLATPAK_REF(installed);
                gchar *full = flatpak_ref_format_ref(ref);
                const QString fullRef = QString::fromUtf8(full);
                g_free(full);
                if (flatpak_ref_get_kind(ref) == FLATPAK_REF_KIND_RUNTIME) {
                    runtimes.append(fullRef);
                    continue;
                }
                UpdateInfo u;
                u.appId = QString::fromUtf8(flatpak_ref_get_name(ref));
                u.ref = fullRef;
                u.name = QString::fromUtf8(flatpak_installed_ref_get_appdata_name(installed));
                u.oldVersion = QString::fromUtf8(flatpak_installed_ref_get_appdata_version(installed));
                apps.append(u);
            }
            g_ptr_array_unref(refs);
        } else {
            message = error ? QString::fromUtf8(error->message) : QStringLiteral("no flatpak installation");
        }
        if (error)
            g_error_free(error);
        if (installation)
            g_object_unref(installation);
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
        std::sort(apps.begin(), apps.end(), [](const UpdateInfo &a, const UpdateInfo &b) {
            return QString::localeAwareCompare(a.name.isEmpty() ? a.appId : a.name, b.name.isEmpty() ? b.appId : b.name) < 0;
        });
        post([this, apps, runtimes, message] { Q_EMIT updatesChecked(apps, runtimes, message); });
    }

    QPointer<QThread> m_job;
    QPointer<QThread> m_check;
    GCancellable *m_cancellable = nullptr;
    GCancellable *m_checkCancellable = g_cancellable_new();
    bool m_cancellableShown = true;
};

// ---------------------------------------------------------------------------
// The flatpak command, behind the same interface

class FlatpakCliBackend : public JobBackend
{
public:
    explicit FlatpakCliBackend(QObject *parent)
        : JobBackend(parent)
    {
        m_process.setProcessChannelMode(QProcess::MergedChannels);
        connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            JobResult r;
            r.ok = status == QProcess::NormalExit && code == 0;
            r.cancelled = m_cancelled;
            // the last line usually says why it failed
            r.detail = r.ok ? QString() : QString::fromUtf8(m_process.readAll()).trimmed().section(QLatin1Char('\n'), -1);
            Q_EMIT finished(r);
        });
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart)
                return;
            JobResult r;
            r.detail = m_process.errorString();
            Q_EMIT finished(r);
        });
    }

    void start(const JobSpec &job) override
    {
        m_cancelled = false;
        QStringList argv = Installer::command(job.action, job.source, job.ref);
        // one app's update; the runtimes job updates everything left
        if (job.action == QLatin1String("update") && job.ref != runtimesRef)
            argv.append(job.ref);
        m_process.start(argv.first(), argv.mid(1));
    }

    void cancel() override
    {
        m_cancelled = true;
        m_process.terminate();
    }

    void checkUpdates() override
    {
        auto *p = new QProcess(this);
        connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus status) {
            QList<UpdateInfo> apps;
            QStringList runtimes;
            const QString out = QString::fromUtf8(p->readAllStandardOutput());
            for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QStringList cols = line.split(QLatin1Char('\t'));
                const QString ref = cols.value(0).trimmed();
                if (ref.startsWith(QLatin1String("runtime/"))) {
                    runtimes.append(ref);
                } else if (ref.startsWith(QLatin1String("app/"))) {
                    UpdateInfo u;
                    u.ref = ref;
                    u.appId = cols.value(1).trimmed();
                    u.newVersion = cols.value(2).trimmed();
                    apps.append(u);
                }
            }
            const bool ok = status == QProcess::NormalExit && code == 0;
            Q_EMIT updatesChecked(apps, runtimes, ok ? QString() : QString::fromUtf8(p->readAllStandardError()).trimmed());
            p->deleteLater();
        });
        p->start(QStringLiteral("flatpak"), {QStringLiteral("remote-ls"), QStringLiteral("--system"), QStringLiteral("--updates"),
                                             QStringLiteral("--columns=ref,application,version")});
    }

private:
    QProcess m_process;
    bool m_cancelled = false;
};

// ---------------------------------------------------------------------------
// System packages through PackageKit

class PackageKitBackend : public JobBackend
{
public:
    using JobBackend::JobBackend;

    void start(const JobSpec &job) override
    {
        m_job = job;
        m_cancelled = false;
        m_error.clear();
        m_reason = Installer::ReasonNone;
        m_firstRemaining = 0;
        m_found.clear();
        m_installed.clear();
        PackageKit::Transaction *t = PackageKit::Daemon::resolve(job.ref, PackageKit::Transaction::FilterArch);
        connect(t, &PackageKit::Transaction::package, this, [this](PackageKit::Transaction::Info info, const QString &id, const QString &) {
            if (PackageKit::Daemon::packageName(id) != m_job.ref)
                return;
            if (info == PackageKit::Transaction::InfoInstalled)
                m_installed.append(id);
            else
                m_found.append(id);
        });
        watch(t, true);
    }

    void cancel() override
    {
        m_cancelled = true;
        if (m_transaction)
            m_transaction->cancel();
    }

private:
    void watch(PackageKit::Transaction *t, bool resolving)
    {
        m_transaction = t;
        connect(t, &PackageKit::Transaction::errorCode, this, [this](PackageKit::Transaction::Error e, const QString &details) {
            m_error = details;
            m_reason = reasonOf(e);
        });
        connect(t, &PackageKit::Transaction::allowCancelChanged, this, [this, t] { Q_EMIT cancellableChanged(t->allowCancel()); });
        if (!resolving) {
            const auto update = [this, t] {
                JobProgress p;
                const uint percent = t->percentage();
                p.percent = percent <= 100 ? int(percent) : -1; // 101 means "not known"
                const qint64 remaining = qint64(t->downloadSizeRemaining());
                if (remaining > m_firstRemaining)
                    m_firstRemaining = remaining;
                p.bytesTotal = m_firstRemaining;
                p.bytesDone = m_firstRemaining - remaining;
                const auto status = t->status();
                p.downloading = m_job.action != QLatin1String("remove")
                    && (status == PackageKit::Transaction::StatusDownload || status == PackageKit::Transaction::StatusWait
                        || status == PackageKit::Transaction::StatusSetup || status == PackageKit::Transaction::StatusQuery
                        || status == PackageKit::Transaction::StatusDepResolve);
                Q_EMIT progress(p);
            };
            connect(t, &PackageKit::Transaction::percentageChanged, this, update);
            connect(t, &PackageKit::Transaction::statusChanged, this, update);
        }
        connect(t, &PackageKit::Transaction::finished, this, [this, resolving](PackageKit::Transaction::Exit exit, uint) {
            m_transaction = nullptr;
            if (resolving && exit == PackageKit::Transaction::ExitSuccess && !m_cancelled) {
                const bool remove = m_job.action == QLatin1String("remove");
                const QStringList &ids = remove ? m_installed : m_found;
                if (ids.isEmpty()) {
                    // installed already, or removed already: nothing to do;
                    // a package nobody offers is not found
                    JobResult r;
                    r.ok = remove || !m_installed.isEmpty();
                    r.reason = Installer::ReasonNotFound;
                    r.detail = QStringLiteral("%1 not found").arg(m_job.ref);
                    Q_EMIT finished(r);
                    return;
                }
                // never pull other packages out with this one; do drop
                // what only it needed
                watch(remove ? PackageKit::Daemon::removePackage(ids.first(), false, true)
                             : PackageKit::Daemon::installPackage(ids.last()),
                      false);
                return;
            }
            JobResult r;
            r.ok = exit == PackageKit::Transaction::ExitSuccess;
            r.cancelled = exit == PackageKit::Transaction::ExitCancelled || exit == PackageKit::Transaction::ExitCancelledPriority
                || (m_cancelled && !r.ok);
            r.reason = m_reason;
            r.detail = m_error;
            Q_EMIT finished(r);
        });
    }

    static Installer::Reason reasonOf(PackageKit::Transaction::Error e)
    {
        switch (e) {
        case PackageKit::Transaction::ErrorNoNetwork: return Installer::ReasonNoNetwork;
        case PackageKit::Transaction::ErrorNoSpaceOnDevice: return Installer::ReasonNoSpace;
        case PackageKit::Transaction::ErrorNotAuthorized: return Installer::ReasonNotAllowed;
        case PackageKit::Transaction::ErrorCannotGetLock: return Installer::ReasonBusy;
        case PackageKit::Transaction::ErrorPackageNotFound: return Installer::ReasonNotFound;
        case PackageKit::Transaction::ErrorTransactionCancelled: return Installer::ReasonCancelled;
        case PackageKit::Transaction::ErrorDepResolutionFailed:
        case PackageKit::Transaction::ErrorCannotRemoveSystemPackage: return Installer::ReasonNeeded;
        default: return Installer::ReasonNone;
        }
    }

    JobSpec m_job;
    QPointer<PackageKit::Transaction> m_transaction;
    QStringList m_found;     // versions that can be installed
    QStringList m_installed; // the installed one
    QString m_error;
    Installer::Reason m_reason = Installer::ReasonNone;
    qint64 m_firstRemaining = 0;
    bool m_cancelled = false;
};

// ---------------------------------------------------------------------------
// Apps Raye offers itself: raye-store-extra, as root through pkexec

class ExtraBackend : public JobBackend
{
public:
    ExtraBackend(const QStringList &helper, QObject *parent)
        : JobBackend(parent)
        , m_helper(helper.isEmpty() ? QStringList{QStringLiteral("pkexec"), QStringLiteral("/usr/libexec/raye-store-extra")} : helper)
    {
        connect(&m_process, &QProcess::readyReadStandardOutput, this, &ExtraBackend::readLines);
        connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            readLines();
            const QString errors = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
            JobResult r;
            r.ok = status == QProcess::NormalExit && code == 0;
            // 3: the helper stopped on our "cancel"
            r.cancelled = !r.ok && m_cancelled && code == 3;
            // pkexec: 126 the dialog was dismissed, 127 not authorized
            if (!r.ok && !r.cancelled && (code == 126 || code == 127) && m_helper.first().endsWith(QLatin1String("pkexec")))
                r.reason = Installer::ReasonNotAllowed;
            r.detail = errors.isEmpty() ? m_lastLine : errors.section(QLatin1Char('\n'), -1);
            Q_EMIT finished(r);
        });
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart)
                return;
            JobResult r;
            r.detail = m_process.errorString();
            Q_EMIT finished(r);
        });
    }

    void start(const JobSpec &job) override
    {
        m_cancelled = false;
        m_cancellable = true;
        m_lastLine.clear();
        m_buffer.clear();
        m_progress = JobProgress();
        m_progress.downloading = job.action != QLatin1String("remove");
        m_process.start(m_helper.first(), m_helper.mid(1) + QStringList{job.action, job.ref});
    }

    void cancel() override
    {
        // the helper runs as root: it is asked, not killed
        if (!m_cancellable || m_process.state() != QProcess::Running)
            return;
        m_cancelled = true;
        m_process.write("cancel\n");
    }

private:
    void readLines()
    {
        m_buffer += m_process.readAllStandardOutput();
        int nl;
        while ((nl = m_buffer.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(m_buffer.left(nl)).trimmed();
            m_buffer.remove(0, nl + 1);
            const QStringList words = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            bool number = false;
            if (words.size() == 2 && words.first() == QLatin1String("progress")) {
                const int percent = words.at(1).toInt(&number);
                if (number) {
                    m_progress.percent = std::clamp(percent, 0, 100);
                    Q_EMIT progress(m_progress);
                }
            } else if (words.size() == 3 && words.first() == QLatin1String("bytes")) {
                bool total = false;
                const qint64 done = words.at(1).toLongLong(&number);
                const qint64 all = words.at(2).toLongLong(&total);
                if (number && total && done >= 0 && all >= 0) {
                    m_progress.bytesDone = done;
                    m_progress.bytesTotal = all;
                    Q_EMIT progress(m_progress);
                }
            } else if (words.size() == 2 && words.first() == QLatin1String("phase")) {
                const bool downloading = words.at(1) == QLatin1String("downloading");
                m_progress.downloading = downloading;
                m_progress.setupWindow = words.at(1) == QLatin1String("setup");
                if (!downloading)
                    m_progress.percent = -1;
                if (downloading != m_cancellable) {
                    m_cancellable = downloading;
                    Q_EMIT cancellableChanged(downloading);
                }
                Q_EMIT progress(m_progress);
            } else if (!line.isEmpty()) {
                m_lastLine = line;
            }
        }
        // a line that never ends is not ours: do not keep it forever
        if (m_buffer.size() > 64 * 1024)
            m_buffer.clear();
    }

    QStringList m_helper;
    QProcess m_process;
    QByteArray m_buffer;
    QString m_lastLine;
    JobProgress m_progress;
    bool m_cancelled = false;
    bool m_cancellable = true;
};

} // namespace

namespace Backends
{

JobBackend *flatpakCommand(QObject *parent)
{
    return new FlatpakCliBackend(parent);
}

JobBackend *flatpak(QObject *parent)
{
    if (qEnvironmentVariableIntValue("RAYE_STORE_FLATPAK_CLI") == 1)
        return flatpakCommand(parent);
    // the library needs the system installation; without it, the command
    // still says what is wrong in its own words
    GError *error = nullptr;
    FlatpakInstallation *installation = flatpak_installation_new_system(nullptr, &error);
    if (!installation) {
        qWarning("raye-store: libflatpak cannot open the system installation (%s), using the flatpak command",
                 error ? error->message : "?");
        if (error)
            g_error_free(error);
        return flatpakCommand(parent);
    }
    g_object_unref(installation);
    return new FlatpakLibBackend(parent);
}

JobBackend *packageKit(QObject *parent)
{
    return new PackageKitBackend(parent);
}

JobBackend *extras(const QStringList &helper, QObject *parent)
{
    return new ExtraBackend(helper, parent);
}

} // namespace Backends
