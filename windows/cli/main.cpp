// raye-windows: the Windows programs in the user's spaces, for the runner,
// Settings and the Start menu.
//
//   raye-windows scan <space> [--json]       Start menu entries after an installer
//   raye-windows list [<space>] [--json]     every installed program
//   raye-windows uninstall <space> <key>     remove one, with the Removing window
//   raye-windows info <file.exe> [--icon-size N]
//                                            name, publisher, icon (JSON)
//
// RAYE_WINDOWS_SPACES and RAYE_RUN_WINDOWS replace the spaces folder and the
// runner (used by the tests).
#include "entries.h"
#include "pe.h"
#include "spaces.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

#include <cstdio>

using namespace RayeWindows;

namespace {

enum Exit { Ok = 0, Failed = 1, Usage = 2, NotFound = 3, StillThere = 4, CannotRemove = 5 };

void out(const QString &text)
{
    QTextStream(stdout) << text << '\n';
}

void err(const QString &text)
{
    QTextStream(stderr) << "raye-windows: " << text << '\n';
}

void printJson(const QJsonValue &v)
{
    const QJsonDocument doc = v.isArray() ? QJsonDocument(v.toArray()) : QJsonDocument(v.toObject());
    std::fputs(doc.toJson(QJsonDocument::Indented).constData(), stdout);
}

int usage()
{
    err(QStringLiteral("usage: raye-windows scan <space> [--json] | list [<space>] [--json] | "
                       "uninstall <space> <key> [--json] | info <file> [--icon-size N]"));
    return Usage;
}

QJsonValue orNull(const QString &s)
{
    return s.isEmpty() ? QJsonValue() : QJsonValue(s);
}

QJsonObject entryJson(const Entry &e)
{
    return {{QStringLiteral("id"), e.id}, {QStringLiteral("name"), e.name},
            {QStringLiteral("target"), e.target}, {QStringLiteral("desktopFile"), orNull(e.desktopFile)},
            {QStringLiteral("icon"), orNull(e.icon)}};
}

int scan(const QString &root, const EntryPaths &paths, const QString &slug, bool json)
{
    Space space;
    if (!findSpace(root, slug, &space)) {
        err(QStringLiteral("no space named \"%1\"").arg(slug));
        return NotFound;
    }
    const ScanResult r = scanSpace(space, paths);
    if (json) {
        QJsonArray added, kept, removed;
        for (const Entry &e : r.added)
            added.append(entryJson(e));
        for (const Entry &e : r.kept)
            kept.append(entryJson(e));
        for (const Entry &e : r.removed)
            removed.append(entryJson(e));
        printJson(QJsonObject{{QStringLiteral("space"), slug}, {QStringLiteral("added"), added},
                              {QStringLiteral("kept"), kept}, {QStringLiteral("removed"), removed}});
    } else {
        for (const Entry &e : r.added)
            out(QStringLiteral("added: %1").arg(e.name));
        for (const Entry &e : r.removed)
            out(QStringLiteral("removed: %1").arg(e.name));
        out(QStringLiteral("%1 entries in %2").arg(r.added.size() + r.kept.size()).arg(slug));
    }
    return Ok;
}

int list(const QString &root, const EntryPaths &paths, const QString &slug, bool json)
{
    if (!slug.isEmpty() && !isValidSlug(slug)) {
        err(QStringLiteral("no space named \"%1\"").arg(slug));
        return NotFound;
    }
    const QList<Program> programs = listPrograms(root, paths, slug);
    if (json) {
        QJsonArray array;
        for (const Program &p : programs) {
            array.append(QJsonObject{
                {QStringLiteral("space"), p.space}, {QStringLiteral("key"), p.entry.key},
                {QStringLiteral("view"), p.entry.view}, {QStringLiteral("name"), p.entry.name},
                {QStringLiteral("publisher"), orNull(p.entry.publisher)},
                {QStringLiteral("version"), orNull(p.entry.version)},
                {QStringLiteral("size"), p.size >= 0 ? QJsonValue(double(p.size)) : QJsonValue()},
                {QStringLiteral("installDate"), orNull(p.entry.installDate)},
                {QStringLiteral("icon"), orNull(p.icon)},
                {QStringLiteral("removable"), !p.entry.noRemove},
                {QStringLiteral("desktopFiles"), QJsonArray::fromStringList(p.desktopFiles)}});
        }
        printJson(array);
        return Ok;
    }
    for (const Program &p : programs) {
        out(QStringLiteral("%1\t%2\t%3\t%4").arg(p.entry.name, p.entry.version, p.space, p.entry.key));
    }
    return Ok;
}

int uninstall(const QString &root, const EntryPaths &paths, const QString &slug, const QString &key, bool json)
{
    QString name;
    const Uninstall r = uninstallProgram(root, slug, key, paths, &name);
    struct { Uninstall r; const char *word; int code; const char *message; } const table[] = {
        {Uninstall::Removed, "removed", Ok, "%1 was uninstalled"},
        {Uninstall::SpaceRemoved, "removed", Ok, "%1 was uninstalled"},
        {Uninstall::NotFound, "not-found", NotFound, "no such program"},
        {Uninstall::NotRemovable, "not-removable", CannotRemove, "%1 cannot be uninstalled"},
        {Uninstall::NoCommand, "no-uninstaller", CannotRemove, "%1 has no uninstaller"},
        {Uninstall::StillInstalled, "still-installed", StillThere, "%1 is still installed"},
        {Uninstall::RunnerFailed, "failed", Failed, "could not start the uninstaller"},
    };
    for (const auto &row : table) {
        if (row.r != r)
            continue;
        if (json) {
            printJson(QJsonObject{{QStringLiteral("result"), QLatin1String(row.word)},
                                  {QStringLiteral("name"), orNull(name)},
                                  {QStringLiteral("spaceRemoved"), r == Uninstall::SpaceRemoved}});
        } else if (row.code == Ok) {
            out(QString::fromLatin1(row.message).arg(name));
        } else {
            err(QString::fromLatin1(row.message).arg(name));
        }
        return row.code;
    }
    return Failed;
}

int info(const EntryPaths &paths, const QString &file, int iconSize)
{
    const QFileInfo fi(file);
    if (!fi.isFile()) {
        err(QStringLiteral("no file \"%1\"").arg(file));
        return NotFound;
    }
    PeFile pe;
    const bool program = pe.load(fi.absoluteFilePath());
    const PeVersion v = program ? pe.version() : PeVersion();
    QString icon;
    if (program && !paths.cache.isEmpty()) {
        const QImage img = pickIcon(pe.icons(0), iconSize);
        if (!img.isNull()) {
            const QByteArray stamp = QFile::encodeName(fi.absoluteFilePath()) + '\n'
                + QByteArray::number(fi.size()) + '\n' + QByteArray::number(fi.lastModified().toMSecsSinceEpoch());
            const QString hash = QString::fromLatin1(QCryptographicHash::hash(stamp, QCryptographicHash::Sha1).toHex().left(16));
            icon = QStringLiteral("%1/icons/file-%2-%3.png").arg(paths.cache, hash).arg(iconSize);
            if (!QFileInfo::exists(icon)) {
                QDir().mkpath(QFileInfo(icon).absolutePath());
                QSaveFile f(icon);
                if (!(f.open(QIODevice::WriteOnly) && img.save(&f, "PNG") && f.commit()))
                    icon.clear();
            }
        }
    }
    const QString signer = program ? pe.signerName() : QString();
    // a name in a signature proves nothing until the signature is checked
    const bool verified = !signer.isEmpty() && signatureVerified(fi.absoluteFilePath());
    printJson(QJsonObject{
        {QStringLiteral("file"), fi.absoluteFilePath()},
        {QStringLiteral("program"), program},
        {QStringLiteral("is64"), program && pe.is64()},
        {QStringLiteral("name"), programName(v, fi.fileName())},
        {QStringLiteral("productName"), orNull(v.productName)},
        {QStringLiteral("description"), orNull(v.fileDescription)},
        {QStringLiteral("company"), orNull(v.companyName)},
        {QStringLiteral("version"), orNull(v.productVersion.isEmpty() ? v.fileVersion : v.productVersion)},
        {QStringLiteral("publisher"), verified ? QJsonValue(signer) : QJsonValue()},
        {QStringLiteral("signer"), orNull(signer)},
        {QStringLiteral("verified"), verified},
        {QStringLiteral("icon"), orNull(icon)}});
    return Ok;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("raye-windows"));
    QStringList args = app.arguments().mid(1);
    const bool json = args.removeAll(QStringLiteral("--json")) > 0;
    int iconSize = 64;
    const qsizetype sizeAt = args.indexOf(QStringLiteral("--icon-size"));
    if (sizeAt >= 0) {
        bool ok = false;
        iconSize = args.value(sizeAt + 1).toInt(&ok);
        if (!ok || iconSize < 16 || iconSize > 256)
            return usage();
        args.remove(sizeAt, 2);
    }
    if (args.isEmpty())
        return usage();

    const QString root = qEnvironmentVariableIsEmpty("RAYE_WINDOWS_SPACES")
        ? defaultSpacesRoot() : qEnvironmentVariable("RAYE_WINDOWS_SPACES");
    EntryPaths paths = defaultEntryPaths();
    if (!qEnvironmentVariableIsEmpty("RAYE_RUN_WINDOWS"))
        paths.runner = qEnvironmentVariable("RAYE_RUN_WINDOWS");

    const QString command = args.takeFirst();
    if (command == QLatin1String("scan") && args.size() == 1)
        return scan(root, paths, args.at(0), json);
    if (command == QLatin1String("list") && args.size() <= 1)
        return list(root, paths, args.value(0), json);
    if (command == QLatin1String("uninstall") && args.size() == 2)
        return uninstall(root, paths, args.at(0), args.at(1), json);
    if (command == QLatin1String("info") && args.size() == 1)
        return info(paths, args.at(0), iconSize);
    return usage();
}
