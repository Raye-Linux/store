// SPDX-License-Identifier: GPL-3.0-or-later
// Windows programs with no version for Raye, and the apps that do their
// job here: found by the Windows name, said on the app's page, and a row
// of first picks.
#include "alternatives.h"
#include "catalog.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {
App app(const QString &id, const QString &name, bool listed = true)
{
    App a;
    a.id = id;
    a.name = name;
    a.summary = QStringLiteral("does things");
    a.listed = listed;
    return a;
}

// plain string literals: moc does not read raw strings well
const QByteArray kJson =
    "{\"alternatives\":["
    "{\"windows\":\"Adobe Photoshop\",\"words\":[\"photoshop\",\"ps\"],\"apps\":[\"org.gimp.GIMP\",\"org.kde.krita\"]},"
    "{\"windows\":\"Paint.NET\",\"apps\":[\"com.github.PintaProject.Pinta\",\"org.gimp.GIMP\"]},"
    "{\"windows\":\"Microsoft Office\",\"words\":[\"Word\",\"Excel\"],\"apps\":[\"org.libreoffice.LibreOffice\"]},"
    "{\"windows\":\"\",\"apps\":[\"org.x.Empty\"]},"
    "{\"windows\":\"No apps\",\"apps\":[]},"
    "{\"windows\":\"Odd id\",\"apps\":[\"../etc/passwd\",\"has space\",\"ok.app.Id\"]}"
    "]}";
} // namespace

