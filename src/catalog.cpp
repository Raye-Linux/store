// SPDX-License-Identifier: GPL-3.0-or-later
// The whole catalog: AppStream, the feeds, the listing rules and the
// ranking of rows. Long, but one model; split it when it gets an owner each.
// The C library first: its headers use names Qt turns into macros.
#include <appstream.h>

#include "catalog.h"
#include "alternatives.h"
#include "windowsprograms.h"

#include <AppStreamQt/branding.h>
#include <AppStreamQt/bundle.h>
#include <AppStreamQt/component.h>
#include <AppStreamQt/contentrating.h>
#include <AppStreamQt/developer.h>
#include <AppStreamQt/icon.h>
#include <AppStreamQt/image.h>
#include <AppStreamQt/launchable.h>
#include <AppStreamQt/pool.h>
#include <AppStreamQt/release-list.h>
#include <AppStreamQt/release.h>
#include <AppStreamQt/screenshot.h>
#include <AppStreamQt/video.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcess>
#include <QRegularExpression>
#include <QMap>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimeZone>
#include <QVariantMap>
#include <QtConcurrent>

#include <algorithm>
#include <climits>
#include <utility>

namespace {

const QString kShare = QStringLiteral("/usr/share/raye-store");
const QString kFeeds = QStringLiteral("/var/cache/raye-store/feeds");
// the feed files are small; anything bigger is not ours
constexpr qint64 kMaxFeedFile = 32 * 1024 * 1024;
constexpr int kMinIcon = 64;
constexpr int kEnoughRatings = 10;
constexpr int kMaxReleases = 10;

bool isPackageName(const QString &name)
{
    static const QRegularExpression package(QStringLiteral("^[a-z0-9][a-z0-9+.-]+$"));
    return package.match(name).hasMatch();
}

QByteArray readSmall(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxFeedFile)
        return {};
    return f.readAll();
}

// seconds since 1970 from a number (seconds or milliseconds) or ISO text
qint64 parseTime(const QJsonValue &v)
{
    if (v.isDouble()) {
        const double t = v.toDouble();
        if (!(t > 0))
            return 0;
        return qint64(t > 1e11 ? t / 1000 : t);
    }
    if (v.isString()) {
        const QDateTime d = QDateTime::fromString(v.toString(), Qt::ISODate);
        return d.isValid() ? qMax<qint64>(0, d.toSecsSinceEpoch()) : 0;
    }
    return 0;
}

QStringList words(const QString &folded)
{
    static const QRegularExpression sep(QStringLiteral("[^\\p{L}\\p{N}]+"));
    return folded.split(sep, Qt::SkipEmptyParts);
}

// Levenshtein distance of at most 1, with two swapped letters counted as one
bool withinOneEdit(const QString &a, const QString &b)
{
    const qsizetype la = a.size(), lb = b.size();
    if (qAbs(la - lb) > 1)
        return false;
    if (la == lb) {
        qsizetype first = -1, mismatches = 0;
        for (qsizetype i = 0; i < la; ++i) {
            if (a.at(i) != b.at(i)) {
                if (++mismatches == 1)
                    first = i;
            }
        }
        if (mismatches <= 1)
            return true;
        return mismatches == 2 && first + 1 < la && a.at(first) == b.at(first + 1) && a.at(first + 1) == b.at(first);
    }
    const QString &longer = la > lb ? a : b;
    const QString &shorter = la > lb ? b : a;
    qsizetype i = 0, j = 0;
    bool skipped = false;
    while (i < longer.size() && j < shorter.size()) {
        if (longer.at(i) == shorter.at(j)) {
            ++i;
            ++j;
        } else if (skipped) {
            return false;
        } else {
            skipped = true;
            ++i;
        }
    }
    return true;
}

bool nearMiss(const QString &query, const QString &candidate)
{
    if (withinOneEdit(query, candidate))
        return true;
    // still typing: the start of a longer name, with one typo
    for (qsizetype len = query.size() - 1; len <= query.size() + 1; ++len) {
        if (len < candidate.size() && withinOneEdit(query, candidate.left(len)))
            return true;
    }
    return false;
}

QList<IconCandidate> iconCandidates(const AppStream::Component &c)
{
    QList<IconCandidate> out;
    for (const AppStream::Icon &icon : c.icons()) {
        IconCandidate ic;
        ic.width = int(icon.width());
        switch (icon.kind()) {
        case AppStream::Icon::KindCached:
            ic.kind = IconCandidate::Cached;
            ic.url = icon.url().toString();
            break;
        case AppStream::Icon::KindLocal:
            ic.kind = IconCandidate::Local;
            ic.url = icon.url().toString();
            break;
        case AppStream::Icon::KindRemote:
            ic.kind = IconCandidate::Remote;
            ic.url = icon.url().toString();
            break;
        case AppStream::Icon::KindStock:
            ic.kind = IconCandidate::Stock;
            ic.url = icon.name();
            break;
        default:
            continue;
        }
        out.append(ic);
    }
    return out;
}

void readScreenshots(const AppStream::Component &c, App &app)
{
    for (const AppStream::Screenshot &s : c.screenshotsAll()) {
        for (const AppStream::Video &v : s.videos()) {
            if (!v.url().isEmpty())
                app.videos.append(v.url().toString());
        }
        Screenshot shot;
        shot.caption = s.caption();
        int thumbWidth = 0;
        for (const AppStream::Image &image : s.images()) {
            const QString url = image.url().toString();
            if (url.isEmpty())
                continue;
            if (image.kind() == AppStream::Image::KindSource) {
                shot.full = url;
            } else if (image.kind() == AppStream::Image::KindThumbnail) {
                // the largest thumbnail up to the strip's 752 px
                const int w = int(image.width());
                if (shot.thumb.isEmpty() || (w <= 752 && w > thumbWidth)) {
                    shot.thumb = url;
                    thumbWidth = w;
                }
            }
        }
        if (shot.full.isEmpty())
            shot.full = shot.thumb;
        if (shot.thumb.isEmpty())
            shot.thumb = shot.full;
        if (shot.full.isEmpty())
            continue;
        // AppStream marks the one to show first
        if (s.isDefault())
            app.screenshots.prepend(shot);
        else
            app.screenshots.append(shot);
    }
    if (!app.screenshots.isEmpty())
        app.screenshot = app.screenshots.first().full;
}

void readReleases(const AppStream::Component &c, App &app)
{
    QList<AppStream::Release> list = c.releasesPlain().entries();
    std::stable_sort(list.begin(), list.end(), [](const AppStream::Release &a, const AppStream::Release &b) {
        return a.timestamp() > b.timestamp();
    });
    for (const AppStream::Release &r : list) {
        if (app.releases.size() == kMaxReleases)
            break;
        app.releases.append(Release{r.version(), r.timestamp(), r.description()});
    }
    if (!app.releases.isEmpty() && app.releases.first().date.isValid())
        app.updatedAt = qMax<qint64>(0, app.releases.first().date.toSecsSinceEpoch());
}

QStringList readKeywords(const AppStream::Component &c)
{
    QStringList out;
    if (GPtrArray *list = as_component_get_keywords(c.cPtr())) {
        for (guint i = 0; i < list->len; ++i)
            out.append(QString::fromUtf8(static_cast<const char *>(g_ptr_array_index(list, i))));
    }
    return out;
}

