// SPDX-License-Identifier: GPL-3.0-or-later
#include "uninstallcommand.h"

#include "applink.h"
#include "installer.h"

#include <KLocalizedString>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

UninstallTarget resolveUninstallTarget(const QString &argument, const QStringList &flatpakRefs,
                                       const std::function<QString(const QString &)> &packageOf)
{
    if (argument.startsWith(QLatin1String("app/"))) {
        if (!Installer::validRef(QStringLiteral("flatpak"), argument))
            return {};
        return {QStringLiteral("flatpak"), argument, argument.section(QLatin1Char('/'), 1, 1)};
    }
    const QString id = appIdFromLink(argument);
    if (id.isEmpty())
        return {};
    const QString bare = id.endsWith(QLatin1String(".desktop")) ? id.chopped(8) : id;
    for (const QString &ref : flatpakRefs) {
        if (ref.section(QLatin1Char('/'), 1, 1) == bare)
            return {QStringLiteral("flatpak"), ref, bare};
    }
    const QString package = packageOf(bare + QLatin1String(".desktop"));
    if (!Installer::validRef(QStringLiteral("apt"), package))
        return {};
    return {QStringLiteral("apt"), package, bare};
}

UninstallTarget resolveUninstallTarget(const QString &argument)
{
    QProcess list;
    list.start(QStringLiteral("flatpak"), {QStringLiteral("list"), QStringLiteral("--app"), QStringLiteral("--columns=ref")});
    list.waitForFinished(20000);
    QStringList refs;
    for (const QString &line : QString::fromUtf8(list.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        refs.append(QStringLiteral("app/") + line.trimmed());
    return resolveUninstallTarget(argument, refs, [](const QString &desktopFile) {
        QProcess owner;
        owner.start(QStringLiteral("dpkg-query"), {QStringLiteral("-S"), QStringLiteral("/usr/share/applications/") + desktopFile});
        owner.waitForFinished(20000);
        // "firefox: /usr/share/applications/firefox.desktop"
        const QString line = QString::fromUtf8(owner.readAllStandardOutput()).section(QLatin1Char('\n'), 0, 0);
        return owner.exitCode() == 0 ? line.section(QLatin1Char(':'), 0, 0).trimmed() : QString();
    });
}

UninstallCommand::UninstallCommand(Installer *installer, QIODevice *out, QObject *parent)
    : QObject(parent)
    , m_installer(installer)
    , m_out(out)
{
    connect(installer, &Installer::revisionMoved, this, [this] {
        if (m_ref.isEmpty())
            return;
        const int percent = m_installer->job(m_ref).value(QStringLiteral("percent"), -1).toInt();
        if (percent > m_percent) {
            m_percent = percent;
            say(QStringLiteral("removing"), percent, {});
        }
    });
    connect(installer, &Installer::finished, this, [this](bool ok, const QString &ref, const QString &message) {
        if (ref != m_ref)
            return;
        m_ref.clear();
        say(ok ? QStringLiteral("done") : QStringLiteral("failed"), ok ? 100 : m_percent, message);
        Q_EMIT done(ok ? 0 : 1);
    });
}

void UninstallCommand::start(const UninstallTarget &target)
{
    const auto fail = [this](const QString &message) {
        say(QStringLiteral("failed"), 0, message);
        // the caller connects done() after start() returns
        QTimer::singleShot(0, this, [this] { Q_EMIT done(1); });
    };
    if (target.source.isEmpty()) {
        fail(i18n("This app isn't on this PC."));
        return;
    }
    m_ref = target.ref;
    say(QStringLiteral("removing"), 0, {});
    const int job = m_installer->uninstall({{QStringLiteral("source"), target.source},
                                            {QStringLiteral("ref"), target.ref},
                                            {QStringLiteral("appId"), target.appId},
                                            {QStringLiteral("name"), target.appId}});
    if (job == 0) {
        m_ref.clear();
        fail(i18n("This app can't be removed here."));
    }
}

void UninstallCommand::say(const QString &state, int percent, const QString &message)
{
    QJsonObject line{{QStringLiteral("state"), state}, {QStringLiteral("percent"), percent}};
    if (!message.isEmpty())
        line.insert(QStringLiteral("message"), message);
    m_out->write(QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
}