class TestAlternatives : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void parsing()
    {
        const QList<Alternative> list = parseAlternatives(kJson);
        QCOMPARE(list.size(), 4);
        QCOMPARE(list.at(0).windows, QStringLiteral("Adobe Photoshop"));
        QCOMPARE(list.at(0).words, (QStringList{QStringLiteral("photoshop"), QStringLiteral("ps")}));
        QCOMPARE(list.at(0).apps, (QStringList{QStringLiteral("org.gimp.GIMP"), QStringLiteral("org.kde.krita")}));
        // ids that are not ids are dropped, the rest of the entry stays
        QCOMPARE(list.at(3).apps, QStringList{QStringLiteral("ok.app.Id")});
        QVERIFY(parseAlternatives("not json").isEmpty());
        QVERIFY(parseAlternatives("{\"alternatives\":{}}").isEmpty());
    }

    void appsLearnWhatTheyReplace()
    {
        QHash<QString, App> byKey;
        for (const App &a : {app(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP")),
                             app(QStringLiteral("com.github.PintaProject.Pinta"), QStringLiteral("Pinta"))})
            byKey.insert(a.id, a);
        byKey[QStringLiteral("org.gimp.GIMP")].keywords = {QStringLiteral("photoshop"), QStringLiteral("image")};
        applyAlternatives(byKey, parseAlternatives(kJson));
        const App &gimp = byKey.value(QStringLiteral("org.gimp.GIMP"));
        QCOMPARE(gimp.replaces, (QStringList{QStringLiteral("Adobe Photoshop"), QStringLiteral("Paint.NET")}));
        // the Windows names and the words people type, each once
        QCOMPARE(gimp.keywords, (QStringList{QStringLiteral("photoshop"), QStringLiteral("image"), QStringLiteral("Adobe Photoshop"),
                                             QStringLiteral("ps"), QStringLiteral("Paint.NET")}));
        QCOMPARE(byKey.value(QStringLiteral("com.github.PintaProject.Pinta")).replaces, QStringList{QStringLiteral("Paint.NET")});
        // an app the catalog does not have is not made up
        QVERIFY(!byKey.contains(QStringLiteral("org.kde.krita")));
    }

    void idsMatchWhateverTheCase()
    {
        QHash<QString, App> byKey;
        byKey.insert(QStringLiteral("org.gimp.gimp"), app(QStringLiteral("org.gimp.gimp"), QStringLiteral("GIMP")));
        applyAlternatives(byKey, parseAlternatives(kJson));
        QCOMPARE(byKey.value(QStringLiteral("org.gimp.gimp")).replaces.size(), 2);
        Catalog catalog;
        catalog.setApps({app(QStringLiteral("org.gimp.gimp.desktop"), QStringLiteral("GIMP"))});
        QTemporaryDir tmp;
        QFile f(tmp.filePath(QStringLiteral("a.json")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kJson);
        f.close();
        qputenv("RAYE_STORE_ALTERNATIVES", f.fileName().toUtf8());
        QCOMPARE(catalog.fromWindows(), QStringList{QStringLiteral("org.gimp.gimp.desktop")});
        qunsetenv("RAYE_STORE_ALTERNATIVES");
    }

    void searchFindsTheWindowsName()
    {
        QHash<QString, App> byKey;
        byKey.insert(QStringLiteral("org.libreoffice.LibreOffice"), app(QStringLiteral("org.libreoffice.LibreOffice"), QStringLiteral("LibreOffice")));
        applyAlternatives(byKey, parseAlternatives(kJson));
        const App office = byKey.value(QStringLiteral("org.libreoffice.LibreOffice"));
        QVERIFY(searchScore(QStringLiteral("excel"), office) > 0);
        QVERIFY(searchScore(QStringLiteral("Microsoft Office"), office) > 0);
        QVERIFY(searchScore(QStringLiteral("Micro"), office) > 0);
        // typed in full, the Windows name puts its stand-in first
        App writer = app(QStringLiteral("org.x.Excelsior"), QStringLiteral("Excelsior"));
        QVERIFY(searchScore(QStringLiteral("excel"), office) > searchScore(QStringLiteral("excel"), writer));
        writer.keywords = {QStringLiteral("excel")};
        QVERIFY(searchScore(QStringLiteral("excel"), writer) < 90);
    }

    void picksFirstChoicesFirst()
    {
        const QList<Alternative> list = parseAlternatives(kJson);
        QCOMPARE(alternativePicks(list),
                 (QStringList{QStringLiteral("org.gimp.GIMP"), QStringLiteral("com.github.PintaProject.Pinta"),
                              QStringLiteral("org.libreoffice.LibreOffice"), QStringLiteral("ok.app.Id"),
                              QStringLiteral("org.kde.krita")}));
    }

    void catalogRowShowsListedPicks()
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath(QStringLiteral("alternatives.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kJson);
        f.close();
        qputenv("RAYE_STORE_ALTERNATIVES", path.toUtf8());
        Catalog catalog;
        catalog.setApps({app(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP")),
                         app(QStringLiteral("org.kde.krita"), QStringLiteral("Krita")),
                         app(QStringLiteral("org.libreoffice.LibreOffice"), QStringLiteral("LibreOffice"), false)});
        QCOMPARE(catalog.fromWindows(), (QStringList{QStringLiteral("org.gimp.GIMP"), QStringLiteral("org.kde.krita")}));
        qunsetenv("RAYE_STORE_ALTERNATIVES");
    }

    void replacesInTheModelAndCache()
    {
        App gimp = app(QStringLiteral("org.gimp.GIMP"), QStringLiteral("GIMP"));
        gimp.replaces = {QStringLiteral("Adobe Photoshop")};
        Catalog catalog;
        catalog.setApps({gimp});
        QCOMPARE(catalog.find(QStringLiteral("org.gimp.GIMP")).value(QStringLiteral("replaces")).toStringList(),
                 QStringList{QStringLiteral("Adobe Photoshop")});
        QList<App> back;
        Feeds feeds;
        QStringList order;
        QVERIFY(decodeCatalog(encodeCatalog({gimp}, {}, {}, QStringLiteral("s")), QStringLiteral("s"), back, feeds, order));
        QCOMPARE(back.first().replaces, gimp.replaces);
    }
};

QTEST_GUILESS_MAIN(TestAlternatives)
#include "test_alternatives.moc"