bool launcherHidden(const QString &desktopId)
{
    if (desktopId.isEmpty() || desktopId.contains(QLatin1Char('/')))
        return false;
    const QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId);
    return !path.isEmpty() && desktopEntryHidden(readSmall(path));
}

App fromComponent(const AppStream::Component &c)
{
    App app;
    app.id = c.id();
    app.name = c.name();
    app.summary = c.summary();
    app.description = c.description();
    const IconChoice icon = pickIcon(iconCandidates(c));
    app.icon = icon.icon;
    app.iconSize = icon.size;
    if (app.iconSize == 0 && !app.icon.isEmpty() && !app.icon.contains(QLatin1Char('/'))) {
        const IconChoice themed = resolveStockIcon(app.icon, QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation));
        if (!themed.icon.isEmpty()) {
            app.icon = themed.icon;
            app.iconSize = themed.size;
        }
    }
    readScreenshots(c, app);
    app.developer = c.developer().name();
    app.license = c.projectLicense();
    app.isFree = c.isFloss();
    app.categories = c.categories();
    app.keywords = readKeywords(c);
    readReleases(c, app);
    for (const AppStream::ContentRating &r : c.contentRatings())
        app.minimumAge = qMax(app.minimumAge, int(r.minimumAge()));
    AppStream::Branding brand = c.branding();
    app.brandLight = brand.color(AppStream::Branding::ColorKind::Primary, AppStream::Branding::ColorSchemeKind::Light);
    app.brandDark = brand.color(AppStream::Branding::ColorKind::Primary, AppStream::Branding::ColorSchemeKind::Dark);
    app.homepage = c.url(AppStream::Component::UrlKindHomepage).toString();
    app.help = c.url(AppStream::Component::UrlKindHelp).toString();
    app.donate = c.url(AppStream::Component::UrlKindDonation).toString();
    app.bugtracker = c.url(AppStream::Component::UrlKindBugtracker).toString();
    app.desktopId = c.launchable(AppStream::Launchable::KindDesktopId).entries().value(0);
    app.noDisplay = launcherHidden(app.desktopId);
    const AppStream::Bundle flatpak = c.bundle(AppStream::Bundle::KindFlatpak);
    if (!flatpak.isEmpty()) {
        app.source = QStringLiteral("flatpak");
        app.ref = flatpak.id();
    } else if (!c.packageNames().isEmpty()) {
        app.source = QStringLiteral("apt");
        app.ref = c.packageNames().first();
    }
    return app;
}

bool userReadsFrench();

QList<App> loadExtras()
{
    return parseExtras(readSmall(kShare + QStringLiteral("/extras.json")), userReadsFrench());
}

struct Loaded {
    QList<App> apps;
    Feeds feeds;
    QStringList windowsOrder;
    QByteArray bytes; // the same, as the cache keeps it
};

QString windowsDataFile()
{
    const QString env = qEnvironmentVariable("RAYE_STORE_WINDOWS_DATA");
    return env.isEmpty() ? kShare + QStringLiteral("/windows-programs.json") : env;
}

bool userReadsFrench()
{
    const QString first = qEnvironmentVariable("LANGUAGE").section(QLatin1Char(':'), 0, 0);
    return (first.isEmpty() ? QLocale().name() : first).startsWith(QLatin1String("fr"));
}

// programs are installed by the Windows layer's runner: without it the
// Store only points at native versions and says which games can't run
bool canInstallWindowsPrograms()
{
    return qEnvironmentVariableIntValue("RAYE_STORE_WINDOWS") == 1
        || !QStandardPaths::findExecutable(QStringLiteral("raye-run-windows")).isEmpty();
}

// bump when App, Feeds or what fills them changes
constexpr int kCacheFormat = 3;

Loaded loadCached()
{
    Loaded out;
    QFile f(Catalog::cacheFile());
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QByteArray bytes = f.readAll();
    if (decodeCatalog(bytes, catalogCacheStamp(), out.apps, out.feeds, out.windowsOrder))
        out.bytes = bytes;
    else
        out = Loaded();
    return out;
}

void saveCache(const QByteArray &bytes)
{
    const QString path = Catalog::cacheFile();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size())
        f.commit();
}

