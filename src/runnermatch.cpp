// SPDX-License-Identifier: GPL-3.0-or-later
#include "runnermatch.h"

#include <KLocalizedString>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace
{

// "RYIX", then the format; a new format makes old files stale
constexpr quint32 kIndexMagic = 0x52594958;
constexpr quint32 kIndexFormat = 1;
// far above any catalog; a broken count must not allocate gigabytes
constexpr qint32 kIndexMaxEntries = 500000;

void stampOf(const QString &path, int depth, QCryptographicHash &hash)
{
    const QFileInfo info(path);
    hash.addData(path.toUtf8());
    if (!info.exists()) {
        hash.addData(":-\n");
        return;
    }
    const QByteArray line = ':' + QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + ':'
        + QByteArray::number(info.isDir() ? -1 : info.size()) + '\n';
    hash.addData(line);
    if (!info.isDir() || depth <= 0)
        return;
    const QStringList children = QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                                      QDir::Name);
    for (const QString &child : children)
        stampOf(path + QLatin1Char('/') + child, depth - 1, hash);
}

} // namespace

bool namesTheLayersUnderneath(const QString &name)
{
    // PlasmaTube and WineZGUI count, Winery and Twine do not
    static const QRegularExpression words(QStringLiteral("\\b(KDE|Kubuntu|KWallet\\w*|Plasma\\w*|Wine(tricks|glass|[A-Z]\\w*)?)\\b"));
    return words.match(name).hasMatch();
}

QList<StoreMatch> storeMatches(const QList<App> &apps, const std::function<bool(const QString &)> &installed,
                               const QString &query, int limit)
{
    struct Scored {
        const App *app;
        int score;
    };
    QList<Scored> found;
    if (foldText(query).simplified().size() < 3)
        return {};
    for (const App &app : apps) {
        if (!app.listed || app.noDisplay || installed(app.ref) || namesTheLayersUnderneath(app.name))
            continue;
        const int score = searchScore(query, app);
        if (score > 0)
            found.append({&app, score});
    }
    std::sort(found.begin(), found.end(), [](const Scored &a, const Scored &b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.app->popularity != b.app->popularity)
            return a.app->popularity > b.app->popularity;
        return QString::localeAwareCompare(a.app->name, b.app->name) < 0;
    });
    QList<StoreMatch> out;
    for (const Scored &s : found) {
        if (out.size() >= limit)
            break;
        StoreMatch m;
        m.id = s.app->id;
        m.text = i18nc("start menu search, an app the user can install", "Get %1…", s.app->name);
        m.subtext = s.app->summary;
        // remote icons are not fetched from the search
        const bool remote = s.app->icon.startsWith(QLatin1String("http:")) || s.app->icon.startsWith(QLatin1String("https:"));
        m.icon = s.app->icon.isEmpty() || remote ? QStringLiteral("system-software-install") : s.app->icon;
        if (m.icon.startsWith(QLatin1String("file://")))
            m.icon = m.icon.mid(7);
        m.relevance = std::clamp(s.score / 100.0, 0.0, 1.0);
        out.append(m);
    }
    return out;
}

QStringList storeLinkArguments(const QString &id)
{
    return {QStringLiteral("appstream://") + appKey(id)};
}

QList<App> searchEntries(const QList<App> &apps)
{
    QList<App> out;
    for (const App &app : apps) {
        // "Get Fortnite…" for a game that can't run here would be a lie
        if (!app.listed || app.noDisplay || app.windowsState == QLatin1String("blocked"))
            continue;
        App e;
        e.id = app.id;
        e.name = app.name;
        e.summary = app.summary;
        e.icon = app.icon;
        e.ref = app.ref;
        e.keywords = app.keywords;
        e.popularity = app.popularity;
        out.append(std::move(e));
    }
    out.squeeze();
    return out;
}

bool writeSearchIndex(const QString &file, const QString &stamp, const QList<App> &entries)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&out);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kIndexMagic << kIndexFormat << stamp << qint32(entries.size());
    for (const App &e : entries)
        stream << e.id << e.name << e.summary << e.icon << e.ref << e.keywords << e.popularity;
    if (stream.status() != QDataStream::Ok) {
        out.cancelWriting();
        return false;
    }
    return out.commit();
}

std::optional<QList<App>> readSearchIndex(const QString &file, const QString &stamp)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return std::nullopt;
    QDataStream stream(&in);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint32 format = 0;
    QString builtFrom;
    qint32 count = -1;
    stream >> magic >> format;
    if (stream.status() != QDataStream::Ok || magic != kIndexMagic || format != kIndexFormat)
        return std::nullopt;
    stream >> builtFrom >> count;
    if (stream.status() != QDataStream::Ok || builtFrom != stamp || count < 0 || count > kIndexMaxEntries)
        return std::nullopt;
    QList<App> entries;
    // a broken count must not reserve memory up front
    entries.reserve(std::min<qint32>(count, 16384));
    for (qint32 i = 0; i < count; ++i) {
        App e;
        stream >> e.id >> e.name >> e.summary >> e.icon >> e.ref >> e.keywords >> e.popularity;
        if (stream.status() != QDataStream::Ok)
            return std::nullopt;
        entries.append(std::move(e));
    }
    if (!stream.atEnd())
        return std::nullopt;
    return entries;
}

QString sourceStamp(const QStringList &roots, const QString &language, int depth)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const QByteArray head = language.toUtf8() + '\n';
    hash.addData(head);
    for (const QString &root : roots)
        stampOf(root, depth, hash);
    return QString::fromLatin1(hash.result().toHex());
}

QStringList defaultSourceRoots()
{
    return {
        // the archive's AppStream, wherever apt and the base system put it
        QStringLiteral("/usr/share/swcatalog"),
        QStringLiteral("/var/lib/swcatalog"),
        QStringLiteral("/var/cache/swcatalog/xml"),
        QStringLiteral("/var/cache/swcatalog/yaml"),
        QStringLiteral("/var/cache/app-info"),
        // Flathub: remote/arch/active is three levels down
        QStringLiteral("/var/lib/flatpak/appstream"),
        QDir::homePath() + QStringLiteral("/.local/share/flatpak/appstream"),
        // launchers: an app installed or removed changes what is hidden
        QStringLiteral("/usr/share/applications"),
        QStringLiteral("/var/lib/flatpak/exports/share/applications"),
        QDir::homePath() + QStringLiteral("/.local/share/applications"),
        // popularity, ratings and picks; Raye's own entries
        QStringLiteral("/var/cache/raye-store/feeds"),
        QStringLiteral("/usr/share/raye-store/extras.json"),
        QStringLiteral("/usr/share/raye-store/windows-alternatives.json"),
        QStringLiteral("/usr/share/raye-store/curation.json"),
        QStringLiteral("/usr/share/raye-store/apt-rank.tsv"),
    };
}
