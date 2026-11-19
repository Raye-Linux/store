#include "entries.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace RayeWindows {

namespace {

constexpr int kIconSizes[] = {256, 64, 48, 32};
constexpr int kMaxLinks = 500;
constexpr qint64 kMaxDesktopFile = 64 * 1024;
const QString kState = QStringLiteral("/.raye-entries.json");
const QString kFallbackIcon = QStringLiteral("raye-windows-program");

QString slugify(const QString &text)
{
    QString plain = text.toLower();
    // letters that do not come apart into a letter and an accent
    static const QList<std::pair<QString, QString>> letters = {
        {QStringLiteral("ø"), QStringLiteral("o")}, {QStringLiteral("æ"), QStringLiteral("ae")},
        {QStringLiteral("œ"), QStringLiteral("oe")}, {QStringLiteral("ß"), QStringLiteral("ss")},
        {QStringLiteral("ł"), QStringLiteral("l")}, {QStringLiteral("đ"), QStringLiteral("d")},
        {QStringLiteral("þ"), QStringLiteral("th")}, {QStringLiteral("ð"), QStringLiteral("d")},
    };
    for (const auto &l : letters)
        plain.replace(l.first, l.second);
    const QString decomposed = plain.normalized(QString::NormalizationForm_KD);
    QString out;
    for (const QChar c : decomposed) {
        if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9')))
            out.append(c);
        else if (c == QLatin1Char('+'))
            out.append(QStringLiteral("plus"));
        else if (c.category() != QChar::Mark_NonSpacing && !out.endsWith(QLatin1Char('-')))
            out.append(QLatin1Char('-'));
    }
    while (out.startsWith(QLatin1Char('-')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out.left(48).isEmpty() ? QStringLiteral("app") : out.left(48);
}

// desktop files: strings escape \ and line breaks, nothing else
QString escapeValue(const QString &v)
{
    QString out;
    for (const QChar c : v) {
        if (c == QLatin1Char('\\'))
            out += QStringLiteral("\\\\");
        else if (c == QLatin1Char('\n'))
            out += QStringLiteral("\\n");
        else if (c == QLatin1Char('\t'))
            out += QStringLiteral("\\t");
        else if (c == QLatin1Char('\r'))
            out += QStringLiteral("\\r");
        else if (c.unicode() >= 32)
            out += c;
    }
    return out;
}

QString unescapeValue(const QString &v)
{
    QString out;
    for (int i = 0; i < v.size(); ++i) {
        if (v.at(i) != QLatin1Char('\\') || i + 1 >= v.size()) {
            out += v.at(i);
            continue;
        }
        const QChar e = v.at(++i);
        out += e == QLatin1Char('n') ? QChar(QLatin1Char('\n')) : e == QLatin1Char('t') ? QChar(QLatin1Char('\t'))
            : e == QLatin1Char('r') ? QChar(QLatin1Char('\r')) : e == QLatin1Char('s') ? QChar(QLatin1Char(' ')) : e;
    }
    return out;
}

QHash<QString, QString> readDesktopFile(const QString &path)
{
    QHash<QString, QString> keys;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxDesktopFile)
        return keys;
    bool inEntry = false;
    for (const QByteArray &raw : f.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (inEntry && eq > 0)
            keys.insert(line.left(eq).trimmed(), unescapeValue(line.mid(eq + 1).trimmed()));
    }
    return keys;
}

bool writeFile(const QString &path, const QByteArray &content, QFileDevice::Permissions perms)
{
    // the mode bits only (Qt adds what the current user may do)
    const QFileDevice::Permissions mode = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
        | QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.size() <= kMaxDesktopFile && existing.readAll() == content
        && (existing.permissions() & mode) == (perms & mode))
        return true;
    existing.close();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(content);
    f.setPermissions(perms);
    return f.commit();
}

// our files in a folder: raye-win-<slug>-*.desktop that say so inside
QStringList ownFiles(const QString &dir, const QString &slug)
{
    QStringList out;
    if (dir.isEmpty())
        return out;
    const QString prefix = QStringLiteral("raye-win-") + slug + QLatin1Char('-');
    const QStringList names = QDir(dir).entryList({prefix + QStringLiteral("*.desktop")}, QDir::Files | QDir::Hidden, QDir::Name);
    for (const QString &n : names) {
        const QString path = dir + QLatin1Char('/') + n;
        if (!QFileInfo(path).isSymLink() && readDesktopFile(path).value(QStringLiteral("X-Raye-Windows-Space")) == slug)
            out.append(path);
    }
    return out;
}

void removeIcons(const QString &id, const QString &iconsDir)
{
    for (int s : kIconSizes)
        QFile::remove(QStringLiteral("%1/%2x%2/apps/%3.png").arg(iconsDir).arg(s).arg(id));
    // the Store's own picture for a program with only small icons
    QFile::remove(QStringLiteral("%1/128x128/apps/%2.png").arg(iconsDir, id));
}

} // namespace

