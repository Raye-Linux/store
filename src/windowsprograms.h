// SPDX-License-Identifier: GPL-3.0-or-later
// Windows programs and games in the Store. Three kinds of entry:
//   - a program Raye installs from its maker's own installer, checked
//     against the SHA-256 in windows-programs.json, in its own space;
//   - a program that has a native version: the Store points at that one;
//   - a game whose anti-cheat keeps it off this system, said before
//     anyone downloads forty gigabytes.
// What Raye checked lives with the Windows layer, not here: the titles
// (raye-windows-titles' files) and the profiles. A program is "optimized"
// only when its profile names a title marked works.
#pragma once

#include "catalog.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

struct WindowsTitle {
    QString id;
    QString name;
    QString state; // works, offline-only, blocked, untested
    QString anticheat;
    QString whyEn;
    QString whyFr;
};

// The *.titles files of these folders, read the way raye-windows-titles
// reads them: folders in order, files by name, the last one naming a
// title wins.
QHash<QString, WindowsTitle> readWindowsTitles(const QStringList &dirs);
// RAYE_WINDOWS_TITLES_PATH, or the shipped folder then the local one
QStringList windowsTitleDirs();

struct WindowsProfile {
    bool found = false;
    QString name;
    QString title;
    QString native;
    QStringList match;
};
WindowsProfile readWindowsProfile(const QString &path);
// RAYE_WINDOWS_PROFILES_PATH or /usr/share/raye/windows-profiles
QString windowsProfileDir();

struct WindowsListing {
    QList<App> apps;                   // programs to install, games that can't run
    QHash<QString, QString> natives;   // app key of the native app -> Windows name
    QStringList order;                 // ids as windows-programs.json lists them
};

// The listing from windows-programs.json (icons relative to iconDir).
// Programs to install only come with an installer runner on the PC.
WindowsListing readWindowsListing(const QByteArray &json, const QString &iconDir,
                                  const QHash<QString, WindowsTitle> &titles,
                                  const QString &profileDir, bool french, bool canInstall);

// optimized, works or blocked, for a program with this profile
QString windowsStateFor(const WindowsProfile &profile, const QHash<QString, WindowsTitle> &titles);

// "windows:<space>"; the space is the profile's name
QString windowsRef(const QString &space);
bool isWindowsRef(const QString &ref);
QString windowsSpace(const QString &ref);
