// SPDX-License-Identifier: GPL-3.0-or-later
// Windows programs people look for that have no version for Raye, and
// the apps that do the same job here (data/windows-alternatives.json).
// Searching for the Windows name finds those apps, their page says which
// program they stand in for, and Home has a row of the first picks.
#pragma once

#include "catalog.h"

#include <QHash>
#include <QList>
#include <QStringList>

struct Alternative {
    QString windows;   // "Adobe Photoshop"
    QStringList words; // what else people type: "photoshop", "ps"
    QStringList apps;  // app ids, best first
};

// Entries without a Windows name or a single valid app id are left out.
QList<Alternative> parseAlternatives(const QByteArray &json);

// Adds the Windows names to the apps of `byKey` (keyed by appKey) that
// stand in for them: in `replaces`, and in the keywords with the words.
void applyAlternatives(QHash<QString, App> &byKey, const QList<Alternative> &list);

// The ids for the row: every first choice, then every second one...
QStringList alternativePicks(const QList<Alternative> &list);

// RAYE_STORE_ALTERNATIVES, or the file the package installs
QString alternativesFile();