Loaded loadEverything()
{
    AppStream::Pool pool;
    // AppStreamQt declares Flags but not the operators, so build it by hand
    AppStream::Pool::Flags flags(AppStream::Pool::FlagLoadOsCatalog);
    flags |= AppStream::Pool::FlagLoadFlatpak;
    pool.setFlags(flags);
    if (!pool.load())
        qWarning("raye-store: AppStream: %s", qPrintable(pool.lastError()));

    QHash<QString, App> byId;
    const auto box = pool.componentsByKind(AppStream::Component::KindDesktopApp);
    for (const AppStream::Component &c : box.toList()) {
        App app = fromComponent(c);
        if (app.ref.isEmpty())
            continue; // nothing to install it from
        const QString key = appKey(app.id);
        auto it = byId.find(key);
        if (it == byId.end() || preferOver(app, *it))
            byId.insert(key, app);
    }
    // Raye's own entries come last so Flathub or the archive win a tie
    for (const App &extra : loadExtras()) {
        if (!byId.contains(appKey(extra.id)))
            byId.insert(appKey(extra.id), extra);
    }
    const QString windowsData = windowsDataFile();
    const WindowsListing windows = readWindowsListing(readSmall(windowsData), QFileInfo(windowsData).absolutePath() + QStringLiteral("/icons"),
                                                      readWindowsTitles(windowsTitleDirs()), windowsProfileDir(),
                                                      userReadsFrench(), canInstallWindowsPrograms());
    for (const App &app : windows.apps) {
        if (!byId.contains(appKey(app.id)))
            byId.insert(appKey(app.id), app);
    }
    // apps that do the job of a Windows program are found by its name
    applyAlternatives(byId, parseAlternatives(readSmall(alternativesFile())));
    // the native version of a Windows program is found by its Windows name too
    for (auto it = windows.natives.cbegin(); it != windows.natives.cend(); ++it) {
        auto app = byId.find(it.key());
        if (app == byId.end())
            continue;
        app->windowsState = QStringLiteral("native");
        if (!app->keywords.contains(it.value(), Qt::CaseInsensitive))
            app->keywords.append(it.value());
    }

    Loaded out;
    out.windowsOrder = windows.order;
    // another cache can be named for development and tests
    out.feeds = loadFeeds(qEnvironmentVariable("RAYE_STORE_FEEDS", kFeeds));
    out.apps = byId.values();
    applyFeeds(out.apps, out.feeds, parseAptRank(readSmall(kShare + QStringLiteral("/apt-rank.tsv"))));
    const QSet<QString> allow = parseCurationAllow(readSmall(kShare + QStringLiteral("/curation.json")));
    for (App &app : out.apps)
        app.listed = isListed(app, allow);
    hideArchiveTwins(out.apps);
    // a stable base order; the rows sort themselves
    std::sort(out.apps.begin(), out.apps.end(), [](const App &a, const App &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    out.bytes = encodeCatalog(out.apps, out.feeds, out.windowsOrder, catalogCacheStamp());
    return out;
}

struct InstalledNow {
    QSet<QString> refs;
    QList<SystemLauncher> launchers;
};

QSet<QString> installedNow()
{
    QSet<QString> found;
    QProcess flatpak;
    flatpak.start(QStringLiteral("flatpak"), {QStringLiteral("list"), QStringLiteral("--app"), QStringLiteral("--columns=ref")});
    flatpak.waitForFinished(20000);
    for (const QString &line : QString::fromUtf8(flatpak.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        found.insert(QStringLiteral("app/") + line.trimmed());
    QProcess dpkg;
    dpkg.start(QStringLiteral("dpkg-query"), {QStringLiteral("-W"), QStringLiteral("-f=${Package} ${db:Status-Abbrev}\\n")});
    dpkg.waitForFinished(20000);
    for (const QString &line : QString::fromUtf8(dpkg.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2 && parts.at(1).startsWith(QLatin1String("ii")))
            found.insert(parts.at(0));
    }
    // Windows programs: a space with at least one program in it
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("raye-windows"));
    if (!tool.isEmpty()) {
        QProcess windows;
        windows.start(tool, {QStringLiteral("list"), QStringLiteral("--json")});
        windows.waitForFinished(20000);
        for (const QJsonValue &v : QJsonDocument::fromJson(windows.readAllStandardOutput()).array()) {
            const QString ref = windowsRef(v.toObject().value(QStringLiteral("space")).toString());
            if (isWindowsRef(ref))
                found.insert(ref);
        }
    }
    return found;
}

QVariant dateOrNothing(qint64 secs)
{
    return secs > 0 ? QVariant(QDateTime::fromSecsSinceEpoch(secs, QTimeZone::UTC)) : QVariant();
}

} // namespace

QString catalogCacheStamp()
{
    return QStringLiteral("%1/%2/%3").arg(kCacheFormat).arg(userReadsFrench() ? QStringLiteral("fr") : QStringLiteral("en"))
        .arg(canInstallWindowsPrograms() ? 1 : 0);
}

static QDataStream &operator<<(QDataStream &s, const Screenshot &v) { return s << v.thumb << v.full << v.caption; }
static QDataStream &operator>>(QDataStream &s, Screenshot &v) { return s >> v.thumb >> v.full >> v.caption; }
static QDataStream &operator<<(QDataStream &s, const Release &v) { return s << v.version << v.date << v.notes; }
static QDataStream &operator>>(QDataStream &s, Release &v) { return s >> v.version >> v.date >> v.notes; }
static QDataStream &operator<<(QDataStream &s, const FeedEntry &v)
{
    return s << v.installs << v.trending << v.verified << v.updatedAt << v.addedAt;
}
static QDataStream &operator>>(QDataStream &s, FeedEntry &v)
{
    return s >> v.installs >> v.trending >> v.verified >> v.updatedAt >> v.addedAt;
}
static QDataStream &operator<<(QDataStream &s, const Rating &v) { return s << v.average << v.count << v.histogram; }
static QDataStream &operator>>(QDataStream &s, Rating &v) { return s >> v.average >> v.count >> v.histogram; }

// one function for both ways, so the two can never list fields differently
template<typename Stream, typename A>
static Stream &appFields(Stream &s, A &a)
{
    s % a.id % a.name % a.summary % a.description % a.icon % a.iconSize % a.screenshot % a.screenshots % a.videos
      % a.developer % a.license % a.source % a.ref % a.categories % a.keywords % a.releases % a.minimumAge
      % a.brandLight % a.brandDark % a.homepage % a.help % a.donate % a.bugtracker % a.licenseUrl % a.desktopId % a.isFree
      % a.preview % a.noDisplay % a.windowsState % a.windowsNote % a.windowsAntiCheat % a.downloadSize
      % a.verified % a.popularity % a.trending % a.updatedAt % a.addedAt % a.ratingAvg % a.ratingCount
      % a.ratingHistogram % a.listed % a.replaces;
    return s;
}

namespace {
struct Writer {
    QDataStream &s;
    template<typename T>
    Writer &operator%(const T &v) { s << v; return *this; }
};
struct Reader {
    QDataStream &s;
    template<typename T>
    Reader &operator%(T &v) { s >> v; return *this; }
};
const quint32 kCacheMagic = 0x52435354; // "RCST"
} // namespace

QByteArray encodeCatalog(const QList<App> &apps, const Feeds &feeds, const QStringList &windowsOrder, const QString &stamp)
{
    QByteArray bytes;
    QDataStream s(&bytes, QIODevice::WriteOnly);
    s.setVersion(QDataStream::Qt_6_0);
    s << kCacheMagic << stamp << qint32(apps.size());
    Writer w{s};
    for (const App &a : apps)
        appFields(w, a);
    // sorted: a QHash comes out in a different order in every process, and
    // equal lists must give equal bytes
    auto sorted = [](const auto &hash) {
        QMap<QString, typename std::decay_t<decltype(hash)>::mapped_type> map;
        for (auto it = hash.cbegin(); it != hash.cend(); ++it)
            map.insert(it.key(), it.value());
        return map;
    };
    s << sorted(feeds.apps) << sorted(feeds.lists) << sorted(feeds.ratings) << feeds.pickOfTheDay << feeds.picksOfTheWeek
      << windowsOrder;
    return bytes;
}

bool decodeCatalog(const QByteArray &bytes, const QString &stamp, QList<App> &apps, Feeds &feeds, QStringList &windowsOrder)
{
    QDataStream s(bytes);
    s.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    QString theirs;
    qint32 count = -1;
    s >> magic >> theirs >> count;
    if (magic != kCacheMagic || theirs != stamp || count < 0 || s.status() != QDataStream::Ok)
        return false;
    QList<App> list;
    list.reserve(qMin(count, 100000));
    Reader r{s};
    for (qint32 i = 0; i < count && s.status() == QDataStream::Ok; ++i) {
        App a;
        appFields(r, a);
        list.append(std::move(a));
    }
    QMap<QString, FeedEntry> feedApps;
    QMap<QString, QStringList> lists;
    QMap<QString, Rating> ratings;
    Feeds f;
    QStringList order;
    s >> feedApps >> lists >> ratings >> f.pickOfTheDay >> f.picksOfTheWeek >> order;
    f.apps = QHash<QString, FeedEntry>(feedApps.cbegin(), feedApps.cend());
    f.lists = QHash<QString, QStringList>(lists.cbegin(), lists.cend());
    f.ratings = QHash<QString, Rating>(ratings.cbegin(), ratings.cend());
    if (s.status() != QDataStream::Ok || !s.atEnd())
        return false;
    apps = std::move(list);
    feeds = std::move(f);
    windowsOrder = std::move(order);
    return true;
}

QString App::kind() const
{
    return appKind(categories);
}

QString appKey(const QString &id)
{
    return id.endsWith(QLatin1String(".desktop")) ? id.chopped(8) : id;
}

QString appKind(const QStringList &categories)
{
    return categories.contains(QLatin1String("Game")) ? QStringLiteral("game") : QStringLiteral("app");
}

QList<App> parseExtras(const QByteArray &json, bool french)
{
    // {"en": ..., "fr": ...}, or plain text for both
    const auto pick = [french](const QJsonValue &v) {
        if (!v.isObject())
            return v.toString();
        const QJsonObject o = v.toObject();
        const QString fr = o.value(QStringLiteral("fr")).toString();
        return french && !fr.isEmpty() ? fr : o.value(QStringLiteral("en")).toString();
    };
    static const QRegularExpression sha256(QStringLiteral("^[0-9a-f]{64}$"));
    QList<App> apps;
    const QJsonArray list = QJsonDocument::fromJson(json).object().value(QStringLiteral("apps")).toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        const QString url = o.value(QStringLiteral("url")).toString();
        const QString pkg = o.value(QStringLiteral("package")).toString();
        if (!url.startsWith(QLatin1String("https://")) || !sha256.match(o.value(QStringLiteral("sha256")).toString()).hasMatch()
            || !isPackageName(pkg))
            continue;
        App app;
        app.id = o.value(QStringLiteral("id")).toString();
        app.name = o.value(QStringLiteral("name")).toString();
        app.summary = pick(o.value(QStringLiteral("summary")));
        app.description = pick(o.value(QStringLiteral("description")));
        app.icon = o.value(QStringLiteral("icon")).toString();
        const QString homepage = o.value(QStringLiteral("homepage")).toString();
        if (homepage.startsWith(QLatin1String("https://")))
            app.homepage = homepage;
        for (const QJsonValue &k : o.value(QStringLiteral("keywords")).toArray())
            app.keywords.append(k.toString());
        // the launcher the package installs, when it is not named after the id
        static const QRegularExpression desktopFile(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*\\.desktop$"));
        const QString desktop = o.value(QStringLiteral("desktop")).toString();
        if (desktopFile.match(desktop).hasMatch())
            app.desktopId = desktop;
        app.developer = o.value(QStringLiteral("developer")).toString();
        app.license = o.value(QStringLiteral("license")).toString();
        app.source = QStringLiteral("raye");
        app.ref = pkg;
        for (const QJsonValue &c : o.value(QStringLiteral("categories")).toArray())
            app.categories.append(c.toString());
        app.preview = o.value(QStringLiteral("preview")).toBool();
        if (!app.id.isEmpty() && !app.name.isEmpty())
            apps.append(app);
    }
    return apps;
}

bool preferOver(const App &candidate, const App &current)
{
    return candidate.source == QLatin1String("flatpak") && current.source != QLatin1String("flatpak");
}

IconChoice pickIcon(const QList<IconCandidate> &candidates)
{
    const IconCandidate *best = nullptr;
    // lower is better: 0 disk >= 128, 1 disk >= 64, 2 remote, 3 disk smaller, 4 theme name
    int bestTier = 5;
    auto tierOf = [](const IconCandidate &c) {
        if (c.url.isEmpty())
            return 5;
        switch (c.kind) {
        case IconCandidate::Cached:
        case IconCandidate::Local:
            return c.width >= 128 ? 0 : c.width >= kMinIcon ? 1 : 3;
        case IconCandidate::Remote:
            return 2;
        case IconCandidate::Stock:
            return 4;
        }
        return 5;
    };
    for (const IconCandidate &c : candidates) {
        const int tier = tierOf(c);
        if (tier == 5)
            continue;
        bool better = tier < bestTier;
        if (!better && tier == bestTier && best) {
            // the closest to 128 among the big ones, else the biggest
            better = tier == 0 ? c.width < best->width : c.width > best->width;
        }
        if (better) {
            best = &c;
            bestTier = tier;
        }
    }
    if (!best)
        return {};
    return {best->url, best->kind == IconCandidate::Stock ? 0 : best->width};
}

IconChoice resolveStockIcon(const QString &name, const QStringList &dataDirs)
{
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+-]*$"));
    if (!plain.match(name).hasMatch())
        return {};
    static const QList<QPair<QString, int>> sizes{{QStringLiteral("128x128"), 128}, {QStringLiteral("scalable"), 128},
                                                  {QStringLiteral("256x256"), 256}, {QStringLiteral("96x96"), 96},
                                                  {QStringLiteral("64x64"), 64}};
    for (const QString &dir : dataDirs) {
        for (const auto &[folder, size] : sizes) {
            for (const char *ext : {".png", ".svg"}) {
                const QString path = dir + QStringLiteral("/icons/hicolor/") + folder + QStringLiteral("/apps/") + name + QLatin1String(ext);
                if (QFileInfo(path).isFile())
                    return {path, size};
            }
        }
    }
    return {};
}

QList<SystemLauncher> readSystemLaunchers(const QStringList &dirs)
{
    QList<SystemLauncher> out;
    QSet<QString> seen;
    for (const QString &dir : dirs) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files | QDir::Readable);
        for (const QFileInfo &file : files) {
            const QString id = file.completeBaseName();
            if (seen.contains(id))
                continue;
            seen.insert(id);
            const QByteArray contents = readSmall(file.filePath());
            if (desktopEntryHidden(contents))
                continue;
            static const QRegularExpression nameLine(QStringLiteral("^Name=(.*)$"), QRegularExpression::MultilineOption);
            const QString name = nameLine.match(QString::fromUtf8(contents)).captured(1).trimmed();
            if (!name.isEmpty())
                out.append({id, name});
        }
    }
    return out;
}

