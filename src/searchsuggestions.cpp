// SPDX-License-Identifier: GPL-3.0-or-later
#include "searchsuggestions.h"

SearchSuggestions::SearchSuggestions(QObject *parent)
    : QObject(parent)
{
    m_filter.setLimit(maxRows);
    m_later.setSingleShot(true);
    m_later.setInterval(debounceMs);
    connect(&m_filter, &CatalogFilter::countChanged, this, &SearchSuggestions::countChanged);
    connect(&m_later, &QTimer::timeout, this, [this] {
        m_query = m_pending;
        m_filter.setText(m_query);
        setCurrent(-1);
        Q_EMIT queryChanged();
        setShown(!m_query.isEmpty());
    });
}

void SearchSuggestions::setSource(QObject *source)
{
    if (m_source == source)
        return;
    m_source = source;
    m_filter.setSourceModel(qobject_cast<QAbstractItemModel *>(source));
    Q_EMIT sourceChanged();
}

void SearchSuggestions::type(const QString &text)
{
    m_pending = text.trimmed();
    if (m_pending.isEmpty()) {
        m_query.clear();
        dismiss();
        return;
    }
    m_later.start();
}

void SearchSuggestions::setCurrent(int row)
{
    row = qBound(-1, row, count());
    if (row == m_current)
        return;
    m_current = row;
    Q_EMIT currentChanged();
}

void SearchSuggestions::down()
{
    if (m_shown)
        setCurrent(m_current >= count() ? 0 : m_current + 1);
}

void SearchSuggestions::up()
{
    if (m_shown)
        setCurrent(m_current <= 0 ? count() : m_current - 1);
}

void SearchSuggestions::accept()
{
    // Enter right after the last letter: the rows may not have caught up
    if (m_later.isActive()) {
        m_later.stop();
        m_query = m_pending;
        m_filter.setText(m_query);
        m_current = -1;
    }
    const int row = m_current;
    const QString query = m_query;
    QString appId;
    if (row >= 0 && row < count())
        appId = m_filter.index(row, 0).data(Catalog::IdRole).toString();
    dismiss();
    if (!appId.isEmpty())
        Q_EMIT openApp(appId);
    else if (!query.isEmpty())
        Q_EMIT showResults(query);
}

void SearchSuggestions::dismiss()
{
    m_later.stop();
    setCurrent(-1);
    setShown(false);
}

void SearchSuggestions::forget()
{
    m_query.clear();
    m_pending.clear();
    dismiss();
}

void SearchSuggestions::setShown(bool shown)
{
    if (shown == m_shown)
        return;
    m_shown = shown;
    Q_EMIT shownChanged();
}
