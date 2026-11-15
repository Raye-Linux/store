// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowsprograms.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

const QString kTitlesShipped = QStringLiteral("/usr/share/raye/windows-titles");
const QString kTitlesLocal = QStringLiteral("/var/lib/raye/windows-titles");
const QString kProfiles = QStringLiteral("/usr/share/raye/windows-profiles");
constexpr qint64 kMaxDataFile = 4 * 1024 * 1024;

QByteArray readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxDataFile)
        return {};
    return f.readAll();
}

bool validSpace(const QString &space)
{
    static const QRegularExpression slug(QStringLiteral("^[a-z0-9][a-z0-9-]*$"));
    return slug.match(space).hasMatch();
}

// {"en": ..., "fr": ...}, or a plain string read as English
QString pick(const QJsonValue &v, bool french)
{
    if (v.isString())
        return v.toString();
    const QJsonObject o = v.toObject();
    const QString fr = o.value(QStringLiteral("fr")).toString();
    return french && !fr.isEmpty() ? fr : o.value(QStringLiteral("en")).toString();
}

QString noteFor(const WindowsTitle &t, bool french)
{
    return french && !t.whyFr.isEmpty() ? t.whyFr : t.whyEn;
}

} // namespace

QStringList windowsTitleDirs()
{
    const QString env = qEnvironmentVariable("RAYE_WINDOWS_TITLES_PATH");
    if (!env.isEmpty())
        return env.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    return {kTitlesShipped, kTitlesLocal};
}

QString windowsProfileDir()
{
    const QString env = qEnvironmentVariable("RAYE_WINDOWS_PROFILES_PATH");
    return env.isEmpty() ? kProfiles : env;
}

QHash<QString, WindowsTitle> readWindowsTitles(const QStringList &dirs)
{
    QHash<QString, WindowsTitle> titles;
    for (const QString &dir : dirs) {
        const QStringList names = QDir(dir).entryList({QStringLiteral("*.titles")}, QDir::Files, QDir::Name);
        for (const QString &name : names) {
            // configparser's rules: [section], key = value or key: value,
            // # and ; comments, indented lines continue the value
            QHash<QString, QHash<QString, QString>> sections;
            QString section, key;
            const QStringList lines = QString::fromUtf8(readText(dir + QLatin1Char('/') + name)).split(QLatin1Char('\n'));
            for (const QString &raw : lines) {
                const QString line = raw.trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
                    continue;
                if (!section.isEmpty() && !key.isEmpty() && (raw.startsWith(QLatin1Char(' ')) || raw.startsWith(QLatin1Char('\t')))) {
                    QString &value = sections[section][key];
                    value += QLatin1Char(' ') + line;
                    continue;
                }
                if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                    section = line.mid(1, line.size() - 2);
                    sections[section];
                    key.clear();
                    continue;
                }
                const qsizetype eq = line.indexOf(QLatin1Char('='));
                const qsizetype colon = line.indexOf(QLatin1Char(':'));
                const qsizetype at = eq < 0 ? colon : colon < 0 ? eq : std::min(eq, colon);
                if (section.isEmpty() || at <= 0)
                    continue;
                key = line.left(at).trimmed().toLower();
                sections[section][key] = line.mid(at + 1).trimmed();
            }
            for (auto it = sections.cbegin(); it != sections.cend(); ++it) {
                const auto &keys = it.value();
                const auto value = [&keys](const char *k) { return keys.value(QLatin1String(k)).simplified(); };
                WindowsTitle t;
                t.id = it.key();
                t.name = value("name");
                t.state = value("state");
                t.anticheat = value("anticheat");
                t.whyEn = value("why_en");
                t.whyFr = value("why_fr");
                titles.insert(t.id, t);
            }
        }
    }
    return titles;
}

WindowsProfile readWindowsProfile(const QString &path)
{
    WindowsProfile p;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return p;
    p.found = true;
    static const QRegularExpression line(QStringLiteral("^([a-z]+)=\"(.*)\"$"));
    for (const QString &raw : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        const auto m = line.match(raw);
        if (!m.hasMatch())
            continue;
        const QString key = m.captured(1), value = m.captured(2);
        if (key == QLatin1String("name"))
            p.name = value;
        else if (key == QLatin1String("title"))
            p.title = value;
        else if (key == QLatin1String("native"))
            p.native = value;
        else if (key == QLatin1String("match"))
            p.match = value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }
    return p;
}

QString windowsStateFor(const WindowsProfile &profile, const QHash<QString, WindowsTitle> &titles)
{
    const QString state = profile.title.isEmpty() ? QString() : titles.value(profile.title).state;
    if (state == QLatin1String("blocked"))
        return QStringLiteral("blocked");
    // a profile of its own, checked by Raye on a real machine
    if (profile.found && state == QLatin1String("works"))
        return QStringLiteral("optimized");
    return QStringLiteral("works");
}