QString systemLauncherFor(const App &app, const QList<SystemLauncher> &launchers)
{
    const QString desktop = appKey(app.desktopId);
    const QString key = appKey(app.id);
    for (const SystemLauncher &l : launchers) {
        if (l.id == desktop || l.id == key)
            return l.id + QLatin1String(".desktop");
    }
    // org.mozilla.firefox is firefox, org.libreoffice.LibreOffice is
    // libreoffice-startcenter, when the names agree as well
    const QString word = key.section(QLatin1Char('.'), -1).toLower();
    const QString name = foldText(app.name);
    if (word.isEmpty() || name.isEmpty())
        return {};
    const auto sameName = [&name](const QString &other) {
        const QString folded = foldText(other);
        return folded == name || folded.startsWith(name + QLatin1Char(' ')) || name.startsWith(folded + QLatin1Char(' '));
    };
    for (const SystemLauncher &l : launchers) {
        const QString id = l.id.toLower();
        if ((id == word || id.startsWith(word + QLatin1Char('-'))) && sameName(l.name))
            return l.id + QLatin1String(".desktop");
    }
    return {};
}

bool desktopEntryHidden(const QByteArray &contents)
{
    bool inEntry = false;
    for (const QByteArray &raw : contents.split('\n')) {
        const QByteArray line = raw.trimmed();
        if (line.startsWith('[')) {
            inEntry = line == "[Desktop Entry]";
            continue;
        }
        if (!inEntry)
            continue;
        const qsizetype eq = line.indexOf('=');
        if (eq < 0)
            continue;
        const QByteArray key = line.left(eq).trimmed();
        const QByteArray value = line.mid(eq + 1).trimmed();
        if ((key == "NoDisplay" || key == "Hidden" || key == "Terminal") && value == "true")
            return true;
    }
    return false;
}

