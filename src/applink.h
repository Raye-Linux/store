// SPDX-License-Identifier: GPL-3.0-or-later
// What the Store is asked to open: "appstream://com.discordapp.Discord"
// from the Start menu search or a web page, or a plain app id on the
// command line.
#pragma once

#include <QString>
#include <QStringList>

// The app id the argument names, or empty when it names nothing the
// Store could look up. The link's host part may come lower-cased (a URL
// host is case-insensitive): Catalog::find matches ids without case.
QString appIdFromLink(const QString &argument);

// The app named by a list of command line arguments: a link, an id, a
// flatpak ref ("app/<id>/<arch>/<branch>") or --open <any of these>.
// Options are skipped; empty when none names an app.
QString appFromArguments(const QStringList &arguments);
