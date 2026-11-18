// SPDX-License-Identifier: GPL-3.0-or-later
#include "alternatives.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

QList<Alternative> parseAlternatives(const QByteArray &json)
{
    static const QRegularExpression appId(QStringLiteral("^[A-Za-z0-9_-]+(\\.[A-Za-z0-9_-]+)+$"));
    QList<Alternative> out;
    const QJsonArray list = QJsonDocument::fromJson(json).object().value(QStringLiteral("alternatives")).toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        Alternative a;
        a.windows = o.value(QStringLiteral("windows")).toString().simplified();
        for (const QJsonValue &w : o.value(QStringLiteral("words")).toArray()) {
            const QString word = w.toString().simplified();
            if (!word.isEmpty())
                a.words.append(word);
        }
        for (const QJsonValue &id : o.value(QStringLiteral("apps")).toArray()) {
            if (appId.match(id.toString()).hasMatch() && !a.apps.contains(id.toString()))
                a.apps.append(id.toString());
        }
        if (!a.windows.isEmpty() && !a.apps.isEmpty())
            out.append(a);
    }
    return out;
}

void applyAlternatives(QHash<QString, App> &byKey, const QList<Alternative> &list)
{
    // Flathub has renamed apps by case alone (org.mozilla.Thunderbird)
    QHash<QString, QString> folded;
    for (auto it = byKey.cbegin(); it != byKey.cend(); ++it)
        folded.insert(it.key().toLower(), it.key());
    for (const Alternative &alt : list) {
        for (const QString &id : alt.apps) {
            auto it = byKey.find(folded.value(appKey(id).toLower()));
            if (it == byKey.end())
                continue;
            if (!it->replaces.contains(alt.windows))
                it->replaces.append(alt.windows);
            for (const QString &word : QStringList{alt.windows} + alt.words) {
                if (!it->keywords.contains(word, Qt::CaseInsensitive))
                    it->keywords.append(word);
            }
        }
    }
}

QStringList alternativePicks(const QList<Alternative> &list)
{
    QStringList out;
    qsizetype longest = 0;
    for (const Alternative &alt : list)
        longest = qMax(longest, alt.apps.size());
    for (qsizetype i = 0; i < longest; ++i) {
        for (const Alternative &alt : list) {
            if (i < alt.apps.size() && !out.contains(alt.apps.at(i)))
                out.append(alt.apps.at(i));
        }
    }
    return out;
}

QString alternativesFile()
{
    const QString env = qEnvironmentVariable("RAYE_STORE_ALTERNATIVES");
    return env.isEmpty() ? QStringLiteral("/usr/share/raye-store/windows-alternatives.json") : env;
}