Rating parseRating(const QJsonObject &stars)
{
    Rating r;
    qint64 sum = 0;
    for (int k = 1; k <= 5; ++k) {
        const QJsonValue v = stars.value(QStringLiteral("star%1").arg(k));
        const double n = v.isDouble() ? v.toDouble() : 0;
        const int count = n > 0 && n < 1e9 ? int(n) : 0;
        r.histogram[k - 1] = count;
        r.count += count;
        sum += qint64(k) * count;
    }
    if (r.count > 0)
        r.average = double(sum) / r.count;
    return r;
}

Feeds loadFeeds(const QString &dir)
{
    Feeds feeds;
    for (const char *name : {"popular", "trending", "recently-updated", "recently-added"}) {
        const QJsonDocument doc = QJsonDocument::fromJson(readSmall(dir + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".json")));
        if (!doc.isArray())
            continue;
        QStringList &order = feeds.lists[QLatin1String(name)];
        for (const QJsonValue &v : doc.array()) {
            const QJsonObject o = v.toObject();
            const QString id = o.value(QStringLiteral("id")).toString();
            if (id.isEmpty())
                continue;
            const QString key = appKey(id);
            FeedEntry &e = feeds.apps[key];
            const double installs = o.value(QStringLiteral("installs_last_month")).toDouble();
            if (installs > 0 && installs < 1e12)
                e.installs = qMax(e.installs, qint64(installs));
            // can be below zero (falling); the highest of the lists wins
            const QJsonValue trending = o.value(QStringLiteral("trending"));
            if (trending.isDouble() && (e.trending == 0 || trending.toDouble() > e.trending))
                e.trending = trending.toDouble();
            e.verified = e.verified || o.value(QStringLiteral("verified")).toBool();
            e.updatedAt = qMax(e.updatedAt, parseTime(o.value(QStringLiteral("updated_at"))));
            e.addedAt = qMax(e.addedAt, parseTime(o.value(QStringLiteral("added_at"))));
            if (!order.contains(key))
                order.append(key);
        }
    }
    const QJsonObject picks = QJsonDocument::fromJson(readSmall(dir + QStringLiteral("/picks.json"))).object();
    feeds.pickOfTheDay = appKey(picks.value(QStringLiteral("day")).toString());
    for (const QJsonValue &v : picks.value(QStringLiteral("week")).toArray()) {
        if (v.isString() && !v.toString().isEmpty())
            feeds.picksOfTheWeek.append(appKey(v.toString()));
    }
    const QJsonObject ratings = QJsonDocument::fromJson(readSmall(dir + QStringLiteral("/ratings.json"))).object();
    for (auto it = ratings.constBegin(); it != ratings.constEnd(); ++it) {
        if (it.value().isObject())
            feeds.ratings.insert(appKey(it.key()), parseRating(it.value().toObject()));
    }
    return feeds;
}

QHash<QString, qint64> parseAptRank(const QByteArray &tsv)
{
    QHash<QString, qint64> rank;
    for (const QByteArray &raw : tsv.split('\n')) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() != 2)
            continue;
        const QString name = QString::fromUtf8(fields.at(0));
        bool ok = false;
        const qint64 count = fields.at(1).toLongLong(&ok);
        if (ok && count > 0 && isPackageName(name))
            rank.insert(name, count);
    }
    return rank;
}

QSet<QString> parseCurationAllow(const QByteArray &json)
{
    QSet<QString> allow;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).object().value(QStringLiteral("allow")).toArray()) {
        if (v.isString() && !v.toString().isEmpty())
            allow.insert(appKey(v.toString()));
    }
    return allow;
}

void applyFeeds(QList<App> &apps, const Feeds &feeds, const QHash<QString, qint64> &aptRank)
{
    qint64 flathubTop = 0, aptTop = 0;
    for (const FeedEntry &e : feeds.apps)
        flathubTop = qMax(flathubTop, e.installs);
    for (qint64 n : aptRank)
        aptTop = qMax(aptTop, n);
    for (App &app : apps) {
        const QString key = appKey(app.id);
        if (app.source == QLatin1String("flatpak")) {
            const auto it = feeds.apps.constFind(key);
            if (it != feeds.apps.constEnd()) {
                app.popularity = it->installs;
                app.trending = it->trending;
                app.verified = it->verified;
                app.updatedAt = qMax(app.updatedAt, it->updatedAt);
                if (it->addedAt > 0)
                    app.addedAt = it->addedAt;
            }
        } else if (app.source == QLatin1String("apt")) {
            const qint64 n = aptRank.value(app.ref);
            app.popularity = flathubTop > 0 && aptTop > 0 ? n * flathubTop / aptTop : n;
        }
        const auto r = feeds.ratings.constFind(key);
        if (r != feeds.ratings.constEnd()) {
            app.ratingAvg = r->average;
            app.ratingCount = r->count;
            app.ratingHistogram = r->histogram;
        }
    }
}

bool isListed(const App &app, const QSet<QString> &allow)
{
    // a game that can't run here has nothing to install, and is listed
    if (app.source == QLatin1String("windows") && app.windowsState == QLatin1String("blocked"))
        return !app.name.isEmpty() && !app.summary.isEmpty();
    if (app.name.isEmpty() || app.summary.isEmpty() || app.ref.isEmpty() || app.noDisplay || app.icon.isEmpty())
        return false;
    if (app.source == QLatin1String("raye") || app.source == QLatin1String("windows"))
        return true; // curated by hand
    const bool remote = app.icon.startsWith(QLatin1String("https://")) || app.icon.startsWith(QLatin1String("http://"));
    // remote icons come at 128 px when AppStream does not say
    if (remote ? (app.iconSize != 0 && app.iconSize < kMinIcon) : app.iconSize < kMinIcon)
        return false;
    if (app.source == QLatin1String("apt"))
        return !app.screenshots.isEmpty() || !app.screenshot.isEmpty() || allow.contains(appKey(app.id));
    return true;
}

void hideArchiveTwins(QList<App> &apps)
{
    QSet<QString> flathub;
    for (const App &app : std::as_const(apps)) {
        if (app.listed && app.source == QLatin1String("flatpak"))
            flathub.insert(foldText(app.name).simplified());
    }
    for (App &app : apps) {
        if (app.listed && app.source == QLatin1String("apt") && flathub.contains(foldText(app.name).simplified()))
            app.listed = false;
    }
}

QString foldText(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD).toCaseFolded();
    QString out;
    out.reserve(decomposed.size());
    for (const QChar ch : decomposed) {
        switch (ch.category()) {
        case QChar::Mark_NonSpacing:
        case QChar::Mark_SpacingCombining:
        case QChar::Mark_Enclosing:
            continue;
        default:
            break;
        }
        switch (ch.unicode()) {
        case 0x00df: out += QLatin1String("ss"); break; // ß
        case 0x00e6: out += QLatin1String("ae"); break; // æ
        case 0x0153: out += QLatin1String("oe"); break; // œ
        case 0x00f8: out += QLatin1Char('o'); break;    // ø
        case 0x0111: out += QLatin1Char('d'); break;    // đ
        case 0x0142: out += QLatin1Char('l'); break;    // ł
        case 0x0131: out += QLatin1Char('i'); break;    // dotless i
        default: out += ch;
        }
    }
    return out;
}

