// SPDX-License-Identifier: GPL-3.0-or-later
// What an install leaves behind. Flatpak downloads a system app into a
// folder of the user's own, /var/tmp/flatpak-cache-XXXXXX, and hands it
// to its system helper; the folder stays once the install is over. The
// Store removes such folders when nothing uses them any more.
#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

namespace Cleanup
{
// Paths in use right now: mount points (flatpak mounts the cache while
// its helper reads it), and the open files and working folders of every
// process this user can look into.
QSet<QString> pathsInUse(const QString &proc = QStringLiteral("/proc"));

// true when `dir` or something under it is in `busy`
bool inUse(const QString &dir, const QSet<QString> &busy);

// The flatpak-cache-* folders in `tmp` owned by `uid` that nothing in
// `busy` uses. Folders of other users, files and links are never listed.
QStringList staleFlatpakCaches(const QString &tmp, uint uid, const QSet<QString> &busy);

// The stale folders of the current user. The folders are listed before
// /proc is read, so one made in between shows up as in use. Slow: run it
// off the main thread.
QStringList findFlatpakCaches(const QString &tmp = QStringLiteral("/var/tmp"));

// Removes the folders; returns how many went. The Store calls it on its
// main thread, once it has checked again that no job of its own started.
int removeFolders(const QStringList &folders);
}
