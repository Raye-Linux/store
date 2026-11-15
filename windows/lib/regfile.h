// The registry files of a space (system.reg, user.reg), read as text,
// without starting anything, and the programs listed as installed there.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

namespace RayeWindows {

struct RegValue {
    enum Type { String, ExpandString, MultiString, Dword, Binary, Other };
    Type type = String;
    QString text;      // strings
    quint32 dword = 0;
    QByteArray bytes;  // hex values
};

struct RegKey {
    QString path;          // as written, with single backslashes
    qint64 modified = 0;   // seconds since 1970, from the key line
    QHash<QString, RegValue> values; // by lower-case name; "" is the default value

    bool has(const QString &name) const { return values.contains(name.toLower()); }
    QString string(const QString &name) const;
    quint32 dword(const QString &name, quint32 fallback = 0) const;
};

class RegFile
{
public:
    bool load(const QString &path);
    bool parse(const QByteArray &text);

    bool is32() const { return m_is32; }   // "#arch=win32"
    const RegKey *key(const QString &path) const;
    // keys right below path (one level), in file order
    QList<const RegKey *> subkeys(const QString &path) const;
    int size() const { return int(m_keys.size()); }

private:
    QList<RegKey> m_keys;
    QHash<QString, int> m_index;   // lower-case path -> m_keys
    bool m_is32 = false;
};

struct UninstallEntry {
    QString key;         // the subkey name, what "raye-windows uninstall" takes
    QString view;        // "64", "32" (both machine-wide) or "user"
    QString name;
    QString publisher;
    QString version;
    QString uninstallString;
    QString quietUninstallString;
    QString displayIcon;
    QString installLocation;
    QString installDate;      // YYYY-MM-DD, from InstallDate or the key's time
    qint64 estimatedSize = 0; // bytes, 0 when not given
    bool windowsInstaller = false;
    bool noRemove = false;
    bool visible = true;      // what Windows lists in Installed apps
};

// every Uninstall entry of both views of the machine, then the user's
QList<UninstallEntry> uninstallEntries(const RegFile &system, const RegFile &user);

// the command that removes it: the quiet one when there is one
QString uninstallCommand(const UninstallEntry &e);

} // namespace RayeWindows
