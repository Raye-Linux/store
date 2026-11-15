#include "spaces.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <filesystem>
#include <set>
#include <system_error>
#include <utility>

#include <sys/stat.h>

namespace RayeWindows {

namespace {

// how deep a size count goes; Windows programs nest, but not this far
constexpr int kMaxDepth = 64;

QString templateKey(const UninstallEntry &e)
{
    return e.view + QLatin1Char('\n') + e.key.toLower();
}

QList<UninstallEntry> entriesIn(const QString &dir)
{
    RegFile system, user;
    system.load(dir + QStringLiteral("/system.reg"));
    user.load(dir + QStringLiteral("/user.reg"));
    return uninstallEntries(system, user);
}

void addUsage(const std::filesystem::path &p, int depth, std::set<std::pair<dev_t, ino_t>> &seen, qint64 &total)
{
    struct stat st;
    if (::lstat(p.c_str(), &st) != 0)
        return;
    if (st.st_nlink > 1 && !S_ISDIR(st.st_mode) && !seen.insert({st.st_dev, st.st_ino}).second)
        return;
    total += qint64(st.st_blocks) * 512;
    if (!S_ISDIR(st.st_mode) || depth >= kMaxDepth)
        return;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec))
        addUsage(it->path(), depth + 1, seen, total);
}

} // namespace

QString defaultSpacesRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/raye/windows");
}

bool isValidSlug(const QString &slug)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9][a-z0-9-]{0,63}$"));
    return re.match(slug).hasMatch();
}

QList<Space> listSpaces(const QString &root)
{
    QList<Space> out;
    const QStringList names = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        if (!isValidSlug(name))
            continue; // .template, *.new and anything not made by the runner
        const QString path = root + QLatin1Char('/') + name;
        const QFileInfo fi(path);
        if (fi.isSymLink() || !QFileInfo(path + QStringLiteral("/drive_c")).isDir())
            continue;
        out.append({name, path});
    }
    return out;
}

bool findSpace(const QString &root, const QString &slug, Space *out)
{
    if (!isValidSlug(slug))
        return false;
    for (const Space &s : listSpaces(root)) {
        if (s.slug == slug) {
            *out = s;
            return true;
        }
    }
    return false;
}

QString windowsUser(const Space &space)
{
    const QStringList users = QDir(space.path + QStringLiteral("/drive_c/users"))
        .entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &u : users) {
        if (u.compare(QLatin1String("Public"), Qt::CaseInsensitive) != 0)
            return u;
    }
    return QStringLiteral("user");
}

QString expandWindowsVars(const QString &path, const QString &user)
{
    if (!path.contains(QLatin1Char('%')))
        return path;
    const QString home = QStringLiteral("C:\\users\\") + user;
    const QList<std::pair<QString, QString>> vars = {
        {QStringLiteral("programfiles(x86)"), QStringLiteral("C:\\Program Files (x86)")},
        {QStringLiteral("programfiles"), QStringLiteral("C:\\Program Files")},
        {QStringLiteral("programw6432"), QStringLiteral("C:\\Program Files")},
        {QStringLiteral("commonprogramfiles(x86)"), QStringLiteral("C:\\Program Files (x86)\\Common Files")},
        {QStringLiteral("commonprogramfiles"), QStringLiteral("C:\\Program Files\\Common Files")},
        {QStringLiteral("programdata"), QStringLiteral("C:\\ProgramData")},
        {QStringLiteral("allusersprofile"), QStringLiteral("C:\\ProgramData")},
        {QStringLiteral("systemroot"), QStringLiteral("C:\\windows")},
        {QStringLiteral("windir"), QStringLiteral("C:\\windows")},
        {QStringLiteral("systemdrive"), QStringLiteral("C:")},
        {QStringLiteral("public"), QStringLiteral("C:\\users\\Public")},
        {QStringLiteral("userprofile"), home},
        {QStringLiteral("appdata"), home + QStringLiteral("\\AppData\\Roaming")},
        {QStringLiteral("localappdata"), home + QStringLiteral("\\AppData\\Local")},
        {QStringLiteral("temp"), home + QStringLiteral("\\AppData\\Local\\Temp")},
        {QStringLiteral("tmp"), home + QStringLiteral("\\AppData\\Local\\Temp")},
    };
    QString out;
    int i = 0;
    while (i < path.size()) {
        const int start = path.indexOf(QLatin1Char('%'), i);
        const int end = start >= 0 ? path.indexOf(QLatin1Char('%'), start + 1) : -1;
        if (start < 0 || end < 0) {
            out += path.mid(i);
            break;
        }
        out += path.mid(i, start - i);
        const QString name = path.mid(start + 1, end - start - 1).toLower();
        QString value;
        for (const auto &v : vars) {
            if (v.first == name) {
                value = v.second;
                break;
            }
        }
        if (value.isNull()) {
            out += path.mid(start, end - start + 1); // unknown: left as written
        } else {
            out += value;
        }
        i = end + 1;
    }
    return out;
}

