// The spaces Windows programs live in (~/.local/share/raye/windows/<name>),
// the programs installed in them and the room they take.
#pragma once

#include "regfile.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace RayeWindows {

struct Space {
    QString slug;
    QString path;
};

// $XDG_DATA_HOME/raye/windows
QString defaultSpacesRoot();
// what raye-run-windows makes: lower case, digits and dashes
bool isValidSlug(const QString &slug);
// every space with a drive_c, by name; the ready space (.template) is not one
QList<Space> listSpaces(const QString &root);
bool findSpace(const QString &root, const QString &slug, Space *out);

// the account folder Wine made in the space (drive_c/users/<name>)
QString windowsUser(const Space &space);
// %ProgramFiles%, %APPDATA%... with the values Wine gives them
QString expandWindowsVars(const QString &path, const QString &user);
// C:\... -> the file in the space, letters in any case; empty when it is
// not there, not on C: or tries to leave the drive
QString toUnixPath(const Space &space, const QString &windowsPath);
// the program of a command line: "C:\a b\x.exe" /S -> C:\a b\x.exe
QString commandProgram(const QString &commandLine);

// bytes on disk under path, links not followed, hard links counted once
qint64 diskUsage(const QString &path);

// Uninstall entries Windows would list, minus those already in the ready
// space (runtimes every space gets)
QList<UninstallEntry> spacePrograms(const Space &space, const QString &root);

// deletes a space folder; only a valid name right under root
bool removeSpaceFolder(const QString &root, const QString &slug);

} // namespace RayeWindows
