// SPDX-License-Identifier: GPL-3.0-or-later
// Every app the Store can offer: Flathub apps and the system's own
// packages, read from AppStream, one entry per app, with what the
// Flathub and ratings feeds add (popularity, trending, verified, stars).
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTimer>

class QFileSystemWatcher;
#include <QVariantList>

class QJsonObject;

struct Screenshot {
    QString thumb;   // small image for the strip
    QString full;    // the full size image
    QString caption;
};

struct Release {
    QString version;
    QDateTime date;
    QString notes; // AppStream markup
};

struct App {
    QString id;
    QString name;
    QString summary;
    QString description; // AppStream markup, already HTML-ish
    QString icon;        // file or http(s) url, or a theme icon name
    int iconSize = 0;    // size of that icon in px, 0 when not known
    QString screenshot;  // first full screenshot, kept for the pages
    QList<Screenshot> screenshots;
    QStringList videos;
    QString developer;
    QString license;
    QString source;      // "flatpak", "apt", or "raye" (offered by Raye itself)
    QString ref;         // flatpak ref or package name
    QStringList categories;
    QStringList keywords;
    QList<Release> releases; // newest first, at most 10
    int minimumAge = 0;      // from the content rating, 0 = everyone
    QString brandLight;      // AppStream branding colours, "#rrggbb" or empty
    QString brandDark;
    QString homepage;
    QString help;
    QString donate;
    QString bugtracker;
    QString licenseUrl;      // the terms a Windows program's setup accepts for the user
    QString desktopId;       // launchable, e.g. "org.gimp.GIMP.desktop"
    bool isFree = false;     // free licence
    bool preview = false;    // early versions: the Store says so
    bool noDisplay = false;  // its launcher is hidden or a console tool
    // Windows programs (see windowsprograms.h): optimized, works, blocked,
    // or native for the native version of a Windows program; else empty
    QString windowsState;
    QString windowsNote;     // what Raye says about it, in the user's language
    bool windowsAntiCheat = false; // blocked by an anti-cheat, not by something missing
    qint64 downloadSize = 0; // the installer's size, when known
    // Windows programs with no version here that this app stands in for
    // (alternatives.h)
    QStringList replaces;
    // from the feeds
    bool verified = false;
    qint64 popularity = 0;   // installs last month (archive apps: scaled)
    double trending = 0;
    qint64 updatedAt = 0;    // seconds since 1970, 0 when not known
    qint64 addedAt = 0;
    double ratingAvg = 0;    // 1 to 5, 0 when nobody rated
    int ratingCount = 0;
    QList<int> ratingHistogram{0, 0, 0, 0, 0}; // 1 to 5 stars
    // decided by the rules of the listing (see isListed)
    bool listed = true;

    QString kind() const; // "game" or "app"
};

// Apps Raye offers itself, from extras.json. An entry without an https
// URL, a full SHA-256 or a plain package name is left out. Summary and
// description are plain text or {"en": ..., "fr": ...}.
QList<App> parseExtras(const QByteArray &json, bool french = false);

// Picks the entry to keep when the same app exists twice: Flathub wins,
// it is newer and runs in a sandbox.
bool preferOver(const App &candidate, const App &current);

// The id without ".desktop": Flathub and the feeds use this form.
QString appKey(const QString &id);

// "game" when one of the categories is Game, "app" otherwise.
QString appKind(const QStringList &categories);

// One icon AppStream lists for an app.
struct IconCandidate {
    enum Kind { Cached, Local, Remote, Stock };
    Kind kind = Stock;
    QString url;  // file or http(s) url, or the theme name for Stock
    int width = 0;
};
struct IconChoice {
    QString icon;
    int size = 0;
};
// 128 px on disk, then 64 on disk, then the remote one, then anything.
IconChoice pickIcon(const QList<IconCandidate> &candidates);
// A theme icon name turned into a file of 64 px or more under the given
// data dirs (hicolor, then pixmaps is not used: too small), or empty.
IconChoice resolveStockIcon(const QString &name, const QStringList &dataDirs);

// true when a .desktop file is not meant to show in menus: NoDisplay,
// Hidden, or a console tool (Terminal=true).
bool desktopEntryHidden(const QByteArray &contents);

// A launcher a system package put in the applications folder.
struct SystemLauncher {
    QString id;   // "firefox", without .desktop
    QString name;
};
// The visible launchers of the given folders; the first folder wins.
QList<SystemLauncher> readSystemLaunchers(const QStringList &dirs);
// The launcher that is this app, installed some other way than its
// listing says: Flathub's org.mozilla.firefox is the system's firefox.
// By launchable and id first; else by the id's last word and the name,
// since a word alone (org.example.Files) proves nothing. Empty if none.
QString systemLauncherFor(const App &app, const QList<SystemLauncher> &launchers);