QString toUnixPath(const Space &space, const QString &windowsPath)
{
    QString p = expandWindowsVars(windowsPath.trimmed(), windowsUser(space));
    if (p.size() >= 2 && p.startsWith(QLatin1Char('"')) && p.endsWith(QLatin1Char('"')))
        p = p.mid(1, p.size() - 2);
    if (p.size() < 3 || p.at(1) != QLatin1Char(':') || p.at(0).toLower() != QLatin1Char('c')
        || (p.at(2) != QLatin1Char('\\') && p.at(2) != QLatin1Char('/')))
        return {};
    QString current = space.path + QStringLiteral("/drive_c");
    const QStringList parts = p.mid(3).split(QRegularExpression(QStringLiteral("[\\\\/]")), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String(".") || part == QLatin1String("..") || part.contains(QChar(0)))
            return {};
        const QString exact = current + QLatin1Char('/') + part;
        if (QFileInfo::exists(exact)) {
            current = exact;
            continue;
        }
        // Wine finds files whatever the case of the letters
        const QStringList names = QDir(current).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        QString match;
        for (const QString &n : names) {
            if (n.compare(part, Qt::CaseInsensitive) == 0) {
                match = n;
                break;
            }
        }
        if (match.isEmpty())
            return {};
        current += QLatin1Char('/') + match;
    }
    return current;
}

QString commandProgram(const QString &commandLine)
{
    const QString c = commandLine.trimmed();
    if (c.startsWith(QLatin1Char('"'))) {
        const int close = c.indexOf(QLatin1Char('"'), 1);
        return close > 0 ? c.mid(1, close - 1) : c.mid(1);
    }
    // unquoted: the shortest prefix ending in .exe, else the first word
    static const QRegularExpression exe(QStringLiteral("^(.*?\\.exe)(\\s|$|,)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = exe.match(c);
    if (m.hasMatch())
        return m.captured(1);
    return c.section(QLatin1Char(' '), 0, 0);
}

qint64 diskUsage(const QString &path)
{
    std::set<std::pair<dev_t, ino_t>> seen;
    qint64 total = 0;
    addUsage(std::filesystem::path(QFile::encodeName(path).toStdString()), 0, seen, total);
    return total;
}

QList<UninstallEntry> spacePrograms(const Space &space, const QString &root)
{
    QSet<QString> ready;
    for (const UninstallEntry &e : entriesIn(root + QStringLiteral("/.template")))
        ready.insert(templateKey(e));
    QList<UninstallEntry> out;
    for (const UninstallEntry &e : entriesIn(space.path)) {
        if (e.visible && !ready.contains(templateKey(e)))
            out.append(e);
    }
    return out;
}

bool removeSpaceFolder(const QString &root, const QString &slug)
{
    if (!isValidSlug(slug) || root.isEmpty())
        return false;
    const QString path = QDir(root).absolutePath() + QLatin1Char('/') + slug;
    const QFileInfo fi(path);
    if (!fi.exists() || fi.isSymLink() || !fi.isDir())
        return false;
    std::error_code ec;
    // remove_all removes links, never what they point to (dosdevices/z: is /)
    std::filesystem::remove_all(std::filesystem::path(QFile::encodeName(path).toStdString()), ec);
    return !ec && !QFileInfo::exists(path);
}

} // namespace RayeWindows
