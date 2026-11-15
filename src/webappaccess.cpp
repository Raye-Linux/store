// SPDX-License-Identifier: GPL-3.0-or-later
// GLib before Qt: gio has a struct member called "signals"
#pragma push_macro("signals")
#undef signals
#include <glib.h>
#pragma pop_macro("signals")

#include "webappaccess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace
{
// flatpak's override files and our notes are both key files
class KeyFile
{
public:
    KeyFile() : m_kf(g_key_file_new()) {}
    ~KeyFile() { g_key_file_free(m_kf); }
    KeyFile(const KeyFile &) = delete;
    KeyFile &operator=(const KeyFile &) = delete;

    // false only when the file is there and cannot be read
    bool load(const QString &path)
    {
        if (!QFile::exists(path))
            return true;
        return g_key_file_load_from_file(m_kf, QFile::encodeName(path).constData(),
                                         GKeyFileFlags(G_KEY_FILE_KEEP_COMMENTS), nullptr);
    }

    QStringList list(const char *group, const char *key) const
    {
        gsize n = 0;
        gchar **values = g_key_file_get_string_list(m_kf, group, key, &n, nullptr);
        QStringList out;
        for (gsize i = 0; i < n; ++i)
            out << QString::fromUtf8(values[i]);
        g_strfreev(values);
        return out;
    }

    void setList(const char *group, const char *key, const QStringList &values)
    {
        if (values.isEmpty()) {
            g_key_file_remove_key(m_kf, group, key, nullptr);
            gsize n = 0;
            gchar **keys = g_key_file_get_keys(m_kf, group, &n, nullptr);
            g_strfreev(keys);
            if (n == 0)
                g_key_file_remove_group(m_kf, group, nullptr);
            return;
        }
        QList<QByteArray> bytes;
        QList<const gchar *> ptrs;
        for (const QString &v : values)
            bytes << v.toUtf8();
        for (const QByteArray &b : std::as_const(bytes))
            ptrs << b.constData();
        g_key_file_set_string_list(m_kf, group, key, ptrs.constData(), gsize(ptrs.size()));
    }

    bool empty() const
    {
        gsize n = 0;
        gchar **groups = g_key_file_get_groups(m_kf, &n);
        g_strfreev(groups);
        return n == 0;
    }

    // writes the file, or removes it when nothing is left
    bool save(const QString &path) const
    {
        if (empty())
            return !QFile::exists(path) || QFile::remove(path);
        if (!QDir().mkpath(QFileInfo(path).path()))
            return false;
        return g_key_file_save_to_file(m_kf, QFile::encodeName(path).constData(), nullptr);
    }

private:
    GKeyFile *m_kf;
};

// "!xdg-data/icons:ro" -> "xdg-data/icons"
QString folderOf(QString entry)
{
    if (entry.startsWith(QLatin1Char('!')))
        entry.remove(0, 1);
    const int colon = entry.lastIndexOf(QLatin1Char(':'));
    if (colon > 0)
        entry.truncate(colon);
    while (entry.endsWith(QLatin1Char('/')))
        entry.chop(1);
    return entry;
}

QString overridePath(const QString &flatpakUserDir, const QString &appId)
{
    return flatpakUserDir + QStringLiteral("/overrides/") + appId;
}

// the id is a file name below: nothing but what Flathub ids are made of
bool plainId(const QString &appId)
{
    return !appId.isEmpty() && !appId.contains(QLatin1Char('/')) && !appId.contains(QLatin1String(".."));
}
} // namespace

namespace WebAppAccess
{
QStringList browsers()
{
    return {QStringLiteral("com.google.Chrome"),
            QStringLiteral("com.google.ChromeDev"),
            QStringLiteral("org.chromium.Chromium"),
            QStringLiteral("io.github.ungoogled_software.ungoogled_chromium"),
            QStringLiteral("com.brave.Browser"),
            QStringLiteral("com.vivaldi.Vivaldi"),
            QStringLiteral("com.opera.Opera")};
}

QStringList entries()
{
    return {QStringLiteral("xdg-data/applications:create"), QStringLiteral("xdg-data/icons:create")};
}

QString userDir()
{
    const QString env = qEnvironmentVariable("FLATPAK_USER_DIR");
    if (!env.isEmpty())
        return env;
    QString data = qEnvironmentVariable("XDG_DATA_HOME");
    if (data.isEmpty() || !QDir::isAbsolutePath(data))
        data = QDir::homePath() + QStringLiteral("/.local/share");
    return data + QStringLiteral("/flatpak");
}

QString defaultStatePath()
{
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty() || !QDir::isAbsolutePath(state))
        state = QDir::homePath() + QStringLiteral("/.local/state");
    return state + QStringLiteral("/raye-store/web-app-access");
}

bool grant(const QString &appId, const QString &flatpakUserDir, const QString &statePath)
{
    if (!browsers().contains(appId))
        return true;
    const QString path = overridePath(flatpakUserDir, appId);
    KeyFile override;
    KeyFile notes;
    if (!override.load(path) || !notes.load(statePath))
        return false;
    QStringList have = override.list("Context", "filesystems");
    QStringList added = notes.list(appId.toUtf8().constData(), "added");
    bool changed = false;
    for (const QString &entry : entries()) {
        const QString folder = folderOf(entry);
        const bool set = std::any_of(have.cbegin(), have.cend(), [&](const QString &e) { return folderOf(e) == folder; });
        if (set)
            continue;
        have << entry;
        if (!added.contains(entry))
            added << entry;
        changed = true;
    }
    if (!changed)
        return true;
    override.setList("Context", "filesystems", have);
    notes.setList(appId.toUtf8().constData(), "added", added);
    // the notes first: a folder given and not noted could never be taken back
    return notes.save(statePath) && override.save(path);
}

bool revoke(const QString &appId, const QString &flatpakUserDir, const QString &statePath)
{
    if (!plainId(appId))
        return true;
    KeyFile notes;
    if (!notes.load(statePath))
        return false;
    const QByteArray group = appId.toUtf8();
    const QStringList added = notes.list(group.constData(), "added");
    if (added.isEmpty())
        return true;
    const QString path = overridePath(flatpakUserDir, appId);
    KeyFile override;
    if (!override.load(path))
        return false;
    QStringList have = override.list("Context", "filesystems");
    for (const QString &entry : added)
        have.removeAll(entry);
    override.setList("Context", "filesystems", have);
    notes.setList(group.constData(), "added", {});
    return override.save(path) && notes.save(statePath);
}
}
