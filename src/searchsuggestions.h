// SPDX-License-Identifier: GPL-3.0-or-later
// What the flyout under the search box shows while you type: the best
// few apps, then "See all results". Up, Down and Enter go through here,
// the focus stays in the box.
#pragma once

#include "catalog.h"

#include <QObject>
#include <QTimer>

class SearchSuggestions : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QAbstractItemModel *rows READ rows CONSTANT)
    Q_PROPERTY(QString query READ query NOTIFY queryChanged)
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    // the row Enter would open: 0 to count-1, count is "See all", -1 none
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY currentChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool showSystemPackages READ showSystemPackages WRITE setShowSystemPackages)
public:
    static constexpr int maxRows = 8;
    static constexpr int debounceMs = 120;

    explicit SearchSuggestions(QObject *parent = nullptr);

    QObject *source() const { return m_source; }
    void setSource(QObject *source);
    QAbstractItemModel *rows() { return &m_filter; }
    QString query() const { return m_query; }
    bool shown() const { return m_shown; }
    int current() const { return m_current; }
    void setCurrent(int row);
    int count() const { return m_filter.count(); }
    bool showSystemPackages() const { return m_filter.showSystemPackages(); }
    void setShowSystemPackages(bool show) { m_filter.setShowSystemPackages(show); }

    // the person typed: the rows follow a moment later
    Q_INVOKABLE void type(const QString &text);
    Q_INVOKABLE void down();
    Q_INVOKABLE void up();
    // opens the current row; no row means the results page
    Q_INVOKABLE void accept();
    Q_INVOKABLE void dismiss();
    // the box was set from outside: forget what had been typed
    Q_INVOKABLE void forget();

Q_SIGNALS:
    void sourceChanged();
    void queryChanged();
    void shownChanged();
    void currentChanged();
    void countChanged();
    void openApp(const QString &appId);
    void showResults(const QString &query);

private:
    void setShown(bool shown);

    CatalogFilter m_filter;
    QObject *m_source = nullptr;
    QTimer m_later;
    QString m_query;
    QString m_pending;
    bool m_shown = false;
    int m_current = -1;
};
