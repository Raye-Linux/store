// SPDX-License-Identifier: GPL-3.0-or-later
// Where apps come from, for the Store's settings: apt's sources (one-line
// .list files and deb822 .sources) and Flatpak's remotes. Read from files
// only; nothing here changes them.
#pragma once

#include <QVariantList>

namespace SoftwareSources
{
// { name, kind ("raye", "ubuntu", "flathub", "other"), address,
//   components (QStringList), enabled }, Raye first, then Ubuntu, Flathub
// and the rest by name. Ubuntu's archive and its security fixes are one.
// `root` is "/" but in the tests.
QVariantList read(const QString &root = QStringLiteral("/"));
}
