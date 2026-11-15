// SPDX-License-Identifier: GPL-3.0-or-later
// Sites installed as apps. A browser built on Chromium writes the
// shortcut and the icon of such an app into ~/.local/share/applications
// and ~/.local/share/icons. Edge's Flatpak may write there; Chrome's and
// the others' may not, so they refuse to make the app at all. When the
// Store installs one of the browsers named in browsers(), it lets that
// browser write in those two folders and nowhere else, through a user
// override of flatpak; when it removes the browser, it takes back what
// it gave. An entry the user set for one of the folders, allowed or
// denied, is never changed.
#pragma once

#include <QString>
#include <QStringList>

namespace WebAppAccess
{
// The Flathub ids of the browsers that get the two folders
QStringList browsers();

// The two entries, in flatpak's words
QStringList entries();

// flatpak's user folder: $FLATPAK_USER_DIR, else $XDG_DATA_HOME/flatpak
QString userDir();

// where the Store notes what it added: ~/.local/state/raye-store/web-app-access
QString defaultStatePath();

// Adds the entries the browser does not have yet to its override file
// in `flatpakUserDir`/overrides, and notes them in `statePath`. Does
// nothing for an app that is not one of browsers(). false when a file
// could not be written.
bool grant(const QString &appId, const QString &flatpakUserDir = userDir(), const QString &statePath = defaultStatePath());

// Takes out of the override file the entries grant() noted for this
// app, and only those; the file goes when nothing is left in it.
bool revoke(const QString &appId, const QString &flatpakUserDir = userDir(), const QString &statePath = defaultStatePath());
}
