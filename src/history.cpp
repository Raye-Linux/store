// SPDX-License-Identifier: GPL-3.0-or-later
#include "history.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
const QStringList actions{QStringLiteral("install"), QStringLiteral("remove"), QStringLiteral("update")};
const QStringList results{QStringLiteral("done"), QStringLiteral("failed"), QStringLiteral("cancelled")};
}

QString History::defaultPath()
{
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    // the spec wants an absolute path; anything else means "not set"
    if (state.isEmpty() || !QDir::isAbsolutePath(state))
        state = QDir::homePath() + QStringLiteral("/.local/state");
    return state + QStringLiteral("/raye-store/history.json");
}

History::History(const QString &path, QObject *parent)
    : QAbstractListModel(parent)
    , m_path(path)
{
    m_entries = read();
}

QList<HistoryEntry> History::parse(const QByteArray &json)
{
    QList<HistoryEntry> entries;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    const QJsonArray list = doc.isObject() ? doc.object().value(QLatin1String("entries")).toArray() : doc.array();
    for (const QJsonValue &value : list) {
        const QJsonObject o = value.toObject();
        HistoryEntry e;
        e.appId = o.value(QLatin1String("id")).toString();
        e.name = o.value(QLatin1String("name")).toString();
        e.icon = o.value(QLatin1String("icon")).toString();
        e.ref = o.value(QLatin1String("ref")).toString();
        e.source = o.value(QLatin1String("source")).toString();
        e.action = o.value(QLatin1String("action")).toString();
        e.result = o.value(QLatin1String("result")).toString();
        e.message = o.value(QLatin1String("message")).toString();
        e.time = QDateTime::fromString(o.value(QLatin1String("time")).toString(), Qt::ISODate);
        // a line that does not say what happened, to what, and when, is noise
        if (e.ref.isEmpty() || !actions.contains(e.action) || !results.contains(e.result) || !e.time.isValid())
            continue;
        e.time = e.time.toUTC();
        if (e.name.isEmpty())
            e.name = e.appId.isEmpty() ? e.ref : e.appId;
        entries.append(e);
        if (entries.size() == MaxEntries)
            break;
    }
    return entries;
}

QByteArray History::serialize(const QList<HistoryEntry> &entries)
{
    QJsonArray list;
    for (const HistoryEntry &e : entries) {
        QJsonObject o{
            {QStringLiteral("id"), e.appId},
            {QStringLiteral("name"), e.name},
            {QStringLiteral("ref"), e.ref},
            {QStringLiteral("source"), e.source},
            {QStringLiteral("action"), e.action},
            {QStringLiteral("result"), e.result},
            {QStringLiteral("time"), e.time.toUTC().toString(Qt::ISODate)},
        };
        if (!e.icon.isEmpty())
            o.insert(QStringLiteral("icon"), e.icon);
        if (!e.message.isEmpty())
            o.insert(QStringLiteral("message"), e.message);
        list.append(o);
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("entries"), list}})
        .toJson(QJsonDocument::Indented);
}

QList<HistoryEntry> History::read() const
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    // a history file is small; one far bigger was not written by us
    if (file.size() > 1024 * 1024)
        return {};
    return parse(file.readAll());
}

void History::write(const QList<HistoryEntry> &entries)
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(serialize(entries));
    file.commit();
}

void History::set(QList<HistoryEntry> entries)
{
    if (entries == m_entries)
        return;
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    Q_EMIT countChanged();
}

void History::add(const HistoryEntry &entry)
{
    // start from the file: another Store window may have added lines
    QList<HistoryEntry> entries = read();
    HistoryEntry e = entry;
    if (!e.time.isValid())
        e.time = QDateTime::currentDateTimeUtc();
    e.time = e.time.toUTC();
    entries.prepend(e);
    while (entries.size() > MaxEntries)
        entries.removeLast();
    write(entries);
    set(entries);
}

void History::reload()
{
    set(read());
}

void History::clear()
{
    write({});
    set({});
}

int History::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant History::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const HistoryEntry &e = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return e.name;
    case AppIdRole: return e.appId;
    case IconRole: return e.icon;
    case RefRole: return e.ref;
    case SourceRole: return e.source;
    case ActionRole: return e.action;
    case ResultRole: return e.result;
    case MessageRole: return e.message;
    case TimeRole: return e.time;
    case OkRole: return e.result == QLatin1String("done");
    }
    return {};
}

QHash<int, QByteArray> History::roleNames() const
{
    return {
        {AppIdRole, "appId"}, {NameRole, "name"}, {IconRole, "icon"}, {RefRole, "ref"},
        {SourceRole, "source"}, {ActionRole, "action"}, {ResultRole, "result"},
        {MessageRole, "message"}, {TimeRole, "time"}, {OkRole, "ok"},
    };
}