// What the feed cache says about one Flathub app.
struct FeedEntry {
    qint64 installs = 0;
    double trending = 0;
    bool verified = false;
    qint64 updatedAt = 0;
    qint64 addedAt = 0;
};
struct Rating {
    double average = 0;
    int count = 0;
    QList<int> histogram{0, 0, 0, 0, 0};
};
// The files the catalog timer writes under /var/cache/raye-store/feeds.
// Every file is optional; a broken file reads as missing.
struct Feeds {
    QHash<QString, FeedEntry> apps;          // by app key, all lists merged
    QHash<QString, QStringList> lists;       // "popular", "trending", ... in feed order
    QHash<QString, Rating> ratings;          // by app key
    QString pickOfTheDay;
    QStringList picksOfTheWeek;
};
Feeds loadFeeds(const QString &dir);
// One ODRS entry: {"star1": n, ..., "star5": n}; star0 is not a rating.
Rating parseRating(const QJsonObject &stars);
// Lines "package<TAB>installs" from popcon (data/apt-rank.tsv).
QHash<QString, qint64> parseAptRank(const QByteArray &tsv);
// The ids curation.json lets in although they have no screenshot.
QSet<QString> parseCurationAllow(const QByteArray &json);

// Popularity, verified, dates and stars on every app. Archive apps get
// their popcon count scaled so the top one ranks with the top Flathub app.
void applyFeeds(QList<App> &apps, const Feeds &feeds, const QHash<QString, qint64> &aptRank);

// The rules of what the Store lists: a name, a summary, an icon of 64 px
// or more, a way to install it; archive apps also need a screenshot or
// a place in curation.json's allow list; hidden launchers never show.
bool isListed(const App &app, const QSet<QString> &allow);
// An archive app listed under the same name as a Flathub one (Chocolate
// Doom is org.chocolate_doom.Doom in one, ...ChocolateDoom in the other)
// would show twice: the archive one leaves the listing.
void hideArchiveTwins(QList<App> &apps);

// Lower case, accents and ligatures folded: "Élève" -> "eleve".
QString foldText(const QString &text);
// 0 when the query does not match; higher is a better match:
// exact name > name prefix > word prefix in name > near miss in the
// name (one typo, queries of 5 letters or more) > keywords > summary.
int searchScore(const QString &query, const App &app);

// The catalog as the last load left it, so the next start shows it at
// once. `stamp` names what the entries depend on (format, language,
// Windows support): bytes made with another stamp do not decode.
QByteArray encodeCatalog(const QList<App> &apps, const Feeds &feeds, const QStringList &windowsOrder, const QString &stamp);
bool decodeCatalog(const QByteArray &bytes, const QString &stamp, QList<App> &apps, Feeds &feeds, QStringList &windowsOrder);
// the stamp of this run
QString catalogCacheStamp();

class Catalog : public QAbstractListModel
{
    Q_OBJECT
    // nothing to show yet: true until the first list, from the cache or read
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // the list shown comes from the latest read, not from the last run's cache
    Q_PROPERTY(bool settled READ settled NOTIFY settledChanged)
    Q_PROPERTY(QString pickOfTheDay READ pickOfTheDay NOTIFY feedsChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, SummaryRole, DescriptionRole, IconRole,
        ScreenshotRole, DeveloperRole, LicenseRole, SourceRole, RefRole, CategoriesRole, InstalledRole,
        PreviewRole, IconSizeRole, ScreenshotsRole, VideosRole, KeywordsRole, ReleasesRole,
        MinimumAgeRole, BrandLightRole, BrandDarkRole, UrlsRole, DesktopIdRole, IsFreeRole,
        VerifiedRole, PopularityRole, TrendingRole, UpdatedAtRole, AddedAtRole, RatingAvgRole,
        RatingCountRole, RatingHistogramRole, KindRole, ListedRole, SystemCopyRole, WindowsStateRole,
        WindowsNoteRole, DownloadSizeRole, WindowsAntiCheatRole, ReplacesRole
    };

    explicit Catalog(QObject *parent = nullptr);

    Q_INVOKABLE void reload();
    Q_INVOKABLE void refreshInstalled();
    // on this PC, from this listing or as a system package
    Q_INVOKABLE bool isInstalled(const QString &ref) const { return m_installed.contains(ref) || m_systemCopies.contains(ref); }
    // only from this listing: what removing or updating it can act on
    Q_INVOKABLE bool ownsInstall(const QString &ref) const { return m_installed.contains(ref); }
    // the launcher ("firefox.desktop") when the system's copy is what is
    // on this PC, else empty
    Q_INVOKABLE QString systemCopy(const QString &ref) const { return m_systemCopies.value(ref); }
    // the app with this id, as the pages want it, or an empty map
    Q_INVOKABLE QVariantMap find(const QString &appId) const;
    // ids of a feed in its own order: popular, trending, recently-updated,
    // recently-added, picks-of-the-week
    Q_INVOKABLE QStringList feed(const QString &name) const;
    // Windows programs in the list's order: "optimized" for those alone,
    // anything else for all of them (native versions included)
    Q_INVOKABLE QStringList windowsPrograms(const QString &which = {}) const;
    // apps that do the job of Windows programs with no version here,
    // first choices first (alternatives.h)
    Q_INVOKABLE QStringList fromWindows() const;
    bool loading() const { return m_loading; }
    bool settled() const { return m_settled; }
    // where the last list is kept between runs; RAYE_STORE_CACHE names another
    static QString cacheFile();
    QString pickOfTheDay() const { return m_feeds.pickOfTheDay; }

