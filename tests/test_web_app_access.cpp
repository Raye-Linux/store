// SPDX-License-Identifier: GPL-3.0-or-later
// The two folders a Chromium browser needs to make apps of sites: given
// to the named browsers only, never to others, never wider, and taken
// back on removal without touching what the user set.
#pragma push_macro("signals")
#undef signals
#include <glib.h>
#pragma pop_macro("signals")

#include "webappaccess.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace
{
const QString chrome = QStringLiteral("com.google.Chrome");

QStringList filesystems(const QString &file)
{
    GKeyFile *kf = g_key_file_new();
    QStringList out;
    if (g_key_file_load_from_file(kf, QFile::encodeName(file).constData(), G_KEY_FILE_NONE, nullptr)) {
        gsize n = 0;
        gchar **list = g_key_file_get_string_list(kf, "Context", "filesystems", &n, nullptr);
        for (gsize i = 0; i < n; ++i)
            out << QString::fromUtf8(list[i]);
        g_strfreev(list);
    }
    g_key_file_free(kf);
    return out;
}

void write(const QString &file, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(file).path());
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
}
} // namespace

class TestWebAppAccess : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString flatpakDir() const { return m_dir.filePath(QStringLiteral("flatpak")); }
    QString state() const { return m_dir.filePath(QStringLiteral("state/web-app-access")); }
    QString overrideOf(const QString &id) const { return flatpakDir() + QStringLiteral("/overrides/") + id; }

private Q_SLOTS:
    void init()
    {
        QDir(flatpakDir()).removeRecursively();
        QFile::remove(state());
    }

    void theEntriesAreTheTwoFoldersOnly()
    {
        QCOMPARE(WebAppAccess::entries(),
                 QStringList({QStringLiteral("xdg-data/applications:create"), QStringLiteral("xdg-data/icons:create")}));
        for (const QString &e : WebAppAccess::entries()) {
            QVERIFY(!e.startsWith(QLatin1String("home")));
            QVERIFY(!e.startsWith(QLatin1String("host")));
        }
    }

    void theListIsChromiumBrowsersWithoutEdge()
    {
        const QStringList list = WebAppAccess::browsers();
        QVERIFY(list.contains(chrome));
        QVERIFY(list.contains(QStringLiteral("org.chromium.Chromium")));
        // Edge has the folders in its own manifest
        QVERIFY(!list.contains(QStringLiteral("com.microsoft.Edge")));
        QVERIFY(!list.contains(QStringLiteral("org.mozilla.firefox")));
    }

    void chromeGetsTheTwoFolders()
    {
        QVERIFY(WebAppAccess::grant(chrome, flatpakDir(), state()));
        QCOMPARE(filesystems(overrideOf(chrome)), WebAppAccess::entries());
    }

    void anotherAppGetsNothing()
    {
        QVERIFY(WebAppAccess::grant(QStringLiteral("org.videolan.VLC"), flatpakDir(), state()));
        QVERIFY(!QFile::exists(overrideOf(QStringLiteral("org.videolan.VLC"))));
        QVERIFY(WebAppAccess::grant(QStringLiteral("com.microsoft.Edge"), flatpakDir(), state()));
        QVERIFY(!QFile::exists(overrideOf(QStringLiteral("com.microsoft.Edge"))));
        QVERIFY(!QFile::exists(state()));
    }

    void removalTakesBackAndLeavesNoFile()
    {
        QVERIFY(WebAppAccess::grant(chrome, flatpakDir(), state()));
        QVERIFY(WebAppAccess::revoke(chrome, flatpakDir(), state()));
        QVERIFY(!QFile::exists(overrideOf(chrome)));
    }

    void grantingTwiceAddsNothingMore()
    {
        QVERIFY(WebAppAccess::grant(chrome, flatpakDir(), state()));
        QVERIFY(WebAppAccess::grant(chrome, flatpakDir(), state()));
        QCOMPARE(filesystems(overrideOf(chrome)), WebAppAccess::entries());
        QVERIFY(WebAppAccess::revoke(chrome, flatpakDir(), state()));
        QVERIFY(!QFile::exists(overrideOf(chrome)));
    }

    void whatTheUserSetStays()
    {
        // denied icons, an extra folder and a device of the user's own
        write(overrideOf(chrome),
              "[Context]\nfilesystems=!xdg-data/icons;xdg-download;\ndevices=dri;\n");
        QVERIFY(WebAppAccess::grant(chrome, flatpakDir(), state()));
        QCOMPARE(filesystems(overrideOf(chrome)),
                 QStringList({QStringLiteral("!xdg-data/icons"), QStringLiteral("xdg-download"),
                              QStringLiteral("xdg-data/applications:create")}));
        QVERIFY(WebAppAccess::revoke(chrome, flatpakDir(), state()));
        QCOMPARE(filesystems(overrideOf(chrome)), QStringList({QStringLiteral("!xdg-data/icons"), QStringLiteral("xdg-download")}));
        QFile f(overrideOf(chrome));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().contains("devices=dri"));
    }

    void aFolderTheUserAllowedIsNotTakenBack()
    {
        write(overrideOf(chrome), "[Context]\nfilesystems=xdg-data/applications;\n");
        QVERIFY(WebAppAccess::grant(chrome, flatpakDir(), state()));
        QVERIFY(WebAppAccess::revoke(chrome, flatpakDir(), state()));
        QCOMPARE(filesystems(overrideOf(chrome)), QStringList({QStringLiteral("xdg-data/applications")}));
    }

    void removalWithoutGrantTouchesNothing()
    {
        write(overrideOf(chrome), "[Context]\nfilesystems=xdg-data/applications:create;\n");
        QVERIFY(WebAppAccess::revoke(chrome, flatpakDir(), state()));
        QCOMPARE(filesystems(overrideOf(chrome)), QStringList({QStringLiteral("xdg-data/applications:create")}));
    }
};

QTEST_GUILESS_MAIN(TestWebAppAccess)
#include "test_web_app_access.moc"
