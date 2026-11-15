// SPDX-License-Identifier: GPL-3.0-or-later
#include "applink.h"

#include <QRegularExpression>
#include <QUrl>

QString appIdFromLink(const QString &argument)
{
    QString id = argument.trimmed();
    static const QString scheme = QStringLiteral("appstream:");
    if (id.startsWith(scheme, Qt::CaseInsensitive)) {
        id = QUrl::fromPercentEncoding(id.mid(scheme.size()).toUtf8());
        while (id.startsWith(QLatin1Char('/')))
            id.remove(0, 1);
        // "?query" or "#fragment" are not part of the id
        const qsizetype end = id.indexOf(QRegularExpression(QStringLiteral("[?#]")));
        if (end >= 0)
            id.truncate(end);
        while (id.endsWith(QLatin1Char('/')))
            id.chop(1);
    }
    // ids are reverse domain names; never a path, never an option
    static const QRegularExpression valid(
        QRegularExpression::anchoredPattern(QStringLiteral("[A-Za-z0-9][A-Za-z0-9._-]{0,254}")));
    if (!valid.match(id).hasMatch() || id.contains(QLatin1String("..")))
        return {};
    return id;
}

QString appFromArguments(const QStringList &arguments)
{
    for (QString argument : arguments) {
        if (argument.startsWith(QLatin1String("--open=")))
            argument = argument.mid(7);
        if (argument.startsWith(QLatin1String("app/")))
            argument = argument.section(QLatin1Char('/'), 1, 1);
        const QString id = appIdFromLink(argument);
        if (!id.isEmpty())
            return id;
    }
    return {};
}