int searchScore(const QString &query, const App &app)
{
    const QString q = foldText(query).simplified();
    if (q.isEmpty())
        return 0;
    const QString name = foldText(app.name).simplified();
    if (name == q)
        return 100;
    if (name.startsWith(q))
        return 80;
    const QStringList nameWords = words(name);
    for (qsizetype i = 1; i < nameWords.size(); ++i) {
        if (nameWords.mid(i).join(QLatin1Char(' ')).startsWith(q))
            return 60;
    }
    const bool fuzzy = q.size() >= 5;
    if (fuzzy) {
        if (nearMiss(q, name))
            return 50;
        for (const QString &w : nameWords) {
            if (nearMiss(q, w))
                return 50;
        }
    }
    // the exact name of a Windows program this app stands in for: it is
    // the answer, ahead of names that only start the same way
    if (!app.replaces.isEmpty()) {
        for (const QString &k : app.keywords) {
            if (foldText(k).simplified() == q)
                return 90;
        }
    }
    int best = 0;
    for (const QString &k : app.keywords) {
        const QString kw = foldText(k).simplified();
        if (kw.isEmpty())
            continue;
        if (kw.startsWith(q))
            return 40;
        if (fuzzy && withinOneEdit(q, kw))
            best = 35;
    }
    if (best)
        return best;
    const QStringList summaryWords = words(foldText(app.summary));
    const QStringList queryWords = words(q);
    if (queryWords.isEmpty())
        return 0;
    for (const QString &qw : queryWords) {
        if (std::none_of(summaryWords.cbegin(), summaryWords.cend(), [&qw](const QString &sw) { return sw.startsWith(qw); }))
            return 0;
    }
    return 20;
}

Catalog::Catalog(QObject *parent)
    : QAbstractListModel(parent)
{
}

QString Catalog::cacheFile()
{
    const QString env = qEnvironmentVariable("RAYE_STORE_CACHE");
    if (!env.isEmpty())
        return env;
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/raye-store/catalog");
}

void Catalog::reload()
{
    if (m_busy) {
        // the fresh metadata came while the old was still being read
        m_again = true;
        return;
    }
    m_busy = true;
    if (m_settled) {
        m_settled = false;
        Q_EMIT settledChanged();
    }
    const bool nothingShown = m_shown.isEmpty() && m_apps.isEmpty();
    if (nothingShown && !m_loading) {
        m_loading = true;
        Q_EMIT loadingChanged();
    }
    // the GUI thread only swaps lists: reading, parsing and comparing all
    // happen on a worker, so the window keeps resizing and painting
    auto show = [this](Loaded loaded) {
        if (!loaded.bytes.isEmpty() && loaded.bytes == m_shown)
            return; // the same list again: no reset, no rebuilt rows
        m_windowsOrder = std::move(loaded.windowsOrder);
        setFeeds(std::move(loaded.feeds));
        setApps(std::move(loaded.apps));
        m_shown = std::move(loaded.bytes);
        refreshInstalled();
        if (m_loading) {
            m_loading = false;
            Q_EMIT loadingChanged();
        }
    };
    auto full = [this, show] {
        auto *watcher = new QFutureWatcher<Loaded>(this);
        connect(watcher, &QFutureWatcher<Loaded>::finished, this, [this, watcher, show] {
            show(watcher->result());
            watcher->deleteLater();
            m_busy = false;
            m_settled = true;
            Q_EMIT settledChanged();
            if (std::exchange(m_again, false))
                reload();
        });
        watcher->setFuture(QtConcurrent::run([] {
            Loaded loaded = loadEverything();
            saveCache(loaded.bytes);
            return loaded;
        }));
    };
    if (!nothingShown) {
        full();
        return;
    }
    // last run's list first, read in a few milliseconds, then the real one
    auto *cached = new QFutureWatcher<Loaded>(this);
    connect(cached, &QFutureWatcher<Loaded>::finished, this, [cached, show, full] {
        Loaded loaded = cached->result();
        if (!loaded.bytes.isEmpty())
            show(std::move(loaded));
        cached->deleteLater();
        full();
    });
    cached->setFuture(QtConcurrent::run(loadCached));
}

void Catalog::refreshInstalled()
{
    // flatpak, dpkg-query and raye-windows are three processes: never on
    // the GUI thread
    if (m_installedBusy) {
        m_installedAgain = true;
        return;
    }
    m_installedBusy = true;
    auto *watcher = new QFutureWatcher<InstalledNow>(this);
    connect(watcher, &QFutureWatcher<InstalledNow>::finished, this, [this, watcher] {
        InstalledNow now = watcher->result();
        watcher->deleteLater();
        m_launchers = std::move(now.launchers);
        setInstalled(std::move(now.refs));
        m_installedBusy = false;
        if (std::exchange(m_installedAgain, false))
            refreshInstalled();
    });
    watcher->setFuture(QtConcurrent::run([] {
        InstalledNow now;
        now.launchers = readSystemLaunchers({QStringLiteral("/usr/local/share/applications"), QStringLiteral("/usr/share/applications")});
        now.refs = installedNow();
        return now;
    }));
}

void Catalog::setInstalled(QSet<QString> refs)
{
    const QHash<QString, QString> copiesBefore = m_systemCopies;
    const bool same = refs == m_installed;
    m_installed = std::move(refs);
    findSystemCopies();
    if (same && copiesBefore == m_systemCopies)
        return;
    if (!m_apps.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_apps.size()) - 1), {InstalledRole, SystemCopyRole});
    Q_EMIT installedChanged();
}

void Catalog::noteInstalled(const QString &ref, bool installed)
{
    QSet<QString> refs = m_installed;
    if (installed)
        refs.insert(ref);
    else
        refs.remove(ref);
    setInstalled(std::move(refs));
}

void Catalog::setSystemLaunchers(QList<SystemLauncher> launchers)
{
    m_launchers = std::move(launchers);
    setInstalled(m_installed);
}

void Catalog::findSystemCopies()
{
    m_systemCopies.clear();
    if (m_launchers.isEmpty())
        return;
    for (const App &app : std::as_const(m_apps)) {
        if (m_installed.contains(app.ref))
            continue;
        const QString launcher = systemLauncherFor(app, m_launchers);
        if (!launcher.isEmpty())
            m_systemCopies.insert(app.ref, launcher);
    }
}

QStringList Catalog::defaultWatchFiles()
{
    // flatpak touches .changed after every install, update or removal;
    // dpkg rewrites its status file
    return {QStringLiteral("/var/lib/flatpak/.changed"), QStringLiteral("/var/lib/dpkg/status")};
}

void Catalog::watchInstalled(const QStringList &files)
{
    m_watchFiles = files;
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        m_installedLater.setSingleShot(true);
        m_installedLater.setInterval(700);
        connect(&m_installedLater, &QTimer::timeout, this, [this] {
            rewatch();
            refreshInstalled();
        });
        const auto later = [this] { m_installedLater.start(); };
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, later);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, later);
    }
    rewatch();
}

