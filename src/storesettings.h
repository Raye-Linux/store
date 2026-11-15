// SPDX-License-Identifier: GPL-3.0-or-later
// The Store's settings page. They live in raye-storerc, where the daily
// update timer reads them with kreadconfig6.
#pragma once

#include <QObject>
#include <QSettings>

class StoreSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool autoUpdate READ autoUpdate WRITE setAutoUpdate NOTIFY changed)
    Q_PROPERTY(bool showSystemPackages READ showSystemPackages WRITE setShowSystemPackages NOTIFY changed)
public:
    explicit StoreSettings(QObject *parent = nullptr);
    // the file itself, for the tests
    explicit StoreSettings(const QString &path, QObject *parent = nullptr);

    bool autoUpdate() const;
    void setAutoUpdate(bool on);
    bool showSystemPackages() const;
    void setShowSystemPackages(bool on);

Q_SIGNALS:
    void changed();

private:
    void set(const QString &key, bool value);

    QSettings m_file;
};