QString guessCategories(const QString &folderAndName)
{
    const QString t = folderAndName.toLower();
    static const struct { const char *pattern; const char *categories; } table[] = {
        // music software before the rest: "Image-Line" would read as graphics
        {"\\b(fl studio|image-line|ableton|cubase|reaper|bitwig|studio one|daw)\\b", "AudioVideo;Audio;"},
        {"\\b(live studio|streamlabs|obs studio)\\b", "AudioVideo;Video;"},
        {"\\b(games?|steam|epic|gog|ubisoft|battle\\.?net|riot|launcher|play)\\b", "Game;"},
        {"\\b(office|word|excel|powerpoint|outlook|writer|calc)\\b", "Office;"},
        {"\\b(affinity|photo|paint|image|draw|design|graphics?|3d|cad)\\b", "Graphics;"},
        {"\\b(music|audio|video|player|media|sound|radio|dj)\\b", "AudioVideo;"},
        {"\\b(code|ide|sdk|developer|compiler|git|editor)\\b", "Development;"},
        {"\\b(chat|social|mail|messenger|browser|web|internet|ftp|torrent)\\b", "Network;"},
    };
    for (const auto &row : table) {
        if (QRegularExpression(QString::fromLatin1(row.pattern)).match(t).hasMatch())
            return QString::fromLatin1(row.categories);
    }
    return QStringLiteral("Utility;");
}