void Catalog::rewatch()
{
    // a file replaced by a new one (dpkg renames its status into place)
    // drops out of the watch: watch it again, and its folder while the
    // file does not exist
    for (const QString &file : std::as_const(m_watchFiles)) {
        if (QFileInfo::exists(file) && !m_watcher->files().contains(file))
            m_watcher->addPath(file);
        const QString dir = QFileInfo(file).absolutePath();
        if (!QFileInfo::exists(file) && QFileInfo::exists(dir) && !m_watcher->directories().contains(dir))
            m_watcher->addPath(dir);
    }
}

void Catalog::setApps(QList<App> apps)
{
    beginResetModel();
    m_apps = std::move(apps);
    endResetModel();
}

void Catalog::setFeeds(Feeds feeds)
{
    m_feeds = std::move(feeds);
    Q_EMIT feedsChanged();
}

QStringList Catalog::feed(const QString &name) const
{
    if (name == QLatin1String("picks-of-the-week"))
        return m_feeds.picksOfTheWeek;
    return m_feeds.lists.value(name);
}

QStringList Catalog::windowsPrograms(const QString &which) const
{
    const bool optimizedOnly = which == QLatin1String("optimized");
    QHash<QString, const App *> byKey;
    for (const App &app : m_apps)
        byKey.insert(appKey(app.id), &app);
    QStringList ids;
    for (const QString &id : m_windowsOrder) {
        const App *app = byKey.value(appKey(id));
        if (!app || !app->listed || app->windowsState.isEmpty())
            continue;
        if (optimizedOnly && app->windowsState != QLatin1String("optimized"))
            continue;
        ids.append(app->id);
    }
    return ids;
}

QStringList Catalog::fromWindows() const
{
    // the catalog's own spelling of each id: Flathub renames by case alone
    QHash<QString, QString> listed;
    for (const App &app : m_apps) {
        if (app.listed)
            listed.insert(appKey(app.id).toLower(), app.id);
    }
    QStringList ids;
    for (const QString &id : alternativePicks(parseAlternatives(readSmall(alternativesFile())))) {
        const QString found = listed.value(appKey(id).toLower());
        if (!found.isEmpty() && !ids.contains(found))
            ids.append(found);
    }
    return ids;
}

int Catalog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_apps.size());
}

QVariant Catalog::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const App &a = m_apps.at(index.row());
    switch (role) {
    case IdRole: return a.id;
    case Qt::DisplayRole:
    case NameRole: return a.name;
    case SummaryRole: return a.summary;
    case DescriptionRole: return a.description;
    case IconRole: return a.icon;
    case IconSizeRole: return a.iconSize;
    case ScreenshotRole: return a.screenshot;
    case ScreenshotsRole: {
        QVariantList list;
        for (const Screenshot &s : a.screenshots)
            list.append(QVariantMap{{QStringLiteral("thumb"), s.thumb}, {QStringLiteral("full"), s.full},
                                    {QStringLiteral("caption"), s.caption}});
        return list;
    }
    case VideosRole: return a.videos;
    case DeveloperRole: return a.developer;
    case LicenseRole: return a.license;
    case SourceRole: return a.source;
    case RefRole: return a.ref;
    case CategoriesRole: return a.categories;
    case KeywordsRole: return a.keywords;
    case InstalledRole: return isInstalled(a.ref);
    case SystemCopyRole: return m_systemCopies.value(a.ref);
    case PreviewRole: return a.preview;
    case ReleasesRole: {
        QVariantList list;
        for (const Release &r : a.releases)
            list.append(QVariantMap{{QStringLiteral("version"), r.version}, {QStringLiteral("date"), r.date},
                                    {QStringLiteral("notes"), r.notes}});
        return list;
    }
    case MinimumAgeRole: return a.minimumAge;
    case BrandLightRole: return a.brandLight;
    case BrandDarkRole: return a.brandDark;
    case UrlsRole:
        return QVariantMap{{QStringLiteral("homepage"), a.homepage}, {QStringLiteral("help"), a.help},
                           {QStringLiteral("donate"), a.donate}, {QStringLiteral("bugtracker"), a.bugtracker},
                           {QStringLiteral("license"), a.licenseUrl}};
    case DesktopIdRole: return a.desktopId;
    case IsFreeRole: return a.isFree;
    case VerifiedRole: return a.verified;
    case PopularityRole: return a.popularity;
    case TrendingRole: return a.trending;
    case UpdatedAtRole: return dateOrNothing(a.updatedAt);
    case AddedAtRole: return dateOrNothing(a.addedAt);
    case RatingAvgRole: return a.ratingAvg;
    case RatingCountRole: return a.ratingCount;
    case RatingHistogramRole: {
        QVariantList list;
        for (int n : a.ratingHistogram)
            list.append(n);
        return list;
    }
    case KindRole: return a.kind();
    case ListedRole: return a.listed;
    case WindowsStateRole: return a.windowsState;
    case WindowsNoteRole: return a.windowsNote;
    case DownloadSizeRole: return a.downloadSize;
    case WindowsAntiCheatRole: return a.windowsAntiCheat;
    case ReplacesRole: return a.replaces;
    }
    return {};
}

QVariantMap Catalog::find(const QString &appId) const
{
    const QString key = appKey(appId);
    // the exact id first; then without case, for links whose host part
    // came lower-cased ("appstream://com.discordapp.discord")
    int found = -1;
    for (int row = 0; row < m_apps.size() && found < 0; ++row) {
        if (appKey(m_apps.at(row).id) == key)
            found = row;
    }
    for (int row = 0; row < m_apps.size() && found < 0; ++row) {
        if (appKey(m_apps.at(row).id).compare(key, Qt::CaseInsensitive) == 0)
            found = row;
    }
    if (found < 0)
        return {};
    QVariantMap m;
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        m.insert(QString::fromLatin1(it.value()), data(index(found), it.key()));
    return m;
}

QHash<int, QByteArray> Catalog::roleNames() const
{
    return {{IdRole, "appId"}, {NameRole, "name"}, {SummaryRole, "summary"},
            {DescriptionRole, "description"}, {IconRole, "iconSource"}, {IconSizeRole, "iconSize"},
            {ScreenshotRole, "screenshot"}, {ScreenshotsRole, "screenshots"}, {VideosRole, "videos"},
            {DeveloperRole, "developer"}, {LicenseRole, "license"}, {SourceRole, "source"},
            {RefRole, "ref"}, {CategoriesRole, "categories"}, {KeywordsRole, "keywords"},
            {InstalledRole, "installed"}, {SystemCopyRole, "systemCopy"}, {PreviewRole, "preview"}, {ReleasesRole, "releases"},
            {MinimumAgeRole, "minimumAge"}, {BrandLightRole, "brandLight"}, {BrandDarkRole, "brandDark"},
            {UrlsRole, "urls"}, {DesktopIdRole, "desktopId"}, {IsFreeRole, "isFree"},
            {VerifiedRole, "verified"}, {PopularityRole, "popularity"}, {TrendingRole, "trending"},
            {UpdatedAtRole, "updatedAt"}, {AddedAtRole, "addedAt"}, {RatingAvgRole, "ratingAvg"},
            {RatingCountRole, "ratingCount"}, {RatingHistogramRole, "ratingHistogram"},
            {KindRole, "kind"}, {ListedRole, "listed"}, {WindowsStateRole, "windowsState"},
            {WindowsNoteRole, "windowsNote"}, {DownloadSizeRole, "downloadSize"},
            {WindowsAntiCheatRole, "windowsAntiCheat"}, {ReplacesRole, "replaces"}};
}

