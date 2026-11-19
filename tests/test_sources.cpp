// SPDX-License-Identifier: GPL-3.0-or-later
// Where apps come from, as the Store's settings list it: apt's sources
// (one-line and deb822) and Flatpak's remotes, read from files only.
#include "softwaresources.h"
#include "storesettings.h"

#include <QSignalSpy>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace
{
void write(const QString &path, const QByteArray &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
}

QVariantMap byName(const QVariantList &list, const QString &name)
{
    for (const QVariant &v : list) {
        if (v.toMap().value(QStringLiteral("name")).toString() == name)
            return v.toMap();
    }
    return {};
}
}

class TestSources : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsAptAndFlatpak()
    {
        QTemporaryDir root;
        write(root.filePath(QStringLiteral("etc/apt/sources.list.d/ubuntu.sources")),
              "Types: deb\n"
              "URIs: http://archive.ubuntu.com/ubuntu/\n"
              "Suites: resolute resolute-updates\n"
              "Components: main restricted universe multiverse\n"
              "Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n"
              "\n"
              "Types: deb\n"
              "URIs: http://security.ubuntu.com/ubuntu/\n"
              "Suites: resolute-security\n"
              "Components: main restricted universe multiverse\n");
        write(root.filePath(QStringLiteral("etc/apt/sources.list.d/raye.sources")),
              "Types: deb\nURIs: https://dl.rayelinux.com/apt/\nSuites: brocken\nComponents: main\n");
        write(root.filePath(QStringLiteral("etc/apt/sources.list.d/mozilla.list")),
              "# Firefox\ndeb [signed-by=/etc/apt/keyrings/mozilla.asc] https://packages.mozilla.org/apt mozilla main\n");
        write(root.filePath(QStringLiteral("etc/apt/sources.list.d/old.sources")),
              "Enabled: no\nTypes: deb\nURIs: https://example.org/repo\nSuites: stable\nComponents: main\n");
        write(root.filePath(QStringLiteral("etc/apt/sources.list.d/old.list.save")), "deb https://nope.example/ x main\n");
        write(root.filePath(QStringLiteral("var/lib/flatpak/repo/config")),
              "[core]\nrepo_version=1\n\n[remote \"flathub\"]\nurl=https://dl.flathub.org/repo/\n");

        const QVariantList list = SoftwareSources::read(root.path());
        // Raye first, then Ubuntu, Flathub, the others
        QCOMPARE(list.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Raye Linux"));
        const QVariantMap ubuntu = byName(list, QStringLiteral("Ubuntu"));
        QCOMPARE(ubuntu.value(QStringLiteral("kind")).toString(), QStringLiteral("ubuntu"));
        QCOMPARE(ubuntu.value(QStringLiteral("components")).toStringList(),
                 (QStringList{QStringLiteral("main"), QStringLiteral("restricted"), QStringLiteral("universe"), QStringLiteral("multiverse")}));
        QVERIFY(ubuntu.value(QStringLiteral("enabled")).toBool());
        // the archive and its security fixes are one source to a person
        int ubuntus = 0;
        for (const QVariant &v : list)
            ubuntus += v.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("ubuntu");
        QCOMPARE(ubuntus, 1);
        QCOMPARE(byName(list, QStringLiteral("packages.mozilla.org")).value(QStringLiteral("kind")).toString(), QStringLiteral("other"));
        QVERIFY(!byName(list, QStringLiteral("example.org")).value(QStringLiteral("enabled")).toBool());
        QVERIFY(byName(list, QStringLiteral("nope.example")).isEmpty());
        QCOMPARE(byName(list, QStringLiteral("Flathub")).value(QStringLiteral("kind")).toString(), QStringLiteral("flathub"));
    }

    // Repair and Clean run the root helper through pkexec; here a shell
    void repairAndClean()
    {
        QTemporaryDir config;
        StoreSettings settings(config.filePath(QStringLiteral("raye-storerc")));
        QCOMPARE(settings.maintenance(), QString());
        settings.setHelper({QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("test \"$0\" = repair")});
        settings.repair();
        QCOMPARE(settings.maintenance(), QStringLiteral("repairing"));
        QTRY_COMPARE(settings.maintenance(), QStringLiteral("repaired"));
        settings.cleanDownloads();
        QTRY_COMPARE(settings.maintenance(), QStringLiteral("cleanFailed"));
        // one at a time
        settings.setHelper({QStringLiteral("sh"), QStringLiteral("-c"), QStringLiteral("sleep 1")});
        settings.repair();
        settings.cleanDownloads();
        QCOMPARE(settings.maintenance(), QStringLiteral("repairing"));
        QTRY_COMPARE(settings.maintenance(), QStringLiteral("repaired"));
    }

    void emptyIsEmpty()
    {
        QTemporaryDir root;
        QVERIFY(SoftwareSources::read(root.path()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSources)
#include "test_sources.moc"