QString windowsRef(const QString &space)
{
    return QStringLiteral("windows:") + space;
}

bool isWindowsRef(const QString &ref)
{
    return ref.startsWith(QLatin1String("windows:")) && validSpace(ref.mid(8));
}

QString windowsSpace(const QString &ref)
{
    return isWindowsRef(ref) ? ref.mid(8) : QString();
}

WindowsListing readWindowsListing(const QByteArray &json, const QString &iconDir,
                                  const QHash<QString, WindowsTitle> &titles,
                                  const QString &profileDir, bool french, bool canInstall)
{
    static const QRegularExpression sha256(QStringLiteral("^[0-9a-f]{64}$"));
    static const QRegularExpression fileName(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+-]*\\.(exe|msi)$"));
    WindowsListing out;
    QSet<QString> offeredTitles;
    const QJsonArray list = QJsonDocument::fromJson(json).object().value(QStringLiteral("programs")).toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        const QString space = o.value(QStringLiteral("profile")).toString();
        const QString name = o.value(QStringLiteral("name")).toString();
        if (!validSpace(space) || name.isEmpty())
            continue;
        const WindowsProfile profile = readWindowsProfile(profileDir + QLatin1Char('/') + space + QStringLiteral(".profile"));
        if (!profile.title.isEmpty())
            offeredTitles.insert(profile.title);
        const QString native = o.value(QStringLiteral("native")).toString();
        if (!native.isEmpty()) {
            out.natives.insert(appKey(native), name);
            out.order.append(native);
            continue;
        }
        const QJsonObject inst = o.value(QStringLiteral("installer")).toObject();
        const QString url = inst.value(QStringLiteral("url")).toString();
        if (!canInstall || !url.startsWith(QLatin1String("https://"))
            || !sha256.match(inst.value(QStringLiteral("sha256")).toString()).hasMatch()
            || !fileName.match(inst.value(QStringLiteral("file")).toString()).hasMatch())
            continue;
        App app;
        app.id = QStringLiteral("windows.") + space;
        app.name = name;
        app.summary = pick(o.value(QStringLiteral("summary")), french);
        app.description = pick(o.value(QStringLiteral("description")), french);
        app.developer = o.value(QStringLiteral("developer")).toString();
        app.license = o.value(QStringLiteral("license")).toString();
        app.homepage = o.value(QStringLiteral("homepage")).toString();
        const QString terms = o.value(QStringLiteral("license_url")).toString();
        if (terms.startsWith(QLatin1String("https://")))
            app.licenseUrl = terms;
        for (const QJsonValue &c : o.value(QStringLiteral("categories")).toArray())
            app.categories.append(c.toString());
        for (const QJsonValue &k : o.value(QStringLiteral("keywords")).toArray())
            app.keywords.append(k.toString());
        const QString icon = o.value(QStringLiteral("icon")).toString();
        if (!icon.isEmpty() && !icon.contains(QLatin1String("..")))
            app.icon = icon.startsWith(QLatin1Char('/')) ? icon : iconDir + QLatin1Char('/') + icon;
        app.iconSize = 128;
        app.source = QStringLiteral("windows");
        app.ref = windowsRef(space);
        app.downloadSize = inst.value(QStringLiteral("size")).toInteger();
        const QString version = inst.value(QStringLiteral("version")).toString();
        if (!version.isEmpty())
            app.releases.append(Release{version, {}, {}});
        app.windowsState = windowsStateFor(profile, titles);
        if (!profile.title.isEmpty() && titles.contains(profile.title))
            app.windowsNote = noteFor(titles.value(profile.title), french);
        out.order.append(app.id);
        out.apps.append(app);
    }
    // games that refuse this system: found by search, with the reason
    for (const WindowsTitle &t : titles) {
        if (t.state != QLatin1String("blocked") || offeredTitles.contains(t.id) || !validSpace(t.id))
            continue;
        App app;
        app.id = QStringLiteral("windows-title.") + t.id;
        app.name = t.name.isEmpty() ? t.id : t.name;
        app.windowsNote = noteFor(t, french);
        app.summary = app.windowsNote;
        app.categories = {QStringLiteral("Game")};
        app.icon = iconDir + QStringLiteral("/windows/game.svg");
        app.iconSize = 128;
        app.source = QStringLiteral("windows");
        app.windowsState = QStringLiteral("blocked");
        app.windowsAntiCheat = !t.anticheat.isEmpty() && t.anticheat != QLatin1String("none");
        if (!app.summary.isEmpty())
            out.apps.append(app);
    }
    return out;
}
