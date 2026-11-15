// SPDX-License-Identifier: GPL-3.0-or-later
// What the Store adds to the Start menu search: "Get Discord…" for the
// apps that match and are not installed yet, under "Raye Store". The
// D-Bus side is runner/main.cpp; this part is kept apart for the tests.
#pragma once

#include "catalog.h"

#include <functional>
#include <optional>

struct StoreMatch {
    QString id;       // the catalog id, given back by Run
    QString text;     // "Get Discord…"
    QString subtext;  // the app's summary
    QString icon;     // a file, or a theme icon name
    double relevance = 0; // 0..1
};

// True when an app's name carries the name of the desktop or of the Windows
// layer. Start does not offer those; the Store still lists them.
bool namesTheLayersUnderneath(const QString &name);

// The best apps for what was typed, best first; nothing under three
// letters. `installed` tells whether a ref is installed already.
QList<StoreMatch> storeMatches(const QList<App> &apps, const std::function<bool(const QString &)> &installed,
                               const QString &query, int limit = 3);

// The command line that opens the Store on this app.
QStringList storeLinkArguments(const QString &id);

// What the search reads of the apps it may offer, nothing more: the runner
// keeps only this in memory (a few MB instead of the whole catalog).
QList<App> searchEntries(const QList<App> &apps);

// The runner's copy of searchEntries on disk. `stamp` names the sources it
// was built from (sourceStamp); a file made from other sources, cut short
// or of another format is not used.
bool writeSearchIndex(const QString &file, const QString &stamp, const QList<App> &entries);
std::optional<QList<App>> readSearchIndex(const QString &file, const QString &stamp);

// Changes when one of the roots (files or folders, and what is below a
// folder up to `depth` levels) changes, appears or goes away, or when the
// language changes. A few dozen stat() calls, no reading.
QString sourceStamp(const QStringList &roots, const QString &language, int depth = 3);
// Where the catalog comes from: AppStream of the archive and of Flathub,
// the feeds the catalog timer writes, Raye's own lists.
QStringList defaultSourceRoots();
