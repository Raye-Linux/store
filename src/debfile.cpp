// SPDX-License-Identifier: GPL-3.0-or-later
#include "debfile.h"

#include "installer.h"

#include <KLocalizedString>
#include <QDir>
#include <QJsonArray>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

DebFile::DebFile(const QString &path, const QStringList &helper, const QStringList &privileged, QObject *parent)
    : QObject(parent)
    , m_helper(helper.isEmpty() ? QStringList{QStringLiteral("/usr/libexec/raye-store-extra")} : helper)
    , m_privileged(privileged.isEmpty() ? QStringList{QStringLiteral("pkexec"), QStringLiteral("/usr/libexec/raye-store-extra")}
                                        : privileged)
{
    // the file itself, not a link to it; the helper refuses links
    const QFileInfo file(path);
    m_path = file.canonicalFilePath().isEmpty() ? QDir::cleanPath(file.absoluteFilePath()) : file.canonicalFilePath();

    connect(&m_reader, &QProcess::finished, this, [this](int code) {
        readInfo(code);
    });
    connect(&m_reader, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            fail(i18n("We can't open this file"), reasonText(QStringLiteral("unknown")));
    });
    connect(&m_installer, &QProcess::readyReadStandardOutput, this, &DebFile::readLines);
    connect(&m_installer, &QProcess::finished, this, &DebFile::installEnded);
    connect(&m_installer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            fail(i18n("We couldn't install %1", m_name), reasonText(QStringLiteral("unknown")));
    });
}

QString DebFile::fileName() const
{
    return QFileInfo(m_path).fileName();
}

void DebFile::load()
{
    m_state = Reading;
    Q_EMIT changed();
    m_reader.start(m_helper.first(), m_helper.mid(1) + QStringList{QStringLiteral("info"), m_path, m_icons.path()});
}

void DebFile::readInfo(int code)
{
    Q_UNUSED(code)
    const QJsonObject info = QJsonDocument::fromJson(m_reader.readAllStandardOutput().trimmed()).object();
    if (!info.value(QStringLiteral("ok")).toBool()) {
        const QString error = info.value(QStringLiteral("error")).toString(QStringLiteral("unknown"));
        fail(i18n("We can't open this file"), reasonText(error));
        return;
    }
    m_package = info.value(QStringLiteral("package")).toString();
    // what root installs must be the file shown here
    m_sha256 = info.value(QStringLiteral("sha256")).toString();
    m_version = info.value(QStringLiteral("version")).toString();
    m_installed = info.value(QStringLiteral("installed")).toString();
    m_summary = info.value(QStringLiteral("summary")).toString();
    m_publisher = publisherName(info.value(QStringLiteral("maintainer")).toString());
    m_name = pickName(info.value(QStringLiteral("names")).toObject());
    if (m_name.isEmpty())
        m_name = m_package;
    const QString icon = info.value(QStringLiteral("icon")).toString();
    m_icon = icon.isEmpty() ? QUrl() : QUrl::fromLocalFile(icon);
    const qint64 installedSize = info.value(QStringLiteral("installedSize")).toInteger();
    m_sizeText = Installer::progressText(installedSize > 0 ? installedSize : info.value(QStringLiteral("fileSize")).toInteger(), 0);

    // already installed: Open works before anything is done
    const QString launcher = info.value(QStringLiteral("launchers")).toArray().first().toString();
    m_launcher = launcher.startsWith(QLatin1String("/usr/share/applications/")) ? launcher : QString();

    const QJsonValue newer = info.value(QStringLiteral("newer"));
    if (m_installed.isEmpty() || newer.isNull())
        m_relation = NotInstalled;
    else if (newer.toInt() > 0)
        m_relation = UpdateAvailable;
    else if (newer.toInt() == 0)
        m_relation = AlreadyInstalled;
    else
        m_relation = NewerInstalled;

    if (!info.value(QStringLiteral("archOk")).toBool()) {
        fail(i18n("This app can't be installed"),
             reasonText(QStringLiteral("architecture"), info.value(QStringLiteral("architecture")).toString()));
        return;
    }
    m_state = Ready;
    Q_EMIT changed();
}

void DebFile::install()
{
    if (m_state != Ready || (m_relation != NotInstalled && m_relation != UpdateAvailable))
        return;
    m_state = Installing;
    m_percent = -1;
    m_phaseText = i18n("Waiting for your password…");
    m_errorTitle.clear();
    m_errorText.clear();
    m_details.clear();
    m_reason.clear();
    m_reasonWhat.clear();
    m_stderr.clear();
    m_buffer.clear();
    m_launcher.clear();
    Q_EMIT changed();
    m_installer.start(m_privileged.first(), m_privileged.mid(1) + QStringList{QStringLiteral("install-file"), m_path, m_sha256});
}

