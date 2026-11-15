// SPDX-License-Identifier: GPL-3.0-or-later
// What the Store did lately ("Recent activity"): one line per finished
// job, newest first, kept in ~/.local/state/raye-store/history.json.
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>

struct HistoryEntry {
    QString appId;
    QString name;
    QString icon;
    QString ref;
    QString source;   // "flatpak", "apt" or "raye"
    QString action;   // "install", "remove" or "update"
    QString result;   // "done", "failed" or "cancelled"
    QString message;  // why it failed, in plain words
    QDateTime time;   // UTC

    bool operator==(const HistoryEntry &other) const = default;
};

class History : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        AppIdRole = Qt::UserRole + 1, NameRole, IconRole, RefRole, SourceRole, ActionRole,
        ResultRole, MessageRole, TimeRole, OkRole
    };

    static constexpr int MaxEntries = 50;

    // the file the Store uses: $XDG_STATE_HOME/raye-store/history.json
    static QString defaultPath();

    explicit History(const QString &path = defaultPath(), QObject *parent = nullptr);

    QString path() const { return m_path; }
    const QList<HistoryEntry> &entries() const { return m_entries; }
    int count() const { return int(m_entries.size()); }

    // adds a line on top, keeps the newest MaxEntries and writes the file
    void add(const HistoryEntry &entry);
    // reads the file again (another Store window may have written it)
    Q_INVOKABLE void reload();
    Q_INVOKABLE void clear();

    // the file format; a broken file or line reads as nothing
    static QList<HistoryEntry> parse(const QByteArray &json);
    static QByteArray serialize(const QList<HistoryEntry> &entries);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void countChanged();

private:
    QList<HistoryEntry> read() const;
    void write(const QList<HistoryEntry> &entries);
    void set(QList<HistoryEntry> entries);

    QString m_path;
    QList<HistoryEntry> m_entries;
};
