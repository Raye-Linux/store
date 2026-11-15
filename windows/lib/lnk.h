// Windows shortcuts (.lnk, the MS-SHLLINK format): where they point, with
// which arguments, from which folder, and with which icon.
#pragma once

#include <QByteArray>
#include <QString>

namespace RayeWindows {

struct Lnk {
    bool valid = false;
    QString target;        // C:\... ; may hold %VARIABLES% when only those are known
    QString arguments;
    QString workingDir;
    QString iconLocation;  // a file; empty when the target's own icon is meant
    int iconIndex = 0;
    QString description;
    QString relativePath;
};

Lnk readLnk(const QByteArray &data);
Lnk readLnkFile(const QString &path);

} // namespace RayeWindows