void DebFile::readLines()
{
    m_buffer += m_installer.readAllStandardOutput();
    int nl;
    bool moved = false;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_buffer.left(nl)).trimmed();
        m_buffer.remove(0, nl + 1);
        const QString word = line.section(QLatin1Char(' '), 0, 0);
        const QString rest = line.section(QLatin1Char(' '), 1);
        if (word == QLatin1String("phase")) {
            m_percent = -1;
            if (rest == QLatin1String("preparing"))
                m_phaseText = i18n("Getting ready…");
            else if (rest == QLatin1String("downloading"))
                m_phaseText = i18n("Downloading what it needs…");
            else
                m_phaseText = i18n("Installing…");
            moved = true;
        } else if (word == QLatin1String("progress")) {
            bool number = false;
            const int percent = rest.toInt(&number);
            if (number) {
                m_percent = std::clamp(percent, 0, 100);
                moved = true;
            }
        } else if (word == QLatin1String("launcher")) {
            if (m_launcher.isEmpty() && rest.startsWith(QLatin1String("/usr/share/applications/")))
                m_launcher = rest;
        } else if (word == QLatin1String("reason")) {
            m_reason = rest.section(QLatin1Char(' '), 0, 0);
            m_reasonWhat = rest.section(QLatin1Char(' '), 1);
        }
    }
    if (m_buffer.size() > 64 * 1024)
        m_buffer.clear();
    if (moved)
        Q_EMIT changed();
}

void DebFile::installEnded(int code, QProcess::ExitStatus status)
{
    readLines();
    const QStringList lines = QString::fromUtf8(m_installer.readAllStandardError()).trimmed().split(QLatin1Char('\n'));
    m_details = lines.mid(std::max<qsizetype>(0, lines.size() - 20)).join(QLatin1Char('\n'));
    if (status == QProcess::NormalExit && code == 0) {
        m_state = Done;
        m_relation = AlreadyInstalled;
        m_percent = 100;
        Q_EMIT changed();
        return;
    }
    // pkexec: 126 the password window was closed, 127 not allowed
    if (status == QProcess::NormalExit && code == 126 && m_reason.isEmpty()) {
        m_state = Ready;
        Q_EMIT changed();
        return;
    }
    const QString text = status == QProcess::NormalExit && code == 127 && m_reason.isEmpty()
        ? Installer::reasonText(Installer::ReasonNotAllowed)
        : reasonText(m_reason.isEmpty() ? QStringLiteral("unknown") : m_reason, m_reasonWhat);
    fail(i18n("We couldn't install %1", m_name), text);
}

void DebFile::fail(const QString &title, const QString &text)
{
    m_state = Failed;
    m_errorTitle = title;
    m_errorText = text;
    Q_EMIT changed();
}

bool DebFile::open()
{
    if (m_launcher.isEmpty())
        return false;
    // kstart waits for a window it may never see, and does not bring it up
    return QProcess::startDetached(QStringLiteral("kioclient"), {QStringLiteral("exec"), m_launcher});
}

QString DebFile::reasonText(const QString &code, const QString &what)
{
    if (code == QLatin1String("badpath") || code == QLatin1String("notfound"))
        return i18n("We can't find this file. It may have been moved or deleted.");
    if (code == QLatin1String("noaccess"))
        return i18n("You don't have permission to open this file.");
    if (code == QLatin1String("notfile") || code == QLatin1String("notdeb"))
        return i18n("This file isn't an app package.");
    if (code == QLatin1String("broken"))
        return i18n("This file is damaged and can't be installed. Try downloading it again.");
    if (code == QLatin1String("toolarge"))
        return i18n("This file is too large to be an app package.");
    if (code == QLatin1String("architecture"))
        return what.isEmpty() ? i18n("This app is made for a different kind of processor and can't run on this PC.")
                              : i18n("This app is made for a different kind of processor (%1) and can't run on this PC.", what);
    if (code == QLatin1String("dependencies"))
        return what.isEmpty() ? i18n("This app needs other parts that Raye can't find. It may be made for a different system.")
                              : i18n("This app needs %1, which Raye can't find. It may be made for a different system.", what);
    if (code == QLatin1String("conflict"))
        return what.isEmpty() ? i18n("This app can't be installed alongside an app that's already on this PC.")
                              : i18n("This app can't be installed alongside %1, which is already on this PC.", what);
    if (code == QLatin1String("changed"))
        return i18n("This file changed after it was opened. Open it again to install it.");
    if (code == QLatin1String("system"))
        return i18n("This file would replace a part of Raye itself, so it can't be installed this way.");
    if (code == QLatin1String("older"))
        return i18n("A newer version of this app is already installed.");
    if (code == QLatin1String("busy"))
        return i18n("Another install or update is running. Try again when it's done.");
    if (code == QLatin1String("space"))
        return i18n("There isn't enough free space on this PC.");
    if (code == QLatin1String("network"))
        return i18n("We couldn't download what this app needs. Check your internet connection and try again.");
    if (code == QLatin1String("setup"))
        return i18n("The app's own setup didn't finish. Try again, or ask whoever made it.");
    return i18n("Something went wrong while installing this app.");
}

QString DebFile::pickName(const QJsonObject &names, const QLocale &locale)
{
    // fr_CA, then fr, then the name with no language
    const QString full = locale.name();
    const QString language = full.section(QLatin1Char('_'), 0, 0);
    for (const QString &key : {full, language, QString()}) {
        const QString name = names.value(key).toString().trimmed();
        if (!name.isEmpty())
            return name;
    }
    return {};
}

QString DebFile::publisherName(const QString &maintainer)
{
    QString name = maintainer;
    name.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    name = name.trimmed();
    if (name.size() >= 2 && name.startsWith(QLatin1Char('"')) && name.endsWith(QLatin1Char('"')))
        name = name.mid(1, name.size() - 2).trimmed();
    return name;
}
