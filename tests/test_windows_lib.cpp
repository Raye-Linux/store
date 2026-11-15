// The Windows programs library and raye-windows, on the files made by
// tools/make-windows-fixtures.py (tests/data/windows) and on spaces built
// here from them.
#include "entries.h"
#include "lnk.h"
#include "pe.h"
#include "regfile.h"
#include "spaces.h"

#include <KShell>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace RayeWindows;

namespace {

const QString kData = QStringLiteral(RAYE_TESTS_DIR "/data/windows/");
const QString kCommonMenu = QStringLiteral("drive_c/ProgramData/Microsoft/Windows/Start Menu/Programs");
const QString kUserMenu = QStringLiteral("drive_c/users/raye/AppData/Roaming/Microsoft/Windows/Start Menu/Programs");
const QString kUninstall = QStringLiteral("[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\");

QByteArray fixture(const QString &name)
{
    QFile f(kData + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    return f.readAll();
}

void put(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

// one key of a .reg text, from its line to the next key
QByteArray regSection(const QByteArray &text, const QString &key)
{
    const QByteArray start = (kUninstall + key + QLatin1Char(']')).toUtf8();
    const qsizetype at = text.indexOf(start);
    if (at < 0)
        return {};
    qsizetype end = text.indexOf("\n[", at + 1);
    return text.mid(at, end < 0 ? -1 : end - at + 1);
}

QRgb centre(const QImage &img)
{
    return img.pixel(img.width() / 2, img.height() / 2);
}

bool near(QRgb a, QRgb b)
{
    return qAbs(qRed(a) - qRed(b)) < 12 && qAbs(qGreen(a) - qGreen(b)) < 12 && qAbs(qBlue(a) - qBlue(b)) < 12;
}

const QRgb kViolet = qRgb(0x7b, 0x3c, 0xff);
const QRgb kGreen = qRgb(0x20, 0xc0, 0x60);
const QRgb kBlue = qRgb(0x30, 0x60, 0xe0);
const QRgb kRed = qRgb(0xe0, 0x30, 0x30);

QHash<QString, QString> desktopKeys(const QString &path)
{
    QHash<QString, QString> keys;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return keys;
    for (const QByteArray &line : f.readAll().split('\n')) {
        const qsizetype eq = line.indexOf('=');
        if (eq > 0)
            keys.insert(QString::fromUtf8(line.left(eq)), QString::fromUtf8(line.mid(eq + 1)));
    }
    return keys;
}

// what a launcher does with an Exec= value: string escapes, then %%, then
// shell-like word splitting (as KIO does it: unquoted $ or ` would mean a shell)
QStringList execWords(QString exec)
{
    QString unescaped;
    for (int i = 0; i < exec.size(); ++i) {
        if (exec.at(i) == QLatin1Char('\\') && i + 1 < exec.size()) {
            const QChar e = exec.at(++i);
            unescaped += e == QLatin1Char('s') ? QChar(QLatin1Char(' ')) : e;
        } else {
            unescaped += exec.at(i);
        }
    }
    unescaped.replace(QStringLiteral("%%"), QStringLiteral("%"));
    KShell::Errors errors;
    const QStringList words = KShell::splitArgs(unescaped, KShell::AbortOnMeta, &errors);
    return errors == KShell::NoError ? words : QStringList{QStringLiteral("<error>")};
}

} // namespace

class WindowsLibTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_root;
    EntryPaths m_paths;

    // a space with several programs, as installers leave it
    void makeBigSpace()
    {
        const QString s = m_root + QStringLiteral("/raye-test/");
        put(s + QStringLiteral("drive_c/Program Files/Raye Test/tool.exe"), fixture(QStringLiteral("tool64.exe")));
        put(s + QStringLiteral("drive_c/Program Files/Raye Test/unins000.exe"), fixture(QStringLiteral("noicon.exe")));
        put(s + QStringLiteral("drive_c/Program Files/Raye Test/readme.txt"), "hello");
        put(s + QStringLiteral("drive_c/Program Files/Ünïcødé 工具/tool.exe"), fixture(QStringLiteral("tool32.exe")));
        put(s + QStringLiteral("drive_c/Program Files/Café/tool.exe"), fixture(QStringLiteral("tool32.exe")));
        put(s + QStringLiteral("drive_c/users/raye/AppData/Local/Chatty/Chatty.exe"), fixture(QStringLiteral("tool32.exe")));
        put(s + QStringLiteral("drive_c/users/raye/AppData/Local/Chatty/app.ico"), fixture(QStringLiteral("app.ico")));
        put(s + kCommonMenu + QStringLiteral("/Raye Test/Raye Test Tool.lnk"), fixture(QStringLiteral("idlist.lnk")));
        put(s + kCommonMenu + QStringLiteral("/Raye Test/Uninstall Raye Test.lnk"), fixture(QStringLiteral("uninstall.lnk")));
        put(s + kCommonMenu + QStringLiteral("/Raye Test/Read me.lnk"), fixture(QStringLiteral("readme.lnk")));
        put(s + kCommonMenu + QStringLiteral("/Raye Test/Website.url"), "[InternetShortcut]\nURL=https://example.org\n");
        put(s + kCommonMenu + QStringLiteral("/Café tool.lnk"), fixture(QStringLiteral("ansi.lnk")));
        // the same shortcut again for the user: one entry
        put(s + kUserMenu + QStringLiteral("/Raye Test Tool.lnk"), fixture(QStringLiteral("env.lnk")));
        put(s + kUserMenu + QStringLiteral("/Ünïcødé tool.lnk"), fixture(QStringLiteral("unicode.lnk")));
        put(s + kUserMenu + QStringLiteral("/Social/Chatty.lnk"), fixture(QStringLiteral("chatty.lnk")));
        put(s + QStringLiteral("drive_c/users/raye/Desktop/Chatty.lnk"), fixture(QStringLiteral("chatty.lnk")));
        put(s + QStringLiteral("system.reg"), fixture(QStringLiteral("system.reg")));
        put(s + QStringLiteral("user.reg"), fixture(QStringLiteral("user.reg")));
        QDir().mkpath(s + QStringLiteral("dosdevices"));
        QVERIFY(QFile::link(QStringLiteral("../drive_c"), s + QStringLiteral("dosdevices/c:")));
        QVERIFY(QFile::link(QStringLiteral("/"), s + QStringLiteral("dosdevices/z:")));
    }

    // a space with one program; its desktop folder is the user's own
    void makeSoloSpace()
    {
        const QString s = m_root + QStringLiteral("/solo/");
        put(s + QStringLiteral("drive_c/Program Files/Raye Test/tool.exe"), fixture(QStringLiteral("tool64.exe")));
        put(s + QStringLiteral("drive_c/Program Files/Raye Test/unins000.exe"), fixture(QStringLiteral("noicon.exe")));
        put(s + kCommonMenu + QStringLiteral("/Raye Test Tool.lnk"), fixture(QStringLiteral("missing-icon.lnk")));
        put(s + QStringLiteral("drive_c/users/raye/x"), "");
        QVERIFY(QFile::link(m_tmp.path() + QStringLiteral("/home-desktop"), s + QStringLiteral("drive_c/users/raye/Desktop")));
        put(m_tmp.path() + QStringLiteral("/home-desktop/Raye Test Tool.lnk"), fixture(QStringLiteral("missing-icon.lnk")));
        const QByteArray head = "WINE REGISTRY Version 2\n;; All keys relative to \\\\Machine\n\n#arch=win64\n\n";
        put(s + QStringLiteral("system.reg"), head + regSection(fixture(QStringLiteral("system.reg")), QStringLiteral("RayeTestTool")));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_root = m_tmp.path() + QStringLiteral("/spaces");
        m_paths.applications = m_tmp.path() + QStringLiteral("/applications");
        m_paths.icons = m_tmp.path() + QStringLiteral("/icons/hicolor");
        m_paths.desktop = m_tmp.path() + QStringLiteral("/Desktop");
        m_paths.cache = m_tmp.path() + QStringLiteral("/cache");
        makeBigSpace();
        makeSoloSpace();
        put(m_root + QStringLiteral("/.template/system.reg"), fixture(QStringLiteral("template-system.reg")));
        QDir().mkpath(m_root + QStringLiteral("/.template/drive_c"));
        QDir().mkpath(m_root + QStringLiteral("/no-drive"));
        QDir().mkpath(m_root + QStringLiteral("/Bad_Name/drive_c"));
        QDir().mkpath(m_root + QStringLiteral("/half.new/drive_c"));
        QVERIFY(QFile::link(m_root + QStringLiteral("/raye-test"), m_root + QStringLiteral("/linked")));
    }

    // --- programs -------------------------------------------------------

    void categoriesForMusicPrograms()
    {
        // "Image-Line" used to make FL Studio a graphics program
        QCOMPARE(guessCategories(QStringLiteral("Image-Line FL Studio 2026")), QStringLiteral("AudioVideo;Audio;"));
        QCOMPARE(guessCategories(QStringLiteral("Ableton Live 12 Suite")), QStringLiteral("AudioVideo;Audio;"));
        QCOMPARE(guessCategories(QStringLiteral("Paint.NET paint.net")), QStringLiteral("Graphics;"));
        QCOMPARE(guessCategories(QStringLiteral("Roblox Roblox Studio")), QStringLiteral("Utility;"));
    }

    void versionPrefersEnglish()
    {
        PeFile pe;
        QVERIFY(pe.loadData(fixture(QStringLiteral("tool64.exe"))));
        QVERIFY(pe.is64());
        const PeVersion v = pe.version();
        QCOMPARE(v.productName, QStringLiteral("Raye Test Tool"));
        QCOMPARE(v.companyName, QStringLiteral("Raye Test Publisher"));
        QCOMPARE(v.fileDescription, QStringLiteral("Raye Test Tool Setup"));
        QCOMPARE(v.fileVersion, QStringLiteral("1.2.3.4"));
        QCOMPARE(v.productVersion, QStringLiteral("1.2.3"));
        QCOMPARE(v.originalFilename, QStringLiteral("tool.exe"));
    }

    void versionUnicodeAnd32Bit()
    {
        PeFile pe;
        QVERIFY(pe.load(kData + QStringLiteral("tool32.exe")));
        QVERIFY(!pe.is64());
        QCOMPARE(pe.version().productName, QStringLiteral("Ünïcødé 工具"));
        QCOMPARE(pe.version().companyName, QStringLiteral("Ça Marche Ltd"));
        QCOMPARE(pe.version().fileVersion, QStringLiteral("1.0"));
    }

    void versionFromFixedInfo()
    {
        PeFile pe;
        QVERIFY(pe.load(kData + QStringLiteral("noicon.exe")));
        QCOMPARE(pe.version().fileDescription, QStringLiteral("Uninstaller"));
        QCOMPARE(pe.version().fileVersion, QStringLiteral("2.0.0.0"));
        QCOMPARE(pe.iconGroupCount(), 0);
        QVERIFY(pe.icons(0).isEmpty());
    }

    void iconGroupsAndPick()
    {
        PeFile pe;
        QVERIFY(pe.load(kData + QStringLiteral("tool64.exe")));
        QCOMPARE(pe.iconGroupCount(), 2);
        const QList<QImage> icons = pe.icons(0);
        QCOMPARE(icons.size(), 3);
        QList<int> widths;
        for (const QImage &i : icons)
            widths.append(i.width());
        QCOMPARE(widths, (QList<int>{256, 48, 32}));
        const QImage big = pickIcon(icons, 256);
        QCOMPARE(big.size(), QSize(256, 256));
        QVERIFY(near(centre(big), kViolet));
        QVERIFY(near(centre(pickIcon(icons, 48)), kGreen));   // its own 48, not a scaled 256
        QVERIFY(near(centre(pickIcon(icons, 32)), kBlue));
        const QImage scaled = pickIcon(icons, 64);
        QCOMPARE(scaled.size(), QSize(64, 64));
        QVERIFY(near(centre(scaled), kViolet));               // from the 256 one
        QCOMPARE(pickIcon(icons, 512).size(), QSize(512, 512));
        QVERIFY(pickIcon({}, 32).isNull());
        // a 16-colour 32 px icon listed first (FL Studio's FL64.exe) loses to
        // the true-colour one of the same size: every image is ARGB32 by now,
        // so the colours it had are kept as text
        QImage poor(32, 32, QImage::Format_ARGB32), rich(32, 32, QImage::Format_ARGB32);
        poor.fill(Qt::red);
        rich.fill(Qt::blue);
        poor.setText(QStringLiteral("bits"), QStringLiteral("4"));
        rich.setText(QStringLiteral("bits"), QStringLiteral("32"));
        QVERIFY(near(centre(pickIcon({poor, rich}, 32)), qRgb(0, 0, 255)));
        QVERIFY(near(centre(pickIcon({rich, poor}, 32)), qRgb(0, 0, 255)));
        QCOMPARE(pe.icons(0).first().text(QStringLiteral("bits")), QStringLiteral("32"));
        // "file,1" is the second group; "file,-200" the group with id 200
        QCOMPARE(pe.icons(1).size(), 1);
        QVERIFY(near(centre(pe.icons(1).first()), kRed));
        QCOMPARE(pe.icons(-200).size(), 1);
        QVERIFY(pe.icons(-5).isEmpty());   // no group with id 5
        QVERIFY(pe.icons(7).isEmpty());
    }

    void iconFile()
    {
        const QList<QImage> ico = iconsFromFile(kData + QStringLiteral("app.ico"));
        QCOMPARE(ico.size(), 2);
        QVERIFY(near(centre(pickIcon(ico, 64)), kGreen));
        QVERIFY(near(centre(pickIcon(ico, 16)), kRed));
        QCOMPARE(iconsFromFile(kData + QStringLiteral("tool64.exe"), 1).size(), 1);
        QVERIFY(iconsFromFile(kData + QStringLiteral("missing.ico")).isEmpty());
    }

    void signer()
    {
        PeFile pe;
        QVERIFY(pe.load(kData + QStringLiteral("tool64.exe")));
        QVERIFY(!pe.signature().isEmpty());
        // found by issuer and serial: not the first certificate in the block
        QCOMPARE(pe.signerName(), QStringLiteral("Raye Test Publisher, Inc."));
        PeFile unsigned32;
        QVERIFY(unsigned32.load(kData + QStringLiteral("tool32.exe")));
        QVERIFY(unsigned32.signature().isEmpty());
        QVERIFY(unsigned32.signerName().isEmpty());
        const QByteArray sig = pe.signature();
        for (int cut = 0; cut < sig.size(); cut += 7)
            QVERIFY(pkcs7SignerName(sig.left(cut)).isEmpty());
    }

    void signatureIsChecked()
    {
        // the fixture's signature is well formed but signs nothing
        QVERIFY(!signatureVerified(kData + QStringLiteral("tool64.exe")));
        QVERIFY(!signatureVerified(kData + QStringLiteral("tool32.exe")));
    }

    void brokenPrograms()
    {
        PeFile pe;
        QVERIFY(!pe.loadData(QByteArray()));
        QVERIFY(!pe.loadData(QByteArray(4096, 'x')));
        QVERIFY(!pe.loadData(QByteArray("MZ") + QByteArray(100, '\0')));
        QVERIFY(!pe.load(kData + QStringLiteral("nothing-here.exe")));
        QVERIFY(pe.load(kData + QStringLiteral("bare.exe")));
        QVERIFY(pe.icons(0).isEmpty());
        QVERIFY(pe.version().productName.isEmpty());
        QVERIFY(pe.signerName().isEmpty());
        PeFile truncated;
        if (truncated.load(kData + QStringLiteral("truncated.exe"))) {
            QVERIFY(truncated.icons(0).isEmpty());
            QVERIFY(truncated.version().productName.isEmpty());
        }
    }

    void damagedProgramsDoNotCrash()
    {
        const QByteArray good = fixture(QStringLiteral("tool64.exe"));
        // broken pictures make the image readers complain: not news here
        const QtMessageHandler previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
        QRandomGenerator rng(20261031);
        for (int round = 0; round < 1500; ++round) {
            QByteArray bad = good;
            const int flips = 1 + int(rng.bounded(8));
            for (int i = 0; i < flips; ++i) {
                // mostly in the headers and the resource tree, where it hurts
                const int at = rng.bounded(4) ? int(rng.bounded(qMin<qsizetype>(bad.size(), 0x600))) : int(rng.bounded(bad.size()));
                bad[at] = char(rng.bounded(256));
            }
            if (rng.bounded(5) == 0)
                bad.truncate(rng.bounded(bad.size()));
            PeFile pe;
            if (pe.loadData(bad)) {
                pe.version();
                for (int g = -2; g < 3; ++g)
                    pe.icons(g);
                pe.signerName();
            }
        }
        qInstallMessageHandler(previous);
    }

    // --- shortcuts ------------------------------------------------------

    void shortcutUnicode()
    {
        const Lnk l = readLnkFile(kData + QStringLiteral("unicode.lnk"));
        QVERIFY(l.valid);
        QCOMPARE(l.target, QStringLiteral("C:\\Program Files\\Ünïcødé 工具\\tool.exe"));
        QCOMPARE(l.arguments, QStringLiteral("--start \"a b\""));
        QCOMPARE(l.workingDir, QStringLiteral("C:\\Program Files\\Ünïcødé 工具"));
        QCOMPARE(l.iconLocation, QStringLiteral("C:\\Program Files\\Raye Test\\tool.exe"));
        QCOMPARE(l.iconIndex, 1);
        QCOMPARE(l.description, QStringLiteral("Ünïcødé tool"));
    }

    void shortcutArgumentsOfOneNul()
    {
        // Roblox Studio's shortcut: arguments counted as one character, a NUL
        QFile f(kData + QStringLiteral("unicode.lnk"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray original = f.readAll();
        const QString args = QStringLiteral("--start \"a b\"");
        QByteArray counted(2, '\0');
        counted[0] = char(args.size());
        counted.append(reinterpret_cast<const char *>(args.utf16()), args.size() * 2);
        const int at = original.indexOf(counted);
        QVERIFY(at > 0);
        const QByteArray data = original.left(at) + QByteArray("\x01\x00\x00\x00", 4)
            + original.mid(at + counted.size());
        const Lnk l = readLnk(data);
        QVERIFY(l.valid);
        QVERIFY(l.arguments.isEmpty());
        QCOMPARE(l.iconLocation, QStringLiteral("C:\\Program Files\\Raye Test\\tool.exe"));
    }

    void shortcutFromItemList()
    {
        const Lnk l = readLnkFile(kData + QStringLiteral("idlist.lnk"));
        QVERIFY(l.valid);
        QCOMPARE(l.target, QStringLiteral("C:\\Program Files\\Raye Test\\tool.exe"));
        QVERIFY(l.iconLocation.isEmpty());
    }

    void shortcutWithVariables()
    {
        const Lnk l = readLnkFile(kData + QStringLiteral("env.lnk"));
        QVERIFY(l.valid);
        QCOMPARE(l.target, QStringLiteral("%ProgramFiles%\\Raye Test\\tool.exe"));
        QCOMPARE(l.iconIndex, -200);
        QCOMPARE(expandWindowsVars(l.target, QStringLiteral("raye")), QStringLiteral("C:\\Program Files\\Raye Test\\tool.exe"));
    }

    void shortcutAnsi()
    {
        const Lnk l = readLnkFile(kData + QStringLiteral("ansi.lnk"));
        QVERIFY(l.valid);
        QCOMPARE(l.target, QStringLiteral("C:\\Program Files\\Café\\tool.exe"));
        QCOMPARE(l.description, QStringLiteral("Café tool"));
        QCOMPARE(l.relativePath, QStringLiteral("..\\..\\Café\\tool.exe"));
    }

    void brokenShortcuts()
    {
        QVERIFY(!readLnk(QByteArray()).valid);
        QVERIFY(!readLnk(QByteArray(200, 'L')).valid);
        QVERIFY(!readLnkFile(kData + QStringLiteral("nope.lnk")).valid);
        const QByteArray good = fixture(QStringLiteral("unicode.lnk"));
        for (int cut = 0; cut < good.size(); ++cut)
            readLnk(good.left(cut)); // never reads past the end
        QRandomGenerator rng(7);
        for (const char *name : {"unicode.lnk", "idlist.lnk", "env.lnk", "ansi.lnk"}) {
            const QByteArray base = fixture(QLatin1String(name));
            for (int round = 0; round < 800; ++round) {
                QByteArray bad = base;
                for (int i = 0; i < 4; ++i)
                    bad[rng.bounded(bad.size())] = char(rng.bounded(256));
                readLnk(bad);
            }
        }
    }

    // --- registry -------------------------------------------------------

    void registryValues()
    {
        RegFile system;
        QVERIFY(system.load(kData + QStringLiteral("system.reg")));
        QVERIFY(!system.is32());
        const RegKey *k = system.key(QStringLiteral("software\\microsoft\\windows\\currentversion\\uninstall\\rayetesttool"));
        QVERIFY(k);
        QCOMPARE(k->string(QStringLiteral("displayname")), QStringLiteral("Raye Test Tool"));
        QCOMPARE(k->string(QStringLiteral("UninstallString")), QStringLiteral("\"C:\\Program Files\\Raye Test\\unins000.exe\""));
        QCOMPARE(k->dword(QStringLiteral("EstimatedSize")), 2048u);
        QCOMPARE(k->modified, 1793300000);
        const RegKey *u = system.key(QStringLiteral("Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Unicode Tool_is1"));
        QVERIFY(u);
        QCOMPARE(u->string(QStringLiteral("DisplayName")), QStringLiteral("Ünïcødé 工具 1.0"));
        QCOMPARE(u->values.value(QStringLiteral("installlocation")).type, RegValue::ExpandString);
        QCOMPARE(u->string(QStringLiteral("InstallLocation")), QStringLiteral("%ProgramFiles%\\Ünïcødé 工具\\"));
        const RegKey *b = system.key(QStringLiteral("Software\\Raye\\Binary"));
        QVERIFY(b);
        QCOMPARE(b->values.value(QStringLiteral("blob")).bytes.size(), 19);
        QCOMPARE(b->string(QStringLiteral("After")), QStringLiteral("ok"));
        RegFile user;
        QVERIFY(user.load(kData + QStringLiteral("user.reg")));
        const RegKey *c = user.key(QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Chatty"));
        QVERIFY(c);
        QCOMPARE(c->string(QStringLiteral("Comments")), QStringLiteral("Line one\nquote \" and back\\slash"));
        QVERIFY(!RegFile().parse("REGEDIT4\n[x]\n"));
    }

    void uninstallEntriesBothViews()
    {
        RegFile system, user;
        QVERIFY(system.load(kData + QStringLiteral("system.reg")));
        QVERIFY(user.load(kData + QStringLiteral("user.reg")));
        QHash<QString, UninstallEntry> byKey;
        for (const UninstallEntry &e : uninstallEntries(system, user))
            byKey.insert(e.key, e);
        const UninstallEntry tool = byKey.value(QStringLiteral("RayeTestTool"));
        QCOMPARE(tool.view, QStringLiteral("64"));
        QVERIFY(tool.visible);
        QCOMPARE(tool.publisher, QStringLiteral("Raye Test Publisher"));
        QCOMPARE(tool.installDate, QStringLiteral("2026-10-29"));
        QCOMPARE(tool.estimatedSize, 2048 * 1024);
        QCOMPARE(uninstallCommand(tool), QStringLiteral("\"C:\\Program Files\\Raye Test\\unins000.exe\""));
        const UninstallEntry uni = byKey.value(QStringLiteral("Unicode Tool_is1"));
        QCOMPARE(uni.view, QStringLiteral("32"));
        QVERIFY(uni.visible);
        QCOMPARE(uninstallCommand(uni), QStringLiteral("\"C:\\Program Files\\Ünïcødé 工具\\unins000.exe\" /SILENT"));
        const UninstallEntry msi = byKey.value(QStringLiteral("{11111111-2222-3333-4444-555555555555}"));
        QVERIFY(msi.windowsInstaller);
        QCOMPARE(uninstallCommand(msi), QStringLiteral("MsiExec.exe /X{11111111-2222-3333-4444-555555555555} /qn"));
        QVERIFY(!byKey.value(QStringLiteral("HiddenPart")).visible);
        QVERIFY(!byKey.value(QStringLiteral("KB123456")).visible);
        QVERIFY(!byKey.value(QStringLiteral("NoName")).visible);
        QVERIFY(byKey.value(QStringLiteral("Sticky")).noRemove);
        const UninstallEntry chatty = byKey.value(QStringLiteral("Chatty"));
        QCOMPARE(chatty.view, QStringLiteral("user"));
        // from the key's time, as a local date
        QCOMPARE(chatty.installDate, QDateTime::fromSecsSinceEpoch(1793330000).date().toString(Qt::ISODate));
    }

    void thirtyTwoBitSpace()
    {
        RegFile system;
        QVERIFY(system.parse("WINE REGISTRY Version 2\n#arch=win32\n\n"
                             "[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\Old] 1700000000\n"
                             "\"DisplayName\"=\"Old \\x4e2d\\x6587\"\n\"UninstallString\"=\"C:\\\\old\\\\un.exe\"\n"));
        QVERIFY(system.is32());
        const QList<UninstallEntry> entries = uninstallEntries(system, RegFile());
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().view, QStringLiteral("32"));
        QCOMPARE(entries.first().name, QStringLiteral("Old 中文"));
    }

    // --- spaces ---------------------------------------------------------

    void spacesFound()
    {
        QStringList slugs;
        for (const Space &s : listSpaces(m_root))
            slugs.append(s.slug);
        QCOMPARE(slugs, (QStringList{QStringLiteral("raye-test"), QStringLiteral("solo")}));
        Space s;
        QVERIFY(findSpace(m_root, QStringLiteral("solo"), &s));
        QVERIFY(!findSpace(m_root, QStringLiteral("../solo"), &s));
        QVERIFY(!findSpace(m_root, QStringLiteral("linked"), &s));
        QVERIFY(isValidSlug(QStringLiteral("notepad-plus-plus")));
        QVERIFY(!isValidSlug(QStringLiteral(".template")));
        QVERIFY(!isValidSlug(QStringLiteral("a/b")));
        QVERIFY(!isValidSlug(QString()));
    }

    void windowsPaths()
    {
        Space s;
        QVERIFY(findSpace(m_root, QStringLiteral("raye-test"), &s));
        QCOMPARE(windowsUser(s), QStringLiteral("raye"));
        const QString tool = s.path + QStringLiteral("/drive_c/Program Files/Raye Test/tool.exe");
        QCOMPARE(toUnixPath(s, QStringLiteral("C:\\Program Files\\Raye Test\\tool.exe")), tool);
        QCOMPARE(toUnixPath(s, QStringLiteral("c:\\PROGRAM FILES\\raye test\\TOOL.EXE")), tool);
        QCOMPARE(toUnixPath(s, QStringLiteral("\"%ProgramFiles%\\Raye Test\\tool.exe\"")), tool);
        QCOMPARE(toUnixPath(s, QStringLiteral("C:/Program Files/Raye Test/tool.exe")), tool);
        QVERIFY(toUnixPath(s, QStringLiteral("C:\\Program Files\\..\\..\\system.reg")).isEmpty());
        QVERIFY(toUnixPath(s, QStringLiteral("Z:\\etc\\passwd")).isEmpty());
        QVERIFY(toUnixPath(s, QStringLiteral("\\\\server\\share\\x.exe")).isEmpty());
        QVERIFY(toUnixPath(s, QStringLiteral("C:\\nowhere\\x.exe")).isEmpty());
        QCOMPARE(expandWindowsVars(QStringLiteral("%APPDATA%\\x;%NOPE%;%LocalAppData%"), QStringLiteral("raye")),
                 QStringLiteral("C:\\users\\raye\\AppData\\Roaming\\x;%NOPE%;C:\\users\\raye\\AppData\\Local"));
        QCOMPARE(commandProgram(QStringLiteral("\"C:\\a b\\un.exe\" /S")), QStringLiteral("C:\\a b\\un.exe"));
        QCOMPARE(commandProgram(QStringLiteral("C:\\a b\\un.exe /S")), QStringLiteral("C:\\a b\\un.exe"));
        QCOMPARE(commandProgram(QStringLiteral("MsiExec.exe /X{1}")), QStringLiteral("MsiExec.exe"));
    }

    void diskUsageStaysInside()
    {
        Space s;
        QVERIFY(findSpace(m_root, QStringLiteral("raye-test"), &s));
        const qint64 used = diskUsage(s.path);
        QVERIFY(used > 0);
        QVERIFY2(used < 10 * 1024 * 1024, "followed dosdevices/z: out of the space");
        // a hard link is counted once
        const QString dir = m_tmp.path() + QStringLiteral("/hard");
        put(dir + QStringLiteral("/a"), QByteArray(64 * 1024, 'a'));
        const qint64 one = diskUsage(dir);
        QCOMPARE(::link(QFile::encodeName(dir + QStringLiteral("/a")).constData(),
                        QFile::encodeName(dir + QStringLiteral("/b")).constData()), 0);
        QCOMPARE(diskUsage(dir), one);
        QCOMPARE(diskUsage(m_tmp.path() + QStringLiteral("/nothing")), 0);
    }

    void programsWithoutTheReadySpace()
    {
        Space s;
        QVERIFY(findSpace(m_root, QStringLiteral("raye-test"), &s));
        QStringList names;
        for (const UninstallEntry &e : spacePrograms(s, m_root))
            names.append(e.name);
        names.sort();
        QCOMPARE(names, (QStringList{QStringLiteral("Chatty"), QStringLiteral("Raye MSI Thing"),
                                     QStringLiteral("Raye Test Tool"), QStringLiteral("Sticky Driver"),
                                     QStringLiteral("Ünïcødé 工具 1.0")}));
    }

    // --- entries --------------------------------------------------------

    void execQuoting()
    {
        const QStringList args = {QStringLiteral("raye-run-windows"), QStringLiteral("--run"),
                                  QStringLiteral("C:\\Program Files\\Sid's \"Game\"\\100% $HOME `x`.exe"),
                                  QStringLiteral("plain"), QString(), QStringLiteral("a\\b")};
        QStringList words;
        for (const QString &a : args)
            words.append(desktopExecArg(a));
        QCOMPARE(execWords(words.join(QLatin1Char(' '))), args);
        QCOMPARE(desktopExecArg(QStringLiteral("plain")), QStringLiteral("plain"));
    }

    void scanWritesEntries()
    {
        Space s;
        QVERIFY(findSpace(m_root, QStringLiteral("raye-test"), &s));
        const ScanResult r = scanSpace(s, m_paths);
        QStringList names;
        for (const Entry &e : r.added)
            names.append(e.name);
        names.sort();
        // no uninstaller, no readme, no web link, one Raye Test Tool
        QCOMPARE(names, (QStringList{QStringLiteral("Café tool"), QStringLiteral("Chatty"),
                                     QStringLiteral("Raye Test Tool"), QStringLiteral("Ünïcødé tool")}));
        QVERIFY(r.removed.isEmpty());

        const QString tool = m_paths.applications + QStringLiteral("/raye-win-raye-test-raye-test-tool.desktop");
        const QHash<QString, QString> k = desktopKeys(tool);
        QCOMPARE(k.value(QStringLiteral("Name")), QStringLiteral("Raye Test Tool"));
        QCOMPARE(k.value(QStringLiteral("Icon")), QStringLiteral("raye-win-raye-test-raye-test-tool"));
        QCOMPARE(k.value(QStringLiteral("StartupWMClass")), QStringLiteral("tool.exe"));
        QCOMPARE(k.value(QStringLiteral("X-Raye-Windows-Space")), QStringLiteral("raye-test"));
        QCOMPARE(k.value(QStringLiteral("Type")), QStringLiteral("Application"));
        QCOMPARE(execWords(k.value(QStringLiteral("Exec"))),
                 (QStringList{QStringLiteral("raye-run-windows"), QStringLiteral("--space"), QStringLiteral("raye-test"),
                              QStringLiteral("--run"), QStringLiteral("C:\\Program Files\\Raye Test\\tool.exe")}));
        QCOMPARE(QFileInfo(tool).permissions() & QFileDevice::ExeOwner, QFileDevice::Permissions());

        // icons: 256 from the program's own picture, down to 32
        for (int size : {256, 64, 48, 32}) {
            const QImage img(QStringLiteral("%1/%2x%2/apps/raye-win-raye-test-raye-test-tool.png").arg(m_paths.icons).arg(size));
            QCOMPARE(img.size(), QSize(size, size));
        }
        QVERIFY(near(centre(QImage(m_paths.icons + QStringLiteral("/48x48/apps/raye-win-raye-test-raye-test-tool.png"))), kGreen));

        // unicode path, arguments, working folder, icon from another file (index 1)
        const QHash<QString, QString> u = desktopKeys(m_paths.applications + QStringLiteral("/raye-win-raye-test-unicode-tool.desktop"));
        QCOMPARE(execWords(u.value(QStringLiteral("Exec"))).mid(4),
                 (QStringList{QStringLiteral("C:\\Program Files\\Ünïcødé 工具\\tool.exe"),
                              QStringLiteral("--args"), QStringLiteral("--start \"a b\"")}));
        QCOMPARE(u.value(QStringLiteral("Path")), s.path + QStringLiteral("/drive_c/Program Files/Ünïcødé 工具"));
        QCOMPARE(u.value(QStringLiteral("Comment")), QStringLiteral("Ünïcødé tool"));
        const QString red = m_paths.icons + QStringLiteral("/32x32/apps/raye-win-raye-test-unicode-tool.png");
        QVERIFY(near(centre(QImage(red)), kRed));
        QVERIFY(!QFileInfo::exists(m_paths.icons + QStringLiteral("/256x256/apps/raye-win-raye-test-unicode-tool.png")));

        // an .ico named by the shortcut; a category from the folder
        const QHash<QString, QString> c = desktopKeys(m_paths.applications + QStringLiteral("/raye-win-raye-test-chatty.desktop"));
        QCOMPARE(c.value(QStringLiteral("Categories")), QStringLiteral("Network;"));
        QVERIFY(near(centre(QImage(m_paths.icons + QStringLiteral("/64x64/apps/raye-win-raye-test-chatty.png"))), kGreen));

        // the installer put Chatty on its desktop: a launcher on the user's
        const QString launcher = m_paths.desktop + QStringLiteral("/raye-win-raye-test-chatty.desktop");
        QVERIFY(QFileInfo::exists(launcher));
        QVERIFY(QFileInfo(launcher).permissions() & QFileDevice::ExeOwner);
        QCOMPARE(QDir(m_paths.desktop).entryList(QDir::Files).size(), 1);

        // nothing for the Wine pieces, nothing outside our names
        for (const QString &f : QDir(m_paths.applications).entryList(QDir::Files))
            QVERIFY2(f.startsWith(QLatin1String("raye-win-raye-test-")), qPrintable(f));
    }

    void rescanChangesNothing()
    {
        Space s;
        QVERIFY(findSpace(m_root, QStringLiteral("raye-test"), &s));
        const QString tool = m_paths.applications + QStringLiteral("/raye-win-raye-test-raye-test-tool.desktop");
        const QDateTime before = QFileInfo(tool).lastModified();
        QTest::qWait(20);
        const ScanResult r = scanSpace(s, m_paths);
        QVERIFY(r.added.isEmpty());
        QVERIFY(r.removed.isEmpty());
        QCOMPARE(r.kept.size(), 4);
        QCOMPARE(QFileInfo(tool).lastModified(), before);
        QCOMPARE(spaceEntries(QStringLiteral("raye-test"), m_paths).size(), 4);
    }

    void deletedLauncherStaysDeleted()
    {
        Space s;
        QVERIFY(findSpace(m_root, QStringLiteral("raye-test"), &s));
        const QString launcher = m_paths.desktop + QStringLiteral("/raye-win-raye-test-chatty.desktop");
        QVERIFY(QFile::remove(launcher));
        scanSpace(s, m_paths);
        QVERIFY(!QFileInfo::exists(launcher));
    }

    void goneShortcutRemovesTheEntry()
    {
        Space s;
        QVERIFY(findSpace(m_root, QStringLiteral("raye-test"), &s));
        QVERIFY(QFile::remove(s.path + QLatin1Char('/') + kUserMenu + QStringLiteral("/Social/Chatty.lnk")));
        QVERIFY(QFile::remove(s.path + QStringLiteral("/drive_c/users/raye/Desktop/Chatty.lnk")));
        const ScanResult r = scanSpace(s, m_paths);
        QCOMPARE(r.removed.size(), 1);
        QCOMPARE(r.removed.first().name, QStringLiteral("Chatty"));
        QVERIFY(!QFileInfo::exists(m_paths.applications + QStringLiteral("/raye-win-raye-test-chatty.desktop")));
        QVERIFY(!QFileInfo::exists(m_paths.icons + QStringLiteral("/64x64/apps/raye-win-raye-test-chatty.png")));
        // reinstalled: the desktop launcher comes back
        put(s.path + QLatin1Char('/') + kUserMenu + QStringLiteral("/Social/Chatty.lnk"), fixture(QStringLiteral("chatty.lnk")));
        put(s.path + QStringLiteral("/drive_c/users/raye/Desktop/Chatty.lnk"), fixture(QStringLiteral("chatty.lnk")));
        QCOMPARE(scanSpace(s, m_paths).added.size(), 1);
        QVERIFY(QFileInfo::exists(m_paths.desktop + QStringLiteral("/raye-win-raye-test-chatty.desktop")));
    }

    void missingIconAndForeignDesktop()
    {
        Space s;
        QVERIFY(findSpace(m_root, QStringLiteral("solo"), &s));
        const ScanResult r = scanSpace(s, m_paths);
        QCOMPARE(r.added.size(), 1);
        // the shortcut names an icon that is not there: the program's own
        QVERIFY(QFileInfo::exists(m_paths.icons + QStringLiteral("/256x256/apps/raye-win-solo-raye-test-tool.png")));
        // its Desktop is the user's own folder: never read as the space's
        QVERIFY(ownLaunchers(QStringLiteral("solo")).isEmpty());
    }

    void programsList()
    {
        const QList<Program> programs = listPrograms(m_root, m_paths);
        QHash<QString, Program> byName;
        for (const Program &p : programs)
            byName.insert(p.space + QLatin1Char('/') + p.entry.key, p);
        QCOMPARE(programs.size(), 6);
        const Program tool = byName.value(QStringLiteral("raye-test/RayeTestTool"));
        QCOMPARE(tool.desktopFiles, (QStringList{m_paths.applications + QStringLiteral("/raye-win-raye-test-raye-test-tool.desktop")}));
        QCOMPARE(tool.icon, m_paths.icons + QStringLiteral("/64x64/apps/raye-win-raye-test-raye-test-tool.png"));
        QVERIFY(tool.size > 0);
        const Program chatty = byName.value(QStringLiteral("raye-test/Chatty"));
        QCOMPARE(chatty.desktopFiles.size(), 1);
        const Program msi = byName.value(QStringLiteral("raye-test/{11111111-2222-3333-4444-555555555555}"));
        QVERIFY(msi.desktopFiles.isEmpty());
        QCOMPARE(msi.size, -1);
        const Program solo = byName.value(QStringLiteral("solo/RayeTestTool"));
        Space s;
        QVERIFY(findSpace(m_root, QStringLiteral("solo"), &s));
        QCOMPARE(solo.size, diskUsage(s.path)); // the whole space goes with it
        QCOMPARE(solo.desktopFiles.size(), 1);
        QCOMPARE(listPrograms(m_root, m_paths, QStringLiteral("solo")).size(), 1);
        QCOMPARE(programs.first().entry.name, QStringLiteral("Chatty")); // by name
    }

    void uninstallKeepsWhatTheUninstallerKept()
    {
        const QString log = m_tmp.path() + QStringLiteral("/runner.log");
        EntryPaths paths = m_paths;
        paths.runner = fakeRunner();
        qputenv("FAKE_LOG", QFile::encodeName(log));
        qputenv("FAKE_MODE", "cancel");
        QString name;
        QCOMPARE(uninstallProgram(m_root, QStringLiteral("solo"), QStringLiteral("raye" "testtool"), paths, &name),
                 Uninstall::StillInstalled);
        QCOMPARE(name, QStringLiteral("Raye Test Tool"));
        QFile f(log);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList args = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(args.mid(0, 6), (QStringList{QStringLiteral("--space"), QStringLiteral("solo"), QStringLiteral("--uninstall"),
                                              QStringLiteral("\"C:\\Program Files\\Raye Test\\unins000.exe\""),
                                              QStringLiteral("--name"), QStringLiteral("Raye Test Tool")}));
        QCOMPARE(args.value(6), QStringLiteral("--icon"));
        QVERIFY(QFileInfo::exists(m_root + QStringLiteral("/solo/system.reg")));
        QCOMPARE(spaceEntries(QStringLiteral("solo"), m_paths).size(), 1);
    }

    void uninstallRefusals()
    {
        EntryPaths paths = m_paths;
        paths.runner = fakeRunner();
        QCOMPARE(uninstallProgram(m_root, QStringLiteral("raye-test"), QStringLiteral("Sticky"), paths), Uninstall::NotRemovable);
        QCOMPARE(uninstallProgram(m_root, QStringLiteral("raye-test"), QStringLiteral("HiddenPart"), paths), Uninstall::NotFound);
        QCOMPARE(uninstallProgram(m_root, QStringLiteral("../raye-test"), QStringLiteral("Sticky"), paths), Uninstall::NotFound);
        paths.runner = m_tmp.path() + QStringLiteral("/no-runner");
        QCOMPARE(uninstallProgram(m_root, QStringLiteral("raye-test"), QStringLiteral("RayeTestTool"), paths), Uninstall::RunnerFailed);
    }

    void uninstallLastProgramRemovesTheSpace()
    {
        EntryPaths paths = m_paths;
        paths.runner = fakeRunner();
        qputenv("FAKE_MODE", "remove");
        qputenv("FAKE_SPACE", QFile::encodeName(m_root + QStringLiteral("/solo")));
        // a link out of the space: the target must survive the removal
        const QString outside = m_tmp.path() + QStringLiteral("/keep-me");
        put(outside + QStringLiteral("/precious"), "x");
        QVERIFY(QFile::link(outside, m_root + QStringLiteral("/solo/drive_c/users/raye/Documents")));
        const QString hidden = m_paths.applications + QStringLiteral("/org.raye.windows.solo.desktop");
        put(hidden, "[Desktop Entry]\nNoDisplay=true\n");
        QCOMPARE(uninstallProgram(m_root, QStringLiteral("solo"), QStringLiteral("RayeTestTool"), paths), Uninstall::SpaceRemoved);
        QVERIFY(!QFileInfo::exists(m_root + QStringLiteral("/solo")));
        QVERIFY(!QFileInfo::exists(hidden));
        QVERIFY(QFileInfo::exists(outside + QStringLiteral("/precious")));
        QVERIFY(spaceEntries(QStringLiteral("solo"), m_paths).isEmpty());
        QVERIFY(!QFileInfo::exists(m_paths.icons + QStringLiteral("/256x256/apps/raye-win-solo-raye-test-tool.png")));
        // the other space is untouched
        QCOMPARE(spaceEntries(QStringLiteral("raye-test"), m_paths).size(), 4);
        QVERIFY(!removeSpaceFolder(m_root, QStringLiteral("..")));
        QVERIFY(!removeSpaceFolder(m_root, QStringLiteral("linked")));
        QVERIFY(QFileInfo::exists(m_root + QStringLiteral("/raye-test/system.reg")));
    }

    void runnerAppIds()
    {
        QCOMPARE(runnerAppId(QStringLiteral("7-zip")), QStringLiteral("org.raye.windows._7_zip"));
        QCOMPARE(runnerAppId(QStringLiteral("npp-8-9-9-installer-x64")),
                 QStringLiteral("org.raye.windows.npp_8_9_9_installer_x64"));
    }

    // --- names ------------------------------------------------------------

    void names_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("name");
        QTest::newRow("vlc") << "vlc-3.0.21-win64.exe" << "VLC";
        QTest::newRow("npp") << "npp.8.6.9.Installer.x64.exe" << "NPP";
        QTest::newRow("discord") << "DiscordSetup.exe" << "Discord";
        QTest::newRow("steam") << "SteamSetup.exe" << "Steam";
        QTest::newRow("firefox") << "Firefox Setup 131.0.3.exe" << "Firefox";
        QTest::newRow("audacity") << "audacity-win-3.6.4-64bit.exe" << "Audacity";
        QTest::newRow("obs") << "OBS-Studio-30.2.3-Windows-Installer.exe" << "OBS Studio";
        QTest::newRow("gimp") << "gimp-2.10.38-setup.exe" << "Gimp";
        QTest::newRow("msi") << "PowerToysSetup-0.85.1-x64.msi" << "PowerToys";
        QTest::newRow("paint") << "paint.net.5.0.13.install.anycpu.web.zip.exe" << "Paint net";
        QTest::newRow("spotify") << "SpotifySetup.exe" << "Spotify";
        QTest::newRow("irfan") << "iview467_x64_setup.exe" << "Iview467";
        QTest::newRow("winrar") << "winrar-x64-701.exe" << "Winrar";
        QTest::newRow("path") << "/home/u/Downloads/7z2408-x64.exe" << "7z2408";
        QTest::newRow("only noise") << "setup.exe" << "setup";
    }

    void names()
    {
        QFETCH(QString, file);
        QFETCH(QString, name);
        QCOMPARE(nameFromFileName(file), name);
    }

    void programNames()
    {
        PeVersion v;
        v.productName = QStringLiteral("Raye Test Tool");
        QCOMPARE(programName(v, QStringLiteral("x.exe")), QStringLiteral("Raye Test Tool"));
        v.productName = QStringLiteral("Setup");
        v.fileDescription = QStringLiteral("Chatty Web Installer");
        QCOMPARE(programName(v, QStringLiteral("x.exe")), QStringLiteral("Chatty"));
        v.fileDescription = QStringLiteral("Self-extracting archive");
        QCOMPARE(programName(v, QStringLiteral("vlc-3.0.21-win64.exe")), QStringLiteral("VLC"));
    }

    // --- the command ------------------------------------------------------

    void commandLine()
    {
        const QString cli = QCoreApplication::applicationDirPath() + QStringLiteral("/raye-windows");
        if (!QFileInfo(cli).isExecutable())
            QSKIP("raye-windows is not built next to the tests");
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("RAYE_WINDOWS_SPACES"), m_root);
        env.insert(QStringLiteral("XDG_DATA_HOME"), m_tmp.path() + QStringLiteral("/xdg-data"));
        env.insert(QStringLiteral("XDG_CACHE_HOME"), m_tmp.path() + QStringLiteral("/xdg-cache"));
        env.insert(QStringLiteral("HOME"), m_tmp.path() + QStringLiteral("/home"));
        auto run = [&](const QStringList &args, int *code) {
            QProcess p;
            p.setProcessEnvironment(env);
            p.start(cli, args);
            p.waitForFinished(60000);
            *code = p.exitCode();
            return p.readAllStandardOutput();
        };
        int code = -1;
        const QJsonObject info = QJsonDocument::fromJson(run({QStringLiteral("info"), kData + QStringLiteral("tool64.exe")}, &code)).object();
        QCOMPARE(code, 0);
        QCOMPARE(info.value(QStringLiteral("name")).toString(), QStringLiteral("Raye Test Tool"));
        QCOMPARE(info.value(QStringLiteral("signer")).toString(), QStringLiteral("Raye Test Publisher, Inc."));
        QCOMPARE(info.value(QStringLiteral("verified")).toBool(), false);
        QVERIFY(info.value(QStringLiteral("publisher")).isNull()); // not checked, not shown
        QCOMPARE(QImage(info.value(QStringLiteral("icon")).toString()).size(), QSize(64, 64));
        const QJsonObject bare = QJsonDocument::fromJson(run({QStringLiteral("info"), kData + QStringLiteral("app.ico")}, &code)).object();
        QCOMPARE(bare.value(QStringLiteral("program")).toBool(), false);
        QVERIFY(bare.value(QStringLiteral("icon")).isNull());

        const QJsonArray list = QJsonDocument::fromJson(run({QStringLiteral("list"), QStringLiteral("--json")}, &code)).array();
        QCOMPARE(code, 0);
        QVERIFY(list.size() >= 5);
        const QJsonObject first = list.first().toObject();
        for (const char *key : {"space", "key", "name", "publisher", "version", "size", "installDate", "icon", "desktopFiles"})
            QVERIFY2(first.contains(QLatin1String(key)), key);

        const QJsonObject scanned = QJsonDocument::fromJson(
            run({QStringLiteral("scan"), QStringLiteral("raye-test"), QStringLiteral("--json")}, &code)).object();
        QCOMPARE(code, 0);
        QCOMPARE(scanned.value(QStringLiteral("added")).toArray().size(), 4); // into this XDG_DATA_HOME
        QVERIFY(QFileInfo::exists(m_tmp.path() + QStringLiteral("/xdg-data/applications/raye-win-raye-test-chatty.desktop")));

        run({QStringLiteral("scan"), QStringLiteral("nope")}, &code);
        QCOMPARE(code, 3);
        run({QStringLiteral("scan"), QStringLiteral("../x")}, &code);
        QCOMPARE(code, 3);
        run({}, &code);
        QCOMPARE(code, 2);
        run({QStringLiteral("uninstall"), QStringLiteral("raye-test")}, &code);
        QCOMPARE(code, 2);
        run({QStringLiteral("info"), kData + QStringLiteral("nothing.exe")}, &code);
        QCOMPARE(code, 3);
    }

private:
    QStringList ownLaunchers(const QString &slug) const
    {
        return QDir(m_paths.desktop).entryList({QStringLiteral("raye-win-") + slug + QStringLiteral("-*")}, QDir::Files);
    }

    // stands in for raye-run-windows: writes its arguments down; "remove"
    // does what an uninstaller does to the solo space
    QString fakeRunner()
    {
        const QString path = m_tmp.path() + QStringLiteral("/fake-runner");
        if (!QFileInfo::exists(path)) {
            put(path, "#!/bin/sh\n"
                      "[ -n \"$FAKE_LOG\" ] && printf '%s\\n' \"$@\" > \"$FAKE_LOG\"\n"
                      "[ \"$FAKE_MODE\" = remove ] || exit 0\n"
                      "printf 'WINE REGISTRY Version 2\\n#arch=win64\\n' > \"$FAKE_SPACE/system.reg\"\n"
                      "rm -f \"$FAKE_SPACE/drive_c/Program Files/Raye Test/tool.exe\"\n"
                      "rm -f \"$FAKE_SPACE/drive_c/ProgramData/Microsoft/Windows/Start Menu/Programs/Raye Test Tool.lnk\"\n");
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        }
        return path;
    }
};

QTEST_GUILESS_MAIN(WindowsLibTest)
#include "test_windows_lib.moc"