namespace {

bool isUninstaller(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral("(^|[^a-z])(un-?inst(all)?(er)?|unins\\d*|uninstaller|d[ée]sinstall\\w*|remove)([^a-z]|$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

struct Link {
    QString rel;      // relative to the space
    QString path;
    bool desktop = false;
};

QList<Link> findLinks(const Space &space)
{
    QList<Link> out;
    QStringList menus = {QStringLiteral("C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs")};
    QStringList desktops;
    const QStringList users = QDir(space.path + QStringLiteral("/drive_c/users"))
        .entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    for (const QString &u : users) {
        menus.append(QStringLiteral("C:\\users\\%1\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs").arg(u));
        desktops.append(QStringLiteral("C:\\users\\%1\\Desktop").arg(u));
    }
    const QDir spaceDir(space.path);
    auto walk = [&](const QString &winDir, bool desktop) {
        const QString dir = toUnixPath(space, winDir);
        // a desktop folder that is the user's own desktop is not the space's
        if (dir.isEmpty() || QFileInfo(dir).isSymLink() || !QFileInfo(dir).isDir())
            return;
        QDirIterator it(dir, {QStringLiteral("*.lnk"), QStringLiteral("*.LNK"), QStringLiteral("*.Lnk")},
                        QDir::Files | QDir::NoSymLinks, desktop ? QDirIterator::NoIteratorFlags : QDirIterator::Subdirectories);
        while (it.hasNext() && out.size() < kMaxLinks) {
            const QString path = it.next();
            out.append({spaceDir.relativeFilePath(path), path, desktop});
        }
    };
    for (const QString &m : menus)
        walk(m, false);
    for (const QString &d : desktops)
        walk(d, true);
    std::stable_sort(out.begin(), out.end(), [](const Link &a, const Link &b) {
        return a.desktop != b.desktop ? !a.desktop : a.rel < b.rel;
    });
    return out;
}

QStringList readState(const Space &space)
{
    QFile f(space.path + kState);
    QStringList made;
    if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
        return made;
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("desktopMade")).toArray()) {
        if (v.isString())
            made.append(v.toString());
    }
    return made;
}

void writeState(const Space &space, const QStringList &made)
{
    QJsonObject o;
    o.insert(QStringLiteral("desktopMade"), QJsonArray::fromStringList(made));
    writeFile(space.path + kState, QJsonDocument(o).toJson(QJsonDocument::Compact), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

} // namespace

EntryPaths defaultEntryPaths()
{
    EntryPaths p;
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    p.applications = data + QStringLiteral("/applications");
    p.icons = data + QStringLiteral("/icons/hicolor");
    p.desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    p.cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/raye-windows");
    return p;
}

QString desktopExecArg(const QString &arg)
{
    static const QString reserved = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");
    bool quote = arg.isEmpty();
    for (const QChar c : arg)
        quote = quote || reserved.contains(c);
    QString word;
    if (!quote) {
        word = arg;
    } else {
        word = QStringLiteral("\"");
        for (const QChar c : arg) {
            if (c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('$') || c == QLatin1Char('\\'))
                word += QLatin1Char('\\');
            if (c == QLatin1Char('\n'))
                continue;
            word += c;
        }
        word += QLatin1Char('"');
    }
    word.replace(QLatin1Char('%'), QStringLiteral("%%")); // not a field code
    return escapeValue(word);
}

bool writeIcons(const QList<QImage> &images, const QString &name, const QString &iconsDir)
{
    int biggest = 0;
    for (const QImage &img : images)
        biggest = qMax(biggest, img.width());
    if (biggest == 0)
        return false;
    bool any = false;
    for (int s : kIconSizes) {
        const QString path = QStringLiteral("%1/%2x%2/apps/%3.png").arg(iconsDir).arg(s).arg(name);
        if (s > biggest && s != 32) {
            QFile::remove(path); // no blurry blow-ups
            continue;
        }
        const QImage img = pickIcon(images, s);
        if (img.isNull())
            continue;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile f(path);
        if (f.open(QIODevice::WriteOnly) && img.save(&f, "PNG") && f.commit())
            any = true;
    }
    return any;
}

QString entryIconPath(const QString &id, const EntryPaths &paths, int preferred)
{
    QList<int> order = {preferred};
    for (int s : kIconSizes) {
        if (s != preferred)
            order.append(s);
    }
    for (int s : order) {
        const QString p = QStringLiteral("%1/%2x%2/apps/%3.png").arg(paths.icons).arg(s).arg(id);
        if (QFileInfo::exists(p))
            return p;
    }
    return {};
}

ScanResult scanSpace(const Space &space, const EntryPaths &paths)
{
    ScanResult result;
    QHash<QString, Entry> before;
    for (const Entry &e : spaceEntries(space.slug, paths))
        before.insert(e.id, e);

    QStringList made = readState(space);
    QStringList madeNow;
    QSet<QString> usedIds, menuTargets;
    QHash<QString, QString> idForTarget;
    QSet<QString> wanted, wantedDesktop;

    for (const Link &link : findLinks(space)) {
        const Lnk lnk = readLnkFile(link.path);
        if (!lnk.valid)
            continue;
        const QString name = QFileInfo(link.path).completeBaseName().trimmed();
        const QString target = expandWindowsVars(lnk.target, windowsUser(space));
        const QString targetName = target.section(QLatin1Char('\\'), -1);
        if (target.contains(QLatin1String("://")) || !targetName.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)
            || isUninstaller(name) || isUninstaller(targetName))
            continue; // web links, readmes, help, uninstallers
        const QString unixTarget = toUnixPath(space, target);
        if (unixTarget.isEmpty() || !QFileInfo(unixTarget).isFile())
            continue;
        const QString sameKey = target.toLower() + QLatin1Char('\n') + lnk.arguments;
        if (!link.desktop && menuTargets.contains(sameKey))
            continue; // the same shortcut for everyone and for the user
        if (!link.desktop)
            menuTargets.insert(sameKey);

        QString id = idForTarget.value(sameKey);
        if (id.isEmpty()) {
            const QString base = QStringLiteral("raye-win-%1-%2").arg(space.slug, slugify(name));
            id = base;
            for (int n = 2; usedIds.contains(id); ++n)
                id = base + QLatin1Char('-') + QString::number(n);
            usedIds.insert(id);
            idForTarget.insert(sameKey, id);

            QList<QImage> images;
            if (!lnk.iconLocation.isEmpty()) {
                const QString iconFile = toUnixPath(space, lnk.iconLocation);
                if (!iconFile.isEmpty())
                    images = iconsFromFile(iconFile, lnk.iconIndex);
            }
            if (images.isEmpty())
                images = iconsFromFile(unixTarget, 0);
            if (!writeIcons(images, id, paths.icons))
                removeIcons(id, paths.icons);
        }
        const bool hasIcon = !entryIconPath(id, paths).isEmpty();

        QStringList exec = {paths.runner, QStringLiteral("--space"), space.slug, QStringLiteral("--run"), target};
        if (!lnk.arguments.trimmed().isEmpty())
            exec << QStringLiteral("--args") << lnk.arguments.trimmed();
        QStringList words;
        for (const QString &w : exec)
            words.append(desktopExecArg(w));
        QString content = QStringLiteral("[Desktop Entry]\nType=Application\n");
        content += QStringLiteral("Name=") + escapeValue(name) + QLatin1Char('\n');
        if (!lnk.description.trimmed().isEmpty())
            content += QStringLiteral("Comment=") + escapeValue(lnk.description.trimmed()) + QLatin1Char('\n');
        content += QStringLiteral("Exec=") + words.join(QLatin1Char(' ')) + QLatin1Char('\n');
        const QString workDir = lnk.workingDir.isEmpty() ? QString() : toUnixPath(space, lnk.workingDir);
        if (!workDir.isEmpty() && QFileInfo(workDir).isDir())
            content += QStringLiteral("Path=") + escapeValue(workDir) + QLatin1Char('\n');
        content += QStringLiteral("Icon=") + (hasIcon ? id : kFallbackIcon) + QLatin1Char('\n');
        // the taskbar matches the window's class, the .exe name in lower case
        content += QStringLiteral("StartupWMClass=") + escapeValue(targetName.toLower()) + QLatin1Char('\n');
        // from the folder in Start (Programs\Games\...) and the name
        const int programs = link.rel.indexOf(QLatin1String("/Programs/"), 0, Qt::CaseInsensitive);
        const QString folder = programs >= 0 ? link.rel.mid(programs + 10) : QString();
        content += QStringLiteral("Categories=") + guessCategories(folder.section(QLatin1Char('/'), 0, -2) + QLatin1Char(' ') + name)
            + QLatin1Char('\n');
        content += QStringLiteral("X-Raye-Windows-Space=") + space.slug + QLatin1Char('\n');
        content += QStringLiteral("X-Raye-Windows-Target=") + escapeValue(target) + QLatin1Char('\n');
        content += QStringLiteral("X-Raye-Windows-Link=") + escapeValue(link.rel) + QLatin1Char('\n');

        Entry entry;
        entry.id = id;
        entry.name = name;
        entry.target = target;
        entry.link = link.rel;
        entry.icon = hasIcon ? entryIconPath(id, paths) : QString();

        if (link.desktop) {
            // a launcher on the desktop once, only because the installer made
            // a shortcut there; if it is deleted, it stays deleted
            madeNow.append(link.rel);
            if (paths.desktop.isEmpty())
                continue;
            const QString file = paths.desktop + QLatin1Char('/') + id + QStringLiteral(".desktop");
            wantedDesktop.insert(file);
            // kept up to date while it is there
            if (!made.contains(link.rel) || QFileInfo::exists(file))
                writeFile(file, content.toUtf8(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                          | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther);
            continue;
        }
        entry.desktopFile = paths.applications + QLatin1Char('/') + id + QStringLiteral(".desktop");
        if (!writeFile(entry.desktopFile, content.toUtf8(),
                       QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther))
            continue;
        wanted.insert(id);
        (before.contains(id) ? result.kept : result.added).append(entry);
    }

    // entries whose shortcut is gone: uninstalled
    for (const Entry &e : std::as_const(before)) {
        if (wanted.contains(e.id))
            continue;
        QFile::remove(e.desktopFile);
        if (!idForTarget.values().contains(e.id))
            removeIcons(e.id, paths.icons);
        result.removed.append(e);
    }
    for (const QString &file : ownFiles(paths.desktop, space.slug)) {
        const QString link = readDesktopFile(file).value(QStringLiteral("X-Raye-Windows-Link"));
        if (madeNow.contains(link) && wantedDesktop.contains(file))
            continue;
        const QString id = QFileInfo(file).completeBaseName();
        QFile::remove(file);
        if (!idForTarget.values().contains(id))
            removeIcons(id, paths.icons);
    }
    // forget launchers whose shortcut is gone, so a reinstall makes them again
    QStringList keep;
    for (const QString &rel : std::as_const(madeNow)) {
        if (!keep.contains(rel))
            keep.append(rel);
    }
    if (keep != made)
        writeState(space, keep);
    return result;
}

QList<Entry> spaceEntries(const QString &slug, const EntryPaths &paths)
{
    QList<Entry> out;
    if (!isValidSlug(slug))
        return out;
    for (const QString &file : ownFiles(paths.applications, slug)) {
        const QHash<QString, QString> keys = readDesktopFile(file);
        Entry e;
        e.id = QFileInfo(file).completeBaseName();
        e.name = keys.value(QStringLiteral("Name"));
        e.target = keys.value(QStringLiteral("X-Raye-Windows-Target"));
        e.link = keys.value(QStringLiteral("X-Raye-Windows-Link"));
        e.desktopFile = file;
        e.icon = entryIconPath(e.id, paths);
        out.append(e);
    }
    return out;
}

QString runnerAppId(const QString &slug)
{
    // what raye-run-windows names a space for the share picker: anything
    // but a-z and 0-9 becomes "_", and a part cannot start with a digit
    QString id;
    for (const QChar c : slug)
        id += (c >= QLatin1Char('a') && c <= QLatin1Char('z')) || c.isDigit() ? c : QLatin1Char('_');
    if (!id.isEmpty() && id.at(0).isDigit())
        id.prepend(QLatin1Char('_'));
    return QStringLiteral("org.raye.windows.") + id;
}

void removeSpaceEntries(const QString &slug, const EntryPaths &paths)
{
    if (!isValidSlug(slug))
        return;
    QFile::remove(paths.applications + QLatin1Char('/') + runnerAppId(slug) + QStringLiteral(".desktop"));
    for (const QString &file : ownFiles(paths.applications, slug)) {
        removeIcons(QFileInfo(file).completeBaseName(), paths.icons);
        QFile::remove(file);
    }
    for (const QString &file : ownFiles(paths.desktop, slug)) {
        removeIcons(QFileInfo(file).completeBaseName(), paths.icons);
        QFile::remove(file);
    }
}

namespace {

// "C:\x\app.exe,1" or "\"C:\x\app.ico\"" -> file and index
QString iconFileOf(const QString &displayIcon, int *index)
{
    QString s = displayIcon.trimmed();
    *index = 0;
    static const QRegularExpression withIndex(QStringLiteral("^(.*),\\s*(-?\\d+)$"));
    const QRegularExpressionMatch m = withIndex.match(s);
    if (m.hasMatch()) {
        s = m.captured(1).trimmed();
        *index = m.captured(2).toInt();
    }
    if (s.size() >= 2 && s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')))
        s = s.mid(1, s.size() - 2);
    return s;
}

QString folderOf(const QString &windowsPath)
{
    QString p = windowsPath.toLower();
    while (p.endsWith(QLatin1Char('\\')))
        p.chop(1);
    return p;
}

bool under(const QString &target, const QString &folder)
{
    return !folder.isEmpty() && target.toLower().startsWith(folder + QLatin1Char('\\'));
}

} // namespace

QList<Program> listPrograms(const QString &root, const EntryPaths &paths, const QString &onlySpace)
{
    QList<Program> out;
    for (const Space &space : listSpaces(root)) {
        if (!onlySpace.isEmpty() && space.slug != onlySpace)
            continue;
        const QList<UninstallEntry> programs = spacePrograms(space, root);
        const QList<Entry> entries = spaceEntries(space.slug, paths);
        const QString user = windowsUser(space);
        for (const UninstallEntry &e : programs) {
            Program p;
            p.space = space.slug;
            p.entry = e;
            int iconIndex = 0;
            const QString iconWin = expandWindowsVars(iconFileOf(e.displayIcon, &iconIndex), user);
            QString folder = folderOf(expandWindowsVars(e.installLocation, user));
            if (folder.isEmpty() && !iconWin.isEmpty())
                folder = folderOf(iconWin.section(QLatin1Char('\\'), 0, -2));
            if (folder.isEmpty())
                folder = folderOf(expandWindowsVars(commandProgram(e.uninstallString), user).section(QLatin1Char('\\'), 0, -2));
            for (const Entry &en : entries) {
                if (programs.size() == 1 || under(en.target, folder))
                    p.desktopFiles.append(en.desktopFile);
                if (p.icon.isEmpty() && (programs.size() == 1 || under(en.target, folder)))
                    p.icon = en.icon;
            }
            if (p.icon.isEmpty() && !iconWin.isEmpty() && !paths.cache.isEmpty()) {
                const QString hash = QString::fromLatin1(QCryptographicHash::hash(
                    (e.view + QLatin1Char('\n') + e.key).toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
                const QString cached = QStringLiteral("%1/icons/%2-%3.png").arg(paths.cache, space.slug, hash);
                if (!QFileInfo::exists(cached)) {
                    const QString file = toUnixPath(space, iconWin);
                    const QImage img = file.isEmpty() ? QImage() : pickIcon(iconsFromFile(file, iconIndex), 64);
                    if (!img.isNull()) {
                        QDir().mkpath(QFileInfo(cached).absolutePath());
                        QSaveFile f(cached);
                        if (f.open(QIODevice::WriteOnly) && img.save(&f, "PNG"))
                            f.commit();
                    }
                }
                if (QFileInfo::exists(cached))
                    p.icon = cached;
            }
            if (programs.size() == 1) {
                p.size = diskUsage(space.path); // removing it frees the whole space
            } else {
                const QString location = toUnixPath(space, expandWindowsVars(e.installLocation, user));
                if (!e.installLocation.isEmpty() && !location.isEmpty() && QFileInfo(location).isDir())
                    p.size = diskUsage(location);
                else if (e.estimatedSize > 0)
                    p.size = e.estimatedSize;
            }
            out.append(p);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Program &a, const Program &b) {
        return QString::localeAwareCompare(a.entry.name, b.entry.name) < 0;
    });
    return out;
}

Uninstall uninstallProgram(const QString &root, const QString &slug, const QString &key,
                           const EntryPaths &paths, QString *name)
{
    Space space;
    if (!findSpace(root, slug, &space))
        return Uninstall::NotFound;
    auto find = [&](UninstallEntry *found) {
        for (const UninstallEntry &e : spacePrograms(space, root)) {
            if (e.key.compare(key, Qt::CaseInsensitive) == 0) {
                *found = e;
                return true;
            }
        }
        return false;
    };
    UninstallEntry e;
    if (!find(&e))
        return Uninstall::NotFound;
    if (name)
        *name = e.name;
    if (e.noRemove)
        return Uninstall::NotRemovable;
    const QString command = uninstallCommand(e);
    if (command.isEmpty())
        return Uninstall::NoCommand;

    QStringList args = {QStringLiteral("--space"), slug, QStringLiteral("--uninstall"), command,
                        QStringLiteral("--name"), e.name};
    for (const Entry &en : spaceEntries(slug, paths)) {
        if (!en.icon.isEmpty()) {
            args << QStringLiteral("--icon") << en.icon;
            break;
        }
    }
    QProcess runner;
    runner.setProcessChannelMode(QProcess::ForwardedChannels);
    runner.setInputChannelMode(QProcess::ForwardedInputChannel);
    runner.start(paths.runner, args);
    if (!runner.waitForStarted(30000))
        return Uninstall::RunnerFailed;
    runner.waitForFinished(-1); // uninstallers take as long as they take
    if (runner.exitStatus() != QProcess::NormalExit)
        return Uninstall::RunnerFailed;

    UninstallEntry still;
    if (find(&still))
        return Uninstall::StillInstalled; // cancelled, or failed: nothing removed
    scanSpace(space, paths);
    if (spacePrograms(space, root).isEmpty() && spaceEntries(slug, paths).isEmpty()) {
        removeSpaceEntries(slug, paths);
        return removeSpaceFolder(root, slug) ? Uninstall::SpaceRemoved : Uninstall::Removed;
    }
    return Uninstall::Removed;
}

QString nameFromFileName(const QString &fileName)
{
    QString base = fileName.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1);
    static const QRegularExpression ext(QStringLiteral("\\.(exe|msi|lnk|com)$"), QRegularExpression::CaseInsensitiveOption);
    base.remove(ext);
    static const QRegularExpression split(QStringLiteral("[-_.\\s()\\[\\]]+"));
    static const QRegularExpression version(QStringLiteral("^(v|r|b|build)?\\d+[a-z]?$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression glued(QStringLiteral("^(.+?)(web)?(setup|installer|install)$"), QRegularExpression::CaseInsensitiveOption);
    static const QSet<QString> noise = {
        QStringLiteral("setup"), QStringLiteral("installer"), QStringLiteral("install"), QStringLiteral("inst"),
        QStringLiteral("win"), QStringLiteral("win32"), QStringLiteral("win64"), QStringLiteral("windows"),
        QStringLiteral("x64"), QStringLiteral("x86"), QStringLiteral("amd64"), QStringLiteral("arm64"),
        QStringLiteral("64bit"), QStringLiteral("32bit"), QStringLiteral("full"), QStringLiteral("offline"),
        QStringLiteral("online"), QStringLiteral("web"), QStringLiteral("latest"), QStringLiteral("release"),
        QStringLiteral("final"), QStringLiteral("stable"), QStringLiteral("en"), QStringLiteral("us"),
        QStringLiteral("multilingual"), QStringLiteral("signed"), QStringLiteral("portable"), QStringLiteral("x86_64"),
    };
    QStringList kept;
    for (QString t : base.split(split, Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch m = glued.match(t);
        if (m.hasMatch())
            t = m.captured(1); // "DiscordSetup" -> "Discord"
        if (noise.contains(t.toLower()) || version.match(t).hasMatch()) {
            if (!kept.isEmpty() && version.match(t).hasMatch())
                break; // everything after the version is noise too
            continue;
        }
        kept.append(t);
    }
    QString name = kept.join(QLatin1Char(' ')).trimmed();
    if (name.isEmpty())
        return base;
    if (name == name.toLower())
        name = name.size() <= 3 ? name.toUpper() : name.at(0).toUpper() + name.mid(1);
    return name;
}

QString programName(const PeVersion &version, const QString &fileName)
{
    static const QRegularExpression tail(QStringLiteral("\\s+(web\\s+)?(setup|installer|install|installation)$"),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QSet<QString> generic = {
        QStringLiteral("setup"), QStringLiteral("installer"), QStringLiteral("install"), QStringLiteral("setup/uninstall"),
        QStringLiteral("self-extracting archive"), QStringLiteral("self extracting archive"), QStringLiteral("application"),
    };
    for (QString candidate : {version.productName, version.fileDescription}) {
        candidate = candidate.simplified();
        candidate.remove(tail);
        if (!candidate.isEmpty() && !generic.contains(candidate.toLower()))
            return candidate;
    }
    return nameFromFileName(fileName);
}

} // namespace RayeWindows
