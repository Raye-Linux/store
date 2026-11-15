// Start menu entries and icons for the programs in a space: written after an
// installer ran ("raye-windows scan"), removed when their shortcut is gone.
#pragma once

#include "lnk.h"
#include "pe.h"
#include "spaces.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace RayeWindows {

struct EntryPaths {
    QString applications;  // ~/.local/share/applications
    QString icons;         // ~/.local/share/icons/hicolor
    QString desktop;       // the user's desktop folder
    QString cache;         // ~/.cache/raye-windows
    QString runner = QStringLiteral("raye-run-windows");
};
EntryPaths defaultEntryPaths();

struct Entry {
    QString id;            // raye-win-<space>-<name>
    QString name;
    QString target;        // C:\...
    QString link;          // the shortcut, relative to the space
    QString desktopFile;
    QString icon;          // a PNG, empty when the program has none
    bool onDesktop = false;
};

struct ScanResult {
    QList<Entry> added;
    QList<Entry> kept;
    QList<Entry> removed;
};

// reads the space's Start menu and desktop shortcuts and brings the entries
// in line with them
ScanResult scanSpace(const Space &space, const EntryPaths &paths);
// what scanSpace wrote for a space, read back from the .desktop files
QList<Entry> spaceEntries(const QString &slug, const EntryPaths &paths);
// removes every entry, launcher and icon of a space
// the hidden desktop file raye-run-windows writes for a space
QString runnerAppId(const QString &slug);
void removeSpaceEntries(const QString &slug, const EntryPaths &paths);

// one word of an Exec= line, quoted and escaped as desktop files want
QString desktopExecArg(const QString &arg);
// a menu category from the shortcut's folder and name
QString guessCategories(const QString &folderAndName);
// the largest PNG written for an entry id (64 px when there is one)
QString entryIconPath(const QString &id, const EntryPaths &paths, int preferred = 64);
// writes the icon sizes Raye uses (256, 64, 48, 32) under hicolor
bool writeIcons(const QList<QImage> &images, const QString &name, const QString &iconsDir);

// A program installed in a space, as Settings lists it
struct Program {
    QString space;
    UninstallEntry entry;
    qint64 size = -1;          // bytes on disk, -1 when unknown
    QString icon;              // a PNG
    QStringList desktopFiles;  // its Start menu entries
};
// every program of every space (or of one), by name
QList<Program> listPrograms(const QString &root, const EntryPaths &paths, const QString &onlySpace = {});

enum class Uninstall {
    Removed,          // the program is gone, its entries too
    SpaceRemoved,     // and it was the last one: the space is deleted
    NotFound,
    NotRemovable,     // the program says it cannot be removed (NoRemove)
    NoCommand,
    StillInstalled,   // its uninstaller ran but the program is still there
    RunnerFailed,
};
// runs the program's uninstaller through the runner (which shows the
// "Removing" window), then tidies up after it
Uninstall uninstallProgram(const QString &root, const QString &slug, const QString &key,
                           const EntryPaths &paths, QString *name = nullptr);

// A program's name for people: ProductName, else FileDescription, else the
// file name without "setup", versions and the like ("vlc-3.0.21-win64.exe"
// -> "VLC")
QString programName(const PeVersion &version, const QString &fileName);
QString nameFromFileName(const QString &fileName);

} // namespace RayeWindows
