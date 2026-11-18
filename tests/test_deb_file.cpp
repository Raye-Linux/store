// SPDX-License-Identifier: GPL-3.0-or-later
// The window for a .deb someone opened: what it shows from the helper's
// answer, how it follows an install, and the plain words when it fails.
// The helper is a little script that answers what each test needs.
#include "debfile.h"

#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{
const QString sha = QStringLiteral("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08");
const QString infoJson = QStringLiteral(
    R"({"ok": true, "package": "hello-world", "version": "2.4-1", "architecture": "amd64", )"
    R"("summary": "Says hello", "maintainer": "Jane Maker <jane@example.org>", "installedSize": 47185920, )"
    R"("names": {"": "Hello World", "fr": "Bonjour le monde"}, "desktop": "hello.desktop", "icon": "%1", )"
    R"("fileSize": 1000, "archOk": %2, "installed": "%3", "newer": %4, "launchers": %5, "sha256": "%6"})");
}

class TestDebFile : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    // a helper that prints `out` and leaves with `code`, after writing its
    // arguments to args.txt
    QStringList script(const QString &name, const QString &out, int code = 0)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        QFile body(m_dir.filePath(name + QStringLiteral(".out")));
        if (!body.open(QIODevice::WriteOnly))
            return {};
        body.write(out.toUtf8());
        body.close();
        f.write(QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > '%1'\ncat '%2'\n%3exit %4\n")
                    .arg(m_dir.filePath(QStringLiteral("args.txt")), body.fileName(),
                         code ? QStringLiteral("echo 'E: last words of apt' >&2\n") : QString())
                    .arg(code)
                    .toUtf8());
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return {path};
    }

    QStringList args() const
    {
        QFile f(m_dir.filePath(QStringLiteral("args.txt")));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    QStringList info(bool archOk = true, const QString &installed = {}, const QString &newer = QStringLiteral("null"))
    {
        return script(QStringLiteral("info"),
                      infoJson.arg(m_dir.filePath(QStringLiteral("icon.png")), archOk ? QStringLiteral("true") : QStringLiteral("false"),
                                   installed, newer,
                                   installed.isEmpty() ? QStringLiteral("[]") : QStringLiteral(R"(["/usr/share/applications/hello.desktop"])"),
                                   sha));
    }

    void waitFor(DebFile &deb, int state)
    {
        QTRY_COMPARE_WITH_TIMEOUT(deb.state(), state, 10000);
    }

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("raye-store");
        QLocale::setDefault(QLocale(QLocale::English));
        QVERIFY(m_dir.isValid());
    }

    void readsTheFile()
    {
        DebFile deb(QStringLiteral("/home/jane/Downloads/hello_2.4-1_amd64.deb"), info());
        deb.load();
        waitFor(deb, DebFile::Ready);
        QCOMPARE(args().first(), QStringLiteral("info"));
        QCOMPARE(args().at(1), QStringLiteral("/home/jane/Downloads/hello_2.4-1_amd64.deb"));
        QCOMPARE(deb.fileName(), QStringLiteral("hello_2.4-1_amd64.deb"));
        QCOMPARE(deb.name(), QStringLiteral("Hello World"));
        QCOMPARE(deb.packageName(), QStringLiteral("hello-world"));
        QCOMPARE(deb.version(), QStringLiteral("2.4-1"));
        QCOMPARE(deb.publisher(), QStringLiteral("Jane Maker"));
        QCOMPARE(deb.summary(), QStringLiteral("Says hello"));
        QCOMPARE(deb.sizeText(), QStringLiteral("45 MB"));
        QCOMPARE(deb.icon(), QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("icon.png"))));
        QCOMPARE(deb.relation(), int(DebFile::NotInstalled));
    }

    void symlinkGoesToItsFile()
    {
        QFile real(m_dir.filePath(QStringLiteral("real.deb")));
        QVERIFY(real.open(QIODevice::WriteOnly));
        real.close();
        const QString link = m_dir.filePath(QStringLiteral("link.deb"));
        QVERIFY(QFile::link(real.fileName(), link));
        DebFile deb(link, info());
        deb.load();
        waitFor(deb, DebFile::Ready);
        QCOMPARE(args().at(1), QFileInfo(real.fileName()).canonicalFilePath());
    }

    void relations_data()
    {
        QTest::addColumn<QString>("installed");
        QTest::addColumn<QString>("newer");
        QTest::addColumn<int>("relation");
        QTest::newRow("absent") << QString() << QStringLiteral("null") << int(DebFile::NotInstalled);
        QTest::newRow("older on this PC") << QStringLiteral("2.3-1") << QStringLiteral("1") << int(DebFile::UpdateAvailable);
        QTest::newRow("same") << QStringLiteral("2.4-1") << QStringLiteral("0") << int(DebFile::AlreadyInstalled);
        QTest::newRow("newer on this PC") << QStringLiteral("3.0-1") << QStringLiteral("-1") << int(DebFile::NewerInstalled);
    }

    void relations()
    {
        QFETCH(QString, installed);
        QFETCH(QString, newer);
        QFETCH(int, relation);
        DebFile deb(QStringLiteral("/tmp/hello.deb"), info(true, installed, newer));
        deb.load();
        waitFor(deb, DebFile::Ready);
        QCOMPARE(deb.relation(), relation);
        QCOMPARE(deb.installedVersion(), installed);
        // what is installed already opens
        QCOMPARE(deb.canOpen(), !installed.isEmpty());
    }

    void unreadableFile()
    {
        DebFile deb(QStringLiteral("/tmp/hello.deb"), script(QStringLiteral("info"), QStringLiteral(R"({"ok": false, "error": "notdeb"})"), 1));
        deb.load();
        waitFor(deb, DebFile::Failed);
        QCOMPARE(deb.errorText(), DebFile::reasonText(QStringLiteral("notdeb")));
        QVERIFY(!deb.errorTitle().isEmpty());
    }

    void wrongArchitecture()
    {
        QStringList helper = script(QStringLiteral("info"), infoJson.arg(QString(), QStringLiteral("false"), QString(), QStringLiteral("null"), QStringLiteral("[]"), sha)
                                                                .replace(QStringLiteral("\"amd64\""), QStringLiteral("\"arm64\"")));
        DebFile deb(QStringLiteral("/tmp/hello.deb"), helper);
        deb.load();
        waitFor(deb, DebFile::Failed);
        QVERIFY(deb.errorText().contains(QStringLiteral("arm64")));
        QCOMPARE(deb.icon(), QUrl());
    }

    void helperMissing()
    {
        DebFile deb(QStringLiteral("/tmp/hello.deb"), {m_dir.filePath(QStringLiteral("nothing-here"))});
        deb.load();
        waitFor(deb, DebFile::Failed);
        QVERIFY(!deb.errorText().isEmpty());
    }

    void installs()
    {
        DebFile deb(QStringLiteral("/tmp/hello.deb"), info(),
                    script(QStringLiteral("root"), QStringLiteral("phase preparing\npackage hello-world\nversion 2.4-1\n"
                                                                  "phase downloading\nprogress 40\nphase installing\nprogress 100\n"
                                                                  "launcher /usr/share/applications/hello.desktop\n")));
        deb.load();
        waitFor(deb, DebFile::Ready);
        QVERIFY(!deb.canOpen());
        QSignalSpy spy(&deb, &DebFile::changed);
        deb.install();
        QCOMPARE(deb.state(), int(DebFile::Installing));
        waitFor(deb, DebFile::Done);
        // the hash of what the window read goes along
        QCOMPARE(args(), (QStringList{QStringLiteral("install-file"), QStringLiteral("/tmp/hello.deb"), sha}));
        QVERIFY(deb.canOpen());
        QCOMPARE(deb.launcher(), QStringLiteral("/usr/share/applications/hello.desktop"));
        QVERIFY(spy.count() >= 2);
        QVERIFY(deb.errorText().isEmpty());
    }

    void installFails()
    {
        DebFile deb(QStringLiteral("/tmp/hello.deb"), info(),
                    script(QStringLiteral("root"), QStringLiteral("package hello-world\nreason dependencies libfoo3\n"), 1));
        deb.load();
        waitFor(deb, DebFile::Ready);
        deb.install();
        waitFor(deb, DebFile::Failed);
        QCOMPARE(deb.errorText(), DebFile::reasonText(QStringLiteral("dependencies"), QStringLiteral("libfoo3")));
        QVERIFY(deb.errorTitle().contains(QStringLiteral("Hello World")));
        QVERIFY(deb.details().contains(QStringLiteral("last words of apt")));
        QVERIFY(!deb.canOpen());
    }

    void passwordDialogClosed()
    {
        DebFile deb(QStringLiteral("/tmp/hello.deb"), info(), script(QStringLiteral("root"), QString(), 126));
        deb.load();
        waitFor(deb, DebFile::Ready);
        deb.install();
        QCOMPARE(deb.state(), int(DebFile::Installing));
        waitFor(deb, DebFile::Ready);
        QVERIFY(deb.errorText().isEmpty());
    }

    void notAllowed()
    {
        DebFile deb(QStringLiteral("/tmp/hello.deb"), info(), script(QStringLiteral("root"), QString(), 127));
        deb.load();
        waitFor(deb, DebFile::Ready);
        deb.install();
        waitFor(deb, DebFile::Failed);
        QCOMPARE(deb.errorText(), QStringLiteral("You need root access to do this."));
    }

    void installOnlyWhenReady()
    {
        DebFile deb(QStringLiteral("/tmp/hello.deb"), info(true, QStringLiteral("3.0"), QStringLiteral("-1")),
                    script(QStringLiteral("root"), QString()));
        deb.load();
        waitFor(deb, DebFile::Ready);
        QFile::remove(m_dir.filePath(QStringLiteral("args.txt")));
        deb.install(); // a newer version is there: nothing to do
        QCOMPARE(deb.state(), int(DebFile::Ready));
        QVERIFY(args().isEmpty());
    }

    void reasons()
    {
        const QStringList codes{QStringLiteral("badpath"), QStringLiteral("notfound"), QStringLiteral("noaccess"),
                                QStringLiteral("notfile"), QStringLiteral("notdeb"), QStringLiteral("broken"),
                                QStringLiteral("toolarge"), QStringLiteral("architecture"), QStringLiteral("dependencies"),
                                QStringLiteral("conflict"), QStringLiteral("older"), QStringLiteral("busy"),
                                QStringLiteral("space"), QStringLiteral("network"), QStringLiteral("setup"),
                                QStringLiteral("changed"), QStringLiteral("system"),
                                QStringLiteral("unknown")};
        const QString generic = DebFile::reasonText(QStringLiteral("unknown"));
        for (const QString &code : codes) {
            const QString text = DebFile::reasonText(code, QStringLiteral("libfoo3"));
            QVERIFY2(!text.isEmpty(), qPrintable(code));
            if (code != QLatin1String("unknown"))
                QVERIFY2(text != generic, qPrintable(code));
        }
        QCOMPARE(DebFile::reasonText(QStringLiteral("something new")), generic);
        QVERIFY(DebFile::reasonText(QStringLiteral("conflict"), QStringLiteral("kwin")).contains(QStringLiteral("kwin")));
        QVERIFY(!DebFile::reasonText(QStringLiteral("conflict")).contains(QStringLiteral("%1")));
        QVERIFY(!DebFile::reasonText(QStringLiteral("dependencies")).contains(QStringLiteral("%1")));
    }

    void names()
    {
        const QJsonObject names{{QString(), QStringLiteral("Hello World")}, {QStringLiteral("fr"), QStringLiteral("Bonjour le monde")},
                                {QStringLiteral("pt_BR"), QStringLiteral("Olá mundo")}};
        QCOMPARE(DebFile::pickName(names, QLocale(QLocale::English)), QStringLiteral("Hello World"));
        QCOMPARE(DebFile::pickName(names, QLocale(QLocale::French, QLocale::Canada)), QStringLiteral("Bonjour le monde"));
        QCOMPARE(DebFile::pickName(names, QLocale(QLocale::Portuguese, QLocale::Brazil)), QStringLiteral("Olá mundo"));
        QCOMPARE(DebFile::pickName({}, QLocale(QLocale::French)), QString());
    }

    void publishers()
    {
        QCOMPARE(DebFile::publisherName(QStringLiteral("Jane Maker <jane@example.org>")), QStringLiteral("Jane Maker"));
        QCOMPARE(DebFile::publisherName(QStringLiteral("\"Some Co.\" <a@b>")), QStringLiteral("Some Co."));
        QCOMPARE(DebFile::publisherName(QStringLiteral("<a@b>")), QString());
        QCOMPARE(DebFile::publisherName(QString()), QString());
    }
};

QTEST_GUILESS_MAIN(TestDebFile)

#include "test_deb_file.moc"