    // follows installs and removals made anywhere (the Store, a terminal,
    // another tool): when one of these files changes, what is installed
    // is read again. By default flatpak's and dpkg's own marks.
    void watchInstalled(const QStringList &files = defaultWatchFiles());
    static QStringList defaultWatchFiles();

    // replaces the list (used by reload, and by the tests)
    void setApps(QList<App> apps);
    void setFeeds(Feeds feeds);
    void setInstalled(QSet<QString> refs);
    // a job the installer just saw through: shown at once, before the
    // next read of what is installed says the same
    void noteInstalled(const QString &ref, bool installed);
    void setSystemLaunchers(QList<SystemLauncher> launchers);
    void setWindowsOrder(QStringList ids) { m_windowsOrder = std::move(ids); }
    const QList<App> &apps() const { return m_apps; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void loadingChanged();
    void settledChanged();
    void feedsChanged();
    void installedChanged();

private:
    void rewatch();
    void findSystemCopies();

    QList<App> m_apps;
    QFileSystemWatcher *m_watcher = nullptr;
    QStringList m_watchFiles;
    QTimer m_installedLater; // a burst of changes is read once
    Feeds m_feeds;
    QSet<QString> m_installed; // refs and package names
    QList<SystemLauncher> m_launchers;
    QHash<QString, QString> m_systemCopies; // ref -> "firefox.desktop"
    QStringList m_windowsOrder;
    bool m_loading = false;
    bool m_settled = false;
    bool m_busy = false;            // a load is running
    bool m_again = false;           // asked for while it ran: load once more
    bool m_installedBusy = false;
    bool m_installedAgain = false;
    QByteArray m_shown;             // the cache bytes of what the list shows
};

// Search box, category, curated lists and sorting on top of the catalog:
// every row of the Store is one of these.
class CatalogFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY changed)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY changed)
    Q_PROPERTY(bool installedOnly READ installedOnly WRITE setInstalledOnly NOTIFY changed)
    // "popularity" (default), "name", "newest" or "rating"
    Q_PROPERTY(QString sortRole READ sortBy WRITE setSortBy NOTIFY changed)
    // "" for everything, "app" or "game"
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY changed)
    // a curated list: only these, in this order
    Q_PROPERTY(QStringList ids READ ids WRITE setIds NOTIFY changed)
    // at most this many rows, 0 for all
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY changed)
    Q_PROPERTY(int minRatingCount READ minRatingCount WRITE setMinRatingCount NOTIFY changed)
    // archive apps the listing hides, found by their exact name
    Q_PROPERTY(bool showSystemPackages READ showSystemPackages WRITE setShowSystemPackages NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit CatalogFilter(QObject *parent = nullptr);

    QString text() const { return m_text; }
    void setText(const QString &text);
    QString category() const { return m_category; }
    void setCategory(const QString &category);
    bool installedOnly() const { return m_installedOnly; }
    void setInstalledOnly(bool only);
    QString sortBy() const { return m_sortBy; }
    void setSortBy(const QString &role);
    QString kind() const { return m_kind; }
    void setKind(const QString &kind);
    QStringList ids() const { return m_ids; }
    void setIds(const QStringList &ids);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    int minRatingCount() const { return m_minRatingCount; }
    void setMinRatingCount(int count);
    bool showSystemPackages() const { return m_showSystemPackages; }
    void setShowSystemPackages(bool show);
    int count() const { return rowCount(); }

    void setSourceModel(QAbstractItemModel *model) override;

Q_SIGNALS:
    void changed();
    void countChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    void update();      // after a setting changed
    template<typename T>
    void assign(T &setting, const T &value)
    {
        if (setting == value)
            return;
        setting = value;
        update();
        Q_EMIT changed();
    }
    void rank() const;  // the rows that pass, in order (cached)

    QString m_text;
    QString m_category;
    QString m_sortBy = QStringLiteral("popularity");
    QString m_kind;
    QStringList m_ids;
    int m_limit = 0;
    int m_minRatingCount = 0;
    bool m_installedOnly = false;
    bool m_showSystemPackages = false;
    mutable bool m_dirty = true;
    mutable QHash<int, int> m_rank; // source row -> position
    QList<QMetaObject::Connection> m_sourceConnections;
};
