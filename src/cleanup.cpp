// SPDX-License-Identifier: GPL-3.0-or-later
#include "cleanup.h"

#include <QDir>
#include <QFile>

#include <sys/stat.h>
#include <unistd.h>

namespace Cleanup
{

namespace
{
// mountinfo escapes spaces and the like as \040
QString unescapeMount(const QByteArray &field)
{
    QByteArray out;
    for (qsizetype i = 0; i < field.size(); ++i) {
        if (field.at(i) == '\\' && i + 3 < field.size()) {
            bool ok = false;
            const int c = field.mid(i + 1, 3).toInt(&ok, 8);
            if (ok) {
                out.append(char(c));
                i += 3;
                continue;
            }
        }
        out.append(field.at(i));
    }
    return QString::fromUtf8(out);
}
// what the link says, as it says it
QString linkTarget(const QString &link)
{
    char buffer[4096];
    const ssize_t n = ::readlink(QFile::encodeName(link).constData(), buffer, sizeof buffer);
    return n > 0 ? QFile::decodeName(QByteArray(buffer, int(n))) : QString();
}
} // namespace

QSet<QString> pathsInUse(const QString &proc)
{
    QSet<QString> busy;
    QFile mounts(proc + QStringLiteral("/self/mountinfo"));
    if (mounts.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = mounts.readAll().split('\n');
        for (const QByteArray &line : lines) {
            const QList<QByteArray> fields = line.split(' ');
            if (fields.size() > 4)
                busy.insert(unescapeMount(fields.at(4)));
        }
    }
    const QStringList pids = QDir(proc).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        bool number = false;
        pid.toInt(&number);
        if (!number)
            continue;
        const QString base = proc + QLatin1Char('/') + pid;
        const QString cwd = linkTarget(base + QStringLiteral("/cwd"));
        if (cwd.startsWith(QLatin1Char('/')))
            busy.insert(cwd);
        // other users' processes cannot be read: they cannot use our folder
        const QStringList fds = QDir(base + QStringLiteral("/fd")).entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
        for (const QString &fd : fds) {
            // "socket:[1234]" and the like are not paths
            const QString target = linkTarget(base + QStringLiteral("/fd/") + fd);
            if (target.startsWith(QLatin1Char('/')))
                busy.insert(target);
        }
    }
    return busy;
}

bool inUse(const QString &dir, const QSet<QString> &busy)
{
    const QString prefix = dir + QLatin1Char('/');
    for (const QString &path : busy) {
        if (path == dir || path.startsWith(prefix))
            return true;
    }
    return false;
}

QStringList staleFlatpakCaches(const QString &tmp, uint uid, const QSet<QString> &busy)
{
    QStringList stale;
    const QStringList names = QDir(tmp).entryList({QStringLiteral("flatpak-cache-*")},
                                                  QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    for (const QString &name : names) {
        const QString path = QDir(tmp).filePath(name);
        struct stat st;
        // lstat: a link named like a cache is never followed
        if (::lstat(QFile::encodeName(path).constData(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != uid)
            continue;
        if (inUse(path, busy))
            continue;
        stale.append(path);
    }
    return stale;
}

QStringList findFlatpakCaches(const QString &tmp)
{
    const QStringList candidates = staleFlatpakCaches(tmp, ::getuid(), {});
    if (candidates.isEmpty())
        return {};
    const QSet<QString> busy = pathsInUse();
    QStringList stale;
    for (const QString &path : candidates)
        if (!inUse(path, busy))
            stale.append(path);
    return stale;
}

int removeFolders(const QStringList &folders)
{
    int removed = 0;
    for (const QString &path : folders) {
        if (QDir(path).removeRecursively())
            ++removed;
        else
            qWarning("raye-store: could not remove %s", qPrintable(path));
    }
    return removed;
}

} // namespace Cleanup