CatalogFilter::CatalogFilter(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    sort(0);
    for (auto sig : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(this, sig, this, &CatalogFilter::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &CatalogFilter::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &CatalogFilter::countChanged);
}

void CatalogFilter::setSourceModel(QAbstractItemModel *model)
{
    if (model == sourceModel())
        return;
    for (const QMetaObject::Connection &c : std::as_const(m_sourceConnections))
        disconnect(c);
    m_sourceConnections.clear();
    // connected before the base class, so the cache is stale before it looks
    if (model) {
        m_sourceConnections << connect(model, &QAbstractItemModel::modelAboutToBeReset, this, [this] { m_dirty = true; });
        auto later = [this] {
            m_dirty = true;
            // a change in one row can move others in or out (limit): redo all
            QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);
        };
        m_sourceConnections << connect(model, &QAbstractItemModel::rowsInserted, this, later)
                            << connect(model, &QAbstractItemModel::rowsRemoved, this, later)
                            << connect(model, &QAbstractItemModel::dataChanged, this, later)
                            << connect(model, &QAbstractItemModel::layoutChanged, this, later);
    }
    m_dirty = true;
    QSortFilterProxyModel::setSourceModel(model);
}

void CatalogFilter::update()
{
    m_dirty = true;
    invalidate();
    Q_EMIT countChanged();
}

void CatalogFilter::setText(const QString &text) { assign(m_text, text); }
void CatalogFilter::setCategory(const QString &category) { assign(m_category, category); }
void CatalogFilter::setInstalledOnly(bool only) { assign(m_installedOnly, only); }
void CatalogFilter::setKind(const QString &kind) { assign(m_kind, kind); }
void CatalogFilter::setIds(const QStringList &ids) { assign(m_ids, ids); }
void CatalogFilter::setLimit(int limit) { assign(m_limit, qMax(0, limit)); }
void CatalogFilter::setMinRatingCount(int count) { assign(m_minRatingCount, qMax(0, count)); }
void CatalogFilter::setShowSystemPackages(bool show) { assign(m_showSystemPackages, show); }

void CatalogFilter::setSortBy(const QString &role)
{
    static const QStringList known{QStringLiteral("popularity"), QStringLiteral("name"), QStringLiteral("newest"),
                                   QStringLiteral("rating")};
    assign(m_sortBy, known.contains(role) ? role : known.first());
}

void CatalogFilter::rank() const
{
    m_dirty = false;
    m_rank.clear();
    const QAbstractItemModel *src = sourceModel();
    if (!src)
        return;

    struct Row {
        int row;
        int pos;
        int score;
        qint64 popularity;
        bool verified;
        qint64 newest;
        double average;
        int ratings;
        QString name;
        QString id;
    };
    QHash<QString, int> wanted;
    for (int i = 0; i < m_ids.size(); ++i)
        wanted.insert(appKey(m_ids.at(i)), i);
    const QString query = foldText(m_text).simplified();

    QList<Row> rows;
    const int n = src->rowCount();
    for (int r = 0; r < n; ++r) {
        const QModelIndex i = src->index(r, 0);
        auto role = [&i](int rl) { return i.data(rl); };
        if (m_installedOnly && !role(Catalog::InstalledRole).toBool())
            continue;
        if (!m_category.isEmpty() && !role(Catalog::CategoriesRole).toStringList().contains(m_category))
            continue;
        if (!m_kind.isEmpty() && role(Catalog::KindRole).toString() != m_kind)
            continue;
        // a game that can't run here is found by search, never browsed into
        if (query.isEmpty() && m_ids.isEmpty() && role(Catalog::WindowsStateRole).toString() == QLatin1String("blocked"))
            continue;
        const QString id = role(Catalog::IdRole).toString();
        int pos = 0;
        if (!m_ids.isEmpty()) {
            pos = wanted.value(appKey(id), -1);
            if (pos < 0)
                continue;
        }
        const int ratings = role(Catalog::RatingCountRole).toInt();
        if (ratings < m_minRatingCount)
            continue;
        // the library shows what is installed, listed or not
        const bool listed = role(Catalog::ListedRole).toBool() || m_installedOnly;
        const QString name = role(Catalog::NameRole).toString();
        int score = 0;
        if (!query.isEmpty()) {
            if (listed) {
                App probe;
                probe.name = name;
                probe.summary = role(Catalog::SummaryRole).toString();
                probe.keywords = role(Catalog::KeywordsRole).toStringList();
                score = searchScore(query, probe);
            } else if (m_showSystemPackages
                       && (foldText(name).simplified() == query || role(Catalog::RefRole).toString() == query)) {
                score = 100;
            }
            if (score == 0)
                continue;
        } else if (!listed) {
            continue;
        }
        const QDateTime added = role(Catalog::AddedAtRole).toDateTime();
        const QDateTime updated = role(Catalog::UpdatedAtRole).toDateTime();
        const qint64 newest = added.isValid() ? added.toSecsSinceEpoch() : updated.isValid() ? updated.toSecsSinceEpoch() : 0;
        rows.append(Row{r, pos, score, role(Catalog::PopularityRole).toLongLong(), role(Catalog::VerifiedRole).toBool(), newest,
                        role(Catalog::RatingAvgRole).toDouble(), ratings, foldText(name), id});
    }

    const QString by = m_sortBy;
    const bool curated = !m_ids.isEmpty();
    const bool searching = !query.isEmpty();
    std::stable_sort(rows.begin(), rows.end(), [&](const Row &a, const Row &b) {
        if (curated && a.pos != b.pos)
            return a.pos < b.pos;
        if (searching && a.score != b.score)
            return a.score > b.score;
        if (by == QLatin1String("name")) {
            if (a.name != b.name)
                return a.name < b.name;
        } else if (by == QLatin1String("newest")) {
            if (a.newest != b.newest)
                return a.newest > b.newest;
        } else if (by == QLatin1String("rating")) {
            const bool ea = a.ratings >= kEnoughRatings, eb = b.ratings >= kEnoughRatings;
            if (ea != eb)
                return ea;
            if (ea) {
                if (a.average != b.average)
                    return a.average > b.average;
                if (a.ratings != b.ratings)
                    return a.ratings > b.ratings;
            }
        }
        if (a.popularity != b.popularity)
            return a.popularity > b.popularity;
        if (a.verified != b.verified)
            return a.verified;
        if (a.name != b.name)
            return a.name < b.name;
        return a.id < b.id;
    });
    if (m_limit > 0 && rows.size() > m_limit)
        rows.resize(m_limit);
    for (int i = 0; i < rows.size(); ++i)
        m_rank.insert(rows.at(i).row, i);
}

bool CatalogFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (parent.isValid())
        return false;
    if (m_dirty)
        rank();
    return m_rank.contains(row);
}

bool CatalogFilter::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (m_dirty)
        rank();
    return m_rank.value(left.row(), INT_MAX) < m_rank.value(right.row(), INT_MAX);
}
